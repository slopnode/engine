#include "net/secure_transport.hpp"

#include "net/secure_random.hpp"

#include <monocypher.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace slopengine {

namespace {

constexpr std::uint8_t kVersion = 2;
constexpr std::uint8_t kHello = 1;
constexpr std::uint8_t kReply = 2;
constexpr std::uint8_t kConfirm = 3;
constexpr std::uint8_t kData = 4;

constexpr std::uint8_t kModeReliable = 0;
constexpr std::uint8_t kModeUnreliable = 1;
constexpr std::uint8_t kModeHandshake = 2;
constexpr std::uint8_t kModeClose = 3;
constexpr std::size_t kMaxCloseReason = 256;

constexpr std::size_t kHelloSize = 2 + 32;
constexpr std::size_t kReplyHeaderSize = 1 + 32 + 32;
constexpr std::size_t kReplySize = kReplyHeaderSize + 16;
constexpr std::size_t kConfirmSize = 1 + 16;
constexpr std::size_t kDataHeaderSize = 10;

constexpr std::string_view kTranscriptLabel = "slopengine-secure-2";

void makeNonce(std::uint8_t mode, std::uint64_t counter, std::uint8_t nonce[24]) {
    std::memset(nonce, 0, 24);
    nonce[0] = mode;
    for (int i = 0; i < 8; ++i) {
        nonce[1 + i] = static_cast<std::uint8_t>(counter >> (8 * i));
    }
}

std::uint64_t readCounter(const std::uint8_t* bytes) {
    std::uint64_t counter = 0;
    for (int i = 0; i < 8; ++i) {
        counter |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    return counter;
}

bool allZero(const SecureKey& key) {
    std::uint8_t accumulated = 0;
    for (std::uint8_t byte : key) {
        accumulated |= byte;
    }
    return accumulated == 0;
}

const std::uint8_t* raw(const std::vector<std::byte>& data) {
    return reinterpret_cast<const std::uint8_t*>(data.data());
}

std::span<const std::byte> asBytes(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const std::byte*>(data), size};
}

bool deriveKeys(
    bool client,
    const SecureKey& ee,
    const SecureKey& es,
    const SecureKey& clientEphemeral,
    const SecureKey& serverStatic,
    const SecureKey& serverEphemeral,
    SecureKey& sendKey,
    SecureKey& receiveKey) {
    if (allZero(ee) || allZero(es)) {
        return false;
    }
    constexpr std::size_t head = kTranscriptLabel.size() + 1;
    std::uint8_t transcriptInput[head + 96];
    std::memcpy(transcriptInput, kTranscriptLabel.data(), kTranscriptLabel.size());
    transcriptInput[kTranscriptLabel.size()] = kVersion;
    std::memcpy(transcriptInput + head, clientEphemeral.data(), 32);
    std::memcpy(transcriptInput + head + 32, serverStatic.data(), 32);
    std::memcpy(transcriptInput + head + 64, serverEphemeral.data(), 32);
    std::uint8_t transcript[64];
    crypto_blake2b(transcript, sizeof(transcript), transcriptInput, sizeof(transcriptInput));

    std::uint8_t shared[64];
    std::memcpy(shared, ee.data(), 32);
    std::memcpy(shared + 32, es.data(), 32);
    std::uint8_t keys[64];
    crypto_blake2b_keyed(keys, sizeof(keys), transcript, sizeof(transcript), shared, sizeof(shared));
    std::memcpy((client ? sendKey : receiveKey).data(), keys, 32);
    std::memcpy((client ? receiveKey : sendKey).data(), keys + 32, 32);
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(keys, sizeof(keys));
    crypto_wipe(transcript, sizeof(transcript));
    return true;
}

bool newEphemeral(SecureKey& secret, SecureKey& publicKey) {
    if (!generateSecureKey(secret)) {
        return false;
    }
    crypto_x25519_public_key(publicKey.data(), secret.data());
    return true;
}

} // namespace

bool SecureReplayWindow::accept(std::uint64_t counter) {
    if (!any) {
        any = true;
        highest = counter;
        seen = 1;
        return true;
    }
    if (counter > highest) {
        const std::uint64_t shift = counter - highest;
        seen = shift >= 64 ? 1 : (seen << shift) | 1;
        highest = counter;
        return true;
    }
    const std::uint64_t distance = highest - counter;
    if (distance >= 64) {
        return false;
    }
    const std::uint64_t bit = std::uint64_t{1} << distance;
    if ((seen & bit) != 0) {
        return false;
    }
    seen |= bit;
    return true;
}

SecureTransport::SecureTransport(std::unique_ptr<NetTransport> inner, const SecureTransportOptions& options)
    : inner_(std::move(inner)), options_(options) {
    if (options_.staticSecret) {
        staticSecret_ = *options_.staticSecret;
        crypto_x25519_public_key(staticPublic_.data(), staticSecret_.data());
        haveStatic_ = true;
    }
}

