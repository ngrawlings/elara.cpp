#pragma once

#include <stddef.h>

/*
 * Per-line source mapping built from cc -E linemarkers.
 * line_map->file_indices[i] and line_map->line_nums[i] describe
 * the original source location of cleaned-source line (i+1).
 */
typedef struct {
  char        **filenames;       /* interned unique source filenames */
  size_t        filename_count;
  unsigned int *file_indices;    /* [i] = index into filenames[] for cleaned line i+1 */
  int          *line_nums;       /* [i] = 1-based line in that file for cleaned line i+1 */
  size_t        count;           /* number of entries == lines in cleaned source */
} ELineMap;

void e_line_map_free(ELineMap *map);

/* Load and preprocess a .e translation unit (same as before, no map). */
char *e_load_translation_unit(const char *path, char err[256]);

/*
 * Like e_load_translation_unit but also builds a line map that translates
 * flat cleaned-source line numbers back to (file, original-line) pairs.
 * line_map may be NULL if only the cleaned source is needed.
 */
char *e_load_translation_unit_with_map(const char *path, ELineMap *line_map, char err[256]);
