#include "ruby.h"
#include "ruby/encoding.h"

extern void Init_encdb(void);
extern void Init_transdb(void);
extern void Init_single_byte(void);

extern rb_encoding OnigEncodingWindows_1252;

/* Internal Ruby API, not exposed by ruby/encoding.h */
extern int rb_enc_register(
    const char *name,
    rb_encoding *encoding
);

void
Init_enc(void)
{
    static int initialized = 0;

    if (initialized) {
        return;
    }

    initialized = 1;

    Init_encdb();

    rb_enc_register(
        "Windows-1252",
        &OnigEncodingWindows_1252
    );

    rb_enc_alias(
        "CP1252",
        "Windows-1252"
    );

    Init_transdb();
    Init_single_byte();
}
