#pragma once
#include "citadel_services.h"
namespace bastion {
// Commands are forwarded only after an explicit command or window button.
class ServerCommands {
public:
    void set_host(const PluginHostApi*);
    void initialize();
    void peer(bool value) { selected_=value; }
    bool shortcut(std::string_view line);
    static std::string_view destination(std::string_view alias);
private:
    bool send(std::string_view name,std::string_view args);
    static int callback(void*,const HailstoneArgs*,HailstoneOut*) noexcept;
    int ui(const HailstoneArgs*,HailstoneOut*);
    const PluginHostApi* host_=nullptr;
    bool selected_=false;
};
}
