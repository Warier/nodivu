// Offline deterministic DSP gate. No microphone or output device is opened.
#include "dsp.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <cassert>
int main() {
    nodivu::PeakLimiter limiter;
    float in[2],out[2];
    for(uint32_t t=0;t<48000;++t) {
        in[0]=t%997==0?8.0f:0.02f;in[1]=-in[0]*0.5f;
        limiter.sample(in,out,0.5f,0.9998f);
        assert(std::abs(out[0])<=0.500001f && std::abs(out[1])<=0.500001f);
        if(t<240)assert(out[0]==0);
        if(t==240)assert(std::abs(out[0]-0.5f)<1e-6f);
        assert(std::abs(out[1]+out[0]*0.5f)<1e-6f);
    }
    limiter.reset();in[0]=in[1]=0;
    for(int i=0;i<1000;++i){limiter.sample(in,out,0.5f,0.9998f);assert(out[0]==0);}
    nodivu::VoiceCleanup voice;
    assert(voice.initialize());
    // Zero intensity must be an exact delayed original, including > 0 dBFS.
    std::vector<float> original(48000), cleaned(48000);
    uint32_t seed=123;
    for(uint32_t t=0;t<original.size();++t) {
        seed=seed*1664525u+1013904223u;
        in[0]=static_cast<float>(static_cast<int32_t>(seed))/2147483648.0f*2;
        in[1]=-in[0];original[t]=in[0];voice.sample(in,out,0);
        if(t>=1440)assert(std::abs(out[0]-original[t-1440])<1e-6f);
    }
    voice.reset();in[0]=in[1]=0;
    for(int i=0;i<4800;++i){voice.sample(in,out,1);assert(out[0]==0);}
    // White noise suppression and measured alignment of the wet signal.
    voice.reset();seed=123;
    double input_energy=0,output_energy=0;
    const auto begin=std::chrono::steady_clock::now();
    for(uint32_t t=0;t<original.size();++t) {
        seed=seed*1664525u+1013904223u;
        in[0]=static_cast<float>(static_cast<int32_t>(seed))/2147483648.0f*0.1f;in[1]=in[0];
        original[t]=in[0];voice.sample(in,out,1);cleaned[t]=out[0];
        assert(std::isfinite(out[0]) && std::abs(out[0])<1 && out[0]==out[1]);
        if(t>4800){input_energy+=in[0]*in[0];output_energy+=out[0]*out[0];}
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    int best=0;double correlation=0;
    for(int lag=1000;lag<1800;++lag){
        double sum=0;for(size_t t=4800;t<original.size();++t)sum+=cleaned[t]*original[t-lag];
        if(sum>correlation){correlation=sum;best=lag;}
    }
    const double ratio=std::sqrt(output_energy/input_energy);
    std::printf("RNNoise stereo 1s CPU wall %.3f ms; noise RMS ratio %.6f; measured lag %d frames\n",elapsed,ratio,best);
    assert(best==1440);assert(ratio<0.7);
    std::puts("PASS: limiter ceiling/stereo/reset; RNNoise dry alignment, reset, finite output, noise attenuation, wet latency.");
}
