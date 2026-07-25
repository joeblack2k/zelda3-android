#include "ra_http.h"

#include <SDL.h>

#include <stdlib.h>
#include <string.h>

typedef struct RaHttpPending {
  uint64_t request_id;
  rc_client_server_callback_t callback;
  void *callback_data;
  rc_client_t *client;
  struct RaHttpPending *next;
} RaHttpPending;

typedef struct RaHttpCompletion {
  rc_client_server_callback_t callback;
  void *callback_data;
  int http_status;
  char *body;
  size_t body_size;
  struct RaHttpCompletion *next;
} RaHttpCompletion;

static SDL_mutex *g_http_mutex;
static RaHttpPending *g_pending;
static RaHttpCompletion *g_completions;
static uint64_t g_next_request_id = 1;
static int g_http_shutdown;

#if defined(__ANDROID__)
int RaHttpPlatformEnqueue(uint64_t request_id, const char *url,
                          const char *post_data, const char *content_type,
                          const char *user_agent);
#else
static int RaHttpPlatformEnqueue(uint64_t request_id, const char *url,
                                 const char *post_data,
                                 const char *content_type,
                                 const char *user_agent) {
  (void)request_id;
  (void)url;
  (void)post_data;
  (void)content_type;
  (void)user_agent;
  return 0;
}
#endif

static char *RaHttpCopyString(const char *source) {
  size_t size;
  char *copy;

  if (!source)
    return NULL;

  size = strlen(source) + 1;
  copy = (char *)malloc(size);
  if (copy)
    memcpy(copy, source, size);
  return copy;
}

static void RaHttpFreePending(RaHttpPending *pending) {
  free(pending);
}

static void RaHttpFreeCompletion(RaHttpCompletion *completion) {
  free(completion->body);
  free(completion);
}

void RaHttpInitialize(void) {
  if (!g_http_mutex)
    g_http_mutex = SDL_CreateMutex();
}

void RaHttpShutdown(void) {
  RaHttpPending *pending;
  RaHttpCompletion *completion;

  if (!g_http_mutex)
    return;

  SDL_LockMutex(g_http_mutex);
  g_http_shutdown = 1;
  pending = g_pending;
  completion = g_completions;
  g_pending = NULL;
  g_completions = NULL;
  SDL_UnlockMutex(g_http_mutex);

  while (pending) {
    RaHttpPending *next = pending->next;
    RaHttpFreePending(pending);
    pending = next;
  }
  while (completion) {
    RaHttpCompletion *next = completion->next;
    RaHttpFreeCompletion(completion);
    completion = next;
  }
}

void RaHttpDispatch(const rc_api_request_t *request,
                    rc_client_server_callback_t callback, void *callback_data,
                    rc_client_t *client) {
  RaHttpPending *pending;
  char *url;
  char *post_data;
  char *content_type;
  char user_agent[64];
  uint64_t request_id;

  if (!request || !request->url || !callback)
    return;

  RaHttpInitialize();
  if (!g_http_mutex)
    return;

  url = RaHttpCopyString(request->url);
  post_data = RaHttpCopyString(request->post_data);
  content_type = RaHttpCopyString(request->content_type);
  pending = (RaHttpPending *)calloc(1, sizeof(*pending));
  if (!url || (request->post_data && !post_data) ||
      (request->content_type && !content_type) || !pending) {
    free(url);
    free(post_data);
    free(content_type);
    free(pending);
    return;
  }

  SDL_LockMutex(g_http_mutex);
  if (g_http_shutdown) {
    SDL_UnlockMutex(g_http_mutex);
    free(url);
    free(post_data);
    free(content_type);
    free(pending);
    return;
  }
  request_id = g_next_request_id++;
  if (request_id == 0)
    request_id = g_next_request_id++;
  pending->request_id = request_id;
  pending->callback = callback;
  pending->callback_data = callback_data;
  pending->client = client;
  pending->next = g_pending;
  g_pending = pending;
  SDL_UnlockMutex(g_http_mutex);

  SDL_snprintf(user_agent, sizeof(user_agent), "%s/%s", "Zelda3Android",
               "2.0");
  if (!RaHttpPlatformEnqueue(request_id, url, post_data, content_type,
                             user_agent)) {
    RaHttpComplete(request_id, RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
                   NULL, 0);
  }

  free(url);
  free(post_data);
  free(content_type);
}

void RaHttpComplete(uint64_t request_id, int http_status, const uint8_t *body,
                    size_t body_size) {
  RaHttpPending **pending_ptr;
  RaHttpPending *pending;
  RaHttpCompletion *completion;

  if (!g_http_mutex || (body_size != 0 && !body) ||
      body_size > kRaHttpMaxResponseBytes)
    return;

  SDL_LockMutex(g_http_mutex);
  if (g_http_shutdown) {
    SDL_UnlockMutex(g_http_mutex);
    return;
  }

  pending_ptr = &g_pending;
  while (*pending_ptr && (*pending_ptr)->request_id != request_id)
    pending_ptr = &(*pending_ptr)->next;
  pending = *pending_ptr;
  if (!pending) {
    SDL_UnlockMutex(g_http_mutex);
    return;
  }

  completion = (RaHttpCompletion *)calloc(1, sizeof(*completion));
  if (completion && body_size != 0) {
    completion->body = (char *)malloc(body_size);
    if (!completion->body) {
      free(completion);
      completion = NULL;
    } else {
      memcpy(completion->body, body, body_size);
    }
  }

  *pending_ptr = pending->next;
  if (completion) {
    completion->callback = pending->callback;
    completion->callback_data = pending->callback_data;
    completion->http_status = http_status;
    completion->body_size = body_size;
    completion->next = g_completions;
    g_completions = completion;
  }
  SDL_UnlockMutex(g_http_mutex);

  RaHttpFreePending(pending);
}

void RaHttpPumpCompletions(void) {
  RaHttpCompletion *completion;

  if (!g_http_mutex)
    return;

  SDL_LockMutex(g_http_mutex);
  completion = g_completions;
  g_completions = NULL;
  SDL_UnlockMutex(g_http_mutex);

  while (completion) {
    RaHttpCompletion *next = completion->next;
    rc_api_server_response_t response;

    response.body = completion->body;
    response.body_length = completion->body_size;
    response.http_status_code = completion->http_status;
    completion->callback(&response, completion->callback_data);
    RaHttpFreeCompletion(completion);
    completion = next;
  }
}
