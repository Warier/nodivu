#include "player.h"
#define NOMINMAX
#include <algorithm>
#include <memory>
#include <mfapi.h>
#include <windows.h>
Player::Player() : decoder_([this] { decode_loop(); }) {}
Player::~Player() {
  // CLAP guarantees stop_processing before destroy. The audio hazard is now
  // clear.
  stopping_.store(true);
  wake_.notify_one();
  if (decoder_.joinable())
    decoder_.join();
  delete clip_.exchange(nullptr);
}
bool Player::load(const char *utf8) {
  try {
    std::wstring path;
    if (utf8) {
      int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                  nullptr, 0);
      if (n <= 1 || n > 32768)
        return false;
      path.resize(n);
      if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                               path.data(), n))
        return false;
      path.pop_back();
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_ = std::move(path);
      request_ = true;
      ++revision_;
      transport_.store(static_cast<uint64_t>(++command_sequence_) << 1);
      seek_request_.store(static_cast<uint64_t>(++seek_sequence_) << 32);
      position_ms_.store(0);
      duration_ms_.store(0);
      status_.store(pending_.empty() ? 0 : 1);
    }
    wake_.notify_one();
    return true;
  } catch (...) {
    return false;
  }
}
bool Player::play() {
  if (!seek(0)) return false;
  return resume();
}
bool Player::resume() {
  const int s = status();
  if (s < 2 || s > 5) return false;
  if (s == 4 && seek_request_.load() == applied_seek_.load() && !seek(0)) return false;
  transport_.store((static_cast<uint64_t>(++command_sequence_) << 1) | 1);
  return true;
}
bool Player::pause() {
  const int s = status();
  if (s < 2 || s > 5) return false;
  transport_.store(static_cast<uint64_t>(++command_sequence_) << 1);
  return true;
}
bool Player::seek(uint32_t milliseconds) {
  const int s = status();
  if (s < 2 || s > 5 || milliseconds > duration()) return false;
  // Seek and play are independent mailboxes: seeking followed by Play in the
  // same quantum cannot discard the seek. Intermediate drag positions coalesce.
  seek_request_.store((static_cast<uint64_t>(++seek_sequence_) << 32) | milliseconds);
  return true;
}
void Player::reset() noexcept {
  // Routing/device reconfiguration preserves position but pauses transport.
  // No allocation, ownership change or decoder lock on the audio thread.
  playing_ = false;
  seen_transport_ = transport_.load();
  int expected = 3;
  status_.compare_exchange_strong(expected, 5);
}
void Player::process(float *l, float *r, unsigned frames) noexcept {
  std::fill_n(l, frames, 0.0f);
  std::fill_n(r, frames, 0.0f);
  // One hazard acquisition attempt: a concurrent publication yields silence, no
  // spinning.
  Clip *clip = clip_.load();
  hazard_.store(clip);
  if (clip != clip_.load() || !clip || status() == 0 || status() == 1 ||
      status() < 0) {
    hazard_.store(nullptr);
    playing_ = false;
    return;
  }
  if (observed_ != clip) {
    observed_ = clip;
    playing_ = false;
    cursor_ = 0;
  }
  const auto seek = seek_request_.load();
  if (seek != seen_seek_) {
    seen_seek_ = seek;
    applied_seek_.store(seek);
    cursor_ = std::min(static_cast<double>(static_cast<uint32_t>(seek)) * clip->rate / 1000.0,
                       static_cast<double>(clip->pcm.size() / 2));
  }
  const auto command = transport_.load();
  if (command != seen_transport_) {
    seen_transport_ = command;
    playing_ = (command & 1) != 0;
  }
  int expected = status();
  if (expected >= 2 && expected <= 5) {
    const int next = playing_ ? 3 : (cursor_ >= clip->pcm.size()/2 ? 4 : (expected == 2 ? 2 : 5));
    status_.compare_exchange_strong(expected, next);
  }
  if (playing_) {
    const size_t count = clip->pcm.size() / 2;
    const double step = static_cast<double>(clip->rate) / 48000.0;
    for (unsigned i = 0; i < frames; i++) {
      if (cursor_ >= static_cast<double>(count)) {
        playing_ = false;
        int ending = 3;
        status_.compare_exchange_strong(ending, 4);
        break;
      }
      const size_t a = static_cast<size_t>(cursor_),
                   b = std::min(a + 1, count - 1);
      const float t = static_cast<float>(cursor_ - a);
      l[i] = clip->pcm[a * 2] + t * (clip->pcm[b * 2] - clip->pcm[a * 2]);
      r[i] = clip->pcm[a * 2 + 1] +
             t * (clip->pcm[b * 2 + 1] - clip->pcm[a * 2 + 1]);
      cursor_ += step;
    }
  }
  position_ms_.store(static_cast<uint32_t>(std::min(cursor_, static_cast<double>(clip->pcm.size()/2)) * 1000.0 / clip->rate));
  hazard_.store(nullptr);
}
void Player::decode_loop() noexcept {
  HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  HRESULT mf = FAILED(com) ? com : MFStartup(MF_VERSION);
  while (!stopping_.load()) {
    std::wstring path;
    uint64_t revision;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return request_ || stopping_.load(); });
      if (stopping_.load())
        break;
      path = std::move(pending_);
      revision = revision_;
      request_ = false;
    }
    try {
      if (FAILED(mf))
        throw mf;
      std::unique_ptr<Clip> next;
      if (!path.empty())
        next = std::make_unique<Clip>(decode_file(path, stopping_));
      Clip *retired = nullptr;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (revision != revision_)
          continue;
        const auto duration = next ? static_cast<uint32_t>((next->pcm.size()/2) * 1000ULL / next->rate) : 0;
        retired = clip_.exchange(next.release());
        duration_ms_.store(duration);
        position_ms_.store(0);
        status_.store(path.empty() ? 0 : 2);
      }
      // Reclamation is decoder-only. SeqCst hazard protects the current audio
      // borrow.
      while (retired && hazard_.load() == retired)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      delete retired;
    } catch (HRESULT hr) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (revision == revision_)
        status_.store(hr);
    } catch (...) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (revision == revision_)
        status_.store(E_FAIL);
    }
  }
  if (SUCCEEDED(mf))
    MFShutdown();
  if (SUCCEEDED(com))
    CoUninitialize();
}
