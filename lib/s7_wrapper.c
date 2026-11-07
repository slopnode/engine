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

#include "s7_slopengine.h"

s7_pointer slopengine_s7_global_value(s7_scheme *sc, s7_pointer symbol)
{
    if (!is_symbol(symbol) || !is_slot(global_slot(symbol)))
        return sc->undefined;
    return slot_value(global_slot(symbol));
}

void slopengine_s7_set_initial_value(s7_scheme *sc, s7_pointer symbol, s7_pointer value)
{
    (void)sc;
    if (is_symbol(symbol) && is_slot(initial_slot(symbol)))
        slot_set_value(initial_slot(symbol), value);
}
