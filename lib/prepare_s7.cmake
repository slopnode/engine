if(NOT INPUT OR NOT OUTPUT)
    message(FATAL_ERROR "prepare_s7.cmake requires INPUT and OUTPUT")
endif()

file(READ "${INPUT}" CONTENT)

if(APPLY_MSVC_PATCH)
    string(REPLACE
        "  /* in MS C, we need to provide inverse hyperbolic trig funcs and cbrt */\n  static double asinh(double x)"
        "#if defined(_MSC_VER) && (_MSC_VER < 1800)\n  /* in MS C, we need to provide inverse hyperbolic trig funcs and cbrt */\n  static double asinh(double x)"
        CONTENT "${CONTENT}")
    string(REPLACE
        "  static double cbrt(double x) {if (x >= 0.0) return(pow(x, 1.0 / 3.0)); return(-pow(-x, 1.0 / 3.0));}\n#endif /* windows */"
        "  static double cbrt(double x) {if (x >= 0.0) return(pow(x, 1.0 / 3.0)); return(-pow(-x, 1.0 / 3.0));}\n#endif\n#endif /* windows */"
        CONTENT "${CONTENT}")
endif()

file(WRITE "${OUTPUT}" "${CONTENT}")
