/* viewport.c */
#include "viewport.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GLFW/glfw3.h>

/* OpenGL headers:
 * On Linux you usually get gl.h transitively via GLFW/glfw3.h.
 * If you need explicit:
 *   #include <GL/gl.h>
 */

#ifdef EPA_ENABLE_CUDA
  #include <cuda_runtime.h>
  #include <cuda_gl_interop.h>
#endif

struct Viewport {
  GLFWwindow *win;
  int w, h;
  int cuda_enabled;

  /* Presentation resources */
  unsigned int tex;   /* GL texture id */
  unsigned int pbo;   /* GL pixel unpack buffer id (optional) */

#ifdef EPA_ENABLE_CUDA
  struct cudaGraphicsResource *cuda_res; /* registered PBO */
#endif
};

static void vp_die(const char *msg) {
  fprintf(stderr, "viewport: %s\n", msg);
}

static void vp_draw_fullscreen_quad(int w, int h, unsigned int tex) {
  /* Fixed-function fullscreen quad (compat profile).
   * No shaders, no GL loader required.
   */
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, tex);

  glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f,      0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f((float)w,  0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f((float)w,  (float)h);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f,      (float)h);
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
}

static int vp_alloc_present_resources(Viewport *vp) {
  /* Texture (RGBA8) */
  if (vp->tex == 0) glGenTextures(1, &vp->tex);
  glBindTexture(GL_TEXTURE_2D, vp->tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

  /* Allocate storage */
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vp->w, vp->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindTexture(GL_TEXTURE_2D, 0);

  /* Optional PBO for fast uploads + CUDA interop */
  if (vp->cuda_enabled) {
    size_t bytes = (size_t)vp->w * (size_t)vp->h * 4u;
    if (vp->pbo == 0) glGenBuffers(1, &vp->pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vp->pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, (GLsizeiptr)bytes, NULL, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

#ifdef EPA_ENABLE_CUDA
    /* Re-register if resizing */
    if (vp->cuda_res) {
      cudaGraphicsUnregisterResource(vp->cuda_res);
      vp->cuda_res = NULL;
    }

    cudaError_t ce = cudaGraphicsGLRegisterBuffer(
        &vp->cuda_res,
        vp->pbo,
        cudaGraphicsRegisterFlagsWriteDiscard
    );
    if (ce != cudaSuccess) {
      vp_die("cudaGraphicsGLRegisterBuffer failed");
      vp->cuda_enabled = 0; /* fallback */
      vp->cuda_res = NULL;
      return 0;
    }
#else
    /* Built without CUDA support: disable cuda path */
    vp->cuda_enabled = 0;
#endif
  }

  return 1;
}

Viewport *vp_create(int width, int height, const char *title, int enable_cuda) {
  if (!glfwInit()) {
    vp_die("glfwInit failed");
    return NULL;
  }

  /* Request a compatibility profile so we can use fixed-function quad.
   * This avoids GL loaders and keeps the viewport "contained".
   */
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  GLFWwindow *win = glfwCreateWindow(width, height, title ? title : "Viewport", NULL, NULL);
  if (!win) {
    vp_die("glfwCreateWindow failed");
    glfwTerminate();
    return NULL;
  }

  glfwMakeContextCurrent(win);
  glfwSwapInterval(1); /* vsync on */

  Viewport *vp = (Viewport *)calloc(1, sizeof(Viewport));
  if (!vp) {
    glfwDestroyWindow(win);
    glfwTerminate();
    return NULL;
  }

  vp->win = win;
  vp->w = width;
  vp->h = height;
  vp->cuda_enabled = enable_cuda ? 1 : 0;

#ifdef EPA_ENABLE_CUDA
  vp->cuda_res = NULL;
#endif

  if (!vp_alloc_present_resources(vp)) {
    /* If CUDA registration failed, we still keep GL alive and just continue. */
    vp_alloc_present_resources(vp);
  }

  return vp;
}

void vp_destroy(Viewport *vp) {
  if (!vp) return;

#ifdef EPA_ENABLE_CUDA
  if (vp->cuda_res) {
    cudaGraphicsUnregisterResource(vp->cuda_res);
    vp->cuda_res = NULL;
  }
#endif

  if (vp->pbo) glDeleteBuffers(1, &vp->pbo);
  if (vp->tex) glDeleteTextures(1, &vp->tex);

  if (vp->win) {
    glfwDestroyWindow(vp->win);
    vp->win = NULL;
  }
  glfwTerminate();
  free(vp);
}

int vp_pump(Viewport *vp) {
  if (!vp || !vp->win) return 0;
  glfwPollEvents();
  return glfwWindowShouldClose(vp->win) ? 0 : 1;
}

int vp_resize(Viewport *vp, int width, int height) {
  if (!vp || width <= 0 || height <= 0) return 0;
  vp->w = width;
  vp->h = height;

  /* Realloc texture + PBO (and re-register CUDA resource) */
  return vp_alloc_present_resources(vp);
}

void vp_begin_frame(Viewport *vp, float r, float g, float b, float a) {
  (void)vp;
  glViewport(0, 0, vp ? vp->w : 1, vp ? vp->h : 1);
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
}

void vp_present_gl(Viewport *vp) {
  if (!vp || !vp->win) return;
  glfwSwapBuffers(vp->win);
}

int vp_present_rgba8(Viewport *vp, const uint8_t *rgba, size_t rgba_bytes) {
  if (!vp || !vp->win) return 0;
  size_t need = (size_t)vp->w * (size_t)vp->h * 4u;
  if (!rgba || rgba_bytes < need) return 0;

  glBindTexture(GL_TEXTURE_2D, vp->tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp->w, vp->h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glBindTexture(GL_TEXTURE_2D, 0);

  glClear(GL_COLOR_BUFFER_BIT);
  vp_draw_fullscreen_quad(vp->w, vp->h, vp->tex);
  glfwSwapBuffers(vp->win);
  return 1;
}

int vp_cuda_map(Viewport *vp, void **device_ptr, size_t *byte_size) {
  if (!vp || !device_ptr || !byte_size) return 0;
  *device_ptr = NULL;
  *byte_size = 0;

  if (!vp->cuda_enabled) return 0;

#ifdef EPA_ENABLE_CUDA
  if (!vp->cuda_res) return 0;

  cudaError_t ce;
  ce = cudaGraphicsMapResources(1, &vp->cuda_res, 0);
  if (ce != cudaSuccess) return 0;

  void *ptr = NULL;
  size_t sz = 0;
  ce = cudaGraphicsResourceGetMappedPointer(&ptr, &sz, vp->cuda_res);
  if (ce != cudaSuccess) {
    cudaGraphicsUnmapResources(1, &vp->cuda_res, 0);
    return 0;
  }

  *device_ptr = ptr;
  *byte_size = sz;
  return 1;
#else
  (void)device_ptr; (void)byte_size;
  return 0;
#endif
}

int vp_cuda_unmap_and_present(Viewport *vp) {
  if (!vp) return 0;
  if (!vp->cuda_enabled) return 0;

#ifdef EPA_ENABLE_CUDA
  if (!vp->cuda_res) return 0;

  cudaError_t ce = cudaGraphicsUnmapResources(1, &vp->cuda_res, 0);
  if (ce != cudaSuccess) return 0;

  /* Upload PBO -> texture and present */
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vp->pbo);
  glBindTexture(GL_TEXTURE_2D, vp->tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp->w, vp->h, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  glClear(GL_COLOR_BUFFER_BIT);
  vp_draw_fullscreen_quad(vp->w, vp->h, vp->tex);
  glfwSwapBuffers(vp->win);
  return 1;
#else
  return 0;
#endif
}

int vp_width(const Viewport *vp) { return vp ? vp->w : 0; }
int vp_height(const Viewport *vp) { return vp ? vp->h : 0; }
int vp_cuda_enabled(const Viewport *vp) { return vp ? vp->cuda_enabled : 0; }
