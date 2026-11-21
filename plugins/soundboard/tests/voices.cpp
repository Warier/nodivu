#include "../audio/soundboard.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
void check(bool ok, const char *text) {
  if (!ok)
    throw std::runtime_error(text);
}
Clip fake(const std::wstring &path, const DecodeCancellation &cancel) {
  if (path == L"wait") {
    while (!cancel.cancelled())
      std::this_thread::sleep_for(1ms);
    return {std::vector<float>(9600, 0.9f), 48000};
  }
  return {std::vector<float>(9600, path == L"one" ? 0.1f : 0.2f), 48000};
}
void ready(Soundboard &bank, unsigned slot) {
  const auto until = std::chrono::steady_clock::now() + 2s;
  while (bank.status(slot) == 1 && std::chrono::steady_clock::now() < until)
    std::this_thread::sleep_for(1ms);
  check(bank.status(slot) == 2, "decode not ready");
}
int main() {
  try {
    Soundboard bank(fake);
    float l[480], r[480];
    auto audio = [&](float expected) {
      bank.process(l, r, 480);
      for (unsigned i = 0; i < 480; ++i)
        check(std::abs(l[i] - expected) < 1e-5f &&
                  std::abs(r[i] - expected) < 1e-5f,
              "incorrect independent sum");
    };
    audio(0);
    check(!bank.load(10, "one") && !bank.trigger(10) && !bank.stop(10),
          "slot overflow accepted");
    for (unsigned i = 0; i < 10; ++i) {
      check(bank.load(i, i % 2 ? "two" : "one"), "load failed");
      ready(bank, i);
      check(bank.trigger(i), "trigger failed");
    }
    audio(1.5f);
    check(bank.stop(0), "stop failed");
    audio(1.4f);
    check(bank.trigger(0), "retrigger failed");
    audio(1.5f);
    check(bank.position(0) == 10 && bank.position(1) == 30,
          "retrigger disturbed another slot");
    check(bank.load(1, nullptr), "clear failed");
    audio(1.3f);
    check(bank.status(1) == 0 && !bank.trigger(1), "cleared slot played");
    bank.reset();
    audio(0);
    check(bank.load(1, "wait") && bank.load(1, "two"), "replacement failed");
    ready(bank, 1);
    check(bank.trigger(1), "replacement trigger failed");
    audio(0.2f);
    for (int cycle = 0; cycle < 100; ++cycle) {
      check(bank.load(1, "one"), "cycle load");
      ready(bank, 1);
      check(bank.trigger(1), "cycle play");
      audio(0.1f);
      check(bank.load(1, nullptr), "cycle clear");
      audio(0);
    }
    std::cout << "PASS: ten voices, additive sum, independent stop/retrigger, "
                 "clear, reset, superseded decode, 100 reloads\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
