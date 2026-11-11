#pragma once

#include <cstddef>
#include <cstdint>

namespace slopengine {

bool secureRandom(std::uint8_t* out, std::size_t size);

}
