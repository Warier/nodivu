#include "capture_source.h"
#include "../capture/stream.h"
#include <cstring>
#include <new>
using namespace nodivu::capture;
namespace {
const char* features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
const clap_plugin_descriptor_t descriptor = {CLAP_VERSION_INIT, "org.nodivu.windows-audio", "Áudio de aplicativos", "Nodivu", "", "", "", "0.1.0", "Captura independente de aplicativo ou sistema", features};
struct Instance { clap_plugin_t plugin; Stream stream; std::vector<Application> targets; };
Instance* self(const clap_plugin_t* p) { return static_cast<Instance*>(p->plugin_data); }
bool CLAP_ABI init(const clap_plugin_t*) { return true; }
void CLAP_ABI destroy(const clap_plugin_t* p) { delete self(p); }
bool CLAP_ABI activate(const clap_plugin_t*, double rate, uint32_t min, uint32_t max) { return rate == 48000 && min && max <= 480; }
void CLAP_ABI deactivate(const clap_plugin_t* p) { self(p)->stream.clear(); }
bool CLAP_ABI start(const clap_plugin_t*) { return true; }
void CLAP_ABI stop(const clap_plugin_t* p) { self(p)->stream.reset(); }
void CLAP_ABI reset(const clap_plugin_t* p) { self(p)->stream.reset(); }
void CLAP_ABI main_callback(const clap_plugin_t*) {}
clap_process_status CLAP_ABI process(const clap_plugin_t* p, const clap_process_t* audio) {
    if (!audio || audio->frames_count > 480 || audio->audio_outputs_count != 1 || !audio->audio_outputs ||
        audio->audio_outputs[0].channel_count != 2 || !audio->audio_outputs[0].data32 ||
        !audio->audio_outputs[0].data32[0] || !audio->audio_outputs[0].data32[1]) return CLAP_PROCESS_ERROR;
    self(p)->stream.process(audio->audio_outputs[0].data32[0], audio->audio_outputs[0].data32[1], audio->frames_count);
    return CLAP_PROCESS_CONTINUE;
}
uint32_t CLAP_ABI count(const clap_plugin_t*, bool input) { return input ? 0 : 1; }
bool CLAP_ABI port(const clap_plugin_t*, uint32_t index, bool input, clap_audio_port_info_t* out) {
    if (input || index || !out) return false;
    *out = {}; out->id = 0; strcpy_s(out->name, "Audio");
    out->flags = CLAP_AUDIO_PORT_IS_MAIN; out->channel_count = 2; out->port_type = CLAP_PORT_STEREO; out->in_place_pair = CLAP_INVALID_ID;
    return true;
}
int32_t CLAP_ABI refresh(const clap_plugin_t* p) {
    const HRESULT hr = applications(self(p)->targets);
    return FAILED(hr) ? hr : static_cast<int32_t>(self(p)->targets.size());
}
bool CLAP_ABI target(const clap_plugin_t* p, uint32_t index, nodivu_capture_target_t* out) {
    if (!out || index >= self(p)->targets.size()) return false;
    try {
        const auto& t = self(p)->targets[index];
        const auto path = utf8(t.executable);
        const auto slash = t.executable.find_last_of(L"\\/");
        const auto label = utf8(t.executable.substr(slash == std::wstring::npos ? 0 : slash + 1));
        if (path.size() >= sizeof(out->executable) || label.size() >= sizeof(out->label)) return false;
        *out = {}; out->process_id = t.identity.process_id; out->creation_time = t.identity.creation_time;
        std::memcpy(out->executable, path.c_str(), path.size() + 1); std::memcpy(out->label, label.c_str(), label.size() + 1);
        return true;
    } catch (...) { return false; }
}
bool CLAP_ABI configure(const clap_plugin_t* p, uint32_t mode, const nodivu_capture_target_t* t) { return self(p)->stream.configure(mode, t); }
bool CLAP_ABI snapshot(const clap_plugin_t* p, nodivu_capture_state_t* state) { if (!state) return false; *state = self(p)->stream.snapshot(); return true; }
const clap_plugin_audio_ports_t ports = {count, port};
const nodivu_capture_source_t capture = {refresh, target, configure, snapshot};
const void* CLAP_ABI extension(const clap_plugin_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
    if (!std::strcmp(id, NODIVU_CAPTURE_SOURCE)) return &capture;
    return nullptr;
}
uint32_t CLAP_ABI types(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* CLAP_ABI describe(const clap_plugin_factory_t*, uint32_t i) { return i ? nullptr : &descriptor; }
const clap_plugin_t* CLAP_ABI create(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
    if (std::strcmp(id, descriptor.id)) return nullptr;
    try {
        auto* s = new Instance;
        s->plugin = {&descriptor, s, init, destroy, activate, deactivate, start, stop, reset, process, extension, main_callback};
        return &s->plugin;
    } catch (...) { return nullptr; }
}
const clap_plugin_factory_t factory = {types, describe, create};
bool CLAP_ABI entry_init(const char*) { return true; }
void CLAP_ABI entry_deinit() {}
const void* CLAP_ABI get_factory(const char* id) { return !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr; }
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, entry_init, entry_deinit, get_factory};
