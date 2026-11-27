#pragma once

#include "hailstone_plugin.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>

namespace bastion {

class ItemDiscovery {
public:
    void set_host(const PluginHostApi* host);
    void peer(bool selected);
    void game_state(int value);
    void begin_zone();
    void pulse();
    // True consumes a reply to the add-on's item-info requests. Books and other item-info traffic pass through.
    bool packet(std::uint16_t opcode, const void* bytes, std::size_t size);
    const char* item_description_append(const HailstoneItem* item);
    std::uint64_t revision() const { return revision_; }

private:
    enum class Status { Queued, Pending, Ready, Retry };
    struct Entry {
        std::uint32_t id = 0, target = 0;
        Status status = Status::Queued;
        std::uint64_t next_at = 0;
        std::string description;
    };

    void clear();
    std::list<Entry>::iterator pending(std::uint32_t target);

    const PluginHostApi* host_ = nullptr;
    bool selected_ = false, in_game_ = false;
    std::list<Entry> entries_;
    std::uint64_t next_pulse_at_ = 0, revision_ = 0;
};

} // namespace bastion
