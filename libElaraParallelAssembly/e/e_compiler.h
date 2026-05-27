#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compile a preprocessed E source string to epaasm text.
 * Returns a heap-allocated null-terminated string the caller must free(),
 * or NULL on error with err filled in.
 */
char *e_compile_src_to_epaasm(const char *src, char err[256]);

/*
 * Load and preprocess an .e file (via cc -E), then compile to epaasm text.
 * Returns a heap-allocated null-terminated string the caller must free(),
 * or NULL on error with err filled in.
 */
char *e_compile_file_to_epaasm(const char *path, char err[256]);

/*
 * Compile multiple .e files together with a shared first-pass cross-kernel
 * worker index, enabling far_signal to resolve worker names across kernels.
 *
 * paths[]   - array of .e file paths to compile
 * count     - number of paths
 * out_asm[] - caller-supplied array of (count) char* pointers; each is set to
 *             a heap-allocated epaasm string (caller must free each) or NULL on
 *             per-file failure. The function returns 0 only on a fatal error
 *             affecting all files; per-file errors are reported via err_per_file[].
 * err_per_file[] - caller-supplied array of (count) char[256]; each receives
 *                  the error string for that file (empty if success). May be NULL.
 * err       - receives a fatal top-level error string. May be NULL.
 * Returns 1 on overall success (individual files may still have errors), 0 on
 * fatal failure.
 */
int e_compile_files_to_epaasm(const char **paths, size_t count,
                               char **out_asm,
                               char (*err_per_file)[256],
                               char err[256]);

#ifdef __cplusplus
}
#endif
