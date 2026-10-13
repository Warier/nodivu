// CLAP adapter only. DSP algorithms/buffers are in dsp.h; UI is in plugin.json.
#include <clap/clap.h>
#include <atomic>
#include <cstdio>
#include <new>
#include "dsp.h"
#ifndef NODIVU_LIMITER
#define NODIVU_LIMITER 0
#endif
namespace {
constexpr bool is_limiter=NODIVU_LIMITER!=0;
const char *features[]={CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,CLAP_PLUGIN_FEATURE_UTILITY,nullptr};
const clap_plugin_descriptor_t descriptor={CLAP_VERSION_INIT,
    is_limiter?"org.nodivu.peak-limiter":"org.nodivu.voice-cleanup",
    is_limiter?"Limitador de picos":"Limpeza de voz", "Nodivu contributors",
    "https://github.com/Warier/nodivu","","","1.0.0",
    is_limiter?"Picos limitados com 5 ms de antecipação":"Redução de ruído RNNoise 0.2, CPU, 30 ms", features};
struct Instance {
    clap_plugin_t plugin{};
    std::atomic<double> values[2];
    nodivu::VoiceCleanup voice;
    nodivu::PeakLimiter limiter;
    bool active=false, processing=false;
    float ceiling=0.89125094f, release=0.9997917f;
    Instance() {values[0].store(is_limiter?-1.0:100.0);values[1].store(100.0);}
};
Instance *state(const clap_plugin_t *p) {return static_cast<Instance*>(p->plugin_data);}
void set_value(Instance *s,clap_id id,double v) {
    if(!std::isfinite(v)) return;
    if(id==0) {
        v=std::clamp(v,is_limiter?-24.0:0.0,is_limiter?0.0:100.0);
        s->values[0].store(v,std::memory_order_relaxed);
        if(is_limiter) s->ceiling=static_cast<float>(std::pow(10.0,v/20.0));
    } else if(id==1 && is_limiter) {
        v=std::clamp(v,10.0,1000.0);s->values[1].store(v,std::memory_order_relaxed);
        s->release=static_cast<float>(std::exp(-1.0/(48*v)));
    }
}
bool CLAP_ABI init(const clap_plugin_t *p) {return is_limiter || state(p)->voice.initialize();}
void CLAP_ABI destroy(const clap_plugin_t *p) {delete state(p);}
bool CLAP_ABI activate(const clap_plugin_t *p,double rate,uint32_t min,uint32_t max) {
    if(rate!=48000||!min||max>480||min>max)return false;
    state(p)->active=true;return true;
}
void CLAP_ABI deactivate(const clap_plugin_t *p) {state(p)->active=false;}
bool CLAP_ABI start(const clap_plugin_t *p) {state(p)->processing=state(p)->active;return state(p)->processing;}
void CLAP_ABI stop(const clap_plugin_t *p) {state(p)->processing=false;}
void CLAP_ABI reset(const clap_plugin_t *p) {if(is_limiter)state(p)->limiter.reset();else state(p)->voice.reset();}
void CLAP_ABI on_main(const clap_plugin_t*) {}
clap_process_status CLAP_ABI process(const clap_plugin_t *p,const clap_process_t *a) {
    auto *s=state(p);
    if(!s->processing||!a||a->frames_count>480||a->audio_inputs_count!=1||a->audio_outputs_count!=1||
       a->audio_inputs[0].channel_count!=2||a->audio_outputs[0].channel_count!=2||!a->audio_inputs[0].data32||!a->audio_outputs[0].data32) return CLAP_PROCESS_ERROR;
    uint32_t event=0,count=a->in_events?a->in_events->size(a->in_events):0;
    for(uint32_t i=0;i<a->frames_count;++i) {
        while(event<count) {
            auto *h=a->in_events->get(a->in_events,event);
            if(!h)return CLAP_PROCESS_ERROR;
            if(h->time>i)break;
            if(h->space_id==CLAP_CORE_EVENT_SPACE_ID&&h->type==CLAP_EVENT_PARAM_VALUE&&h->size>=sizeof(clap_event_param_value_t)) {
                auto *v=reinterpret_cast<const clap_event_param_value_t*>(h); set_value(s,v->param_id,v->value);
            }
            ++event;
        }
        float in[2]={a->audio_inputs[0].data32[0][i],a->audio_inputs[0].data32[1][i]},out[2]{};
        if(!std::isfinite(in[0])||!std::isfinite(in[1])) return CLAP_PROCESS_ERROR;
        if(is_limiter)s->limiter.sample(in,out,s->ceiling,s->release);
        else s->voice.sample(in,out,static_cast<float>(s->values[0].load(std::memory_order_relaxed)/100.0));
        for(uint32_t c=0;c<2;++c)a->audio_outputs[0].data32[c][i]=out[c];
    }
    return CLAP_PROCESS_CONTINUE;
}
uint32_t CLAP_ABI port_count(const clap_plugin_t*,bool) {return 1;}
bool CLAP_ABI port_info(const clap_plugin_t*,uint32_t i,bool input,clap_audio_port_info_t *info) {
    if(i)return false;*info={};info->id=input?10:20;
    std::snprintf(info->name,sizeof(info->name),"Audio");info->flags=CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count=2;info->port_type=CLAP_PORT_STEREO;info->in_place_pair=input?20:10;return true;
}
const clap_plugin_audio_ports_t ports={port_count,port_info};
uint32_t CLAP_ABI param_count(const clap_plugin_t*) {return is_limiter?2:1;}
bool CLAP_ABI param_info(const clap_plugin_t*,uint32_t i,clap_param_info_t *info) {
    if(i>=(is_limiter?2u:1u))return false;*info={};info->id=i;info->flags=CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name,sizeof(info->name),"%s",is_limiter?(i?"Release (ms)":"Teto (dBFS)"):"Limpeza (%)");
    info->min_value=is_limiter?(i?10:-24):0;info->max_value=is_limiter?(i?1000:0):100;
    info->default_value=is_limiter?(i?100:-1):100;return true;
}
bool CLAP_ABI param_value(const clap_plugin_t *p,clap_id id,double *v) {
    if(id>=param_count(p))return false;*v=state(p)->values[id].load(std::memory_order_relaxed);return true;
}
bool CLAP_ABI value_text(const clap_plugin_t*,clap_id,double v,char *out,uint32_t size) {return std::snprintf(out,size,"%.2f",v)>0;}
bool CLAP_ABI text_value(const clap_plugin_t*,clap_id,const char*,double*) {return false;}
void CLAP_ABI flush(const clap_plugin_t *p,const clap_input_events_t *in,const clap_output_events_t*) {
    if(!in)return;
    for(uint32_t i=0;i<in->size(in);++i) {
        auto *h=in->get(in,i);
        if(h&&h->space_id==CLAP_CORE_EVENT_SPACE_ID&&h->type==CLAP_EVENT_PARAM_VALUE&&h->size>=sizeof(clap_event_param_value_t)) {
            auto *v=reinterpret_cast<const clap_event_param_value_t*>(h);set_value(state(p),v->param_id,v->value);
        }
    }
}
const clap_plugin_params_t params={param_count,param_info,param_value,value_text,text_value,flush};
uint32_t CLAP_ABI latency(const clap_plugin_t*) {return is_limiter?nodivu::limiter_latency:nodivu::voice_latency;}
const clap_plugin_latency_t latency_ext={latency};
const void *CLAP_ABI extension(const clap_plugin_t*,const char *id) {
    if(!std::strcmp(id,CLAP_EXT_AUDIO_PORTS))return &ports;
    if(!std::strcmp(id,CLAP_EXT_PARAMS))return &params;
    if(!std::strcmp(id,CLAP_EXT_LATENCY))return &latency_ext;
    return nullptr;
}
uint32_t CLAP_ABI plugin_count(const clap_plugin_factory_t*) {return 1;}
const clap_plugin_descriptor_t *CLAP_ABI get_descriptor(const clap_plugin_factory_t*,uint32_t index) {return index?nullptr:&descriptor;}
const clap_plugin_t *CLAP_ABI create(const clap_plugin_factory_t*,const clap_host_t *host,const char *id) {
    if(!host||!clap_version_is_compatible(host->clap_version)||std::strcmp(id,descriptor.id))return nullptr;
    auto *s=new(std::nothrow) Instance;if(!s)return nullptr;
    s->plugin={&descriptor,s,init,destroy,activate,deactivate,start,stop,reset,process,extension,on_main};return &s->plugin;
}
const clap_plugin_factory_t factory={plugin_count,get_descriptor,create};
bool CLAP_ABI entry_init(const char*) {return true;}
void CLAP_ABI entry_deinit() {}
const void *CLAP_ABI get_factory(const char *id) {return !std::strcmp(id,CLAP_PLUGIN_FACTORY_ID)?&factory:nullptr;}
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry={CLAP_VERSION_INIT,entry_init,entry_deinit,get_factory};
