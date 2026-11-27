/*
 * SampVoice Plus - Mobile Client C SDK
 *
 * Threading & memory ownership summary:
 * - Every input/output struct starts with `uint32_t struct_size` initialized by the caller
 *   to `sizeof(the_struct)` (must be at least the V1 size of that struct) for binary
 *   compatibility across SDK header revisions.
 * - For array parameters (`svp_set_player_entries`, `svp_get_speakers`, `svp_get_my_streams`),
 *   element 0's `struct_size` defines the element byte stride used to step all elements.
 *   When a caller supplies a struct larger than known by this library, trailing bytes beyond
 *   what this library recognizes are left untouched and output `struct_size` reports bytes written.
 * - Host callbacks (`send_control`, `get_camera`, `get_local_ped_position`, optional entity/LOS
 *   queries, and `decode_text`) are invoked strictly inside `svp_frame()` on the single game thread.
 *   Exceptions:
 *   - `get_bass_proc`: invoked on the background audio thread during `svp_create()`.
 *   - `log`: may be invoked from internal background audio/network threads or the game thread.
 * - `svp_create()` starts a single background audio thread per process; attempting a second
 *   concurrent `svp_create()` returns `SVP_ERR_STATE`.
 * - Caller-supplied buffers (`out_buf`, `out_keys`, `out_status`, `out_speakers`,
 *   `out_streams`) are owned by the caller and written synchronously before return.
 */

#ifndef SVPLUS_MOBILE_H
#define SVPLUS_MOBILE_H

#include <stddef.h>
#include <stdint.h>

#ifndef SVP_API
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(SVPLUS_MOBILE_EXPORTS)
#      define SVP_API __declspec(dllexport)
#    elif defined(SVPLUS_MOBILE_IMPORTS)
#      define SVP_API __declspec(dllimport)
#    else
#      define SVP_API
#    endif
#  elif defined(__GNUC__) && __GNUC__ >= 4
#    define SVP_API __attribute__((visibility("default")))
#  else
#    define SVP_API
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SVP_API_VERSION             1u

#define SVP_JOIN_PAYLOAD_SIZE       16u
#define SVP_MAX_NAME_BYTES          128u
#define SVP_MAX_STREAM_NAME_BYTES   128u
#define SVP_MAX_ENDPOINT_BYTES      128u
#define SVP_MAX_STREAM_TAGS         8u
#define SVP_MAX_PLAYERS             1004u
#define SVP_INVALID_OBJECT_ID       0xFFFFu

typedef int32_t svp_result_t;

enum {
    SVP_OK              = 0,
    SVP_ERR_INVALID_ARG = -1,
    SVP_ERR_STATE       = -2,
    SVP_ERR_BUFFER      = -3,
    SVP_ERR_MEMORY      = -4,
    SVP_ERR_INTERNAL    = -5,
    SVP_ERR_UNAVAILABLE = -6
};

enum {
    SVP_TALK_MODE_PUSH_TO_TALK = 0,
    SVP_TALK_MODE_TOGGLE       = 1
};

enum {
    SVP_BLACKLIST_SCOPE_SERVER = 0,
    SVP_BLACKLIST_SCOPE_ALL    = 1
};

enum {
    SVP_VOICE_STATE_OK          = 0,
    SVP_VOICE_STATE_NO_MICRO    = 1,
    SVP_VOICE_STATE_CANNOT_HEAR = 2
};

enum {
    SVP_STREAM_KIND_GLOBAL     = 0,
    SVP_STREAM_KIND_POSITIONAL = 1
};

enum {
    SVP_ICON_SPEAKER   = 0,
    SVP_ICON_RADIO     = 1,
    SVP_ICON_PHONE     = 2,
    SVP_ICON_MEGAPHONE = 3,
    SVP_ICON_POLICE    = 4,
    SVP_ICON_ADMIN     = 5,
    SVP_ICON_GLOBAL    = 6
};

enum {
    SVP_LOG_DEBUG = 0,
    SVP_LOG_INFO  = 1,
    SVP_LOG_WARN  = 2,
    SVP_LOG_ERROR = 3
};

typedef struct svp_session svp_session_t;

