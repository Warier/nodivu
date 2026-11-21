#pragma once
#include <clap/clap.h>
// Optional owner/main-thread ABI. Paths are copied before returning. Null
// clears. A slot is 0..count-1. Status follows file-transport/1; times are
// milliseconds. No pointer ownership crosses this extension. Audio is the
// single stereo CLAP output.
#define NODIVU_FILE_BANK "org.nodivu.file-bank/1"
typedef struct nodivu_file_bank {
  uint32_t(CLAP_ABI *count)(const clap_plugin_t *);
  bool(CLAP_ABI *load)(const clap_plugin_t *, uint32_t slot, const char *utf8);
  bool(CLAP_ABI *trigger)(const clap_plugin_t *, uint32_t slot);
  bool(CLAP_ABI *stop)(const clap_plugin_t *, uint32_t slot);
  int32_t(CLAP_ABI *status)(const clap_plugin_t *, uint32_t slot);
  uint32_t(CLAP_ABI *position)(const clap_plugin_t *, uint32_t slot);
  uint32_t(CLAP_ABI *duration)(const clap_plugin_t *, uint32_t slot);
} nodivu_file_bank_t;
