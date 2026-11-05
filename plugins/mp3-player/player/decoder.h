#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
// Player-only API. No CLAP, renderer, graph or host types cross this boundary.
// Returns stereo interleaved float32 at 48000 Hz. Decoder thread only.
// Limit: 600 seconds; no decode or disk access in the audio callback.
struct Clip {
  std::vector<float> pcm;
  unsigned rate = 0;
};
// Borrowed only during one decoder call. The Player owns these atomics and joins
// its decoder before destroying them. A new request invalidates the old token.
struct DecodeCancellation {
  const std::atomic<bool> &stopping;
  const std::atomic<uint64_t> &current_revision;
  uint64_t revision;
  bool cancelled() const noexcept {
    return stopping.load() || current_revision.load() != revision;
  }
};
using DecodeFunction = Clip (*)(const std::wstring &, const DecodeCancellation &);
Clip decode_file(const std::wstring &path, const DecodeCancellation &cancel);
