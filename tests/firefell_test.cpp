#include "firefell.h"
#include "citadel_test_host.h"
#include <iostream>
using namespace citadel_test;
int main() { try {
    Host h;
    const auto original=h.api.get_player;
    h.api.get_player=[](void* raw,HailstonePlayer* p) { auto& fixture=*static_cast<Host*>(raw); p->size=sizeof(*p); p->spawn_id=fixture.player; p->zone_id=406; p->zone_instance=77; p->in_zone=1; std::memcpy(p->name,"Fixture",8); return 1; };
    bastion::firefell::Adapter a; a.set_host(&h.api); a.initialize(); a.peer(true); a.game_state(GAMESTATE_INGAME); a.pulse();
    check(h.sent.size()==1 && h.sent.back().bytes.size()==152,"missing exact FFZ1 hello");
    const auto hello=h.sent.back().bytes;
    auto response=hello; put(response,6,2,2); put(response,12,406); put(response,16,77); put(response,20,250,2);
    text(response,24,"Firefell: The First Claim");
    check(a.packet(0x7d25,response.data(),response.size()),"FFZ1 not consumed");
    check(a.name(406,77) && std::string(a.name(406,77))=="Firefell: The First Claim","valid instance name missing");
    check(!a.name(406,78),"label leaked to another instance");
    auto bad=response; put(bad,20,449,2); a.packet(0x7d25,bad.data(),bad.size()); check(a.name(406,77)!=nullptr,"malformed context replaced good state");
    h.incarnation++; check(!a.name(406,77),"actor replacement retained context"); a.pulse();
    a.packet(0x7d25,response.data(),response.size()); check(!a.name(406,77),"old nonce accepted after actor replacement");
    auto fresh=h.sent.back().bytes; put(fresh,6,3,2); put(fresh,12,406); put(fresh,16,77);
    a.packet(0x7d25,fresh.data(),fresh.size()); check(!a.name(406,77),"Clear retained label");
    a.begin_zone(); a.packet(0x7d25,response.data(),response.size()); check(!a.name(406,77),"zoning accepted old context");
    a.peer(false); check(!a.packet(0x7d25,response.data(),response.size()),"unrelated peer traffic consumed");
    h.api.get_player=original;
    std::cout<<checks<<" Firefell checks passed\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
