#pragma once
#include <clap/clap.h>
// Nodivu extension v1, main-thread-only; copied UTF-8 path, no pointer
// retained. Local regular files only. nullptr clears. load queues work and
// returns immediately.
#define NODIVU_FILE_PLAYER "org.nodivu.file-player/1"
typedef struct nodivu_file_player {
  bool(CLAP_ABI *load)(const clap_plugin_t *, const char *utf8);
  bool(CLAP_ABI *play)(const clap_plugin_t *);
  int32_t(CLAP_ABI *status)(const clap_plugin_t *);
} nodivu_file_player_t;

// Optional, independent extension. Main-thread calls, milliseconds, no pointers
// retained. play resumes; at EOF it restarts. seek preserves play/pause state.
#define NODIVU_FILE_TRANSPORT "org.nodivu.file-transport/1"
typedef struct nodivu_file_transport {
  bool(CLAP_ABI *play)(const clap_plugin_t *);
  bool(CLAP_ABI *pause)(const clap_plugin_t *);
  bool(CLAP_ABI *seek)(const clap_plugin_t *, uint32_t milliseconds);
  uint32_t(CLAP_ABI *position_ms)(const clap_plugin_t *);
  uint32_t(CLAP_ABI *duration_ms)(const clap_plugin_t *);
  int32_t(CLAP_ABI *status)(const clap_plugin_t *); // adds 5=paused
} nodivu_file_transport_t;
