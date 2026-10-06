/* Host side of the SPRITE_NATIVE test: reads "rx ry dX dY jc jp prio" (hex floats), prints
 * "sx sy sz" with the same header as the Vita build. gcc -O2 -ffp-contract=off spc_math.c -lm */
#include <stdio.h>
#include "../../../src/vita-sprite-native.h"
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);   /* line-buffered: also used interactively by t_install.rb */
    double rx, ry, dX, dY; long jc, jp, pr;
    while (scanf("%la %la %la %la %ld %ld %ld", &rx, &ry, &dX, &dY, &jc, &jp, &pr) == 7) {
        double sx, sy; long sz;
        vitaSpcScreen(rx, ry, dX, dY, jc, jp, pr, &sx, &sy, &sz);
        printf("%a %a %ld %ld %ld\n", sx, sy, sz, (long)sx, (long)sy);
    }
    return 0;
}
