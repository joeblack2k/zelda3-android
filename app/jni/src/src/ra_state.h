#ifndef ZELDA3_RA_STATE_H_
#define ZELDA3_RA_STATE_H_

#include <stddef.h>
#include <stdint.h>

size_t RaStateSerialize(uint8_t *buffer, size_t size);
int RaStateDeserialize(const uint8_t *buffer, size_t size);

#endif  // ZELDA3_RA_STATE_H_
