/* Directory index of the read-only game folders (MKXP_VITA_FS_INDEX), see vita-fs-index.cpp. */
#pragma once

/* 1: path names an existing regular file; 0: it does not (no such file, or a directory: open would
 * fail); -1: unknown (not a game asset folder, or the folder could not be listed) -> try for real. */
extern "C" int vitaFsIndexQuery(const char *path);
/* PERF fs_idx=known files/known missing/unknown/folders listed (cumulative). */
extern "C" void vitaFsIndexStats(unsigned *found, unsigned *missing, unsigned *unknown, unsigned *dirs);
/* Asset roots (each ends with '/'): set once at start-up; host test may set its own. */
extern "C" void vitaFsIndexSetRoots(const char *const *roots, int n);
