#include "kinbound.h"
#include "hailstone_byte_io.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks = 0;
void check(bool condition, const char *why) { ++checks; if (!condition) throw std::runtime_error(why); }
std::vector<std::byte> metadata(std::uint32_t id, bool selected = true) {
    hailstone::ByteWriter w;
    w.le<std::uint32_t>(0x314e494b); w.le<std::uint8_t>(1); w.le<std::uint8_t>(selected ? 1 : 0);
    w.le<std::uint16_t>(0); w.le(id); return w.take();
}
std::string copy(const char *text) { return text ? text : ""; }
}
int main() {
    try {
        bastion::KinboundCatalog catalog;
        const auto packet = metadata(70000);
        for (std::size_t size = 0; size < packet.size(); ++size)
            check(catalog.receive(packet.data(), size) == bastion::KinboundCatalog::Receive::Malformed, "truncated metadata admitted");
        for (std::size_t at : {std::size_t(0), std::size_t(4), std::size_t(5), std::size_t(6)}) {
            auto invalid = packet; invalid[at] = std::byte{0xff};
            check(catalog.receive(invalid.data(), invalid.size()) == bastion::KinboundCatalog::Receive::Malformed, "bad metadata envelope admitted");
        }
        check(catalog.receive(packet.data(), packet.size()) == bastion::KinboundCatalog::Receive::Accepted, "valid full-width item ID rejected");
        HailstoneItem item{}; item.size = sizeof(item); item.id = 70000;
        std::memcpy(item.name, "<Bound & Item>", sizeof("<Bound & Item>"));
        check(copy(catalog.item_flags(&item, "Magic, Attunable, No Trade")) == "Magic, Kinbound, No Trade", "flag replacement altered No Trade");
        check(copy(catalog.item_flags(&item, "Attuneable")) == "Kinbound", "alternate attuneable token rejected");
        check(!catalog.item_flags(&item, "Kinbound, No Trade"), "duplicate Kinbound token");
        check(copy(catalog.item_description_append(&item)).find("Click effects require attunement") != std::string::npos, "attunement explanation missing");
        check(copy(catalog.loot_confirmation(&item, nullptr, 0, true)).starts_with("<Bound & Item> is Kinbound."), "plain item name escaped/changed");
        HailstoneItem bag = item; bag.id = 100;
        const std::uint32_t children[] = {1, 70000, 99};
        check(copy(catalog.loot_confirmation(&bag, children, 3, true)).find("contains Kinbound items") != std::string::npos, "nested Kinbound warning missing");
        check(!catalog.loot_confirmation(&bag, nullptr, 0, true), "ordinary item prompted");
        check(copy(catalog.loot_confirmation(&bag, nullptr, 1, false)).find("incomplete") != std::string::npos, "incomplete traversal treated unrestricted");
        const auto revision = catalog.revision(); catalog.clear();
        check(catalog.revision() != revision && !catalog.item_description_append(&item), "retired catalog remained active");
        for (std::uint32_t id = 1; id <= bastion::KinboundCatalog::kMaximumEntries + 1; ++id) {
            const auto data = metadata(id); catalog.receive(data.data(), data.size());
        }
        item.id = 999999;
        check(!catalog.item_flags(&item, "Attunable") && copy(catalog.item_description_append(&item)).find("incomplete") != std::string::npos,
              "bounded overflow invented sharing flags or hid uncertainty");
        std::cout << "Kinbound PASS " << checks << " checks\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
