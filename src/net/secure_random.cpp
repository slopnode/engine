#include "net/secure_random.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace slopengine {

bool secureRandom(std::uint8_t* out, std::size_t size) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(out, size);
    return true;
#else
    std::size_t filled = 0;
    while (filled < size) {
        const ssize_t got = getrandom(out + filled, size - filled, 0);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        filled += static_cast<std::size_t>(got);
    }
    return true;
#endif
}

}
