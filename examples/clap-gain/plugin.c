/* Fixture CLAP C independente do Rust/Nodivu. Falhas são tipos explicitamente de teste. */
#include <clap/clap.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef GAIN_SCALE
#define GAIN_SCALE 0.5f
#endif
enum { GAIN, SEPARATE, SOURCE, INIT_FAIL, ACTIVATE_FAIL, START_FAIL, PROCESS_FAIL, NAN_OUTPUT, MONO, RESTART, CONSUMER, TYPE_COUNT };
static const char *const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, NULL};
#define DESC(id, name) {CLAP_VERSION_INIT, id, name, "Nodivu test", "", "", "", "0.1.0", "Fixture offline", features}
static const clap_plugin_descriptor_t descriptors[TYPE_COUNT] = {
    DESC("org.nodivu.fixture.gain", "Ganho C"),
    DESC("org.nodivu.fixture.separate", "Ganho sem in-place"),
    DESC("org.nodivu.fixture.source", "Fonte DC de teste"),
    DESC("org.nodivu.fixture.init-fail", "Recusa init"),
    DESC("org.nodivu.fixture.activate-fail", "Recusa activate"),
    DESC("org.nodivu.fixture.start-fail", "Recusa start"),
    DESC("org.nodivu.fixture.process-fail", "Recusa process"),
    DESC("org.nodivu.fixture.nan", "NaN deliberado"),
    DESC("org.nodivu.fixture.mono", "Formato fora do perfil"),
    DESC("org.nodivu.fixture.restart", "Solicita reativação"),
    DESC("org.nodivu.fixture.consumer", "Consumidor C")
};
typedef struct {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_thread_check_t *threads;
    int kind;
    bool initialized, active, processing;
    double gain;
} Instance;
static Instance *state(const clap_plugin_t *p) { return (Instance *)p->plugin_data; }
static void main_thread(Instance *s) { assert(s->threads && s->threads->is_main_thread(s->host)); assert(!s->threads->is_audio_thread(s->host)); }
static void audio_thread(Instance *s) { assert(s->threads && s->threads->is_audio_thread(s->host)); }
static bool CLAP_ABI init(const clap_plugin_t *p) {
    Instance *s = state(p);
    s->threads = (const clap_host_thread_check_t *)s->host->get_extension(s->host, CLAP_EXT_THREAD_CHECK);
    main_thread(s);
    if (s->kind == INIT_FAIL) return false;
    s->initialized = true;
    s->host->request_callback(s->host);
    return true;
}
static void CLAP_ABI destroy(const clap_plugin_t *p) {
    Instance *s = state(p); main_thread(s); assert(!s->active && !s->processing); free(s);
}
static bool CLAP_ABI activate(const clap_plugin_t *p, double rate, uint32_t min, uint32_t max) {
    Instance *s = state(p); main_thread(s); assert(s->initialized && !s->active);
    assert(rate == 48000 && min == 1 && max == 480);
    if (s->kind == ACTIVATE_FAIL) return false;
    s->active = true; return true;
}
static void CLAP_ABI deactivate(const clap_plugin_t *p) {
    Instance *s = state(p); main_thread(s); assert(s->active && !s->processing); s->active = false;
}
static bool CLAP_ABI start(const clap_plugin_t *p) {
    Instance *s = state(p); audio_thread(s); assert(s->active && !s->processing);
    if (s->kind == START_FAIL) return false;
    s->processing = true; return true;
}
static void CLAP_ABI stop(const clap_plugin_t *p) {
    Instance *s = state(p); audio_thread(s); assert(s->processing); s->processing = false;
}
static void CLAP_ABI reset(const clap_plugin_t *p) { audio_thread(state(p)); }
static void CLAP_ABI on_main(const clap_plugin_t *p) {
    Instance *s = state(p); main_thread(s);
}
static clap_process_status CLAP_ABI process(const clap_plugin_t *p, const clap_process_t *audio) {
    Instance *s = state(p); audio_thread(s); assert(s->processing);
    assert(audio->frames_count > 0 && audio->frames_count <= 480);
    if (s->kind == CONSUMER) {
        assert(audio->audio_outputs_count == 0 && audio->audio_outputs == NULL);
        assert(audio->audio_inputs_count == 1 && audio->audio_inputs[0].channel_count == 2);
        for (uint32_t c = 0; c < 2; ++c) for (uint32_t i = 0; i < audio->frames_count; ++i) assert(isfinite(audio->audio_inputs[0].data32[c][i]));
        return CLAP_PROCESS_CONTINUE;
    }
    assert(audio->audio_outputs_count == 1 && audio->audio_outputs[0].channel_count == 2);
    if (s->kind == PROCESS_FAIL) return CLAP_PROCESS_ERROR;
    uint32_t event = 0, count = audio->in_events->size(audio->in_events);
    assert(count <= 32 && audio->in_events->get(audio->in_events, count) == NULL);
    if (s->kind == SEPARATE) assert(audio->audio_inputs[0].data32[0] != audio->audio_outputs[0].data32[0]);
    for (uint32_t i = 0; i < audio->frames_count; ++i) {
        while (event < count) {
            const clap_event_header_t *header = audio->in_events->get(audio->in_events, event);
            if (header->time != i) break;
            assert(header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE);
            const clap_event_param_value_t *param = (const clap_event_param_value_t *)header;
            assert(param->param_id == 7 && param->note_id == -1 && param->port_index == -1);
            s->gain = param->value; ++event;
        }
        for (uint32_t c = 0; c < 2; ++c) {
            float input = s->kind == SOURCE ? 1.0f : audio->audio_inputs[0].data32[c][i];
            audio->audio_outputs[0].data32[c][i] = s->kind == NAN_OUTPUT ? NAN : input * (float)s->gain * GAIN_SCALE;
        }
    }
    if (s->kind == RESTART) s->host->request_restart(s->host);
    s->host->request_callback(s->host);
    return CLAP_PROCESS_CONTINUE;
}
static uint32_t CLAP_ABI port_count(const clap_plugin_t *p, bool input) {
    main_thread(state(p)); return (input && state(p)->kind == SOURCE) || (!input && state(p)->kind == CONSUMER) ? 0 : 1;
}
static bool CLAP_ABI port_info(const clap_plugin_t *p, uint32_t index, bool input, clap_audio_port_info_t *info) {
    Instance *s = state(p); main_thread(s); assert(!s->active);
    if (index != 0 || (input && s->kind == SOURCE) || (!input && s->kind == CONSUMER)) return false;
    memset(info, 0, sizeof(*info)); info->id = input ? 10 : 20;
    strcpy_s(info->name, sizeof(info->name), "Audio"); info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = s->kind == MONO ? 1 : 2; info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = s->kind == SEPARATE || s->kind == SOURCE || s->kind == CONSUMER ? CLAP_INVALID_ID : (input ? 20 : 10);
    return true;
}
static const clap_plugin_audio_ports_t ports = {port_count, port_info};
static uint32_t CLAP_ABI param_count(const clap_plugin_t *p) { main_thread(state(p)); return state(p)->kind == CONSUMER ? 0 : 1; }
static bool CLAP_ABI param_info(const clap_plugin_t *p, uint32_t index, clap_param_info_t *info) {
    main_thread(state(p)); if (index) return false; memset(info, 0, sizeof(*info));
    info->id = 7; info->flags = CLAP_PARAM_IS_AUTOMATABLE; strcpy_s(info->name, sizeof(info->name), "Amplitude");
    info->min_value = 0; info->max_value = 1; info->default_value = 1; return true;
}
static bool CLAP_ABI param_value(const clap_plugin_t *p, clap_id id, double *value) {
    main_thread(state(p)); if (id != 7) return false; *value = state(p)->gain; return true;
}
static bool CLAP_ABI value_text(const clap_plugin_t *p, clap_id id, double v, char *out, uint32_t size) {
    (void)p; (void)id; (void)v; (void)out; (void)size; return false;
}
static bool CLAP_ABI text_value(const clap_plugin_t *p, clap_id id, const char *in, double *out) {
    (void)p; (void)id; (void)in; (void)out; return false;
}
static void CLAP_ABI flush(const clap_plugin_t *p, const clap_input_events_t *in, const clap_output_events_t *out) {
    (void)p; (void)in; (void)out;
}
static const clap_plugin_params_t params = {param_count, param_info, param_value, value_text, text_value, flush};
static uint32_t CLAP_ABI latency(const clap_plugin_t *p) { main_thread(state(p)); assert(state(p)->active); return 0; }
static const clap_plugin_latency_t latency_ext = {latency};
static const void *CLAP_ABI get_extension(const clap_plugin_t *p, const char *id) {
    assert(state(p)->initialized);
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
    if (!strcmp(id, CLAP_EXT_PARAMS)) return &params;
    if (!strcmp(id, CLAP_EXT_LATENCY)) return &latency_ext;
    return NULL;
}
static uint32_t CLAP_ABI plugin_count(const clap_plugin_factory_t *factory) { (void)factory; return TYPE_COUNT; }
static const clap_plugin_descriptor_t *CLAP_ABI descriptor(const clap_plugin_factory_t *factory, uint32_t index) {
    (void)factory; return index < TYPE_COUNT ? &descriptors[index] : NULL;
}
static const clap_plugin_t *CLAP_ABI create(const clap_plugin_factory_t *factory, const clap_host_t *host, const char *id) {
    (void)factory;
    for (int i = 0; i < TYPE_COUNT; ++i) if (!strcmp(id, descriptors[i].id)) {
        Instance *s = calloc(1, sizeof(*s)); if (!s) return NULL;
        s->kind = i; s->host = host; s->gain = 1;
        s->plugin = (clap_plugin_t){&descriptors[i], s, init, destroy, activate, deactivate, start, stop, reset, process, get_extension, on_main};
        return &s->plugin;
    }
    return NULL;
}
static const clap_plugin_factory_t factory = {plugin_count, descriptor, create};
static bool CLAP_ABI entry_init(const char *path) { return path && *path; }
static void CLAP_ABI entry_deinit(void) {}
static const void *CLAP_ABI get_factory(const char *id) { return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : NULL; }
#ifdef INVALID_CLAP_ABI
#define FIXTURE_VERSION {0, 0, 0}
#else
#define FIXTURE_VERSION CLAP_VERSION_INIT
#endif
#ifdef NO_CLAP_ENTRY
#define FIXTURE_ENTRY not_clap_entry
#else
#define FIXTURE_ENTRY clap_entry
#endif
CLAP_EXPORT const clap_plugin_entry_t FIXTURE_ENTRY = {FIXTURE_VERSION, entry_init, entry_deinit, get_factory};