SecureTransport::~SecureTransport() {
    for (auto& entry : sessions_) {
        wipe(entry.second);
    }
    crypto_wipe(staticSecret_.data(), staticSecret_.size());
    if (options_.staticSecret) {
        crypto_wipe(options_.staticSecret->data(), options_.staticSecret->size());
    }
}

bool SecureTransport::listen(std::string_view address) {
    if (!haveStatic_) {
        if (!generateSecureKey(staticSecret_)) {
            return false;
        }
        crypto_x25519_public_key(staticPublic_.data(), staticSecret_.data());
        haveStatic_ = true;
    }
    return inner_->listen(address);
}

NetConnectionId SecureTransport::connect(std::string_view address) {
    const NetConnectionId connection = inner_->connect(address);
    if (connection != kNoNetConnection) {
        Session session{};
        session.client = true;
        sessions_[connection] = session;
    }
    return connection;
}

bool SecureTransport::accept(NetConnectionId connection) {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end() || found->second.client || found->second.accepted) {
        return false;
    }
    if (!inner_->accept(connection)) {
        return false;
    }
    found->second.accepted = true;
    return true;
}

bool SecureTransport::send(NetConnectionId connection, std::span<const std::byte> data, NetSendMode mode) {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end() || found->second.phase != Phase::Established ||
        data.size() > kMaxNetMessageBytes - kSecureOverheadBytes) {
        return false;
    }
    return seal(connection, found->second, data, mode == NetSendMode::Reliable ? kModeReliable : kModeUnreliable);
}

bool SecureTransport::seal(NetConnectionId connection, Session& session, std::span<const std::byte> data,
                           std::uint8_t modeByte) {
    const NetSendMode mode = modeByte == kModeUnreliable ? NetSendMode::Unreliable : NetSendMode::Reliable;
    const std::uint64_t counter = session.sendCounter[modeByte == kModeUnreliable ? 1 : 0]++;
    std::vector<std::uint8_t> frame(kSecureOverheadBytes + data.size());
    frame[0] = kData;
    frame[1] = modeByte;
    for (int i = 0; i < 8; ++i) {
        frame[2 + i] = static_cast<std::uint8_t>(counter >> (8 * i));
    }
    std::uint8_t nonce[24];
    makeNonce(modeByte, counter, nonce);
    crypto_aead_lock(
        frame.data() + kSecureOverheadBytes,
        frame.data() + kDataHeaderSize,
        session.sendKey.data(),
        nonce,
        frame.data(),
        kDataHeaderSize,
        reinterpret_cast<const std::uint8_t*>(data.data()),
        data.size());
    if (!inner_->send(connection, asBytes(frame.data(), frame.size()), mode)) {
        return false;
    }
    ++session.stats.sentMessages;
    return true;
}

void SecureTransport::close(NetConnectionId connection, std::string_view reason) {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end()) {
        return;
    }
    if (found->second.phase == Phase::Established) {
        const auto* text = reinterpret_cast<const std::byte*>(reason.data());
        seal(connection, found->second, {text, std::min(reason.size(), kMaxCloseReason)}, kModeClose);
        inner_->close(connection, {});
    } else {
        inner_->close(connection, reason);
    }
    wipe(found->second);
    sessions_.erase(found);
}

void SecureTransport::poll(std::vector<NetEvent>& out) {
    std::vector<NetEvent> events;
    inner_->poll(events);
    for (NetEvent& event : events) {
        switch (event.kind) {
        case NetEventKind::Incoming:
            sessions_[event.connection] = Session{};
            out.push_back(std::move(event));
            break;
        case NetEventKind::Connected:
            onConnected(event.connection, out);
            break;
        case NetEventKind::Message:
            onMessage(event.connection, event.data, out);
            break;
        case NetEventKind::Disconnected: {
            const auto found = sessions_.find(event.connection);
            if (found == sessions_.end()) {
                break;
            }
            if (found->second.phase == Phase::Established) {
                event.reason = "connection lost";
            }
            if (found->second.client || found->second.accepted) {
                out.push_back(std::move(event));
            }
            wipe(found->second);
            sessions_.erase(found);
            break;
        }
        }
    }
}

NetConnectionStats SecureTransport::stats(NetConnectionId connection) const {
    NetConnectionStats out = inner_->stats(connection);
    const auto found = sessions_.find(connection);
    out.sentMessages = found != sessions_.end() ? found->second.stats.sentMessages : 0;
    out.receivedMessages = found != sessions_.end() ? found->second.stats.receivedMessages : 0;
    return out;
}

std::optional<SecureKey> SecureTransport::peerKey(NetConnectionId connection) const {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end() || !found->second.client || found->second.phase != Phase::Established) {
        return std::nullopt;
    }
    return found->second.peerStatic;
}

