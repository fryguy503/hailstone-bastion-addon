#include "factions_protocol.h"
#include "citadel_services.h"
#include <map>
#include <set>
namespace bastion::factions {
std::optional<Packet> decode(std::span<const std::byte> bytes) {
    if (bytes.size() < 48 || bytes.size() > 48 + 32 * 252)
        return std::nullopt;
    try {
        hailstone::ByteReader r(bytes);
        Packet p;
        if (r.le<std::uint32_t>() != 0x31434146 || r.le<std::uint16_t>() != 1)
            return std::nullopt;
        p.kind = static_cast<Kind>(r.le<std::uint16_t>());
        p.request = r.le<std::uint32_t>();
        p.session = r.le<std::uint32_t>();
        p.character = r.le<std::uint32_t>();
        p.result = r.le<std::uint32_t>();
        p.hash = r.le<std::uint64_t>();
        p.categories = r.le<std::uint32_t>();
        p.entries = r.le<std::uint32_t>();
        p.offset = r.le<std::uint32_t>();
        const auto count = r.le<std::uint32_t>();
        if (!p.request || !p.session || !p.character || p.result > 4 || p.categories > 128 || p.entries > 16384)
            return std::nullopt;
        if (p.kind == Kind::Error) {
            if (!p.result || count || p.offset || p.categories || p.entries || p.hash || bytes.size() != 48)
                return std::nullopt;
            return p;
        }
        if (p.result || !p.hash)
            return std::nullopt;
        if (p.kind == Kind::Unchanged) {
            if (count || p.offset || bytes.size() != 48)
                return std::nullopt;
            return p;
        }
        const bool categories = p.kind == Kind::Categories;
        if (!categories && p.kind != Kind::Entries)
            return std::nullopt;
        const auto total = categories ? p.categories : p.entries;
        const auto stride = categories ? 132u : 252u;
        if (count > (categories ? 16u : 32u) || p.offset > total || count > total - p.offset ||
            (!count && (total || p.offset)) || bytes.size() != 48 + std::size_t(count) * stride)
            return std::nullopt;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto raw = bytes.subspan(48 + std::size_t(i) * stride, stride);
            hailstone::ByteReader row(raw);
            if (categories) {
                Category c;
                c.id = row.le<std::uint32_t>();
                c.parent = row.le<std::uint32_t>();
                c.order = row.le<std::int32_t>();
                c.name = services::text(row, 96, true);
                c.icon = services::text(row, 24);
                if (!c.id || c.parent == c.id)
                    return std::nullopt;
                std::copy(raw.begin(), raw.end(), c.wire.begin());
                p.category_rows.push_back(std::move(c));
            } else {
                Entry e;
                e.id = row.le<std::int32_t>();
                e.earned = row.le<std::int32_t>();
                e.minimum = row.le<std::int32_t>();
                e.maximum = row.le<std::int32_t>();
                e.effective = row.le<std::int32_t>();
                e.next = row.le<std::int32_t>();
                e.reaction = row.le<std::uint8_t>();
                const auto members = row.le<std::uint8_t>();
                if (row.le<std::uint16_t>() || e.id <= 0 || e.minimum > e.maximum || e.reaction < 1 || e.reaction > 9 ||
                    !members || members > 32)
                    return std::nullopt;
                std::set<std::uint32_t> ids;
                for (unsigned n = 0; n < 32; ++n) {
                    const auto id = row.le<std::uint32_t>();
                    if ((n < members) != bool(id))
                        return std::nullopt;
                    if (id) {
                        if (!ids.insert(id).second)
                            return std::nullopt;
                        e.categories.push_back(id);
                    }
                }
                e.name = services::text(row, 96, true);
                std::copy(raw.begin(), raw.end(), e.wire.begin());
                p.entry_rows.push_back(std::move(e));
            }
        }
        return p;
    } catch (const std::out_of_range &) {
        return std::nullopt;
    }
}
std::vector<std::byte> request(std::uint32_t id, std::uint32_t session, std::uint64_t known) {
    if (!id || !session)
        return {};
    hailstone::ByteWriter w;
    w.le<std::uint32_t>(0x31434146);
    w.le<std::uint16_t>(1);
    w.le<std::uint16_t>(1);
    w.le(id);
    w.le(session);
    w.le(known);
    w.le<std::uint32_t>(0);
    return w.take();
}
bool validate(const std::vector<Category> &categories, const std::vector<Entry> &entries, std::uint64_t expected) {
    std::map<std::uint32_t, std::uint32_t> parents;
    std::set<std::int32_t> ids;
    std::uint64_t hash = UINT64_C(14695981039346656037);
    const auto feed = [&](const auto &wire) {
        for (const auto b : wire) {
            hash ^= std::to_integer<std::uint8_t>(b);
            hash *= UINT64_C(1099511628211);
        }
    };
    for (const auto &c : categories) {
        if (!parents.emplace(c.id, c.parent).second)
            return false;
        feed(c.wire);
    }
    for (const auto &c : categories) {
        auto id = c.parent;
        std::size_t depth = 0;
        while (id) {
            const auto p = parents.find(id);
            if (p == parents.end() || ++depth > parents.size())
                return false;
            id = p->second;
        }
    }
    for (const auto &e : entries) {
        if (!ids.insert(e.id).second)
            return false;
        for (auto id : e.categories)
            if (!parents.contains(id))
                return false;
        feed(e.wire);
    }
    return (hash ? hash : 1) == expected;
}
bool belongs(const Entry &entry, std::uint32_t selected, const std::vector<Category> &categories) {
    if (!selected)
        return true;
    for (auto id : entry.categories) {
        for (std::size_t depth = 0; id && depth <= categories.size(); ++depth) {
            if (id == selected)
                return true;
            const auto p =
                std::find_if(categories.begin(), categories.end(), [&](const Category &c) { return c.id == id; });
            if (p == categories.end())
                break;
            id = p->parent;
        }
    }
    return false;
}
int gauge(const Entry &e) {
    const auto range = std::int64_t(e.maximum) - e.minimum;
    if (!range)
        return 1000;
    return static_cast<int>(std::clamp((std::int64_t(e.earned) - e.minimum) * 1000 / range, INT64_C(0), INT64_C(1000)));
}
const char *reaction_name(std::uint8_t value) {
    static constexpr const char *names[] = {"Unknown",     "Ally",         "Warmly",  "Kindly",      "Amiably",
                                            "Indifferent", "Apprehensive", "Dubious", "Threatening", "Scowls"};
    return value <= 9 ? names[value] : names[0];
}
} // namespace bastion::factions
