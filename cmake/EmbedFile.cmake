file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" hexLength)
math(EXPR byteLength "${hexLength} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
file(WRITE "${OUTPUT}"
"#include <cstddef>\n"
"extern const unsigned char ${SYMBOL}[];\n"
"extern const std::size_t ${SYMBOL}_size;\n"
"const unsigned char ${SYMBOL}[] = {${bytes}0x00};\n"
"const std::size_t ${SYMBOL}_size = ${byteLength};\n")