void SecureTransport::onConnected(NetConnectionId connection, std::vector<NetEvent>& out) {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end()) {
        return;
    }
    Session& session = found->second;
    if (!session.client) {
        session.phase = Phase::AwaitingHello;
        return;
    }
    if (!newEphemeral(session.ephemeralSecret, session.ephemeralPublic)) {
        fail(connection, "no secure randomness", out);
        return;
    }
    std::uint8_t hello[kHelloSize];
    hello[0] = kHello;
    hello[1] = kVersion;
    std::memcpy(hello + 2, session.ephemeralPublic.data(), 32);
    session.phase = Phase::AwaitingReply;
    inner_->send(connection, asBytes(hello, sizeof(hello)), NetSendMode::Reliable);
}

void SecureTransport::onMessage(NetConnectionId connection, const std::vector<std::byte>& data, std::vector<NetEvent>& out) {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end() || data.empty()) {
        return;
    }
    Session& session = found->second;
    if (data.size() > kMaxNetMessageBytes) {
        fail(connection, "message too large", out);
        return;
    }
    const std::uint8_t* bytes = raw(data);
    const std::uint8_t type = bytes[0];

    if (session.phase == Phase::AwaitingHello && !session.client) {
        if (type != kHello || data.size() != kHelloSize || bytes[1] != kVersion || !haveStatic_) {
            fail(connection, "handshake failed", out);
            return;
        }
        SecureKey clientEphemeral{};
        std::memcpy(clientEphemeral.data(), bytes + 2, 32);
        if (!newEphemeral(session.ephemeralSecret, session.ephemeralPublic)) {
            fail(connection, "no secure randomness", out);
            return;
        }
        SecureKey ee{};
        SecureKey es{};
        crypto_x25519(ee.data(), session.ephemeralSecret.data(), clientEphemeral.data());
        crypto_x25519(es.data(), staticSecret_.data(), clientEphemeral.data());
        const bool derived = deriveKeys(
            false, ee, es, clientEphemeral, staticPublic_, session.ephemeralPublic, session.sendKey, session.receiveKey);
        crypto_wipe(ee.data(), ee.size());
        crypto_wipe(es.data(), es.size());
        crypto_wipe(session.ephemeralSecret.data(), session.ephemeralSecret.size());
        if (!derived) {
            fail(connection, "handshake failed", out);
            return;
        }
        std::uint8_t reply[kReplySize];
        reply[0] = kReply;
        std::memcpy(reply + 1, staticPublic_.data(), 32);
        std::memcpy(reply + 33, session.ephemeralPublic.data(), 32);
        std::uint8_t nonce[24];
        makeNonce(kModeHandshake, 0, nonce);
        crypto_aead_lock(nullptr, reply + kReplyHeaderSize, session.sendKey.data(), nonce, reply, kReplyHeaderSize, nullptr, 0);
        session.phase = Phase::AwaitingConfirm;
        inner_->send(connection, asBytes(reply, sizeof(reply)), NetSendMode::Reliable);
        return;
    }

    if (session.phase == Phase::AwaitingReply && session.client) {
        if (type != kReply || data.size() != kReplySize) {
            fail(connection, "handshake failed", out);
            return;
        }
        SecureKey serverStatic{};
        SecureKey serverEphemeral{};
        std::memcpy(serverStatic.data(), bytes + 1, 32);
        std::memcpy(serverEphemeral.data(), bytes + 33, 32);
        if (options_.pinnedServerKey && crypto_verify32(options_.pinnedServerKey->data(), serverStatic.data()) != 0) {
            fail(connection, "server key mismatch", out);
            return;
        }
        SecureKey ee{};
        SecureKey es{};
        crypto_x25519(ee.data(), session.ephemeralSecret.data(), serverEphemeral.data());
        crypto_x25519(es.data(), session.ephemeralSecret.data(), serverStatic.data());
        const bool derived = deriveKeys(
            true, ee, es, session.ephemeralPublic, serverStatic, serverEphemeral, session.sendKey, session.receiveKey);
        crypto_wipe(ee.data(), ee.size());
        crypto_wipe(es.data(), es.size());
        crypto_wipe(session.ephemeralSecret.data(), session.ephemeralSecret.size());
        std::uint8_t nonce[24];
        makeNonce(kModeHandshake, 0, nonce);
        if (!derived || crypto_aead_unlock(nullptr, bytes + kReplyHeaderSize, session.receiveKey.data(), nonce, bytes,
                                           kReplyHeaderSize, nullptr, 0) != 0) {
            fail(connection, "handshake failed", out);
            return;
        }
        session.peerStatic = serverStatic;
        std::uint8_t confirm[kConfirmSize];
        confirm[0] = kConfirm;
        crypto_aead_lock(nullptr, confirm + 1, session.sendKey.data(), nonce, confirm, 1, nullptr, 0);
        inner_->send(connection, asBytes(confirm, sizeof(confirm)), NetSendMode::Reliable);
        session.phase = Phase::Established;
        out.push_back(NetEvent{NetEventKind::Connected, connection, {}, {}});
        return;
    }

    if (session.phase == Phase::AwaitingConfirm && !session.client) {
        if (type == kData && data.size() > 1 && bytes[1] == kModeUnreliable) {
            return;
        }
        std::uint8_t nonce[24];
        makeNonce(kModeHandshake, 0, nonce);
        if (type != kConfirm || data.size() != kConfirmSize ||
            crypto_aead_unlock(nullptr, bytes + 1, session.receiveKey.data(), nonce, bytes, 1, nullptr, 0) != 0) {
            fail(connection, "handshake failed", out);
            return;
        }
        session.phase = Phase::Established;
        out.push_back(NetEvent{NetEventKind::Connected, connection, {}, {}});
        return;
    }

    if (session.phase != Phase::Established || type != kData || data.size() < kSecureOverheadBytes ||
        (bytes[1] != kModeReliable && bytes[1] != kModeUnreliable && bytes[1] != kModeClose)) {
        if (session.phase == Phase::Established && data.size() > 1 && type == kData && bytes[1] == kModeUnreliable) {
            return;
        }
        fail(connection, "bad message", out);
        return;
    }
    const std::uint8_t mode = bytes[1];
    const bool ordered = mode != kModeUnreliable;
    const std::uint64_t counter = readCounter(bytes + 2);
    if (ordered && counter != session.reliableReceive) {
        fail(connection, "bad message", out);
        return;
    }
    std::uint8_t nonce[24];
    makeNonce(mode, counter, nonce);
    const std::size_t size = data.size() - kSecureOverheadBytes;
    std::vector<std::byte> plain(size);
    if (crypto_aead_unlock(
            reinterpret_cast<std::uint8_t*>(plain.data()),
            bytes + kDataHeaderSize,
            session.receiveKey.data(),
            nonce,
            bytes,
            kDataHeaderSize,
            bytes + kSecureOverheadBytes,
            size) != 0) {
        if (ordered) {
            fail(connection, "bad message", out);
        }
        return;
    }
    if (mode == kModeClose) {
        std::string reason(reinterpret_cast<const char*>(plain.data()), std::min(size, kMaxCloseReason));
        inner_->close(connection, {});
        const bool report = session.client || session.accepted;
        wipe(session);
        sessions_.erase(found);
        if (report) {
            out.push_back(NetEvent{NetEventKind::Disconnected, connection, {}, std::move(reason)});
        }
        return;
    }
    if (ordered) {
        ++session.reliableReceive;
    } else if (!session.unreliableReceive.accept(counter)) {
        return;
    }
    ++session.stats.receivedMessages;
    out.push_back(NetEvent{NetEventKind::Message, connection, std::move(plain), {}});
}

