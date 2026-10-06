/* host test stub */
#pragma once
#include <cstdint>
enum { SCE_CTRL_DOWN = 0x40, SCE_CTRL_LTRIGGER = 0x100, SCE_CTRL_RTRIGGER = 0x200 };
struct SceCtrlData { uint64_t timeStamp; unsigned buttons; };
extern "C" int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count);
