/* Background whole-file read-ahead (MKXP_VITA_ANIM_PREFETCH), see vita-prefetch.cpp. */
#pragma once
#include <string>
#include <vector>

/* Queue a file (full path) for reading by the background thread; duplicates are ignored. If it does
 * not exist and alt is given, the thread reads alt instead (kept under alt's path): the caller does
 * not have to ask the card which of the two exists. */
void vitaPrefetchAdd(const std::string &path, const std::string &alt = std::string());
/* readWholeFile: true and the bytes if the file was read ahead (waits if it is being read now);
 * false if it was not queued, failed, or was still waiting in the queue (then the caller reads it). */
bool vitaPrefetchTake(const std::string &path, std::vector<unsigned char> &out);
/* Drop everything not taken (queued, read, or being read: freed when its read ends). */
void vitaPrefetchClear();
/* PERF pf=queued/taken/waited/dropped/kb_held (cumulative except kb_held). */
void vitaPrefetchStats(unsigned *queued, unsigned *taken, unsigned *waited, unsigned *dropped, unsigned *kbHeld);
/* Cap on the bytes held by read-ahead entries (default 8 MiB). */
void vitaPrefetchSetCap(size_t bytes);
