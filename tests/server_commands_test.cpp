#include "server_commands.h"
#include "citadel_test_host.h"
#include <iostream>
using namespace citadel_test;
namespace { std::vector<std::string> forwarded; }
int main() { try {
    Host h; h.api.do_command=[](void*,const char* line) { forwarded.emplace_back(line); };
    bastion::ServerCommands c; c.set_host(&h.api); c.initialize();
    check(!c.shortcut("/season status") && forwarded.empty(),"inactive shortcut forwarded");
    c.peer(true);
    check(c.shortcut("/SeAsOn status") && forwarded.back()=="/say #season status","case insensitive alias broken");
    check(c.shortcut("/requestdz PlayerCase") && forwarded.back()=="/say #requestdz PlayerCase","argument case changed");
    check(c.shortcut("/trophy") && forwarded.back()=="/say #trophies","irregular alias broken");
    check(c.shortcut("/ic off") && forwarded.back()=="/say #impliedcasting off","short alias broken");
    const auto n=forwarded.size(); check(!c.shortcut("/season status\n/say forged") && forwarded.size()==n,"newline injection forwarded");
    check(!c.shortcut("/pet attack") && forwarded.size()==n,"stock pet command intercepted");
    h.in_zone=false; c.shortcut("/season status"); check(forwarded.size()==n,"out-of-zone command forwarded");
    c.peer(false); c.shortcut("/requestdz PlayerCase"); check(forwarded.size()==n,"world switch retained shortcuts");
    c.set_host(nullptr); check(!h.functions.contains("commands"),"command callback survived unload");
    std::cout<<checks<<" command checks passed\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
