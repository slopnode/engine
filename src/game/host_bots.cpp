#include "game/host_bots.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace slopengine {

HostBots::~HostBots() {
    stopAll();
}

bool HostBots::supported() {
#ifdef _WIN32
    return false;
#else
    return true;
#endif
}

void HostBots::configure(HostBotLaunch launch) {
    launch_ = std::move(launch);
}

void HostBots::setCount(int count) {
    reap();
    target_ = std::max(0, count);
    while (static_cast<int>(bots_.size()) > target_) {
#ifndef _WIN32
        kill(bots_.back().pid, SIGTERM);
#endif
        stopping_.push_back(bots_.back().pid);
        Log::info(LogCategory::General, "NET: stopping bot %d", bots_.back().slot);
        bots_.pop_back();
    }
    update();
}

void HostBots::update() {
    reap();
    const auto now = std::chrono::steady_clock::now();
    if (static_cast<int>(bots_.size()) >= target_ || now < nextSpawn_) {
        return;
    }
    int slot = 1;
    while (std::any_of(bots_.begin(), bots_.end(), [&](const Bot& bot) { return bot.slot == slot; })) {
        ++slot;
    }
    if (!spawn(slot)) {
        target_ = static_cast<int>(bots_.size());
        return;
    }
    nextSpawn_ = now + std::chrono::milliseconds(400);
}

bool HostBots::spawn(int slot) {
#ifdef _WIN32
    (void)slot;
    return false;
#else
    std::error_code error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || launch_.behavior.empty() || launch_.port.empty()) {
        Log::warning(LogCategory::General, "NET: can't start a bot, %s",
                     error ? "the engine binary wasn't found" : "no bot script or port");
        return false;
    }
    const std::string profile = launch_.profile + "-bot-" + std::to_string(slot);
    const std::filesystem::path logDir = launch_.profilesDir / profile;
    std::filesystem::create_directories(logDir, error);
    const std::string logPath = (logDir / "bot.log").string();

    std::vector<std::string> args{self.string(), "--game", launch_.game.string()};
    for (const std::filesystem::path& mod : launch_.mods) {
        args.push_back("--mod");
        args.push_back(mod.string());
    }
    args.insert(args.end(), {"--profile", profile, "--dedicated", "--connect", "127.0.0.1:" + launch_.port, "--bot",
                             launch_.behavior, "--player-name", "Bot " + std::to_string(slot), "--quit-on-disconnect"});
    std::vector<char*> argv;
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    pid_t pid = 0;
    const int failed = posix_spawn(&pid, self.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (failed != 0) {
        Log::warning(LogCategory::General, "NET: can't start bot %d", slot);
        return false;
    }
    Log::info(LogCategory::General, "NET: started bot %d playing %s, log in %s", slot, launch_.behavior.c_str(),
              logPath.c_str());
    bots_.push_back({static_cast<int>(pid), slot});
    return true;
#endif
}

void HostBots::reap() {
#ifndef _WIN32
    std::erase_if(bots_, [this](const Bot& bot) {
        int status = 0;
        if (waitpid(bot.pid, &status, WNOHANG) != bot.pid) {
            return false;
        }
        Log::warning(LogCategory::General, "NET: bot %d exited, see its bot.log", bot.slot);
        target_ = std::max(0, target_ - 1);
        return true;
    });
    std::erase_if(stopping_, [](int pid) {
        int status = 0;
        return waitpid(pid, &status, WNOHANG) != 0;
    });
#endif
}

void HostBots::stopAll() {
#ifndef _WIN32
    std::vector<int> stopping = std::move(stopping_);
    stopping_.clear();
    for (const Bot& bot : bots_) {
        kill(bot.pid, SIGTERM);
        stopping.push_back(bot.pid);
    }
    bots_.clear();
    target_ = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (int pid : stopping) {
        int status = 0;
        while (waitpid(pid, &status, WNOHANG) == 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
#endif
}

}
