#include "net/conditioned_transport.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <random>

namespace slopengine {

namespace {

using Clock = std::chrono::steady_clock;

class ConditionedTransport final : public NetTransport {
public:
    ConditionedTransport(std::unique_ptr<NetTransport> inner, const NetConditions& conditions, std::uint32_t seed)
        : inner_(std::move(inner)), conditions_(conditions), random_(seed) {}

    bool listen(std::string_view address) override { return inner_->listen(address); }
    NetConnectionId connect(std::string_view address) override { return inner_->connect(address); }
    bool accept(NetConnectionId connection) override { return inner_->accept(connection); }

    bool send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) override {
        if (data.size() > kMaxNetMessageBytes) {
            return false;
        }
        if (mode == NetSendMode::Unreliable && conditions_.lossFraction > 0.0f &&
            std::uniform_real_distribution<float>(0.0f, 1.0f)(random_) < conditions_.lossFraction) {
            return true;
        }
        float delayMs = conditions_.lagMs;
        if (conditions_.jitterMs > 0.0f) {
            delayMs += std::uniform_real_distribution<float>(-conditions_.jitterMs, conditions_.jitterMs)(random_);
        }
        Clock::time_point due = Clock::now() + std::chrono::microseconds(static_cast<long long>(std::max(0.0f, delayMs) * 1000.0f));
        if (mode == NetSendMode::Reliable) {
            Clock::time_point& last = lastReliable_[connection];
            due = std::max(due, last);
            last = due;
        }
        queue_.push_back({due, connection, std::vector<std::byte>(data.begin(), data.end()), mode});
        return true;
    }

    void close(NetConnectionId connection, std::string_view reason) override {
        flush(connection, Clock::time_point::max());
        lastReliable_.erase(connection);
        inner_->close(connection, reason);
    }

    void poll(std::vector<NetEvent>& out) override {
        flush(kNoNetConnection, Clock::now());
        inner_->poll(out);
    }

    NetConnectionStats stats(NetConnectionId connection) const override { return inner_->stats(connection); }
    void keepAlive() override { inner_->keepAlive(); }
    void setPeersPerAddress(std::size_t peers) override { inner_->setPeersPerAddress(peers); }

private:
    struct Pending {
        Clock::time_point due;
        NetConnectionId connection = kNoNetConnection;
        std::vector<std::byte> data;
        NetSendMode mode = NetSendMode::Unreliable;
    };

    void flush(NetConnectionId only, Clock::time_point until) {
        std::stable_sort(queue_.begin(), queue_.end(), [](const Pending& a, const Pending& b) { return a.due < b.due; });
        for (auto it = queue_.begin(); it != queue_.end();) {
            const bool mine = only == kNoNetConnection || it->connection == only;
            if (mine && it->due <= until) {
                inner_->send(it->connection, it->data, it->mode);
                it = queue_.erase(it);
            } else if (only == kNoNetConnection) {
                break;
            } else {
                ++it;
            }
        }
    }

    std::unique_ptr<NetTransport> inner_;
    NetConditions conditions_;
    std::mt19937 random_;
    std::deque<Pending> queue_;
    std::map<NetConnectionId, Clock::time_point> lastReliable_;
};

}

std::unique_ptr<NetTransport> createConditionedTransport(
    std::unique_ptr<NetTransport> inner,
    const NetConditions& conditions,
    std::uint32_t seed) {
    return std::make_unique<ConditionedTransport>(std::move(inner), conditions, seed);
}

}
