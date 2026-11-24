#include "pet_inventory_addon.h"
namespace bastion::pet_inventory {
namespace {
constexpr std::size_t MaxPacket = 262144, MaxStored = MaxPacket * kSlots.size();
bool identity(const HailstoneItem &item) {
    return item.size >= offsetof(HailstoneItem, instance_id) + sizeof(item.instance_id) && item.id &&
           std::find(std::begin(item.serial), std::end(item.serial), '\0') != std::end(item.serial) && item.serial[0] &&
           std::find(std::begin(item.name), std::end(item.name), '\0') != std::end(item.name);
}
bool presentation(const HailstoneItem &item) {
    return item.size >= offsetof(HailstoneItem, instance_id) + sizeof(item.instance_id) && item.id &&
           std::find(std::begin(item.serial), std::end(item.serial), '\0') != std::end(item.serial) &&
           std::find(std::begin(item.name), std::end(item.name), '\0') != std::end(item.name);
}
} // namespace
void Adapter::clear_pet() {
    pet_ = 0;
    pet_incarnation_ = 0;
    known_slots_ = 0;
    items_.clear();
    status_ = {};
    status_known_ = false;
    pending_ = false;
    refresh_requested_ = false;
    attempts_ = 0;
    next_poll_ = last_refresh_ = 0;
    revision_ = services::token();
}
void Adapter::retire() {
    context_.reset();
    clear_pet();
    shown_ = sent_ = false;
    message_ = "Open Pet Inventory to request received equipment.";
}
void Adapter::set_host(const PluginHostApi *h) {
    shutdown();
    host_ = h;
}
void Adapter::initialize() {
    if (HAILSTONE_HOST_HAS(host_, add_ui_function))
        host_->add_ui_function(host_->host, "pet_inventory", ui, this);
}
void Adapter::shutdown() {
    if (HAILSTONE_HOST_HAS(host_, remove_ui_function))
        host_->remove_ui_function(host_->host, "pet_inventory");
    retire();
    selected_ = in_game_ = false;
}
void Adapter::peer(bool v) {
    selected_ = v;
    retire();
}
void Adapter::game_state(int v) {
    in_game_ = v == GAMESTATE_INGAME;
    if (!in_game_)
        retire();
}
void Adapter::begin_zone() {
    retire();
    in_game_ = false;
}
bool Adapter::synchronize() {
    const auto current = services::context(host_, selected_, in_game_);
    if (current != context_) {
        retire();
        context_ = current;
    }
    if (!current)
        return false;
    const auto time = host_->monotonic_ms(host_->host);
    if (time < now_) {
        retire();
        context_ = current;
    }
    now_ = time;
    HailstonePlayer player{};
    player.size = sizeof(player);
    if (!host_->get_player(host_->host, &player) ||
        player.size < offsetof(HailstonePlayer, pet_id) + sizeof(player.pet_id))
        return false;
    std::uint64_t incarnation = 0;
    if (player.pet_id) {
        HailstoneSpawn spawn{};
        spawn.size = sizeof(spawn);
        if (host_->get_spawn(host_->host, player.pet_id, &spawn) &&
            spawn.size >= offsetof(HailstoneSpawn, incarnation) + sizeof(spawn.incarnation))
            incarnation = spawn.incarnation;
    }
    if (pet_ != player.pet_id || pet_incarnation_ != incarnation) {
        clear_pet();
        pet_ = player.pet_id;
        pet_incarnation_ = incarnation;
        message_ = "Pet identity changed. Refresh to load its equipment.";
    }
    return true;
}
bool Adapter::fresh() const {
    return status_known_ && !refresh_requested_ && now_ >= received_at_ && now_ - received_at_ < 10000;
}
bool Adapter::known(std::uint32_t slot) const { return supported(slot) && (known_slots_ & (1u << slot)); }
bool Adapter::can_equip(std::uint32_t slot) const {
    return context_ && pet_ && pet_incarnation_ && fresh() && !pending_ && known(slot) &&
           (status_.flags & ManagedPet) && !(status_.flags & Casting);
}
bool Adapter::can_remove(std::uint32_t slot) const {
    return can_equip(slot) && !(status_.flags & Locked) && status_.ownership_known &&
           (status_.managed & (1u << slot)) && items_.contains(slot) && identity(items_.at(slot).value);
}
std::uint32_t Adapter::lock_remaining() const {
    if (!status_known_ || now_ < received_at_)
        return 0;
    const auto elapsed = now_ - received_at_;
    return elapsed >= status_.remaining_ms ? 0 : status_.remaining_ms - static_cast<std::uint32_t>(elapsed);
}
bool Adapter::cursor(HailstoneItem &out, bool *present) const {
    if (present)
        *present = true;
    if (!HAILSTONE_HOST_HAS(host_, get_item))
        return false;
    out = {};
    out.size = sizeof(out);
    const HailstoneItemSlot slot{0, 33, -1, -1};
    const bool found = host_->get_item(host_->host, &slot, &out) != 0;
    if (present)
        *present = found;
    return found && identity(out);
}
void Adapter::pulse() {
    if (!synchronize())
        return;
    if (pending_ && now_ - sent_at_ >= 10000) {
        pending_ = false;
        status_known_ = false;
        message_ = "No current status received. Refresh before another equipment change; no change was retried.";
        const auto delay = std::min<std::uint64_t>(30000, UINT64_C(2500) << std::min(attempts_, 3u));
        next_poll_ = now_ > UINT64_MAX - delay ? UINT64_MAX : now_ + delay;
    }
    if (shown_ && !pending_ && now_ >= next_poll_ && attempts_ < 5 && (!sent_ || now_ - sent_at_ >= 1100)) {
        next_poll_ = now_ > UINT64_MAX - 5000 ? UINT64_MAX : now_ + 5000;
        const bool missing_items = status_.ownership_known && (status_.occupied & ~known_slots_) &&
                                   now_ - last_refresh_ >= 10000;
        // A regular NPC hand-in can replace an item without changing either status mask.
        const bool charm_refresh_due = (status_.flags & ManagedPet) && (status_.flags & Charmed) &&
                                       now_ - last_refresh_ >= 30000;
        const bool full_refresh = refresh_requested_ || !status_known_ || missing_items || charm_refresh_due;
        send(full_refresh ? Action::Refresh : Action::Status);
    }
}
bool Adapter::send(Action action, std::uint32_t slot, std::uint32_t count, std::uint64_t expected_revision) {
    if (!synchronize() || pending_ || (sent_ && now_ - sent_at_ < 1100) ||
        ((action == Action::Equip || action == Action::Remove) &&
         (!expected_revision || expected_revision != revision_)))
        return false;
    const auto bytes = request(action, slot, count);
    if (bytes.empty())
        return false;
    if (action == Action::Refresh) {
        items_.clear();
        known_slots_ = 0;
        status_known_ = false;
        refresh_requested_ = false;
        last_refresh_ = now_;
    }
    pending_ = sent_ = true;
    sent_at_ = now_;
    ++attempts_;
    const auto context = context_;
    const auto pet = pet_;
    const auto incarnation = pet_incarnation_;
    const auto operation = services::token();
    revision_ = operation;
    message_ = action == Action::Refresh ? "Loading received pet equipment..."
               : action == Action::Status
                   ? "Refreshing pet status..."
                   : "Equipment request sent. Await authoritative inventory and pet status; sending is not success.";
    const bool admitted =
        host_->send_packet(host_->host, HAILSTONE_STREAM_ZONE, kOpcode, bytes.data(), bytes.size()) != 0;
    if (!admitted && context_ == context && pet_ == pet && pet_incarnation_ == incarnation && pending_ &&
        revision_ == operation) {
        pending_ = false;
        status_known_ = false;
        message_ = "Connection unavailable. Refresh before trying again.";
    }
    return admitted;
}
bool Adapter::refresh() {
    if (!synchronize())
        return false;
    attempts_ = 0;
    refresh_requested_ = true;
    next_poll_ = now_;
    return pending_ || (sent_ && now_ - sent_at_ < 1100) || send(Action::Refresh);
}
bool Adapter::equip(std::uint64_t revision, std::uint32_t slot, std::uint32_t item, std::string_view serial,
                    std::uint32_t instance, std::uint32_t quantity) {
    if (!synchronize() || revision != revision_ || !can_equip(slot) || !quantity)
        return false;
    HailstoneItem current{};
    if (!cursor(current) || current.id != item || services::bounded(current.serial) != serial ||
        current.instance_id != instance || current.stack_count != quantity)
        return false;
    return send(Action::Equip, slot, quantity, revision);
}
bool Adapter::remove(std::uint64_t revision, std::uint32_t slot, std::uint32_t item, std::string_view serial,
                     std::uint32_t instance) {
    if (!synchronize() || revision != revision_ || !can_remove(slot))
        return false;
    HailstoneItem current{};
    bool present = true;
    cursor(current, &present);
    if (present || revision != revision_ || !items_.contains(slot))
        return false;
    const auto &owned = items_.at(slot).value;
    if (owned.id != item || services::bounded(owned.serial) != serial || owned.instance_id != instance)
        return false;
    return send(Action::Remove, slot, 1, revision);
}
bool Adapter::inspect(std::uint64_t revision, std::uint32_t slot) {
    if (!synchronize() || revision != revision_ || !items_.contains(slot) ||
        !HAILSTONE_HOST_HAS(host_, inspect_item_packet))
        return false;
    const auto copy = items_.at(slot).packet;
    return host_->inspect_item_packet(host_->host, copy.data(), copy.size()) != 0;
}
bool Adapter::packet(std::uint16_t opcode, const void *bytes, std::size_t size) {
    if (!selected_)
        return false;
    const bool private_packet = opcode == kOpcode;
    if (!private_packet && opcode != kItemPacket && opcode != kDeleteItem)
        return false;
    synchronize();
    if (!context_ || (!bytes && size))
        return private_packet;
    if (private_packet) {
        const auto value = decode({static_cast<const std::byte *>(bytes), size});
        if (!value || ((value->flags & ManagedPet) && (!pet_ || !pet_incarnation_)))
            return true;
        bool changed = !status_known_ || status_ != *value;
        status_ = *value;
        status_known_ = true;
        received_at_ = now_;
        pending_ = false;
        attempts_ = 0;
        if (!refresh_requested_)
            next_poll_ = now_ > UINT64_MAX - 5000 ? UINT64_MAX : now_ + 5000;
        if (!(status_.flags & ManagedPet)) {
            items_.clear();
            known_slots_ = 0;
        } else if (status_.ownership_known) {
            const auto previous_known = known_slots_;
            for (const auto slot : kSlots) {
                const auto bit = 1u << slot;
                if (!(status_.occupied & bit)) {
                    changed |= items_.erase(slot) != 0;
                    known_slots_ |= bit;
                } else if (!items_.contains(slot)) {
                    known_slots_ &= ~bit;
                }
            }
            changed |= known_slots_ != previous_known;
        }
        if (changed)
            revision_ = services::token();
        message_ = "Received pet status. Equipment authority remains with the server.";
        return true;
    }
    if (!pet_ || !pet_incarnation_)
        return false;
    if (opcode == kDeleteItem) {
        if (const auto slot = deleted_slot({static_cast<const std::byte *>(bytes), size})) {
            items_.erase(*slot);
            known_slots_ |= 1u << *slot;
            revision_ = services::token();
        }
        return false;
    }
    if (size < 4 || size > MaxPacket || !HAILSTONE_HOST_HAS(host_, decode_item_packet))
        return false;
    hailstone::ByteReader reader(bytes, size);
    if (reader.le<std::uint32_t>() != 0x67)
        return false;
    HailstoneItemSlot slot{};
    HailstoneItem item{};
    item.size = sizeof(item);
    if (!host_->decode_item_packet(host_->host, bytes, size, &slot, &item) || slot.type || slot.slot < 1800 ||
        slot.sub != -1 || slot.aug != -1 || !presentation(item))
        return false;
    const auto equipment = static_cast<std::uint32_t>(slot.slot - 1800);
    if (!supported(equipment))
        return false;
    std::size_t total = size;
    for (const auto &[key, value] : items_)
        if (key != equipment)
            total += value.packet.size();
    if (total > MaxStored)
        return false;
    items_[equipment] = {item, {static_cast<const std::byte *>(bytes), static_cast<const std::byte *>(bytes) + size}};
    known_slots_ |= 1u << equipment;
    revision_ = services::token();
    return false;
}
bool Adapter::command(std::string_view text) {
    std::string action(text);
    for (char &c : action) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    if (!action.empty() && action != "open" && action != "refresh")
        return false;
    if (!synchronize())
        return false;
    const bool was_shown = shown_;
    shown_ = action.empty() ? !shown_ : true;
    if (!shown_) return true;
    if (action == "refresh" || !was_shown || !status_known_)
        refresh();
    return true;
}
int Adapter::ui(void *self, const HailstoneArgs *args, HailstoneOut *out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.call(args,out); });
}
int Adapter::call(const HailstoneArgs *args, HailstoneOut *out) {
    const auto op = services::argument(host_, args, 0, 16);
    if (!op)
        return 0;
    if (*op == "visibility") {
        services::Output o{host_, out};
        if (!o.valid()) return 0;
        const bool active = selected_ && in_game_;
        o.begin();
        o.boolean("active", active);
        o.boolean("shown", active && shown_);
        o.end();
        return 1;
    }
    synchronize();
    bool accepted = false;
    if (*op == "open")
        accepted = command("open");
    else if (*op == "close") {
        shown_ = false;
        accepted = true;
    } else if (*op == "refresh")
        accepted = refresh();
    else if (*op == "equip" || *op == "remove" || *op == "inspect") {
        const auto revision = services::decimal(host_, args, 1), slot = services::decimal(host_, args, 2);
        if (revision && slot && *slot <= UINT32_MAX) {
            if (*op == "inspect")
                accepted = inspect(*revision, static_cast<std::uint32_t>(*slot));
            else {
                const auto item = services::decimal(host_, args, 3), instance = services::decimal(host_, args, 5);
                const auto serial = services::argument(host_, args, 4, 16);
                if (item && *item <= UINT32_MAX && instance && *instance <= UINT32_MAX && serial) {
                    if (*op == "remove")
                        accepted =
                            remove(*revision, static_cast<std::uint32_t>(*slot), static_cast<std::uint32_t>(*item),
                                   *serial, static_cast<std::uint32_t>(*instance));
                    else if (const auto quantity = services::decimal(host_, args, 6);
                             quantity && *quantity <= UINT32_MAX)
                        accepted = equip(*revision, static_cast<std::uint32_t>(*slot),
                                         static_cast<std::uint32_t>(*item), *serial,
                                         static_cast<std::uint32_t>(*instance), static_cast<std::uint32_t>(*quantity));
                }
            }
        }
    } else if (*op != "state")
        return 0;
    services::Output o{host_, out};
    if (!o.valid())
        return 0;
    HailstoneItem held{};
    bool cursor_present = true;
    const bool has_cursor = cursor(held, &cursor_present);
    const auto rows = items_;
    const auto status = status_;
    const auto revision = revision_;
    const auto message = message_;
    const auto remaining = lock_remaining();
    const bool available = context_.has_value(), shown = shown_, pending = pending_, current = fresh();
    const auto known_mask = known_slots_;
    std::array<bool, kSlots.size()> equip_ok{}, remove_ok{};
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        equip_ok[i] = can_equip(kSlots[i]) && has_cursor && held.stack_count;
        remove_ok[i] = can_remove(kSlots[i]) && !cursor_present;
    }
    o.begin();
    o.boolean("available", available);
    o.boolean("shown", shown);
    o.boolean("pending", pending);
    o.boolean("fresh", current);
    o.boolean("accepted", accepted);
    o.identity("revision", revision);
    o.string("message", message);
    o.string("pet_name", status.name);
    o.boolean("status_known", status_known_);
    o.boolean("managed_pet", (status.flags & ManagedPet) != 0);
    o.boolean("locked", (status.flags & Locked) != 0);
    o.boolean("charmed", (status.flags & Charmed) != 0);
    o.boolean("casting", (status.flags & Casting) != 0);
    o.boolean("ownership_known", status.ownership_known);
    o.number("lock_remaining_ms", remaining);
    if (has_cursor) {
        o.key("cursor");
        o.begin();
        o.identity("id", held.id);
        o.string("name", services::bounded(held.name));
        o.string("serial", services::bounded(held.serial));
        o.identity("instance_id", held.instance_id);
        o.identity("quantity", held.stack_count);
        o.end();
    }
    o.array("slots");
    for (std::size_t i = 0; i < kSlots.size(); ++i) {
        const auto slot = kSlots[i];
        const auto item = rows.find(slot);
        o.begin();
        o.identity("slot", slot);
        o.string("label", slot_name(slot));
        o.boolean("known", (known_mask & (1u << slot)) != 0);
        o.boolean("occupied", item != rows.end());
        o.boolean("managed_known", status.ownership_known);
        o.boolean("managed", (status.managed & (1u << slot)) != 0);
        o.boolean("can_equip", equip_ok[i]);
        o.boolean("can_remove", remove_ok[i]);
        if (item != rows.end()) {
            o.identity("id", item->second.value.id);
            o.string("name", services::bounded(item->second.value.name));
            o.string("serial", services::bounded(item->second.value.serial));
            o.identity("instance_id", item->second.value.instance_id);
            o.identity("quantity", item->second.value.stack_count);
            o.number("icon", item->second.value.icon);
        }
        o.end();
    }
    o.end();
    o.end();
    return 1;
}
} // namespace bastion::pet_inventory
