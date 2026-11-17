// SPDX-License-Identifier: MIT
#include "../server/native_integrity.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace native = bastion_native_integrity;
namespace {
unsigned checks = 0;
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
std::vector<std::uint8_t> report(std::uint64_t nonce = 123) {
    auto data = native::header(2, 0, nonce);
    native::write(data, native::required_features, 8); native::write(data, 3, 4); native::write(data, 0, 4);
    for (unsigned binary = 0; binary < 2; ++binary) {
        native::write(data, 12345 + binary, 8); native::write(data, 98765 + binary, 8);
        for (unsigned n = 0; n < 32; ++n) data.push_back(static_cast<std::uint8_t>(n + binary));
    }
    constexpr std::string_view version = "0.1.0-preview.1";
    for (std::size_t n = 0; n < 32; ++n) data.push_back(n < version.size() ? static_cast<std::uint8_t>(version[n]) : 0);
    return data;
}
}
int main() try {
    auto bytes = report(); native::Report decoded;
    check(bytes.size() == 168 && native::decode(bytes, decoded), "wire report size or decoder");
    check(decoded.nonce == 123 && decoded.features == 0x3fff && decoded.executable.bytes == 12345 &&
          decoded.addon.fnv63 == 98766 && decoded.addon_version == "0.1.0-preview.1", "wire fields");
    const auto pair = native::hex(decoded.executable.sha) + ':' + native::hex(decoded.addon.sha);
    check(native::approved(pair, decoded), "exact binary pair");
    check(native::approved("invalid; " + pair + "\r\n", decoded), "delimited approved pair");
    std::string uppercase = pair;
    for (auto& c : uppercase) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - ('a' - 'A'));
    check(native::approved(uppercase, decoded), "uppercase hash");
    for (const auto& invalid : {std::string{}, std::string{"0"}, pair.substr(1), pair + '0',
                               native::hex(decoded.addon.sha) + ':' + native::hex(decoded.executable.sha)})
        check(!native::approved(invalid, decoded), "unapproved or reversed pair");
    const auto challenge = native::challenge(123);
    check(challenge.size() == 32 && native::read(challenge, 8, 4) == 1 && native::read(challenge, 24, 8) == 0x3fff, "challenge");
    const auto ack = native::acknowledgement(123, native::Status::Accepted, "Bastion integration verified.");
    check(ack.size() == 152 && native::read(ack, 8, 4) == 3 && native::read(ack, 12, 4) == 1 && ack.back() == 0, "acknowledgement");
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        native::Report output; output.nonce = 999;
        check(!native::decode(std::span(bytes).first(size), output) && output.nonce == 999, "truncated report modified output");
    }
    for (const std::size_t at : {0u, 4u, 8u, 12u, 32u, 36u, 143u}) {
        auto altered = bytes; altered[at] ^= 255;
        check(!native::decode(altered, decoded), "invalid header/version/reserved/text accepted");
    }
    for (const std::size_t at : {16u, 40u, 48u, 88u, 96u}) {
        auto altered = bytes; std::fill_n(altered.begin() + static_cast<std::ptrdiff_t>(at), 8, std::uint8_t{0});
        check(!native::decode(altered, decoded), "zero identity accepted");
    }
    for (const std::size_t at : {56u, 104u}) {
        auto altered = bytes; std::fill_n(altered.begin() + static_cast<std::ptrdiff_t>(at), 32, std::uint8_t{0});
        check(!native::decode(altered, decoded), "zero SHA accepted");
    }
    native::Session session;
    check(session.evaluate(bytes, 1000, pair, decoded) == native::Status::Rejected, "unsolicited report");
    session.issue(123, 1000);
    check(session.evaluate(bytes, 1000, "", decoded) == native::Status::Rejected && !session.verified(), "unconfigured server failed open");
    check(session.evaluate(bytes, 1001, pair, decoded) == native::Status::Accepted && session.verified(), "approved report");
    check(session.evaluate(bytes, 1002, pair, decoded) == native::Status::Accepted, "identical retransmission");
    auto altered = bytes; altered[48] ^= 1;
    check(session.evaluate(altered, 1003, pair, decoded) == native::Status::Rejected, "identity changed after acceptance");
    session.issue(456, 1004);
    check(!session.verified() && session.evaluate(bytes, 1005, pair, decoded) == native::Status::Rejected, "previous world nonce");
    session.issue(123, 1000);
    check(session.evaluate(bytes, 61000, pair, decoded) == native::Status::Rejected, "expired challenge");
    altered = bytes; altered[24] &= 0xfe;
    check(session.evaluate(altered, 1001, pair, decoded) == native::Status::Unsupported, "missing native integration capability");
    const auto near_wrap = std::numeric_limits<std::uint32_t>::max() - 100;
    session.issue(123, near_wrap);
    check(session.evaluate(bytes, 50, pair, decoded) == native::Status::Accepted, "monotonic timer wrap");
    session.issue(123, 1000, 7);
    check(session.evaluate(bytes, 1001, pair, decoded, 8) == native::Status::Rejected && !session.verified(), "different authenticated account reused challenge");
    check(session.evaluate(bytes, 1001, pair, decoded, 7) == native::Status::Accepted, "bound authenticated account");
    session = native::Session{};
    session.issue(456, 1002, 8);
    check(!session.verified() && session.evaluate(bytes, 1003, pair, decoded, 8) == native::Status::Rejected, "reauthentication retained native acceptance");
    check(!native::is_native(std::array<std::uint8_t, 8>{0x31,0x54,0x49,0x43,2,0,0,0}), "legacy CIT1 version 2 intercepted");
    std::cout << "Server integrity: " << checks << " checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Server integrity after " << checks << " checks: " << error.what() << '\n'; return 1;
}
