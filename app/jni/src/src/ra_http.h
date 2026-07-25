#ifndef ZELDA3_RA_HTTP_H_
#define ZELDA3_RA_HTTP_H_

#include <stddef.h>
#include <stdint.h>

#include <rc_client.h>

enum {
  kRaHttpMaxResponseBytes = 1024 * 1024,
};

void RaHttpInitialize(void);
void RaHttpShutdown(void);
void RaHttpPumpCompletions(void);

void RaHttpDispatch(const rc_api_request_t *request,
                    rc_client_server_callback_t callback, void *callback_data,
                    rc_client_t *client);
void RaHttpComplete(uint64_t request_id, int http_status, const uint8_t *body,
                    size_t body_size);

#endif  // ZELDA3_RA_HTTP_H_
