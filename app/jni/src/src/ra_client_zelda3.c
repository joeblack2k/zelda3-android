#include "ra_client_zelda3.h"

#include "ra_http.h"
#include "ra_memory.h"

#include <rc_consoles.h>

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
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
  char client_name[65];
  char client_version[33];
  char *username;
  char *secret;
} RaClientZelda3Config;

static RaClientZelda3Config g_config;
static rc_client_t *g_client;
static int g_authenticated;
static int g_game_valid;
static int g_unsupported_game;
static int g_disconnected;
static int g_logout_requested;
static uint32_t g_event_counts[kRaEventTypeCount];
static uint32_t g_frame_count;
static uint32_t g_last_event_type;

static char *RaClientZelda3_Copy(const char *value) {
  char *copy;
  size_t size;

  if (!value || !value[0])
    return NULL;
  size = strlen(value) + 1;
  copy = (char *)malloc(size);
  if (copy)
    memcpy(copy, value, size);
  return copy;
}

static void RaClientZelda3_ClearConfig(void) {
  if (g_config.secret) {
    memset(g_config.secret, 0, strlen(g_config.secret));
    free(g_config.secret);
  }
  free(g_config.username);
  memset(&g_config, 0, sizeof(g_config));
}

static void RaClientZelda3_Log(const char *message) {
#ifdef __ANDROID__
  __android_log_print(ANDROID_LOG_INFO, "Zelda3RA", "%s", message);
#else
  fprintf(stderr, "Zelda3RA: %s\n", message);
#endif
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

static void RC_CCONV RaClientZelda3_OnGameLoaded(int result,
                                                  const char *error_message,
                                                  rc_client_t *client,
                                                  void *userdata) {
  const rc_client_game_t *game;

  (void)error_message;
  (void)userdata;
  if (result != RC_OK || !client) {
    g_unsupported_game = 1;
    return;
  }
  game = rc_client_get_game_info(client);
  if (!game || game->id != kRaExpectedGameId ||
      game->console_id != RC_CONSOLE_SUPER_NINTENDO || !game->hash ||
      strcmp(game->hash, kRaExpectedHash) != 0) {
    g_unsupported_game = 1;
    rc_client_unload_game(client);
    return;
  }
  g_game_valid = 1;
}

static void RC_CCONV RaClientZelda3_OnLogin(int result,
                                             const char *error_message,
                                             rc_client_t *client,
                                             void *userdata) {
  const rc_client_user_t *user;

  (void)error_message;
  (void)userdata;
  if (result != RC_OK || !client)
    return;
  user = rc_client_get_user_info(client);
  if (!user)
    return;
  g_authenticated = 1;
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

  switch (event->type) {
    case RC_CLIENT_EVENT_TYPE_NONE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD:
    case RC_CLIENT_EVENT_RESET:
    case RC_CLIENT_EVENT_GAME_COMPLETED:
    case RC_CLIENT_EVENT_SERVER_ERROR:
    case RC_CLIENT_EVENT_SUBSET_COMPLETED:
      break;
    case RC_CLIENT_EVENT_DISCONNECTED:
      g_disconnected = 1;
      break;
    case RC_CLIENT_EVENT_RECONNECTED:
      g_disconnected = 0;
      break;
    default:
      break;
  }
}

static void RC_CCONV RaClientZelda3_OnLog(const char *message,
                                           const rc_client_t *client) {
  (void)message;
  (void)client;
  RaClientZelda3_Log("client error");
}

void RaClientZelda3_Configure(int enabled, int spectator, const char *client_name,
                              const char *client_version, const char *username,
                              const char *secret, int secret_is_token) {
  RaClientZelda3_ClearConfig();
  g_config.enabled = enabled && username && username[0] && secret && secret[0];
  g_config.spectator = spectator != 0;
  g_config.secret_is_token = secret_is_token != 0;
  SDL_strlcpy(g_config.client_name, client_name ? client_name : "Zelda3AndroidRA",
              sizeof(g_config.client_name));
  SDL_strlcpy(g_config.client_version, client_version ? client_version : "0.1.0",
              sizeof(g_config.client_version));
  if (g_config.enabled) {
    g_config.username = RaClientZelda3_Copy(username);
    g_config.secret = RaClientZelda3_Copy(secret);
    if (!g_config.username || !g_config.secret)
      RaClientZelda3_ClearConfig();
  }
}

void RaClientZelda3_Initialize(void) {
  char user_agent[128];

  if (!g_config.enabled || g_client)
    return;
  RaHttpInitialize();
  g_client = rc_client_create(RaMemoryRead, RaHttpDispatch);
  if (!g_client)
    return;
  rc_client_set_hardcore_enabled(g_client, 0);
  rc_client_set_spectator_mode_enabled(g_client, g_config.spectator);
  rc_client_set_event_handler(g_client, RaClientZelda3_OnEvent);
  rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_ERROR,
                           RaClientZelda3_OnLog);
  SDL_snprintf(user_agent, sizeof(user_agent), "%s/%s", g_config.client_name,
               g_config.client_version);
  RaHttpSetUserAgent(user_agent);

  if (g_config.secret_is_token)
    rc_client_begin_login_with_token(g_client, g_config.username, g_config.secret,
                                     RaClientZelda3_OnLogin, NULL);
  else
    rc_client_begin_login_with_password(g_client, g_config.username, g_config.secret,
                                        RaClientZelda3_OnLogin, NULL);
  memset(g_config.secret, 0, strlen(g_config.secret));
  free(g_config.secret);
  g_config.secret = NULL;
}