typedef struct svp_host_t {
    uint32_t struct_size;
    void*    user;

    /**
     * Mandatory. Queued outgoing control packets (`222` + header + payload) are
     * delivered strictly on the game thread inside `svp_frame()` via RakNet
     * HIGH_PRIORITY, RELIABLE, ordering channel 0.
     */
    void (*send_control)(void* user, const uint8_t* bytes, uint32_t size);

    /** Mandatory. Camera world pos, normalized forward, and normalized up vectors (game thread only). */
    int32_t (*get_camera)(void* user, float pos[3], float front[3], float up[3]);

    /** Mandatory. Local player ped world coordinates (game thread only). */
    int32_t (*get_local_ped_position)(void* user, float pos[3]);

    /** Optional. Remote streamed-in player world coordinates (game thread only). */
    int32_t (*get_player_position)(void* user, uint16_t player_id, float pos[3]);

    /** Optional. Vehicle world coordinates (game thread only). */
    int32_t (*get_vehicle_position)(void* user, uint16_t vehicle_id, float pos[3]);

    /** Optional. Global object world coordinates (`CreateObject`, game thread only inside `svp_frame()`). */
    int32_t (*get_object_position)(void* user, uint16_t object_id, float pos[3]);

    /** Optional. Returns non-zero if local player is currently occupying `vehicle_id` (game thread only inside `svp_frame()`). */
    int32_t (*is_local_in_vehicle)(void* user, uint16_t vehicle_id);

    /**
     * Optional. World line-of-sight check for voice occlusion (game thread only inside `svp_frame()`).
     * `ignore_object_id` is `SVP_INVALID_OBJECT_ID` when no target object should be excluded.
     * If omitted (NULL), wall voice occlusion stays disabled.
     */
    int32_t (*is_line_of_sight_clear)(void* user, const float from[3], const float to[3], uint16_t ignore_object_id);

    /**
     * Optional. Custom BASS symbol lookup (for statically linked or renamed BASS libraries).
     * Invoked on the background audio thread during `svp_create()`.
     * If omitted (NULL), the library tries `dlopen("libbass.so", RTLD_NOLOAD)` then `dlopen("libbass.so")` + `dlsym`.
     */
    void* (*get_bass_proc)(void* user, const char* symbol_name);

    /**
     * Optional. Converts legacy server stream/control text and player names to NUL-terminated UTF-8.
     * Invoked strictly on the game thread inside `svp_frame()`.
     * Returns non-zero on success; if omitted or returning 0, ISO-8859-1 -> UTF-8 fallback is used.
     */
    int32_t (*decode_text)(void* user, const uint8_t* raw, uint32_t len, char* out_utf8, uint32_t out_cap);

    /**
     * Optional. Log sink callback (`SVP_LOG_*`).
     * WARNING: May be invoked from internal background audio or network threads as well as the game thread.
     * If omitted (NULL), logs go to Android logcat (on Android targets).
     */
    void (*log)(void* user, int32_t level, const char* message);
} svp_host_t;

typedef struct svp_config_t {
    uint32_t struct_size;
    uint8_t  sound_enable;         /* 0 or 1 (default 1) */
    int32_t  sound_volume;         /* 0..100 (default 100) */
    int32_t  local_volume;         /* 0..200 (default 100) */
    int32_t  global_volume;        /* 0..200 (default 100) */
    uint8_t  sound_balancer;       /* 0 or 1 (default 0) */
    uint8_t  sound_filter;         /* 0 or 1 (default 0) */
    uint8_t  micro_enable;         /* 0 or 1 (default 1) */
    int32_t  micro_gain;           /* 0..200% (default 100) */
    uint8_t  micro_noise_suppress; /* 0 or 1 (default 1) */
    uint8_t  talk_mode;            /* SVP_TALK_MODE_* (default PUSH_TO_TALK) */
    uint8_t  deafened;             /* 0 or 1 (default 0) */
} svp_config_t;

typedef struct svp_player_entry_t {
    uint32_t struct_size;
    char     player_name[SVP_MAX_NAME_BYTES];
    char     server_endpoint[SVP_MAX_ENDPOINT_BYTES];
    uint8_t  scope;       /* SVP_BLACKLIST_SCOPE_* */
    uint8_t  muted;       /* 0 or 1 */
    int32_t  volume_pct;  /* 0..200 (100 = unity) */
} svp_player_entry_t;

