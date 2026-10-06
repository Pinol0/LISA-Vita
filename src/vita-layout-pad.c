/* Diagnostic only: inert zero padding that moves the code/rodata linked after it to the
 * addresses it has in the pthread libruby build. Never executed or referenced at runtime. */

#ifndef VITA_PAD_NAME
#error "VITA_PAD_NAME must be defined"
#endif

#define VITA_CAT2(a, b) a##b
#define VITA_CAT(a, b) VITA_CAT2(a, b)
#define VITA_STR2(x) #x
#define VITA_STR(x) VITA_STR2(x)

#if VITA_PAD_TEXT_SIZE > 0
__attribute__((used, aligned(4), section(".text.vita_layout_pad_" VITA_STR(VITA_PAD_NAME))))
const unsigned char VITA_CAT(vita_layout_pad_text_, VITA_PAD_NAME)[VITA_PAD_TEXT_SIZE] = {0};
#endif

#if VITA_PAD_RODATA_SIZE > 0
__attribute__((used, aligned(4), section(".rodata.vita_layout_pad_" VITA_STR(VITA_PAD_NAME))))
const unsigned char VITA_CAT(vita_layout_pad_rodata_, VITA_PAD_NAME)[VITA_PAD_RODATA_SIZE] = {0};
#endif
