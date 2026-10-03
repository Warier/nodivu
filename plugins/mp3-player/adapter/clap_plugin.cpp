// ABI/lifecycle adapter ONLY. Playback and decoding do not know CLAP.
#include "../player/player.h"
#include "file_player.h"
#include <cstring>
#include <new>
static const char *features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
static const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT,
    "org.nodivu.mp3-player",
    "Player de áudio",
    "Nodivu",
    "",
    "",
    "",
    "0.2.0",
    "MP3, WAV e M4A com transporte",
    features};
struct Instance {
  clap_plugin_t plugin;
  Player player;
};
static Instance *self(const clap_plugin_t *p) {
  return static_cast<Instance *>(p->plugin_data);
}
static bool CLAP_ABI init(const clap_plugin_t *) { return true; }
static void CLAP_ABI destroy(const clap_plugin_t *p) { delete self(p); }
static bool CLAP_ABI activate(const clap_plugin_t *, double rate, uint32_t min,
                              uint32_t max) {
  return rate == 48000 && min >= 1 && max <= 480;
}
static void CLAP_ABI deactivate(const clap_plugin_t *) {}
static bool CLAP_ABI start(const clap_plugin_t *) { return true; }
static void CLAP_ABI stop(const clap_plugin_t *p) { self(p)->player.reset(); }
static void CLAP_ABI reset(const clap_plugin_t *p) { self(p)->player.reset(); }
static void CLAP_ABI main_callback(const clap_plugin_t *) {}
static clap_process_status CLAP_ABI process(const clap_plugin_t *p,
                                            const clap_process_t *audio) {
  if (!audio || audio->frames_count > 480 || audio->audio_outputs_count != 1)
    return CLAP_PROCESS_ERROR;
  self(p)->player.process(audio->audio_outputs[0].data32[0],
                          audio->audio_outputs[0].data32[1],
                          audio->frames_count);
  return CLAP_PROCESS_CONTINUE;
}
static uint32_t CLAP_ABI count(const clap_plugin_t *, bool input) {
  return input ? 0 : 1;
}
static bool CLAP_ABI port(const clap_plugin_t *, uint32_t index, bool input,
                          clap_audio_port_info_t *out) {
  if (input || index)
    return false;
  *out = {};
  out->id = 0;
  strcpy_s(out->name, "Audio");
  out->flags = CLAP_AUDIO_PORT_IS_MAIN;
  out->channel_count = 2;
  out->port_type = CLAP_PORT_STEREO;
  out->in_place_pair = CLAP_INVALID_ID;
  return true;
}
static const clap_plugin_audio_ports_t ports = {count, port};
static bool CLAP_ABI load_file(const clap_plugin_t *p, const char *path) {
  return self(p)->player.load(path);
}
static bool CLAP_ABI play(const clap_plugin_t *p) {
  return self(p)->player.play();
}
static int32_t CLAP_ABI status(const clap_plugin_t *p) {
  return self(p)->player.status();
}
static bool CLAP_ABI resume(const clap_plugin_t *p) { return self(p)->player.resume(); }
static bool CLAP_ABI pause(const clap_plugin_t *p) { return self(p)->player.pause(); }
static bool CLAP_ABI seek(const clap_plugin_t *p, uint32_t ms) { return self(p)->player.seek(ms); }
static uint32_t CLAP_ABI position(const clap_plugin_t *p) { return self(p)->player.position(); }
static uint32_t CLAP_ABI duration(const clap_plugin_t *p) { return self(p)->player.duration(); }
static int32_t CLAP_ABI legacy_status(const clap_plugin_t *p) { const int s=status(p); return s==5 ? 2 : s; }
static const nodivu_file_transport_t transport = {resume, pause, seek, position, duration, status};
static const nodivu_file_player_t files = {load_file, play, legacy_status};
static const void *CLAP_ABI extension(const clap_plugin_t *, const char *id) {
  if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
    return &ports;
  if (!strcmp(id, NODIVU_FILE_TRANSPORT))
    return &transport;
  if (!strcmp(id, NODIVU_FILE_PLAYER))
    return &files;
  return nullptr;
}
static uint32_t CLAP_ABI types(const clap_plugin_factory_t *) { return 1; }
static const clap_plugin_descriptor_t *CLAP_ABI
describe(const clap_plugin_factory_t *, uint32_t i) {
  return i ? nullptr : &descriptor;
}
static const clap_plugin_t *CLAP_ABI create(const clap_plugin_factory_t *,
                                            const clap_host_t *,
                                            const char *id) {
  if (strcmp(id, descriptor.id))
    return nullptr;
  try {
    auto *s = new Instance;
    s->plugin = {&descriptor, s,    init,  destroy, activate,  deactivate,
                 start,       stop, reset, process, extension, main_callback};
    return &s->plugin;
  } catch (...) {
    return nullptr;
  }
}
static const clap_plugin_factory_t factory = {types, describe, create};
static bool CLAP_ABI entry_init(const char *) { return true; }
static void CLAP_ABI entry_deinit() {}
static const void *CLAP_ABI get_factory(const char *id) {
  return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &factory : nullptr;
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entry_init, entry_deinit, get_factory};