typedef struct svp_status_t {
    uint32_t struct_size;
    uint8_t  connected;
    uint8_t  server_has_voice;
    uint8_t  voice_ready;
    uint8_t  mic_available;
    uint8_t  transmitting;
    float    input_level;
    uint8_t  server_muted;
    uint8_t  toggle_allowed;
    uint8_t  nearby_list_enabled;
    uint8_t  occlusion_enabled;
    float    latency_ms; /* < 0 (-1.0f) when unknown */
    char     server_endpoint[SVP_MAX_ENDPOINT_BYTES];
    uint64_t keys_generation;
    uint64_t speakers_generation;
} svp_status_t;

typedef struct svp_stream_tag_t {
    uint32_t color;
    char     name[SVP_MAX_STREAM_NAME_BYTES];
} svp_stream_tag_t;

typedef struct svp_speaker_info_t {
    uint32_t         struct_size;
    uint16_t         player_id;
    char             player_name[SVP_MAX_NAME_BYTES];
    char             stream_name[SVP_MAX_STREAM_NAME_BYTES];
    uint32_t         stream_color;
    uint8_t          icon_id;
    uint8_t          blocked;
    uint32_t         tag_count;
    svp_stream_tag_t tags[SVP_MAX_STREAM_TAGS];
} svp_speaker_info_t;

typedef struct svp_local_stream_t {
    uint32_t struct_size;
    uint32_t stream_id;
    uint32_t color;
    uint8_t  kind; /* SVP_STREAM_KIND_* */
    char     name[SVP_MAX_STREAM_NAME_BYTES];
} svp_local_stream_t;

/** Thread-safe. Returns the C API ABI version (`SVP_API_VERSION`). */
SVP_API uint32_t svp_get_api_version(void);

/** Thread-safe. Returns packed SampVoice Plus version (`MAJOR*10000 + MINOR*100 + PATCH`). */
SVP_API uint32_t svp_get_version(void);

/**
 * Any thread (game/launcher init). Creates the process-wide voice session and starts the
 * background 10 ms audio thread. Returns `SVP_ERR_STATE` if a session already exists.
 * `initial_config` may be NULL to use defaults.
 */
SVP_API svp_result_t svp_create(const svp_host_t* host,
                                const svp_config_t* initial_config,
                                svp_session_t** out_session);

/**
 * Caller must ensure no other `svp_*` call for `session` is running concurrently during
 * `svp_destroy()`, and no `svp_*` call is made for `session` after `svp_destroy()` begins.
 * Stops the audio thread, closes UDP and BASS capture/playback resources cleanly, clears
 * platform pointers, and frees the session. A subsequent `svp_create()` starts fresh.
 */
SVP_API void svp_destroy(svp_session_t* session);

/**
 * Any thread (typically RakNet thread). Writes the 16-byte SampVoice Plus RPC 25 join trailer
 * into `out_buf` and stores the server endpoint. `out_cap` must be >= `SVP_JOIN_PAYLOAD_SIZE`
 * (else `SVP_ERR_BUFFER`). Always pass the connected RakNet IPv4 address in network byte order
 * as `ipv4_net` (if `0`, `host` must be a numeric IPv4 string, not a hostname).
 */
SVP_API svp_result_t svp_build_join_payload(svp_session_t* session,
                                            const char* host,
                                            uint32_t ipv4_net,
                                            uint16_t port,
                                            uint8_t* out_buf,
                                            uint32_t out_cap,
                                            uint32_t* out_len);

/** Any thread (typically RakNet thread). Notifies the voice session when RakNet connection state starts or ends. */
SVP_API svp_result_t svp_set_connected(svp_session_t* session, uint8_t connected);

/**
 * Any thread (typically RakNet thread). Enqueues an incoming RakNet packet starting at ID `222`
 * to be processed in order inside `svp_frame()`. Sets `*out_consumed` to 1 if packet ID was
 * `222` (else 0). Internal queue holds up to 8192 packets / 4 MB; newer packets are dropped with
 * a rate-limited warning if `svp_frame()` stalls.
 */
