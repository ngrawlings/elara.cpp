#pragma once

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

#ifdef __cplusplus
}
#endif