void RaClientZelda3_Pump(void) {
  RaHttpPumpCompletions();
  if (g_logout_requested) {
    g_logout_requested = 0;
    if (g_client)
      rc_client_logout(g_client);
    g_authenticated = 0;
    g_game_valid = 0;
  }
}

void RaClientZelda3_Idle(void) {
  if (g_client)
    rc_client_idle(g_client);
}

void RaClientZelda3_DoFrame(void) {
  if (g_client) {
    rc_client_do_frame(g_client);
    ++g_frame_count;
  }
}

void RaClientZelda3_Reset(void) {
  if (g_client)
    rc_client_reset(g_client);
}

void RaClientZelda3_RequestLogout(void) {
  g_logout_requested = 1;
}

int RaClientZelda3_IsCasualAuthenticatedGameLoaded(void) {
  return g_client && g_authenticated && g_game_valid && !g_config.spectator &&
         rc_client_is_game_loaded(g_client);
}

size_t RaClientZelda3_Snapshot(char *buffer, size_t buffer_size) {
  const rc_client_user_t *user = g_client ? rc_client_get_user_info(g_client) : NULL;
  const rc_client_game_t *game = g_client ? rc_client_get_game_info(g_client) : NULL;
  rc_client_user_game_summary_t summary = {0};
  RaHttpStats http_stats = {0};
  char rich_presence[128] = "";

  if (!buffer || buffer_size == 0)
    return 0;
  if (g_client) {
    rc_client_get_user_game_summary(g_client, &summary);
    if (rc_client_has_rich_presence(g_client))
      rc_client_get_rich_presence_message(g_client, rich_presence,
                                          sizeof(rich_presence));
  }
  RaHttpGetStats(&http_stats);
  return (size_t)SDL_snprintf(
      buffer, buffer_size,
      "state=%d user=%s game=%u/%u/%s summary=%u/%u/%u unsupported=%u "
      "rp=%s disconnect=%u events=%u:%u invalid_reads=%u frames=%u "
      "http=%u/%u/%u/%u/%u",
      g_client ? rc_client_get_load_game_state(g_client) : 0,
      user && user->username ? "authenticated" : "none",
      game ? game->id : 0, game ? game->console_id : 0,
      g_game_valid ? "verified" : (g_unsupported_game ? "unsupported" : "none"),
      summary.num_unlocked_achievements, summary.num_core_achievements,
      summary.points_unlocked, g_unsupported_game, rich_presence,
      g_disconnected, g_last_event_type,
      g_last_event_type < kRaEventTypeCount ? g_event_counts[g_last_event_type] : 0,
      RaMemoryGetInvalidReadCount(), g_frame_count, http_stats.dispatched,
      http_stats.completed, http_stats.dropped, http_stats.pending,
      http_stats.queued_completions);
}

void RaClientZelda3_Shutdown(void) {
  if (g_client) {
    rc_client_destroy(g_client);
    g_client = NULL;
  }
  RaHttpShutdown();
  RaClientZelda3_ClearConfig();
  g_authenticated = 0;
  g_game_valid = 0;
}
