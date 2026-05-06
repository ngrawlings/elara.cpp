/* viewport.h - minimal "one viewport" for OpenGL display + optional CUDA->PBO interop
 *
 * Goals:
 *  - C only
 *  - single window "viewport"
 *  - OpenGL path: you render normally, call vp_present_gl()
 *  - CUDA path: you write pixels into a mapped PBO via CUDA, then vp_present_cuda_unmap_and_swap()
 *
 * Dependencies:
 *  - GLFW (window + context)
 *  - OpenGL (compat profile; uses fixed-function quad to avoid GL loaders)
 *  - Optional: CUDA runtime + cuda_gl_interop (guarded by EPA_ENABLE_CUDA)
 *
 * Build (OpenGL only):
 *   gcc -O2 -std=c11 viewport.c -c
 *   gcc -o app app.o viewport.o -lglfw -lGL -ldl -lpthread
 *
 * Build (CUDA interop enabled):
 *   gcc -O2 -std=c11 -DEPA_ENABLE_CUDA viewport.c -c
 *   nvcc -c your_kernel.cu
 *   gcc -o app app.o viewport.o your_kernel.o -lglfw -lGL -ldl -lpthread -lcudart
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Viewport Viewport;

/* Create a window + GL context. If enable_cuda != 0 and built with EPA_ENABLE_CUDA,
 * a PBO is created and registered with CUDA for zero-copy mapping.
 */
Viewport *vp_create(int width, int height, const char *title, int enable_cuda);

/* Destroy window/context and free GPU resources */
void vp_destroy(Viewport *vp);

/* Poll events; returns 0 if window should close */
int vp_pump(Viewport *vp);

/* Resize viewport + backing texture/PBO */
int vp_resize(Viewport *vp, int width, int height);

/* Begin frame for the simple built-in presenter. (optional clear) */
void vp_begin_frame(Viewport *vp, float r, float g, float b, float a);

/* OpenGL path:
 * - you render however you want
 * - call vp_present_gl() to swap buffers
 */
void vp_present_gl(Viewport *vp);

/* CPU pixel path (handy for early testing even in CUDA mode):
 * Upload RGBA8 pixels of size width*height*4 and present to screen.
 */
int vp_present_rgba8(Viewport *vp, const uint8_t *rgba, size_t rgba_bytes);

/* CUDA path:
 * Map the PBO and return a device pointer + byte size.
 * You launch your kernel to fill it (RGBA8 recommended).
 */
int vp_cuda_map(Viewport *vp, void **device_ptr, size_t *byte_size);

/* Unmap PBO, upload to texture, draw fullscreen quad, swap buffers */
int vp_cuda_unmap_and_present(Viewport *vp);

int vp_width(const Viewport *vp);
int vp_height(const Viewport *vp);
int vp_cuda_enabled(const Viewport *vp);

#ifdef __cplusplus
}
#endif
