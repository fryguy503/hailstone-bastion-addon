#include "home_city_protocol.h"
#include "citadel_services.h"
#include <set>
namespace bastion::home_city {
std::optional<Response> decode(std::span<const std::byte> bytes) {
    if (bytes.size() < 216 || bytes.size() > 216 + 32 * 72)
        return std::nullopt;
    try {
        hailstone::ByteReader r(bytes);
        Response out;
        if (r.le<std::uint32_t>() != 0x31594348 || r.le<std::uint16_t>() != 1)
            return std::nullopt;
        out.action = static_cast<Action>(r.le<std::uint16_t>());
        out.request = r.le<std::uint32_t>();
        out.result = static_cast<Result>(r.le<std::uint32_t>());
        out.grant = r.le<std::uint64_t>();
        out.snapshot = r.le<std::uint64_t>();
        out.race = r.le<std::uint32_t>();
        out.class_id = r.le<std::uint32_t>();
        out.deity = r.le<std::uint32_t>();
        out.current_home = r.le<std::uint32_t>();
        const auto count = r.le<std::uint32_t>();
        out.flags = r.le<std::uint32_t>();
        if (out.action < Action::Probe || out.action > Action::Suppress || !out.request ||
            out.result > Result::Unsafe || count > 32 || (out.flags & ~3u) ||
            bytes.size() != 216 + std::size_t(count) * 72 ||
            (count && (out.result != Result::Ok || out.action > Action::List || !out.grant || !out.snapshot)))
            return std::nullopt;
        out.message = services::text(r, 160);
        std::set<std::uint32_t> ids;
        for (std::uint32_t i = 0; i < count; ++i) {
            Entry e;
            e.choice = r.le<std::uint32_t>();
            e.zone = r.le<std::uint32_t>();
            e.name = services::text(r, 64);
            if (!e.choice || !e.zone || !ids.insert(e.choice).second)
                return std::nullopt;
            out.entries.push_back(std::move(e));
        }
        return out;
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }
}
std::vector<std::byte> request(Action action, std::uint32_t id, std::uint32_t choice, std::uint64_t grant,
                               std::uint64_t snapshot) {
    if (!id || action < Action::Probe || action > Action::Suppress ||
        (action <= Action::List ? (choice || grant || snapshot)
                                : (!grant || !snapshot || (action == Action::Commit ? !choice : choice != 0))))
        return {};
    hailstone::ByteWriter w;
    w.le<std::uint32_t>(0x31594348);
    w.le<std::uint16_t>(1);
    w.le(static_cast<std::uint16_t>(action));
    w.le(id);
    w.le(choice);
    w.le(grant);
    w.le(snapshot);
    return w.take();
}
} // namespace bastion::home_city
