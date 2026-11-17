#pragma once
#include "citadel_services.h"
#include "pet_inventory_protocol.h"
#include <map>
namespace bastion::pet_inventory {
struct Item {
    HailstoneItem value{};
    std::vector<std::byte> packet;
};
class Adapter {
  public:
    void set_host(const PluginHostApi *);
    void initialize();
    void shutdown();
    void peer(bool);
    void game_state(int);
    void begin_zone();
    void pulse();
    bool packet(std::uint16_t, const void *, std::size_t);
    bool command(std::string_view);
    bool refresh();
    bool equip(std::uint64_t, std::uint32_t slot, std::uint32_t item, std::string_view serial, std::uint32_t instance,
               std::uint32_t quantity);
    bool remove(std::uint64_t, std::uint32_t slot, std::uint32_t item, std::string_view serial, std::uint32_t instance);
    bool inspect(std::uint64_t, std::uint32_t);
    bool fresh() const;
    bool pending() const { return pending_; }
    std::uint64_t revision() const { return revision_; }
    const std::map<std::uint32_t, Item> &items() const { return items_; }
    const Status &status() const { return status_; }

  private:
    void retire();
    void clear_pet();
    bool synchronize();
    bool send(Action, std::uint32_t = 0, std::uint32_t = 0, std::uint64_t expected_revision = 0);
    bool cursor(HailstoneItem &, bool *present = nullptr) const;
    bool known(std::uint32_t) const;
    bool can_equip(std::uint32_t) const;
    bool can_remove(std::uint32_t) const;
    std::uint32_t lock_remaining() const;
    static int ui(void *, const HailstoneArgs *, HailstoneOut *) noexcept;
    int call(const HailstoneArgs *, HailstoneOut *);
    const PluginHostApi *host_ = nullptr;
    bool selected_ = false, in_game_ = false, shown_ = false, sent_ = false, pending_ = false, status_known_ = false;
    std::optional<services::Context> context_;
    std::uint64_t revision_ = services::token(), now_ = 0, sent_at_ = 0, received_at_ = 0, pet_incarnation_ = 0,
                  next_poll_ = 0;
    std::uint32_t pet_ = 0, known_slots_ = 0;
    unsigned attempts_ = 0;
    Action action_ = Action::Refresh;
    Status status_;
    std::map<std::uint32_t, Item> items_;
    std::string message_ = "Open Pet Inventory to request received equipment.";
};
} // namespace bastion::pet_inventory
