// SPDX-License-Identifier: MIT
#pragma once
#include "citadel_services.h"
#include <array>
#include <optional>

namespace bastion::integrity {
inline constexpr std::uint16_t opcode=0x7d1a;
inline constexpr std::uint32_t magic=0x43495431,version=3;
inline constexpr std::uint32_t native_report_abi=3; // CIT1 v3 accepts exactly SDK ABI 3.
inline constexpr std::uint64_t features=0x3fff,challenge_ttl_ms=60000,ack_timeout_ms=10000;
inline constexpr std::string_view addon_version="0.1.0-preview.13";
inline constexpr bool login_checkpoint(std::uint16_t op) {
    return op==0x0f13||op==0x4b8d||op==0x298d||op==0x578f;
}
enum class Status { Inactive,Waiting,Measuring,Queued,Sending,Sent,Accepted,Rejected,Unsupported,Malformed,TimedOut,Failed,LegacyQueued,LegacySubmitted,ReconnectRequired };
const char* label(Status);
class Adapter {
public:
    void set_host(const PluginHostApi*);
    void initialize();
    void peer(bool);
    void world_login(bool zoning);
    void game_state(int);
    void pulse();
    // Wrapper routes only packets from the current incoming WORLD connection.
    bool packet(std::uint16_t,const void*,std::size_t);
    // Submit the legacy DLL slot before the original WORLD file check or EnterWorld.
    void world_outgoing(std::uint16_t);
    Status status() const {return status_;}
    bool report_sent() const {return sent_;}
    unsigned attempts() const {return attempts_;}
    bool legacy_submitted() const {return legacy_submitted_;}
    unsigned legacy_attempts() const {return legacy_attempts_;}
    std::string_view reason() const {return reason_;}
private:
    struct Ack {std::uint32_t flags=0;std::string reason;};
    void retire(Status,std::string_view);
    void reset_legacy();
    void finish_world_login();
    void send_legacy(std::uint64_t);
    bool clock(std::uint64_t&);
    bool measure(std::uint32_t,HailstoneBinaryMeasurement&);
    void challenge(std::uint64_t,std::uint64_t,std::uint64_t);
    void send(std::uint64_t);
    void acknowledge(const Ack&);
    static int callback(void*,const HailstoneArgs*,HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*,HailstoneOut*);
    const PluginHostApi* host_=nullptr;
    bool selected_=false,sent_=false,measured_=false,has_clock_=false,world_zoning_=false;
    bool native_protocol_=false,legacy_attempted_=false,legacy_measured_=false;
    bool legacy_pending_=false,legacy_sending_=false,legacy_submitted_=false;
    Status status_=Status::Inactive;
    std::string reason_;
    std::uint64_t generation_=0,nonce_=0,deadline_=0,ack_deadline_=0,next_=0,last_clock_=0;
    unsigned attempts_=0;
    std::array<std::uint64_t,64> seen_{};
    std::size_t seen_count_=0;
    std::array<std::byte,168> report_{};
    std::optional<Ack> reentrant_ack_;
    std::uint64_t host_bytes_=0,addon_bytes_=0;
    std::uint64_t legacy_next_=0,legacy_bytes_=0,legacy_checksum_=0;
    unsigned legacy_attempts_=0;
    std::array<std::byte,16> legacy_report_{};
};
} // namespace bastion::integrity
