#include "ra_client_zelda3.h"

#include "ra_http.h"
#include "ra_memory.h"
#include "ra_state.h"

#include <rc_consoles.h>

#include <SDL.h>

#include <stdio.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

enum {
  kRaExpectedGameId = 355,
  kRaEventTypeCount = RC_CLIENT_EVENT_SUBSET_COMPLETED + 1,
};

static const char kRaExpectedHash[] = "608c22b8ff930c62dc2de54bcd6eba72";

typedef struct RaClientZelda3Config {
  int enabled;
  int spectator;
  int secret_is_token;
  int verified;
  char client_name[65];
  char client_version[33];
  char username[128];
  char secret[256];
  char android_release[32];
  char android_model[96];
} RaClientZelda3Config;

typedef struct RaClientZelda3Commands {
  RaClientZelda3Config config;
  int configure_pending;
  int logout_pending;
  int paused;
  char snapshot[2048];
  char ui_model[64 * 1024];
} RaClientZelda3Commands;

static RaClientZelda3Config g_config;
static RaClientZelda3Commands g_commands;
static SDL_mutex *g_command_mutex;
static SDL_SpinLock g_command_init_lock;
static rc_client_t *g_client;
static int g_authenticated;
static int g_game_valid;
static int g_unsupported_game;
static int g_disconnected;
static int g_paused;
static int g_login_result;
static int g_game_result;
static int g_logout_done;
static uint32_t g_disconnect_count;
static uint32_t g_reconnect_count;
static uint32_t g_event_counts[kRaEventTypeCount];
static uint32_t g_frame_count;
static uint32_t g_last_event_type;
static char g_last_event[256];
static char g_user_agent[384];
static uint8_t g_pending_progress[RA_STATE_MAX_PROGRESS];
static size_t g_pending_progress_size;
static int g_pending_progress_valid;
static int g_pending_progress_reset;
static int g_ui_dirty = 1;
static uint32_t g_ui_last_refresh_frame;
static char g_ui_build[64 * 1024];

static int RaClientZelda3_EnsureCommandMutex(void) {
  SDL_AtomicLock(&g_command_init_lock);
  if (!g_command_mutex)
    g_command_mutex = SDL_CreateMutex();
  SDL_AtomicUnlock(&g_command_init_lock);
  return g_command_mutex != NULL;
}

static void RaClientZelda3_ClearConfig(RaClientZelda3Config *config) {
  if (!config)
    return;
  SDL_memset(config->secret, 0, sizeof(config->secret));
  SDL_memset(config, 0, sizeof(*config));
}

static void RaClientZelda3_CopyConfig(RaClientZelda3Config *config, int enabled,
                                      int spectator, int verified,
                                      const char *client_name,
                                      const char *client_version,
                                      const char *username, const char *secret,
                                      int secret_is_token,
                                      const char *android_release,
                                      const char *android_model) {
  SDL_memset(config, 0, sizeof(*config));
  config->enabled = enabled && username && username[0] && secret && secret[0];
  config->spectator = spectator != 0;
  config->verified = verified != 0;
  config->secret_is_token = secret_is_token != 0;
  SDL_strlcpy(config->client_name,
              client_name && client_name[0] ? client_name : "Zelda3AndroidRA",
              sizeof(config->client_name));
  SDL_strlcpy(config->client_version,
              client_version && client_version[0] ? client_version : "0.1.0",
              sizeof(config->client_version));
  SDL_strlcpy(config->android_release,
              android_release && android_release[0] ? android_release : "unknown",
              sizeof(config->android_release));
  SDL_strlcpy(config->android_model,
              android_model && android_model[0] ? android_model : "unknown",
              sizeof(config->android_model));
  if (config->enabled) {
    SDL_strlcpy(config->username, username, sizeof(config->username));
    SDL_strlcpy(config->secret, secret, sizeof(config->secret));
    if (!config->username[0] || !config->secret[0])
      RaClientZelda3_ClearConfig(config);
  }
}

static void RaClientZelda3_Log(const char *message) {
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_INFO, "Zelda3RA", "%s", message);
#else
  fprintf(stderr, "Zelda3RA: %s\n", message);
#endif
}

static const char *RaClientZelda3_ResultClass(int result) {
  if (result == RC_OK)
    return "ok";
  return result < 0 ? "transport" : "server";
}

