#include "mus-config.h"
#include "s7.h"

#ifdef _MSC_VER
static bool is_decodable(s7_scheme *sc, s7_pointer p) {
    (void)sc;
    (void)p;
    return false;
}
#endif

#include "s7_patched.c"
