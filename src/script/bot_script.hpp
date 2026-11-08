#pragma once

#include <flecs.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct s7_scheme;
typedef struct s7_cell* s7_pointer;

namespace slopengine {

struct BotKnownEntity {
    std::string name;
    bool player = false;
};

class BotScript {
public:
    BotScript(flecs::world& world, s7_scheme* scheme, std::uint32_t seed,
              std::function<std::vector<BotKnownEntity>()> known);
    ~BotScript();

    BotScript(const BotScript&) = delete;
    BotScript& operator=(const BotScript&) = delete;

    bool load(const std::string& source, const std::string& name);
    void tick(float dt);
    const std::string& name() const { return name_; }

private:
    s7_scheme* scheme_ = nullptr;
    s7_pointer environment_ = nullptr;
    long long protectedAt_ = -1;
    std::string name_;
};

}
