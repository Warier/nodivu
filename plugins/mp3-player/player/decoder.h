#pragma once
#include <atomic>
#include <string>
#include <vector>
// Player-only API. No CLAP, renderer, graph or host types cross this boundary.
// Returns stereo interleaved float32 at 48000 Hz. Decoder thread only.
// Limit: 600 seconds; no decode or disk access in the audio callback.
struct Clip {
  std::vector<float> pcm;
  unsigned rate = 0;
};
Clip decode_file(const std::wstring &path, const std::atomic<bool> &stopping);