void SecureTransport::fail(NetConnectionId connection, const char* reason, std::vector<NetEvent>& out) {
    const auto found = sessions_.find(connection);
    if (found == sessions_.end()) {
        return;
    }
    if (found->second.phase == Phase::Established) {
        const std::string_view text(reason);
        seal(connection, found->second, {reinterpret_cast<const std::byte*>(text.data()), text.size()}, kModeClose);
        inner_->close(connection, {});
    } else {
        inner_->close(connection, reason);
    }
    if (found->second.client || found->second.accepted) {
        out.push_back(NetEvent{NetEventKind::Disconnected, connection, {}, reason});
    }
    wipe(found->second);
    sessions_.erase(found);
}

void SecureTransport::wipe(Session& session) {
    crypto_wipe(session.ephemeralSecret.data(), session.ephemeralSecret.size());
    crypto_wipe(session.sendKey.data(), session.sendKey.size());
    crypto_wipe(session.receiveKey.data(), session.receiveKey.size());
}

std::unique_ptr<SecureTransport> createSecureTransport(
    std::unique_ptr<NetTransport> inner,
    const SecureTransportOptions& options) {
    return std::make_unique<SecureTransport>(std::move(inner), options);
}

std::string secureKeyFingerprint(const SecureKey& key) {
    std::uint8_t digest[16];
    crypto_blake2b(digest, sizeof(digest), key.data(), key.size());
    std::string out;
    char pair[3];
    for (std::size_t i = 0; i < sizeof(digest); ++i) {
        std::snprintf(pair, sizeof(pair), "%02x", digest[i]);
        if (i > 0 && i % 2 == 0) {
            out += ':';
        }
        out += pair;
    }
    return out;
}

bool generateSecureKey(SecureKey& secret) {
    return secureRandom(secret.data(), secret.size());
}

}
