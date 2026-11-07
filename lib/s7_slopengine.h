#pragma once

#include "s7.h"

#ifdef __cplusplus
extern "C" {
#endif

s7_pointer slopengine_s7_global_value(s7_scheme *sc, s7_pointer symbol);
void slopengine_s7_set_initial_value(s7_scheme *sc, s7_pointer symbol, s7_pointer value);

#ifdef __cplusplus
}
#endif
