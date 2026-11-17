#pragma once

#include "hailstone_plugin.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace bastion {

class KinboundCatalog {
public:
    enum class Receive { Malformed, Accepted, Incomplete };
    static constexpr std::size_t kMaximumEntries = 65536;
    static constexpr std::size_t kMaximumDescendants = 4096;

    Receive receive(const void* data, std::size_t size);
    void clear();
    std::uint64_t revision() const { return revision_; }
    const char* item_flags(const HailstoneItem* item, const char* stock_flags);
    const char* item_description_append(const HailstoneItem* item);
    const char* loot_confirmation(const HailstoneItem* item, const std::uint32_t* descendants,
                                  std::size_t count, bool descendants_complete);

private:
    bool eligible(std::uint32_t id) const { return ids_.contains(id); }
    Receive mark_incomplete();

    std::unordered_set<std::uint32_t> ids_;
    bool incomplete_ = false;
    std::uint64_t revision_ = 0;
    // SDK return values are borrowed until the next call. The host must copy
    // each result immediately, including while planning mixed Loot All.
    std::string flags_, description_, confirmation_;
};

} // namespace bastion
