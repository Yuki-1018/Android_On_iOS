#ifndef ANDROID_EMUGL_BRIDGE_H
#define ANDROID_EMUGL_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All entry points except the renderer's post callback run on the QEMU thread.
 * Streams return bytes transferred, -2 for backpressure and -4 for closure. */
int ae_gpu_init(unsigned width, unsigned height);
void *ae_gpu_open(void);
void ae_gpu_close(void *stream);
int ae_gpu_fd(void *stream);
int ae_gpu_send(void *stream, const void *data, size_t size);
int ae_gpu_receive(void *stream, void *data, size_t size);
unsigned ae_gpu_poll(void *stream); /* 1 readable, 2 writable, 4 closed */
void ae_gpu_invalidate_frame(void);
/* Update a persistent BGRA backing store; untouched rows retain their pixels. */
int ae_gpu_frame_region(uint8_t *bgra, size_t size, unsigned *first, unsigned *rows);
int ae_gpu_frame(uint8_t *bgra, size_t size); /* latest frame, top-left origin */
#ifdef __cplusplus
}
#endif
#endif
