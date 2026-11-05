// Deterministic lifecycle tests. The decoder below is explicitly synthetic,
// so these waits do NOT prove that a blocked Media Foundation call can abort.
#include "../player/player.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
static void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
struct Gate {
  std::mutex mutex;
  std::condition_variable wake;
  bool entered = false;
  std::atomic<unsigned> cancellations{0};
  void await_entry() {
    std::unique_lock<std::mutex> lock(mutex);
    check(wake.wait_for(lock, 2s, [&] { return entered; }), "decoder did not enter");
  }
};
// Test-only coordinator. Each Player is joined before the next coordinator is
// assigned, and no production decoder or CLAP API exposes this test seam.
static Gate *gate = nullptr;
static Clip controlled_decode(const std::wstring &path, const DecodeCancellation &cancel) {
  if (path != L"ready") {
    {
      std::lock_guard<std::mutex> lock(gate->mutex);
      gate->entered = true;
    }
    gate->wake.notify_one();
    while (!cancel.cancelled()) std::this_thread::sleep_for(1ms);
    ++gate->cancellations;
    if (path == L"stale-error") throw E_FAIL;
    // Deliberately return stale successful PCM: Player must reject it too.
    return Clip{std::vector<float>(9600, 0.8f), 48000};
  }
  return Clip{std::vector<float>(9600, 0.2f), 48000};
}
static void await_ready(Player &player) {
  const auto until = Clock::now() + 2s;
  while (player.status() == 1 && Clock::now() < until) std::this_thread::sleep_for(1ms);
  check(player.status() == 2, "replacement was not ready, or stale error overwrote it");
}
int main() {
  try {
    // Production decoder must reject an invalidated token before opening a path.
    std::atomic<bool> stopping{false}; std::atomic<uint64_t> revision{2};
    try { decode_file(L"this-file-must-not-be-opened.wav", {stopping, revision, 1});
      check(false, "cancelled decode succeeded");
    } catch (HRESULT hr) { check(hr == E_ABORT, "cancelled decode performed file I/O"); }

    double max_close_ms = 0;
    for (int i = 0; i < 100; ++i) {
      Gate state; gate = &state;
      Player player(controlled_decode);
      check(player.load(i % 2 ? "stale-error" : "stale-success"), "load refused");
      state.await_entry();
      check(player.load("ready"), "replacement refused");
      await_ready(player);
      check(state.cancellations == 1, "old decode was not cancelled");
      check(player.duration() == 100, "replacement duration wrong");
      check(player.resume(), "play refused");
      float left[480], right[480]; player.process(left, right, 480);
      check(std::all_of(left, left + 480, [](float v) { return v == 0.2f; }), "stale PCM became audible");
      check(player.load(nullptr), "clear refused");
      player.process(left, right, 480);
      check(player.status() == 0 && player.duration() == 0, "clear did not reset state");
      check(std::all_of(left, left + 480, [](float v) { return v == 0; }), "clear not silent");
    }
    {
      Gate state; gate = &state; Player player(controlled_decode);
      check(player.load("stale-error"), "load refused"); state.await_entry();
      check(player.load(nullptr), "clear during decode refused");
      const auto until = Clock::now() + 2s;
      while (!state.cancellations && Clock::now() < until) std::this_thread::sleep_for(1ms);
      check(state.cancellations == 1, "clear failed to cancel pending decode");
      check(player.status() == 0, "cancelled error overwrote clear");
    }
    for (int i = 0; i < 100; ++i) {
      Gate state; gate = &state;
      auto player = std::make_unique<Player>(controlled_decode);
      check(player->load("stale-success"), "load refused"); state.await_entry();
      const auto start = Clock::now(); player.reset();
      const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
      max_close_ms = std::max(max_close_ms, ms);
      check(ms < 2000, "cooperative decoder shutdown exceeded test deadline");
      check(state.cancellations == 1, "shutdown failed to cancel");
    }
    for (int i = 0; i < 100; ++i) { Player idle; }
    gate = nullptr;
    std::cout << "PASS: 100 replacements (stale success/error), clear during decode, "
                 "100 cooperative shutdowns, 100 idle shutdowns; max close ms=" << max_close_ms << '\n';
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
