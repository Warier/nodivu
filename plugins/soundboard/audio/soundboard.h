#pragma once
#include "../../mp3-player/player/player.h"
#include <array>
#include <memory>
// Ten independent voices. Owners/decoder threads allocate; the callback only
// loads stable pointers and sums bounded scratch buffers. Clearing never
// deletes a Player while DSP may see it; teardown happens after
// stop_processing.
class Soundboard {
public:
  static constexpr unsigned Slots = 10, MaxFrames = 480;
  explicit Soundboard(DecodeFunction decode = decode_short) : decode_(decode) {
    for (auto &pointer : published_)
      pointer.store(nullptr);
  }
  bool load(unsigned slot, const char *path) noexcept {
    if (slot >= Slots)
      return false;
    try {
      if (!players_[slot]) {
        if (!path)
          return true;
        players_[slot] = std::make_unique<Player>(decode_);
        published_[slot].store(players_[slot].get());
      }
      return players_[slot]->load(path);
    } catch (...) {
      return false;
    }
  }
  bool trigger(unsigned slot) noexcept {
    return slot < Slots && players_[slot] && players_[slot]->play();
  }
  bool stop(unsigned slot) noexcept {
    if (slot >= Slots)
      return false;
    auto *p = players_[slot].get();
    if (!p || p->status() < 2)
      return true;
    return p->pause() && p->seek(0);
  }
  int status(unsigned slot) const noexcept {
    return slot < Slots && players_[slot] ? players_[slot]->status() : 0;
  }
  unsigned position(unsigned slot) const noexcept {
    return slot < Slots && players_[slot] ? players_[slot]->position() : 0;
  }
  unsigned duration(unsigned slot) const noexcept {
    return slot < Slots && players_[slot] ? players_[slot]->duration() : 0;
  }
  void reset() noexcept {
    for (auto &pointer : published_)
      if (auto *p = pointer.load())
        p->reset();
  }
  void process(float *left, float *right, unsigned frames) noexcept {
    if (!left || !right || frames > MaxFrames)
      return;
    for (unsigned i = 0; i < frames; ++i)
      left[i] = right[i] = 0;
    for (auto &pointer : published_)
      if (auto *p = pointer.load()) {
        p->process(left_.data(), right_.data(), frames);
        for (unsigned i = 0; i < frames; ++i) {
          left[i] += left_[i];
          right[i] += right_[i];
        }
      }
    // Unity sum, not normalization. Host limiter/clamp is downstream, so
    // the user may insert a Gain or Limiter without altering each sound.
  }

private:
  static Clip decode_short(const std::wstring &path,
                           const DecodeCancellation &cancel) {
    return decode_file_limited(path, cancel, 60);
  }
  DecodeFunction decode_;
  std::array<std::unique_ptr<Player>, Slots> players_;
  std::array<std::atomic<Player *>, Slots> published_;
  std::array<float, MaxFrames> left_{}, right_{};
};
