#include "item_discovery.h"

#include "hailstone_byte_io.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string_view>

namespace bastion {
namespace {

constexpr std::uint16_t kReadBook = 0x72df;
constexpr std::uint32_t kItemInfo = 2;
constexpr std::size_t kTextOffset = 22, kPacketSize = 8216;
constexpr std::uint32_t kWindow = 0xfe, kTargetMask = 0xc0000000, kTargetMarker = 0x40000000;
constexpr std::uint64_t kSendInterval = 100, kReplyTimeout = 3000, kRetryDelay = 30000;
constexpr std::size_t kMaxEntries = 256;

std::uint32_t next_target() {
    // RoF2's ReadBook decoder narrows the window to one byte. EQEmu echoes
    // the full 32-bit target, so concurrent requests and later zones can use
    // distinct tokens while holding the window at a recognizable value.
    static std::atomic<std::uint32_t> sequence{
        static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    return kTargetMarker | (sequence.fetch_add(1, std::memory_order_relaxed) & ~kTargetMask);
}

std::vector<std::byte> request(std::uint32_t id, std::uint32_t target) {
    hailstone::ByteWriter writer;
    writer.reserve(kPacketSize);
    writer.le(kWindow);
    // With no possessions slot, the server leaves type as ItemInfo and does
    // not process the lookup as a recipe-bearing inventory book.
    writer.le<std::int16_t>(-1); // slot
    writer.le<std::int16_t>(-1); // bag subslot
    writer.le<std::int16_t>(-1); // augment
    writer.le<std::int16_t>(0);  // padding
    writer.le(kItemInfo);
    writer.le(target);
    writer.zeros(2); // can_cast and can_scribe
    writer.fixed_string(std::to_string(id) + '#', kPacketSize - kTextOffset);
    return writer.take();
}

std::string discovery_line(std::string_view text) {
    constexpr std::string_view label = "Discovered By: ", line_break = "<br>";
    const auto offset = text.rfind(label);
    if (offset == text.npos || !text.ends_with(line_break) ||
        (offset != 0 && !text.substr(0, offset).ends_with(line_break))) return {};

    text.remove_prefix(offset + label.size());
    if (text.size() <= line_break.size()) return {};
    text.remove_suffix(line_break.size());
    // EQEmu appends the first discoverer's character name after any charm
    // STML. Copy the name alone so arbitrary book markup never reaches the UI.
    if (text.size() > 63 || !std::ranges::all_of(text, [](char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        })) return {};
    return std::string(label) + std::string(text);
}

} // namespace

void ItemDiscovery::clear() {
    if (!entries_.empty()) ++revision_;
    entries_.clear();
    next_pulse_at_ = 0;
}

void ItemDiscovery::set_host(const PluginHostApi* host) {
    clear();
    selected_ = in_game_ = false;
    host_ = host;
}

void ItemDiscovery::peer(bool selected) {
    clear();
    selected_ = selected;
}

void ItemDiscovery::game_state(int value) {
    in_game_ = value == GAMESTATE_INGAME;
    if (!in_game_) clear();
}

void ItemDiscovery::begin_zone() {
    in_game_ = false;
    clear();
}

std::list<ItemDiscovery::Entry>::iterator ItemDiscovery::pending(std::uint32_t target) {
    return std::ranges::find_if(entries_, [target](const Entry& entry) {
        return entry.status == Status::Pending && entry.target == target;
    });
}

void ItemDiscovery::pulse() {
    if (!selected_ || !in_game_ || entries_.empty() || !HAILSTONE_HOST_HAS(host_, monotonic_ms) ||
        !HAILSTONE_HOST_HAS(host_, send_packet) || !HAILSTONE_HOST_HAS(host_, get_player)) return;
    const auto now = host_->monotonic_ms(host_->host);
    if (now < next_pulse_at_) return;
    next_pulse_at_ = now + kSendInterval;
    for (auto& entry : entries_) {
        if (entry.status == Status::Pending && now >= entry.next_at) {
            entry.status = Status::Retry;
            entry.next_at += kRetryDelay;
        }
    }
    const auto entry = std::ranges::find_if(entries_, [](const Entry& value) { return value.status == Status::Queued; });
    if (entry == entries_.end()) return;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (!host_->get_player(host_->host, &player) ||
        player.size < offsetof(HailstonePlayer, in_zone) + sizeof(player.in_zone) || !player.in_zone) return;

    const auto target = next_target();
    const auto bytes = request(entry->id, target);
    entry->status = Status::Pending;
    entry->target = target;
    entry->next_at = now + kReplyTimeout;
    if (!host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kReadBook, bytes.data(), bytes.size())) {
        // The host can dispatch other callbacks while sending. Re-find the
        // request so a reset or synchronous reply cannot invalidate it.
        const auto still_pending = pending(target);
        if (still_pending != entries_.end()) {
            still_pending->status = Status::Retry;
            still_pending->next_at = now + kRetryDelay;
        }
    }
}

bool ItemDiscovery::packet(std::uint16_t opcode, const void* bytes, std::size_t size) {
    if (!selected_ || opcode != kReadBook || !bytes || size < 20) return false;
    hailstone::ByteReader reader(bytes, size);
    if (reader.le<std::uint32_t>() != kWindow) return false;
    const auto slot = reader.le<std::int16_t>();
    const auto sub = reader.le<std::int16_t>();
    const auto aug = reader.le<std::int16_t>();
    reader.skip(2);
    if (reader.le<std::uint32_t>() != kItemInfo) return false;
    const auto target = reader.le<std::uint32_t>();
    if ((target & kTargetMask) != kTargetMarker) return false;

    // Suppress the add-on-owned reply even after timeout or zone transition.
    // Only the matching live request can change an item's description.
    if (!in_game_ || size <= kTextOffset || size > kPacketSize || slot != -1 || sub != -1 || aug != -1 ||
        reader.le<std::uint8_t>() || reader.le<std::uint8_t>()) return true;
    const auto entry = pending(target);
    if (entry == entries_.end()) return true;
    const auto now = host_->monotonic_ms(host_->host);
    if (now >= entry->next_at) return true;

    const auto* text = static_cast<const char*>(bytes) + kTextOffset;
    const auto* end = static_cast<const char*>(std::memchr(text, 0, size - kTextOffset));
    if (!end) return true;
    entry->description = discovery_line({text, static_cast<std::size_t>(end - text)});
    if (entry->description.empty()) {
        entry->status = Status::Retry;
        entry->next_at = now + kRetryDelay;
    } else {
        entry->status = Status::Ready;
        ++revision_;
    }
    return true;
}

const char* ItemDiscovery::item_description_append(const HailstoneItem* item) {
    if (!selected_ || !in_game_ || !item || item->size < offsetof(HailstoneItem, id) + sizeof(item->id) ||
        !item->id || !HAILSTONE_HOST_HAS(host_, monotonic_ms) || !HAILSTONE_HOST_HAS(host_, send_packet) ||
        !HAILSTONE_HOST_HAS(host_, get_player)) return nullptr;

    const auto entry = std::ranges::find(entries_, item->id, &Entry::id);
    if (entry != entries_.end()) {
        if (entry->status == Status::Ready) {
            entries_.splice(entries_.end(), entries_, entry);
            return entry->description.c_str();
        }
        if (entry->status == Status::Retry && host_->monotonic_ms(host_->host) >= entry->next_at) {
            entry->status = Status::Queued;
            entries_.splice(entries_.end(), entries_, entry);
        }
        return nullptr;
    }
    if (entries_.size() == kMaxEntries) {
        const auto old = std::ranges::find_if(entries_, [](const Entry& value) {
            return value.status == Status::Ready || value.status == Status::Retry;
        });
        if (old == entries_.end()) return nullptr;
        entries_.erase(old);
    }
    entries_.emplace_back();
    entries_.back().id = item->id;
    return nullptr;
}

} // namespace bastion
