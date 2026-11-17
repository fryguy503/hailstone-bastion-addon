#include "gear_score_state.h"
#include <new>

namespace bastion::gear_score {
std::string format_level(std::uint32_t milli) { return std::to_string((std::uint64_t{milli} + 500) / 1000); }
std::string format_average(std::uint32_t milli) {
    const auto tenths = (std::uint64_t{milli} + 50) / 100;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}
void State::changed() { if (!++presentation_revision_) ++presentation_revision_; }
void State::begin(std::uint32_t session) {
    ready_ = false; session_ = session; generation_ = reset_floor_ = summary_revision_ = 0;
    templates_.clear(); configurations_.clear(); summary_ = {};
    changed();
}
Receive State::receive(const Packet& p) try {
    if (!valid(p)) return Receive::Invalid;
    if (!session_ || p.session != session_ || p.action == Action::Hello) return Receive::Ignored;
    if (p.action == Action::HelloAck) {
        if (ready_) return Receive::Ignored;
        ready_ = true; generation_ = p.generation; reset_floor_ = p.revision;
        changed(); return Receive::Changed;
    }
    if (!ready_) return Receive::Ignored;
    if (p.action == Action::Reset) {
        if (p.generation == generation_ && p.revision <= reset_floor_) return Receive::Ignored;
        templates_.clear(); configurations_.clear(); summary_ = {};
        generation_ = p.generation; reset_floor_ = p.revision; summary_revision_ = 0;
        changed(); return Receive::Changed;
    }
    if (p.generation != generation_ || p.revision < reset_floor_) return Receive::Ignored;
    if (p.action == Action::Summary) {
        if (summary_.received && p.revision <= summary_revision_) return Receive::Ignored;
        summary_ = {p.flags, p.equipped, p.owned, true}; summary_revision_ = p.revision;
        changed(); return Receive::Changed;
    }
    if (p.action != Action::Item) return Receive::Ignored;
    const Rating incoming{p.base, p.configuration, p.revision, p.flags};
    auto base = templates_.find(p.key.item);
    auto configuration = configurations_.find(p.key);
    const bool replace_base = base == templates_.end() ||
        (base->second.revision <= p.revision && base->second != incoming);
    const bool replace_configuration = configuration == configurations_.end() || configuration->second.revision < p.revision;
    if (!replace_base && !replace_configuration) return Receive::Ignored;
    // Allocate every needed map node before altering either live cache. A
    // failed allocation must not publish only half of this item's observation.
    std::map<std::uint32_t, Rating> new_base;
    std::map<Key, Rating> new_configuration;
    if (replace_base && base == templates_.end()) new_base.emplace(p.key.item, incoming);
    if (replace_configuration && configuration == configurations_.end()) new_configuration.emplace(p.key, incoming);
    if (replace_base) {
        if (base != templates_.end()) base->second = incoming;
        else {
            if (templates_.size() == kMaximumTemplates) templates_.erase(templates_.begin());
            templates_.insert(new_base.extract(new_base.begin()));
        }
    }
    if (replace_configuration) {
        if (configuration != configurations_.end()) configuration->second = incoming;
        else {
            if (configurations_.size() == kMaximumConfigurations) configurations_.erase(configurations_.begin());
            configurations_.insert(new_configuration.extract(new_configuration.begin()));
        }
    }
    changed(); return Receive::Changed;
} catch (const std::bad_alloc&) { return Receive::Invalid; }

std::string State::description(const Key& key, bool complete) const {
    if (!ready_ || !key.item) return {};
    const Rating* rating = nullptr;
    bool combined = false;
    if (complete)
        if (const auto found = configurations_.find(key); found != configurations_.end()) {
            rating = &found->second; combined = (rating->flags & ConfigurationValid) != 0;
        }
    if (!rating || !(rating->flags & BaseValid)) {
        const auto found = templates_.find(key.item);
        rating = found == templates_.end() ? nullptr : &found->second; combined = false;
    }
    if (!rating || !(rating->flags & BaseValid)) return "ilvl: Unavailable";
    if (rating->flags & NotGear) return "ilvl: Not gear";
    std::string text = combined ? "ilvl: " : "Base ilvl: ";
    text += format_level(combined ? rating->configuration : rating->base);
    if (rating->flags & ReviewRequired) text += " (provisional)";
    if (combined && rating->configuration != rating->base) {
        const auto difference = static_cast<std::int64_t>((std::uint64_t{rating->configuration} + 500) / 1000) -
                                static_cast<std::int64_t>((std::uint64_t{rating->base} + 500) / 1000);
        text += "\nBase ilvl: " + format_level(rating->base) + " | Augments: " +
            (difference >= 0 ? "+" : "") + std::to_string(difference);
    }
    return text;
}
std::string State::equipped() const {
    auto text = summary_.received && (summary_.flags & EquippedValid) ? format_average(summary_.equipped) : "Unavailable";
    if (summary_.received && (summary_.flags & ReviewRequired)) text += " (provisional)";
    return text;
}
std::string State::owned() const {
    auto text = summary_.received && (summary_.flags & OwnedValid) ? format_average(summary_.owned) : "Unavailable";
    if (summary_.received && (summary_.flags & OwnedStale)) text += " (stale)";
    if (summary_.received && (summary_.flags & ReviewRequired)) text += " (provisional)";
    return text;
}
} // namespace bastion::gear_score