static void RaClientZelda3_LogResult(const char *operation, int result) {
  char message[96];
  SDL_snprintf(message, sizeof(message), "%s result=%d class=%s", operation,
               result, RaClientZelda3_ResultClass(result));
  RaClientZelda3_Log(message);
}

#ifdef __ANDROID__
static jclass g_bridge_class;
static jmethodID g_persist_token_method;

static void RaClientZelda3_PersistToken(const rc_client_user_t *user) {
  JNIEnv *env;
  jclass local_class;
  jstring username;
  jstring token;

  if (!user || !user->username || !user->token)
    return;
  env = (JNIEnv *)SDL_AndroidGetJNIEnv();
  if (!env)
    return;
  if (!g_bridge_class) {
    local_class = (*env)->FindClass(env,
        "com/dishii/zelda3/RetroAchievementsBridge");
    if (!local_class || (*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
      return;
    }
    g_bridge_class = (jclass)(*env)->NewGlobalRef(env, local_class);
    (*env)->DeleteLocalRef(env, local_class);
    if (!g_bridge_class)
      return;
    g_persist_token_method = (*env)->GetStaticMethodID(env, g_bridge_class,
        "persistReturnedToken", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!g_persist_token_method || (*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
      (*env)->DeleteGlobalRef(env, g_bridge_class);
      g_bridge_class = NULL;
      return;
    }
  }
  username = (*env)->NewStringUTF(env, user->username);
  token = (*env)->NewStringUTF(env, user->token);
  if (username && token)
    (*env)->CallStaticVoidMethod(env, g_bridge_class, g_persist_token_method,
                                 username, token);
  if ((*env)->ExceptionCheck(env))
    (*env)->ExceptionClear(env);
  if (username)
    (*env)->DeleteLocalRef(env, username);
  if (token)
    (*env)->DeleteLocalRef(env, token);
}
#else
static void RaClientZelda3_PersistToken(const rc_client_user_t *user) {
  (void)user;
}
#endif

static void RaClientZelda3_CopyEventText(char *destination, size_t destination_size,
                                         const char *source) {
  size_t i;

  if (!destination || destination_size == 0)
    return;
  if (!source) {
    destination[0] = '\0';
    return;
  }
  SDL_strlcpy(destination, source, destination_size);
  for (i = 0; destination[i]; ++i) {
    if ((unsigned char)destination[i] < 0x20)
      destination[i] = ' ';
  }
}

static void RaClientZelda3_SetLastEvent(const rc_client_event_t *event) {
  const char *title = NULL;
  const char *description = NULL;
  const char *value = NULL;
  char title_copy[80];
  char description_copy[96];
  char value_copy[32];

  if (event->achievement) {
    title = event->achievement->title;
    description = event->achievement->description;
    value = event->achievement->measured_progress;
  } else if (event->leaderboard) {
    title = event->leaderboard->title;
    description = event->leaderboard->description;
    value = event->leaderboard->tracker_value;
  } else if (event->leaderboard_tracker) {
    value = event->leaderboard_tracker->display;
  } else if (event->server_error) {
    title = event->server_error->api;
    value = "server_error";
  } else if (event->subset) {
    title = event->subset->title;
  }
  RaClientZelda3_CopyEventText(title_copy, sizeof(title_copy), title);
  RaClientZelda3_CopyEventText(description_copy, sizeof(description_copy),
                               description);
  RaClientZelda3_CopyEventText(value_copy, sizeof(value_copy), value);
  SDL_snprintf(g_last_event, sizeof(g_last_event), "%u:%s:%s:%s", event->type,
               title_copy, description_copy, value_copy);
  g_ui_dirty = 1;
}

static void RaClientZelda3_ApplyPendingProgress(void) {
  int result;

  if (!g_client || !g_game_valid)
    return;
  if (g_pending_progress_valid) {
    result = rc_client_deserialize_progress_sized(g_client, g_pending_progress,
                                                  g_pending_progress_size);
    g_pending_progress_valid = 0;
    g_pending_progress_size = 0;
    if (result == RC_OK) {
      RaClientZelda3_Log("progress restore=ok");
      return;
    }
    RaClientZelda3_LogResult("progress restore", result);
    g_pending_progress_reset = 1;
  }
  if (g_pending_progress_reset) {
    rc_client_deserialize_progress_sized(g_client, NULL, 0);
    g_pending_progress_reset = 0;
    RaClientZelda3_Log("progress restore=reset");
  }
}

static void RC_CCONV RaClientZelda3_OnGameLoaded(int result,
                                                  const char *error_message,
                                                  rc_client_t *client,
                                                  void *userdata) {
  const rc_client_game_t *game;

  (void)error_message;
  (void)userdata;
  g_game_result = result;
  if (result != RC_OK || !client) {
    g_unsupported_game = 1;
    RaClientZelda3_LogResult("game", result);
    return;
  }
  game = rc_client_get_game_info(client);
  if (!game || game->id != kRaExpectedGameId ||
      game->console_id != RC_CONSOLE_SUPER_NINTENDO || !game->hash ||
      SDL_strcmp(game->hash, kRaExpectedHash) != 0) {
    g_unsupported_game = 1;
    RaClientZelda3_Log("game result=0 class=integrity");
    rc_client_unload_game(client);
    return;
  }
  g_game_valid = 1;
  RaClientZelda3_ApplyPendingProgress();
  g_ui_dirty = 1;
}

static void RC_CCONV RaClientZelda3_OnLogin(int result,
                                             const char *error_message,
                                             rc_client_t *client,
                                             void *userdata) {
  const rc_client_user_t *user;

  (void)error_message;
  (void)userdata;
  g_login_result = result;
  if (result != RC_OK || !client) {
    RaClientZelda3_LogResult("login", result);
    return;
  }
  user = rc_client_get_user_info(client);
  if (!user) {
    g_login_result = -1;
    RaClientZelda3_LogResult("login", g_login_result);
    return;
  }
  g_authenticated = 1;
  g_ui_dirty = 1;
  RaClientZelda3_PersistToken(user);
  rc_client_begin_load_game(client, kRaExpectedHash, RaClientZelda3_OnGameLoaded,
                            NULL);
}

static void RC_CCONV RaClientZelda3_OnEvent(const rc_client_event_t *event,
                                             rc_client_t *client) {
  (void)client;
  if (!event)
    return;
  g_last_event_type = event->type;
  if (event->type < kRaEventTypeCount)
    ++g_event_counts[event->type];
  RaClientZelda3_SetLastEvent(event);
  {
    char message[64];
    SDL_snprintf(message, sizeof(message), "event type=%u", event->type);
    RaClientZelda3_Log(message);
  }

  if (event->type == RC_CLIENT_EVENT_DISCONNECTED) {
    g_disconnected = 1;
    ++g_disconnect_count;
  } else if (event->type == RC_CLIENT_EVENT_RECONNECTED) {
    g_disconnected = 0;
    ++g_reconnect_count;
  }
  g_ui_dirty = 1;
}

static void RC_CCONV RaClientZelda3_OnLog(const char *message,
                                           const rc_client_t *client) {
  (void)message;
  (void)client;
  RaClientZelda3_Log("client error");
}

static void RaClientZelda3_DestroyClient(void) {
  if (g_client) {
    rc_client_destroy(g_client);
    g_client = NULL;
  }
  RaHttpShutdown();
}

static void RaClientZelda3_ResetRuntimeState(void) {
  g_authenticated = 0;
  g_game_valid = 0;
  g_unsupported_game = 0;
  g_disconnected = 0;
  g_login_result = 1;
  g_game_result = 1;
  g_logout_done = 0;
  g_disconnect_count = 0;
  g_reconnect_count = 0;
  g_frame_count = 0;
  g_last_event_type = 0;
  g_last_event[0] = '\0';
  SDL_memset(g_event_counts, 0, sizeof(g_event_counts));
  g_user_agent[0] = '\0';
  g_pending_progress_size = 0;
  g_pending_progress_valid = 0;
  g_pending_progress_reset = 0;
  g_ui_dirty = 1;
  g_ui_last_refresh_frame = 0;
}

static void RaClientZelda3_InitializeClient(void) {
  char clause[96];

  if (!g_config.enabled || g_client)
    return;
  RaHttpInitialize();
  g_client = rc_client_create(RaMemoryRead, RaHttpDispatch);
  if (!g_client) {
    RaClientZelda3_Log("client result=-1 class=create");
    return;
  }
  rc_client_set_hardcore_enabled(g_client, 0);
  rc_client_set_spectator_mode_enabled(g_client, g_config.spectator);
  rc_client_set_event_handler(g_client, RaClientZelda3_OnEvent);
  rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_ERROR,
                           RaClientZelda3_OnLog);
  rc_client_get_user_agent_clause(g_client, clause, sizeof(clause));
  SDL_snprintf(g_user_agent, sizeof(g_user_agent), "%s/%s (Android %s; %s) %s",
               g_config.client_name, g_config.client_version,
               g_config.android_release, g_config.android_model, clause);
  RaHttpSetUserAgent(g_user_agent);

  if (g_config.secret_is_token)
    rc_client_begin_login_with_token(g_client, g_config.username, g_config.secret,
                                     RaClientZelda3_OnLogin, NULL);
  else
    rc_client_begin_login_with_password(g_client, g_config.username, g_config.secret,
                                        RaClientZelda3_OnLogin, NULL);
  SDL_memset(g_config.secret, 0, sizeof(g_config.secret));
}

static void RaClientZelda3_CopyUiText(char *destination, size_t destination_size,
                                      const char *source) {
  size_t i;

  if (!destination || destination_size == 0)
    return;
  if (!source) {
    destination[0] = '\0';
    return;
  }
  for (i = 0; i + 1 < destination_size && source[i]; ++i) {
    unsigned char value = (unsigned char)source[i];
    destination[i] = value < 0x20 || value == '\t' ? ' ' : (char)value;
  }
  destination[i] = '\0';
}

static int RaClientZelda3_AppendUiRecord(char *model, size_t model_size,
                                         size_t *offset, const char *record) {
  size_t record_size = SDL_strlen(record);

  if (!offset || *offset + record_size >= model_size)
    return 0;
  SDL_memcpy(model + *offset, record, record_size);
  *offset += record_size;
  model[*offset] = '\0';
  return 1;
}

static void RaClientZelda3_UpdateUiModel(void) {
  const rc_client_user_t *user = NULL;
  const rc_client_game_t *game = NULL;
  rc_client_user_game_summary_t summary = {0};
  rc_client_achievement_list_t *list = NULL;
  char username[128];
  char game_title[160];
  char rich_presence[160] = "";
  char last_event[256];
  char record[640];
  size_t offset = 0;
  uint32_t i;
  int disconnected = 0;
  const char *mode = "disabled";
  const char *status;

  if (g_config.enabled)
    mode = g_config.spectator ? "spectator" : "casual";
  if (!g_config.verified)
    status = "unverified";
  else if (!g_config.enabled)
    status = "disabled";
  else if (g_disconnected)
    status = "disconnected";
  else if (g_game_valid)
    status = g_authenticated ? "ready" : "loading";
  else if (g_login_result != 1 || g_game_result != 1 || g_unsupported_game)
    status = "error";
  else
    status = "connecting";

  username[0] = '\0';
  game_title[0] = '\0';
  RaClientZelda3_CopyUiText(last_event, sizeof(last_event), g_last_event);
  if (g_client) {
    user = rc_client_get_user_info(g_client);
    game = rc_client_get_game_info(g_client);
    rc_client_get_user_game_summary(g_client, &summary);
    disconnected = g_disconnected;
    if (user)
      RaClientZelda3_CopyUiText(username, sizeof(username),
                                user->display_name ? user->display_name :
                                user->username);
    if (game)
      RaClientZelda3_CopyUiText(game_title, sizeof(game_title), game->title);
    if (rc_client_has_rich_presence(g_client)) {
      char value[160];
      rc_client_get_rich_presence_message(g_client, value, sizeof(value));
      RaClientZelda3_CopyUiText(rich_presence, sizeof(rich_presence), value);
    }
  }
  SDL_snprintf(record, sizeof(record),
               "V\t1\nM\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%d\t0\t%d\n",
               mode, status, username, game_title, game ? game->id : 0,
               summary.num_unlocked_achievements, summary.num_core_achievements,
               summary.points_unlocked, user ? user->score : 0, rich_presence,
               last_event, disconnected, g_config.spectator);
  RaClientZelda3_AppendUiRecord(g_ui_build, sizeof(g_ui_build), &offset, record);

  if (g_client && g_game_valid) {
    list = rc_client_create_achievement_list(
        g_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
  }
  if (list) {
    for (i = 0; i < list->num_buckets; ++i) {
      const rc_client_achievement_bucket_t *bucket = &list->buckets[i];
      uint32_t j;
      char bucket_label[96];

      RaClientZelda3_CopyUiText(bucket_label, sizeof(bucket_label), bucket->label);
      for (j = 0; j < bucket->num_achievements; ++j) {
        const rc_client_achievement_t *achievement = bucket->achievements[j];
        char title[144];
        char description[240];
        char progress[32];

        RaClientZelda3_CopyUiText(title, sizeof(title), achievement->title);
        RaClientZelda3_CopyUiText(description, sizeof(description),
                                  achievement->description);
        RaClientZelda3_CopyUiText(progress, sizeof(progress),
                                  achievement->measured_progress);
        SDL_snprintf(record, sizeof(record), "A\t%s\t%u\t%s\t%s\t%u\t%d\t%s\n",
                     bucket_label, achievement->id, title, description,
                     achievement->points,
                     achievement->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED,
                     progress);
        if (!RaClientZelda3_AppendUiRecord(g_ui_build, sizeof(g_ui_build),
                                           &offset, record))
          goto done;
      }
    }
  }
done:
  if (list)
    rc_client_destroy_achievement_list(list);
  if (RaClientZelda3_EnsureCommandMutex()) {
    SDL_LockMutex(g_command_mutex);
    SDL_strlcpy(g_commands.ui_model, g_ui_build, sizeof(g_commands.ui_model));
    SDL_UnlockMutex(g_command_mutex);
  }
  g_ui_dirty = 0;
  g_ui_last_refresh_frame = g_frame_count;
}

static void RaClientZelda3_UpdateSnapshot(void) {
  const rc_client_user_t *user = NULL;
  const rc_client_game_t *game = NULL;
  rc_client_user_game_summary_t summary = {0};
  RaHttpStats http_stats = {0};
  char rich_presence[128] = "";
  char events[256] = "";
  size_t event_offset = 0;
  uint32_t i;
  int rp_supported = 0;
  int state = 0;
  char snapshot[2048];

  if (g_client) {
    user = rc_client_get_user_info(g_client);
    game = rc_client_get_game_info(g_client);
    state = rc_client_get_load_game_state(g_client);
    rc_client_get_user_game_summary(g_client, &summary);
    rp_supported = rc_client_has_rich_presence(g_client);
    if (rp_supported)
      rc_client_get_rich_presence_message(g_client, rich_presence,
                                          sizeof(rich_presence));
  }
  for (i = 0; i < kRaEventTypeCount && event_offset < sizeof(events); ++i) {
    int written = SDL_snprintf(events + event_offset, sizeof(events) - event_offset,
                               "%s%u", i == 0 ? "" : ",", g_event_counts[i]);
    if (written < 0 || (size_t)written >= sizeof(events) - event_offset)
      break;
    event_offset += (size_t)written;
  }
  RaHttpGetStats(&http_stats);
  SDL_snprintf(
      snapshot, sizeof(snapshot),
      "enabled=%u mode=%s lifecycle=%s state=%d login=%s login_result=%d/%s "
      "game_result=%d/%s game=%u console=%u hash=%s expected_hash=%s hardcore=0 "
      "spectator=%u summary=core:%u/unlocked:%u/unsupported:%u rp_supported=%u "
      "rp=%s disconnect=%u reconnect=%u disconnected=%u events=%s last_event=%s "
      "invalid_reads=%u frames=%u http=%u/%u/%u/%u/%u logout=%u ua=%s",
      g_config.enabled, g_config.spectator ? "spectator" :
      (g_config.enabled ? "casual" : "disabled"), g_paused ? "paused" : "resumed",
      state, g_authenticated ? "authenticated" :
      (g_login_result == 1 ? "pending" : "failed"), g_login_result,
      RaClientZelda3_ResultClass(g_login_result), g_game_result,
      RaClientZelda3_ResultClass(g_game_result), game ? game->id : 0,
      game ? game->console_id : 0, game && game->hash ?
      (SDL_strcmp(game->hash, kRaExpectedHash) == 0 ? "match" : "mismatch") :
      "none", kRaExpectedHash, g_config.spectator,
      summary.num_core_achievements, summary.num_unlocked_achievements,
      summary.num_unsupported_achievements, rp_supported, rich_presence,
      g_disconnect_count, g_reconnect_count, g_disconnected, events, g_last_event,
      RaMemoryGetInvalidReadCount(), g_frame_count, http_stats.dispatched,
      http_stats.completed, http_stats.dropped, http_stats.pending,
      http_stats.queued_completions, g_logout_done, g_user_agent);
  if (RaClientZelda3_EnsureCommandMutex()) {
    SDL_LockMutex(g_command_mutex);
    SDL_strlcpy(g_commands.snapshot, snapshot, sizeof(g_commands.snapshot));
    SDL_UnlockMutex(g_command_mutex);
  }
  if (g_ui_dirty || g_frame_count - g_ui_last_refresh_frame >= 120)
    RaClientZelda3_UpdateUiModel();
}

void RaClientZelda3_QueueConfigure(int enabled, int spectator, int verified,
                                   const char *client_name,
                                   const char *client_version,
                                   const char *username, const char *secret,
                                   int secret_is_token,
                                   const char *android_release,
                                   const char *android_model) {
  if (!RaClientZelda3_EnsureCommandMutex())
    return;
  SDL_LockMutex(g_command_mutex);
  RaClientZelda3_CopyConfig(&g_commands.config, enabled, spectator, verified,
                            client_name,
                            client_version, username, secret, secret_is_token,
                            android_release, android_model);
  g_commands.configure_pending = 1;
  SDL_UnlockMutex(g_command_mutex);
}

void RaClientZelda3_QueueLogout(void) {
  if (!RaClientZelda3_EnsureCommandMutex())
    return;
  SDL_LockMutex(g_command_mutex);
  g_commands.logout_pending = 1;
  SDL_UnlockMutex(g_command_mutex);
}

void RaClientZelda3_SetPaused(int paused) {
  if (!RaClientZelda3_EnsureCommandMutex())
    return;
  SDL_LockMutex(g_command_mutex);
  g_commands.paused = paused != 0;
  SDL_UnlockMutex(g_command_mutex);
}

void RaClientZelda3_Initialize(void) {
  RaClientZelda3_Pump();
}

void RaClientZelda3_Pump(void) {
  RaClientZelda3Config pending_config;
  int configure_pending = 0;
  int logout_pending = 0;
  int paused = g_paused;
  int resumed;

  SDL_memset(&pending_config, 0, sizeof(pending_config));
  if (RaClientZelda3_EnsureCommandMutex()) {
    SDL_LockMutex(g_command_mutex);
    paused = g_commands.paused;
    logout_pending = g_commands.logout_pending;
    g_commands.logout_pending = 0;
    if (g_commands.configure_pending) {
      pending_config = g_commands.config;
      SDL_memset(&g_commands.config, 0, sizeof(g_commands.config));
      g_commands.configure_pending = 0;
      configure_pending = 1;
    }
    SDL_UnlockMutex(g_command_mutex);
  }
  resumed = g_paused && !paused;
  g_paused = paused;

  if (configure_pending) {
    RaClientZelda3_DestroyClient();
    RaClientZelda3_ClearConfig(&g_config);
    g_config = pending_config;
    RaClientZelda3_ResetRuntimeState();
  }
  if (g_paused) {
    if (logout_pending && RaClientZelda3_EnsureCommandMutex()) {
      SDL_LockMutex(g_command_mutex);
      g_commands.logout_pending = 1;
      SDL_UnlockMutex(g_command_mutex);
    }
    RaClientZelda3_UpdateSnapshot();
    return;
  }

  RaClientZelda3_InitializeClient();
  RaHttpPumpCompletions();
  if (logout_pending) {
    if (g_client)
      rc_client_logout(g_client);
    g_authenticated = 0;
    g_game_valid = 0;
    g_config.enabled = 0;
    SDL_memset(g_config.username, 0, sizeof(g_config.username));
    g_logout_done = 1;
    g_ui_dirty = 1;
  }
  /* SDL can suspend this thread between Java lifecycle callbacks; only the
   * resumed game thread performs the required immediate idle. */
  if (resumed && g_client)
    rc_client_idle(g_client);
  RaClientZelda3_UpdateSnapshot();
}

void RaClientZelda3_Idle(void) {
  if (!g_paused && g_client)
    rc_client_idle(g_client);
  RaClientZelda3_UpdateSnapshot();
}

void RaClientZelda3_DoFrame(void) {
  if (!g_paused && g_client) {
    rc_client_do_frame(g_client);
    ++g_frame_count;
  }
  RaClientZelda3_UpdateSnapshot();
}

void RaClientZelda3_Reset(void) {
  if (!g_paused && g_client)
    rc_client_reset(g_client);
  RaClientZelda3_UpdateSnapshot();
}

int RaClientZelda3_IsCasualIntegrityEnabled(void) {
  return g_config.enabled && !g_config.spectator;
}

size_t RaClientZelda3_SerializeProgress(uint8_t *buffer, size_t buffer_size) {
  size_t size;

  if (!g_client || !g_game_valid || !buffer)
    return 0;
  size = rc_client_progress_size(g_client);
  if (size == 0 || size > buffer_size || size > RA_STATE_MAX_PROGRESS) {
    RaClientZelda3_Log("progress save=unavailable");
    return 0;
  }
  if (rc_client_serialize_progress_sized(g_client, buffer, size) != RC_OK) {
    RaClientZelda3_Log("progress save=failed");
    return 0;
  }
  return size;
}

int RaClientZelda3_DeserializeProgress(const uint8_t *buffer, size_t buffer_size) {
  int result;

  if (!buffer || buffer_size == 0) {
    RaClientZelda3_ResetProgress();
    return 1;
  }
  if (buffer_size > RA_STATE_MAX_PROGRESS)
    return 0;
  if (!g_client || !g_game_valid) {
    SDL_memcpy(g_pending_progress, buffer, buffer_size);
    g_pending_progress_size = buffer_size;
    g_pending_progress_valid = 1;
    g_pending_progress_reset = 0;
    RaClientZelda3_Log("progress restore=pending");
    return 1;
  }
  result = rc_client_deserialize_progress_sized(g_client, buffer, buffer_size);
  if (result == RC_OK) {
    g_pending_progress_valid = 0;
    g_pending_progress_size = 0;
    g_pending_progress_reset = 0;
    RaClientZelda3_Log("progress restore=ok");
    g_ui_dirty = 1;
    return 1;
  }
  RaClientZelda3_LogResult("progress restore", result);
  RaClientZelda3_ResetProgress();
  return 0;
}

void RaClientZelda3_ResetProgress(void) {
  g_pending_progress_valid = 0;
  g_pending_progress_size = 0;
  g_pending_progress_reset = 1;
  RaClientZelda3_ApplyPendingProgress();
  g_ui_dirty = 1;
}

size_t RaClientZelda3_SnapshotCached(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0)
    return 0;
  if (!RaClientZelda3_EnsureCommandMutex()) {
    buffer[0] = '\0';
    return 0;
  }
  SDL_LockMutex(g_command_mutex);
  SDL_strlcpy(buffer, g_commands.snapshot, buffer_size);
  SDL_UnlockMutex(g_command_mutex);
  return SDL_strlen(buffer);
}

size_t RaClientZelda3_UiModelCached(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0)
    return 0;
  if (!RaClientZelda3_EnsureCommandMutex()) {
    buffer[0] = '\0';
    return 0;
  }
  SDL_LockMutex(g_command_mutex);
  SDL_strlcpy(buffer, g_commands.ui_model, buffer_size);
  SDL_UnlockMutex(g_command_mutex);
  return SDL_strlen(buffer);
}

void RaClientZelda3_Shutdown(void) {
  RaClientZelda3_DestroyClient();
  RaClientZelda3_ClearConfig(&g_config);
  RaClientZelda3_ResetRuntimeState();
  if (RaClientZelda3_EnsureCommandMutex()) {
    SDL_LockMutex(g_command_mutex);
    RaClientZelda3_ClearConfig(&g_commands.config);
    g_commands.configure_pending = 0;
    g_commands.logout_pending = 0;
    g_commands.paused = 0;
    SDL_strlcpy(g_commands.snapshot, "enabled=0 lifecycle=shutdown",
                sizeof(g_commands.snapshot));
    SDL_strlcpy(g_commands.ui_model, "V\t1\nM\tdisabled\tdisabled\t\t\t0\t0\t0\t0\t0\t\t\t0\t0\t0\n",
                sizeof(g_commands.ui_model));
    SDL_UnlockMutex(g_command_mutex);
  }
}
