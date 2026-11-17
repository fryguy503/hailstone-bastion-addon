#pragma once

#include "gear_score_protocol.h"
#include <map>
#include <string>

namespace bastion::gear_score {
enum class Receive { Invalid, Ignored, Changed };
struct Summary {
    std::uint16_t flags = 0;
    std::uint32_t equipped = 0, owned = 0;
    bool received = false;
};
class State {
public:
    void begin(std::uint32_t session);
    void retire() { begin(0); }
    Receive receive(const Packet& packet);
    std::string description(const Key& key, bool complete) const;
    std::string equipped() const;
    std::string owned() const;
    bool ready() const { return ready_; }
    std::uint32_t session() const { return session_; }
    std::uint32_t generation() const { return generation_; }
    std::uint64_t revision() const { return presentation_revision_; }
    const Summary& summary() const { return summary_; }
    std::size_t templates() const { return templates_.size(); }
    std::size_t configurations() const { return configurations_.size(); }
private:
    struct Rating {
        std::uint32_t base, configuration, revision;
        std::uint16_t flags;
        bool operator==(const Rating&) const = default;
    };
    void changed();
    bool ready_ = false;
    std::uint32_t session_ = 0, generation_ = 0, reset_floor_ = 0, summary_revision_ = 0;
    std::uint64_t presentation_revision_ = 0;
    Summary summary_;
    std::map<std::uint32_t, Rating> templates_;
    std::map<Key, Rating> configurations_;
};
std::string format_level(std::uint32_t milli);
std::string format_average(std::uint32_t milli);
} // namespace bastion::gear_score