SVP_API svp_result_t svp_on_control_packet(svp_session_t* session,
                                           const uint8_t* data,
                                           uint32_t size,
                                           uint8_t* out_consumed);

/**
 * Single game thread only, once per frame (including loading screens).
 * Decodes pending player names via `decode_text`, applies queued incoming `222` control packets
 * in arrival order, drains outgoing control packets to `host->send_control`, and updates 3D
 * spatial snapshots and optional LOS occlusion.
 */
SVP_API svp_result_t svp_frame(svp_session_t* session);

/**
 * Any thread. Stages a connected player's raw display name for deferred decoding and publication
 * on the game thread inside `svp_frame()`.
 */
SVP_API svp_result_t svp_set_player_name(svp_session_t* session, uint16_t player_id, const char* name);

/** Any thread. Stages removal of a disconnected player's cached name for `svp_frame()`. */
SVP_API svp_result_t svp_clear_player(svp_session_t* session, uint16_t player_id);

/** Any thread. Sets touch/button activation key state (`vk` Virtual-Key code, `down` 0 or 1). */
SVP_API svp_result_t svp_set_key_state(svp_session_t* session, uint8_t vk_code, uint8_t down);

/**
 * Any thread (non-blocking). Copies server-registered activation Virtual-Key codes (`AddKey`),
 * returning the active count in `out_count` and generation counter in `out_generation`.
 * `out_keys` may be NULL when `max_keys == 0` to query total count only.
 */
SVP_API svp_result_t svp_get_keys(svp_session_t* session,
                                  uint8_t* out_keys,
                                  uint32_t max_keys,
                                  uint32_t* out_count,
                                  uint64_t* out_generation);

/** Any thread. Blocks touch/key activation transmission while chat or a dialog is open. */
SVP_API svp_result_t svp_set_input_blocked(svp_session_t* session, uint8_t blocked);

/** Any thread. Stops transmission and mutes playback cleanly while app is backgrounded. */
SVP_API svp_result_t svp_set_suspended(svp_session_t* session, uint8_t suspended);

/** Any thread. Publishes an updated audio/capture/deafen configuration snapshot atomically. */
SVP_API svp_result_t svp_set_config(svp_session_t* session, const svp_config_t* config);

/**
 * Any thread. Replaces per-player mute and volume overrides managed by the launcher.
 * `entries[0].struct_size` defines the element byte stride across all `count` entries
 * (must be >= V1 `sizeof(svp_player_entry_t)`).
 */
SVP_API svp_result_t svp_set_player_entries(svp_session_t* session,
                                            const svp_player_entry_t* entries,
                                            uint32_t count);

/** Any thread (non-blocking). Copies live connection, mic, server option, and generation state. */
SVP_API svp_result_t svp_get_status(svp_session_t* session, svp_status_t* out_status);

/**
 * Any thread (non-blocking). Copies active speakers (UTF-8 strings truncated at codepoint bounds).
 * Caller initializes `out_speakers[0].struct_size` (used as the array element byte stride and
 * must be >= V1 `sizeof(svp_speaker_info_t)`), or passes NULL / 0 for count query.
 */
SVP_API svp_result_t svp_get_speakers(svp_session_t* session,
                                      svp_speaker_info_t* out_speakers,
                                      uint32_t max_speakers,
                                      uint32_t* out_count);

/** Any thread (non-blocking). Returns `SVP_VOICE_STATE_*` (`>= 0`) or `< 0` (`SVP_ERR_*`). */
SVP_API int32_t svp_get_player_voice_state(svp_session_t* session, uint16_t player_id);

/**
 * Any thread (non-blocking). Copies streams where the local player is attached as speaker.
 * Caller initializes `out_streams[0].struct_size` (used as the array element byte stride and
 * must be >= V1 `sizeof(svp_local_stream_t)`), or passes NULL / 0 for count query.
 */
SVP_API svp_result_t svp_get_my_streams(svp_session_t* session,
                                        svp_local_stream_t* out_streams,
                                        uint32_t max_streams,
                                        uint32_t* out_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SVPLUS_MOBILE_H */
