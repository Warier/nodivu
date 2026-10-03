#pragma once
#include "decoder.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
static_assert(std::atomic<uint64_t>::is_always_lock_free, "RT transport requires lock-free atomics");
// All ownership/allocation lives on main/decoder threads. process/reset are RT.
class Player {
public:
  Player();
  ~Player();
  bool load(const char *utf8); // main; nullptr clears; never waits for decoding
  bool play(); // legacy restart
  bool resume();
  bool pause();
  bool seek(uint32_t milliseconds);
  uint32_t position() const { return position_ms_.load(); }
  uint32_t duration() const { return duration_ms_.load(); }
  int status() const {
    return status_.load();
  } // 0 empty,1 loading,2 ready,3 playing,4 ended,5 paused; negative HRESULT
  void process(float *left, float *right, unsigned frames) noexcept;
  void reset() noexcept;

private:
  void decode_loop() noexcept;
  std::atomic<bool> stopping_{false};
  std::atomic<int> status_{0};
  std::atomic<Clip *> clip_{nullptr}, hazard_{nullptr};
  std::atomic<uint64_t> transport_{0}, seek_request_{0}, applied_seek_{0};
  std::atomic<uint32_t> position_ms_{0}, duration_ms_{0};
  uint32_t command_sequence_ = 0, seek_sequence_ = 0; // main thread only
  std::mutex mutex_;
  std::condition_variable wake_;
  std::wstring pending_;
  bool request_ = false;
  uint64_t revision_ = 0;
  std::thread decoder_;
  // Audio-thread-only cursor. No shared_ptr destruction or mutex on this
  // thread.
  Clip *observed_ = nullptr;
  uint64_t seen_transport_ = 0, seen_seek_ = 0;
  double cursor_ = 0;
  bool playing_ = false;
};
