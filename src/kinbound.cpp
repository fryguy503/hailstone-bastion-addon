#include "kinbound.h"
#include "hailstone_byte_io.h"

#include <algorithm>
#include <new>
#include <string_view>

namespace bastion {
namespace {
constexpr std::uint32_t kMagic = 0x314e494b;
constexpr std::size_t kMaximumFlagsBytes = 4096;
constexpr std::string_view kDescription =
    "KINBOUND\n"
    "Unattuned: share within your forum account.\n"
    "Attuned: bound to this character, NO TRADE.\n"
    "Click effects require attunement.\n"
    "Use #kinbound with the item on your cursor to confirm attunement.";
constexpr std::string_view kIncomplete =
    "Item sharing metadata is incomplete. This item may have sharing or attunement restrictions.";

bool valid_item(const HailstoneItem* item) {
    return item && item->size >= offsetof(HailstoneItem, id) + sizeof(item->id) && item->id != 0;
}

std::string item_name(const HailstoneItem& item) {
    if (item.size <= offsetof(HailstoneItem, name)) return "This item";
    const auto available = std::min<std::size_t>(sizeof(item.name), item.size - offsetof(HailstoneItem, name));
    std::size_t size = 0;
    while (size < available && item.name[size]) ++size;
    return size ? std::string(item.name, size) : "This item";
}
} // namespace

void KinboundCatalog::clear() {
    ids_.clear();
    incomplete_ = false;
    ++revision_; // A reset is a presentation generation boundary, even if empty.
}

KinboundCatalog::Receive KinboundCatalog::mark_incomplete() {
    if (incomplete_) return Receive::Accepted;
    incomplete_ = true;
    ++revision_;
    return Receive::Incomplete;
}

KinboundCatalog::Receive KinboundCatalog::receive(const void* data, std::size_t size) {
    if (!data || size != 12) return Receive::Malformed;
    hailstone::ByteReader reader(data, size);
    if (reader.le<std::uint32_t>() != kMagic) return Receive::Malformed;
    const auto version = reader.le<std::uint8_t>();
    const auto selected = reader.le<std::uint8_t>();
    const auto reserved = reader.le<std::uint16_t>();
    const auto id = reader.le<std::uint32_t>();
    if (version != 1 || selected > 1 || reserved || (!id && selected)) return Receive::Malformed;
    if (!id) {
        clear();
    } else if (!selected) {
        if (ids_.erase(id)) ++revision_;
    } else if (!eligible(id)) {
        // A deliberate bounded-host policy, not a KIN1 item-ID ceiling. Once
        // incomplete, warn for unknown items until an explicit catalog reset.
        if (ids_.size() == kMaximumEntries) return mark_incomplete();
        try {
            ids_.insert(id);
        } catch (const std::bad_alloc&) {
            return mark_incomplete();
        }
        ++revision_;
    }
    return Receive::Accepted;
}

const char* KinboundCatalog::item_flags(const HailstoneItem* item, const char* stock_flags) {
    if (!valid_item(item) || !eligible(item->id) || !stock_flags) return nullptr;
    std::size_t size = 0;
    while (size < kMaximumFlagsBytes && stock_flags[size]) ++size;
    if (size == kMaximumFlagsBytes) return nullptr;
    const std::string_view original(stock_flags, size);
    std::size_t attunable = std::string_view::npos, attunable_bytes = 0, no_trade = std::string_view::npos;
    for (std::size_t begin = 0; begin < original.size();) {
        const auto comma = original.find(',', begin);
        const auto end = comma == std::string_view::npos ? original.size() : comma;
        auto first = begin;
        while (first < end && (original[first] == ' ' || original[first] == '\t')) ++first;
        auto last = end;
        while (last > first && (original[last - 1] == ' ' || original[last - 1] == '\t')) --last;
        const auto token = original.substr(first, last - first);
        if (token == "Kinbound") return nullptr;
        if (token == "Attunable" || token == "Attuneable") {
            attunable = first;
            attunable_bytes = last - first;
        } else if (token == "No Trade") {
            no_trade = first;
        }
        if (comma == std::string_view::npos) break;
        begin = comma + 1;
    }
    flags_.assign(original);
    if (attunable != std::string_view::npos) flags_.replace(attunable, attunable_bytes, "Kinbound");
    else if (no_trade != std::string_view::npos) flags_.insert(no_trade, "Kinbound, ");
    else flags_ += flags_.empty() ? "Kinbound" : ", Kinbound";
    return flags_.c_str();
}

const char* KinboundCatalog::item_description_append(const HailstoneItem* item) {
    if (!valid_item(item)) return nullptr;
    description_.clear();
    if (eligible(item->id)) description_ = kDescription;
    if (incomplete_) {
        if (!description_.empty()) description_ += "\n\n";
        description_ += kIncomplete;
    }
    return description_.empty() ? nullptr : description_.c_str();
}

const char* KinboundCatalog::loot_confirmation(const HailstoneItem* item, const std::uint32_t* descendants,
                                             std::size_t count, bool descendants_complete) {
    if (!valid_item(item)) return nullptr;
    const bool root = eligible(item->id);
    const bool bounded = count <= kMaximumDescendants && (!count || descendants);
    bool contains = false;
    if (bounded) {
        for (std::size_t i = 0; i < count; ++i) {
            if (eligible(descendants[i])) { contains = true; break; }
        }
    }
    const bool uncertain = incomplete_ || !descendants_complete || !bounded;
    if (!root && !contains && !uncertain) return nullptr;
    confirmation_ = item_name(*item);
    if (root || contains) {
        confirmation_ += root ? " is Kinbound." : " contains Kinbound items.";
        confirmation_ += "\n\nKinbound items can only be shared within your forum account until attuned. "
                         "Equipping or manually attuning permanently binds them to that character. "
                         "Click effects require attunement.\n\nLooting does not attune items. Loot this item?";
    }
    if (uncertain) {
        confirmation_ += "\n\n";
        confirmation_ += kIncomplete;
        if (!root && !contains) confirmation_ += " Loot this item?";
    }
    return confirmation_.c_str();
}

} // namespace bastion
