/* Background decode of battle animation images and sound effects (MKXP_VITA_ANIM_PREDECODE),
 * see vita-predecode.cpp. */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

/* Queue: an image by its source path (as Bitmap.new resolves it), a sound by its RGSS name. */
void vitaPredecodeImage(const std::string &srcPath);
void vitaPredecodeSound(const std::string &rgssName);
/* The result if it was decoded ahead (waits if being decoded now); false: compute it as before. */
bool vitaPredecodeTakeImage(const std::string &srcPath, int &w, int &h, std::vector<unsigned char> &px);
bool vitaPredecodeTakeSound(const std::string &rgssName, std::vector<uint8_t> &pcm, int &sampleSize, int &channels, int &rate);
/* Drop everything not taken. */
void vitaPredecodeClear();
void vitaPredecodeSetCaps(size_t imageBytes, size_t soundBytes);
