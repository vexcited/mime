/* the display's gamma ramp, kept for the presenters and windows of the Linux host. */
#ifndef MIME_HOST_GAMMA_H
#define MIME_HOST_GAMMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* three channels of 256 values, red first; null for the identity. */
void mime_gamma_set(const uint16_t *ramp);
/* the ramp and the count of changes; false while it is the identity. */
bool mime_gamma_ramp(uint16_t ramp[3][256], uint32_t *generation);
/* each channel's 8-bit output for an 8-bit input; false while it is the identity. */
bool mime_gamma_table(uint8_t table[3][256]);
/* the SPIR-V of the pass that draws a composed frame through the ramp. */
bool mime_gamma_shaders(const uint32_t **vertex, size_t *vertex_size, const uint32_t **fragment, size_t *fragment_size);

#endif
