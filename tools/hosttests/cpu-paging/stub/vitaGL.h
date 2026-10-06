/* host test stub (tools/hosttests/cpu-paging): the vitaGL calls bitmap-vita-minimal.cpp makes. */
#pragma once
#include <GL/gl.h>
#include <cstddef>
enum vglMemType { VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_PHYCONT, VGL_MEM_ALL };
extern "C" size_t vglMemFree(vglMemType type);
extern "C" void *vglGetTexDataPointer(GLenum target);
#ifndef VGL_ALIGN
#define VGL_ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif
