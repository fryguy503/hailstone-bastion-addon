#include "saved_expeditions_protocol.h"
#include "citadel_services.h"
#include <set>
namespace bastion::saved_expeditions {
bool valid_uuid(std::string_view s) {
    if (s.size() != 36)
        return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-')
                return false;
        } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    }
    return true;
}
std::optional<Response> decode(std::span<const std::byte> bytes) {
    if (bytes.size() < 160 || bytes.size() > 160 + 16 * 272)
        return std::nullopt;
    try {
        hailstone::ByteReader r(bytes);
        Response out;
        if (r.le<std::uint32_t>() != 0x31525a44 || r.le<std::uint16_t>() != 1)
            return std::nullopt;
        out.action = static_cast<Action>(r.le<std::uint16_t>());
        out.request = r.le<std::uint32_t>();
        out.result = static_cast<Result>(r.le<std::uint32_t>());
        out.server_time = r.le<std::uint64_t>();
        const auto count = r.le<std::uint32_t>();
        if (r.le<std::uint32_t>() || !out.request || out.result > Result::MustLeaveInstance || count > 16 ||
            (out.action != Action::ListResult && out.action != Action::ResumeResult) ||
            (count && (out.action != Action::ListResult || out.result != Result::Ok)) ||
            bytes.size() != 160 + std::size_t(count) * 272)
            return std::nullopt;
        out.message = services::text(r, 128);
        std::set<std::uint32_t> ids;
        std::set<std::string> uuids;
        for (std::uint32_t i = 0; i < count; ++i) {
            Entry e;
            e.dz_id = r.le<std::uint32_t>();
            e.zone_id = r.le<std::uint32_t>();
            e.zone_version = r.le<std::uint32_t>();
            e.instance_id = r.le<std::uint32_t>();
            e.members = r.le<std::uint32_t>();
            e.max_members = r.le<std::uint32_t>();
            e.leader_id = r.le<std::uint32_t>();
            e.eligibility = static_cast<Result>(r.le<std::uint32_t>());
            e.expires = r.le<std::uint64_t>();
            const auto uuid_bytes = r.bytes(40);
            hailstone::ByteReader u(uuid_bytes);
            e.uuid = services::text(u, 40, true);
            if (!valid_uuid(e.uuid) ||
                std::any_of(uuid_bytes.begin() + 36, uuid_bytes.end(), [](std::byte b) { return b != std::byte{}; }))
                return std::nullopt;
            e.name = services::text(r, 128);
            e.leader = services::text(r, 64);
            if (!e.dz_id || !e.zone_id || !e.instance_id || e.instance_id > 65535 ||
                e.eligibility > Result::MustLeaveInstance || !ids.insert(e.dz_id).second ||
                !uuids.insert(e.uuid).second)
                return std::nullopt;
            out.entries.push_back(std::move(e));
        }
        return out;
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }
}
std::vector<std::byte> request(Action action, std::uint32_t id, const Entry *e) {
    if (!id || (action != Action::List && action != Action::Resume) ||
        (action == Action::Resume && (!e || !e->dz_id || !valid_uuid(e->uuid))) || (action == Action::List && e))
        return {};
    hailstone::ByteWriter w;
    w.le<std::uint32_t>(0x31525a44);
    w.le<std::uint16_t>(1);
    w.le(static_cast<std::uint16_t>(action));
    w.le(id);
    w.le(e ? e->dz_id : 0u);
    w.fixed_string(e ? e->uuid : std::string_view{}, 40);
    w.zeros(8);
    return w.take();
}
const char *result_text(Result result) {
    static constexpr const char *text[] = {"Ready to rejoin.",
                                           "Saved expeditions are disabled.",
                                           "Invalid request.",
                                           "This saved expedition no longer exists.",
                                           "You do not have permission to rejoin.",
                                           "This expedition has expired.",
                                           "Leave your current expedition first.",
                                           "This expedition is locked.",
                                           "This expedition is full.",
                                           "An expedition lockout prevents rejoining.",
                                           "Your season does not match.",
                                           "Resolve the pending invitation first.",
                                           "The server is busy. Refresh shortly.",
                                           "Saved expedition data is unavailable.",
                                           "You are already a member.",
                                           "Leave the instance before rejoining."};
    const auto i = static_cast<std::uint32_t>(result);
    return i < 16 ? text[i] : "Unknown expedition status.";
}
} // namespace bastion::saved_expeditions
