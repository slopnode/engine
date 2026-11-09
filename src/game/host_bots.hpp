#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace slopengine {

struct HostBotLaunch {
    std::filesystem::path game;
    std::vector<std::filesystem::path> mods;
    std::string profile;
    std::filesystem::path profilesDir;
    std::string port;
    std::string behavior;
};

class HostBots {
public:
    HostBots() = default;
    ~HostBots();

    HostBots(const HostBots&) = delete;
    HostBots& operator=(const HostBots&) = delete;

    static bool supported();
    void configure(HostBotLaunch launch);
    void setCount(int count);
    int count() const { return static_cast<int>(bots_.size()); }
    const std::string& behavior() const { return launch_.behavior; }
    void reap();
    void stopAll();

private:
    struct Bot {
        int pid = 0;
        int slot = 0;
    };

    bool spawn(int slot);

    HostBotLaunch launch_;
    std::vector<Bot> bots_;
    std::vector<int> stopping_;
};

}
