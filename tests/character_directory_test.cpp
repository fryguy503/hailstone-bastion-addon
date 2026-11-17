#include "citadel_preferences.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void check(bool value) { ++checks; if(!value) throw std::runtime_error("Character directory scope regression"); }
template<std::size_t N,std::size_t M> void text(char (&out)[N],const char (&value)[M]) {
    static_assert(M<=N);std::copy_n(value,M,out);
}
struct Host {
    PluginHostApi api{};
    HailstonePlayer player{};
    HailstoneServer server{};
    unsigned directory_reads=0, policy_reads=0;
    bool readable=true, path_available=true, server_available=true, policy_available=true, prefix_only=false;
    std::uint64_t context=UINT64_MAX-19;
    std::uint32_t policy_size=sizeof(HailstoneMapPolicy);
    std::string profile="profile-a";
    std::function<void()> during_path;
    Host() {
        api.size=sizeof(api);api.abi=HAILSTONE_PLUGIN_ABI;api.host=this;
        player.size=sizeof(player);player.in_zone=1;player.spawn_id=1;player.zone_id=2;
        text(player.name,"Player");
        server.size=sizeof(server);server.port=5999;
        text(server.name,"Server");text(server.host,"login.example");
        text(server.world,"World");text(server.world_host,"world.example");
        api.get_player=[](void* self,HailstonePlayer* out) {
            const auto& h=*static_cast<Host*>(self);if(!h.readable)return 0;*out=h.player;return 1;
        };
        api.server_info=[](void* self,HailstoneServer* out) {
            const auto& h=*static_cast<Host*>(self);if(!h.readable||!h.server_available)return 0;*out=h.server;return 1;
        };
        api.get_map_policy=[](void* self,HailstoneMapPolicy* out) {
            auto& h=*static_cast<Host*>(self);++h.policy_reads;
            if(!h.policy_available)return 0;
            if(h.prefix_only) return 1; // An old prefix writer preserves caller size and never fills context.
            *out={};out->size=h.policy_size;out->context=h.context;return 1;
        };
        api.character_config_dir=[](void* self,char* out,std::size_t size) {
            auto& h=*static_cast<Host*>(self);++h.directory_reads;
            if(h.during_path)h.during_path();
            if(!h.path_available)return 0;
            const std::string text=std::string("characters/")+h.profile+"/"+h.server.world+"/"+h.player.name;
            if(text.size()>=size)return 0;
            std::memcpy(out,text.c_str(),text.size()+1);return static_cast<int>(text.size());
        };
    }
};
void opaque_contexts() {
    Host h;bastion::CharacterDirectory cache;
    const auto first=cache.get(&h.api);check(!first.empty());
    const auto policy_reads=h.policy_reads;
    check(cache.get(&h.api)==first&&h.directory_reads==1&&h.policy_reads>policy_reads);
    // Same names, endpoints and numeric actor/zone can belong to a different saved profile/session.
    h.profile="profile-b";++h.context;
    const auto second=cache.get(&h.api);
    check(second!=first&&second.generic_string().find("profile-b")!=std::string::npos&&h.directory_reads==2);
    auto reads=h.directory_reads;
    h.policy_available=false;check(cache.get(&h.api).empty()&&h.directory_reads==reads);
    h.policy_available=true;check(cache.get(&h.api)==second&&h.directory_reads==++reads);
    const auto context=h.context;h.context=0;
    check(cache.get(&h.api).empty()&&h.directory_reads==reads);
    h.context=context;check(cache.get(&h.api)==second&&h.directory_reads==++reads);
    h.policy_size=offsetof(HailstoneMapPolicy,context);
    check(cache.get(&h.api).empty()&&h.directory_reads==reads);
    h.policy_size=sizeof(HailstoneMapPolicy);
    check(cache.get(&h.api)==second&&h.directory_reads==++reads);
    h.prefix_only=true;check(cache.get(&h.api).empty()&&h.directory_reads==reads);
    h.prefix_only=false;check(cache.get(&h.api)==second&&h.directory_reads==++reads);
    h.server_available=false;check(cache.get(&h.api).empty()&&h.directory_reads==reads);
    h.server_available=true;check(cache.get(&h.api)==second&&h.directory_reads==++reads);
    cache.reset();h.during_path=[&]{++h.context;h.profile="profile-c";};
    check(cache.get(&h.api).empty()); // A context change during resolution cannot publish the prior path.
    h.during_path={};const auto third=cache.get(&h.api);
    check(third!=second&&third.generic_string().find("profile-c")!=std::string::npos);
    reads=h.directory_reads;cache.reset();
    check(cache.get(&h.api)==third&&h.directory_reads==reads+1); // Explicit lifecycle retirement forces a fresh path.
}
}
int main() {
    Host h;bastion::CharacterDirectory cache;
    const auto first=cache.get(&h.api);check(!first.empty());
    for(unsigned i=0;i<500;++i)check(cache.get(&h.api)==first);
    check(h.directory_reads==1); // Unchanged frame/UI calls perform no directory-creating host query.
    text(h.player.name,"Other");check(cache.get(&h.api)!=first);check(h.directory_reads==2);
    text(h.server.world,"OtherWorld");check(!cache.get(&h.api).empty());check(h.directory_reads==3);
    ++h.player.zone_instance;check(!cache.get(&h.api).empty());check(h.directory_reads==4);
    h.readable=false;check(cache.get(&h.api).empty());check(h.directory_reads==4);
    h.readable=true;check(!cache.get(&h.api).empty());check(h.directory_reads==5);
    h.player.in_zone=0;check(cache.get(&h.api).empty());check(h.directory_reads==5);
    h.player.in_zone=1;check(!cache.get(&h.api).empty());check(h.directory_reads==6);
    cache.reset();h.path_available=false;check(cache.get(&h.api).empty());
    h.path_available=true;check(!cache.get(&h.api).empty());check(h.directory_reads==8);
    cache.reset();h.during_path=[&]{++h.player.zone_instance;};
    check(cache.get(&h.api).empty()); // A reentrant scope change cannot publish the old path.
    h.during_path={};check(!cache.get(&h.api).empty());
    cache.reset();h.during_path=[&]{cache.reset();};check(cache.get(&h.api).empty());
    h.during_path={};check(!cache.get(&h.api).empty());
    cache.reset();h.during_path=[&]{check(cache.get(&h.api).empty());};
    check(!cache.get(&h.api).empty());h.during_path={};
    Host other;check(!cache.get(&other.api).empty());check(other.directory_reads==1);
    check(cache.get(nullptr).empty());
    other.api.server_info=nullptr;
    check(!cache.get(&other.api).empty());check(!cache.get(&other.api).empty());
    check(other.directory_reads==3); // Older/small hosts retain the uncached compatibility path.
    opaque_contexts();
    std::cout<<"PASS character directory "<<checks<<" checks\n";
}
