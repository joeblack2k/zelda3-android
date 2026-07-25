#ifndef ZELDA3_RA_CLIENT_ZELDA3_H_
#define ZELDA3_RA_CLIENT_ZELDA3_H_

#include <stddef.h>

void RaClientZelda3_QueueConfigure(int enabled, int spectator,
                                   const char *client_name,
                                   const char *client_version,
                                   const char *username, const char *secret,
                                   int secret_is_token,
                                   const char *android_release,
                                   const char *android_model);
void RaClientZelda3_Initialize(void);
void RaClientZelda3_Pump(void);
void RaClientZelda3_Idle(void);
void RaClientZelda3_DoFrame(void);
void RaClientZelda3_Reset(void);
void RaClientZelda3_Shutdown(void);
void RaClientZelda3_QueueLogout(void);
void RaClientZelda3_SetPaused(int paused);
int RaClientZelda3_IsCasualIntegrityEnabled(void);
size_t RaClientZelda3_SnapshotCached(char *buffer, size_t buffer_size);

#endif  // ZELDA3_RA_CLIENT_ZELDA3_H_
