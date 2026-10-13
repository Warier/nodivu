#pragma once
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <rnnoise.h>

namespace nodivu {
// RNNoise 0.2: analysis/synthesis overlap (480) + lookahead (480).
// The adapter assembles another 480 samples, independent of host callback sizes.
constexpr uint32_t voice_latency = 1440;
constexpr uint32_t limiter_latency = 240;
class VoiceCleanup {
    DenoiseState *states[2]{};
    void *initial = nullptr;
    size_t state_size = 0;
    float input[2][480]{}, wet[2][480]{}, dry[2][voice_latency]{};
    uint32_t cursor = 0, dry_cursor = 0;
    float amount = 1;
public:
    VoiceCleanup() = default;
    VoiceCleanup(const VoiceCleanup&) = delete;
    VoiceCleanup& operator=(const VoiceCleanup&) = delete;
    ~VoiceCleanup() { for (auto *s : states) std::free(s); std::free(initial); }
    bool initialize() {
        state_size = static_cast<size_t>(rnnoise_get_size());
        if (!state_size || rnnoise_get_frame_size() != 480) return false;
        initial = std::calloc(1, state_size);
        if (!initial || rnnoise_init(static_cast<DenoiseState*>(initial), nullptr)) return false;
        for (auto &s : states) {
            s = static_cast<DenoiseState*>(std::malloc(state_size));
            if (!s) return false;
        }
        reset(); return true;
    }
    // Pinned RNNoise state owns no heap pointers: model pointers refer to constant
    // built-in arrays. Copying this initialized snapshot also clears recurrent state.
    void reset() {
        for (auto *s : states) if (s) std::memcpy(s, initial, state_size);
        std::memset(input,0,sizeof(input)); std::memset(wet,0,sizeof(wet));
        std::memset(dry,0,sizeof(dry)); cursor=0; dry_cursor=0;
    }
    void sample(const float in[2], float out[2], float target) {
        amount += std::clamp(target-amount, -1.0f/480, 1.0f/480);
        for (uint32_t c=0;c<2;++c) {
            const float original = dry[c][dry_cursor]; dry[c][dry_cursor]=in[c];
            out[c] = original + amount*(wet[c][cursor]/32768.0f-original);
            // Voice model operates on PCM16 amplitude, with headroom bounded before
            // its nonlinear stages. It does not reconstruct an already clipped mic.
            input[c][cursor]=std::clamp(in[c],-1.0f,1.0f)*32768.0f;
        }
        dry_cursor=(dry_cursor+1)%voice_latency;
        if (++cursor==480) {
            cursor=0;
            for (uint32_t c=0;c<2;++c) rnnoise_process_frame(states[c],wet[c],input[c]);
        }
    }
};

class PeakLimiter {
    // Monotonic peak queue spans [t-lookahead,t], including the outgoing sample.
    struct Peak { uint64_t time=0; float value=0; } peaks[limiter_latency+2]{};
    float delay[2][limiter_latency]{};
    uint32_t cursor=0, first=0, count=0;
    uint64_t time=0;
    float gain=1;
public:
    void reset() { *this=PeakLimiter{}; }
    void sample(const float in[2], float out[2], float ceiling, float release) {
        constexpr uint32_t capacity=limiter_latency+2;
        while (count && time>=limiter_latency && peaks[first].time<time-limiter_latency) {first=(first+1)%capacity;--count;}
        const float value=std::max(std::abs(in[0]),std::abs(in[1]));
        while (count && peaks[(first+count-1)%capacity].value<=value) --count;
        peaks[(first+count)%capacity]={time,value}; ++count;
        const float peak=peaks[first].value;
        const float target=peak>ceiling ? ceiling/peak : 1;
        gain=std::min(target,1-(1-gain)*release);
        for(uint32_t c=0;c<2;++c) {out[c]=delay[c][cursor]*gain;delay[c][cursor]=in[c];}
        cursor=(cursor+1)%limiter_latency; ++time;
    }
};
}
