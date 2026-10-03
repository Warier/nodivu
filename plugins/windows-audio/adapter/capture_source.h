#pragma once
#include <clap/clap.h>
#include <stdint.h>

// All extension calls are on the host/main thread, never the audio thread.
// Strings are bounded UTF-8, copied on configure; no pointer escapes a call.
#define NODIVU_CAPTURE_SOURCE "org.nodivu.capture-source/1"
typedef struct nodivu_capture_target {
    uint32_t process_id;
    uint32_t reserved;
    uint64_t creation_time;
    char executable[4096];
    char label[128];
} nodivu_capture_target_t;
typedef struct nodivu_capture_state {
    int32_t status; // 0 empty, 1 starting, 2 capturing, 3 waiting, 4 ambiguous, 5 error
    int32_t error;  // HRESULT, retained separately from product state
    uint32_t process_id;
    uint32_t queued_frames;
    uint64_t captured_frames;
    uint64_t dropped_frames;
    uint64_t underflow_frames;
    uint64_t discontinuities;
    uint64_t packet_age_us;
    uint64_t max_packet_age_us;
    uint32_t target_buffer_frames;
    uint32_t sample_rate;
} nodivu_capture_state_t;
typedef struct nodivu_capture_source {
    int32_t(CLAP_ABI *refresh)(const clap_plugin_t*);
    bool(CLAP_ABI *target)(const clap_plugin_t*, uint32_t index, nodivu_capture_target_t*);
    // 0 clear, 1 app (exact executable; optional live PID+creation), 2 exclude host tree.
    bool(CLAP_ABI *configure)(const clap_plugin_t*, uint32_t mode, const nodivu_capture_target_t*);
    bool(CLAP_ABI *snapshot)(const clap_plugin_t*, nodivu_capture_state_t*);
} nodivu_capture_source_t;
