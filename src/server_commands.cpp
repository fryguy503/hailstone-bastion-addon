// SPDX-License-Identifier: MIT
#include "server_commands.h"
#include <algorithm>
#include <array>
namespace bastion {
namespace {
struct Command { const char* name; const char* help; const char* access; const char* group; };
constexpr Command commands[]={
#include "server_command_catalog.inc"
};
constexpr std::pair<std::string_view,std::string_view> aliases[]={
    {"don","don"},{"endurance","endurance"},{"editor","editor"},{"statboard","statboard"},
    {"hotzone","hotzone"},{"hotzones","hotzones"},{"season","season"},{"seasons","seasons"},
    {"checkkills","checkkills"},{"pop","pop"},{"gates","gates"},{"daynight","daynight"},
    {"trophy","trophies"},{"campfirebuff","campfirebuff"},{"destroycampfire","destroycampfire"},
    {"autoinvite","autoinvite"},{"groupkick","groupkick"},{"afktribute","afktribute"},
    {"petdetails","petdetails"},{"requestdz","requestdz"},{"dzr","requestdz"},
    {"impliedcasting","impliedcasting"},{"ic","impliedcasting"},{"factionreport","factionreport"},
    {"raidmanager","raidmanager"},{"kinbound","kinbound"}};
bool safe(std::string_view s) { return s.size()<=768 && std::none_of(s.begin(),s.end(),[](unsigned char c) { return c<32 || c==127; }); }
}
std::string_view ServerCommands::destination(std::string_view alias) {
    for(const auto& [from,to]:aliases) if(from==alias) return to;
    return {};
}
void ServerCommands::set_host(const PluginHostApi* h) {
    if(HAILSTONE_HOST_HAS(host_,remove_ui_function)) host_->remove_ui_function(host_->host,"commands");
    host_=h; selected_=false;
}
void ServerCommands::initialize() { if(HAILSTONE_HOST_HAS(host_,add_ui_function)) host_->add_ui_function(host_->host,"commands",callback,this); }
bool ServerCommands::send(std::string_view name,std::string_view args) {
    if(!selected_ || !safe(args) || !HAILSTONE_HOST_HAS(host_,do_command)) return false;
    HailstonePlayer p{}; p.size=sizeof(p);
    if(!HAILSTONE_HOST_HAS(host_,get_player) || !host_->get_player(host_->host,&p) || !p.in_zone) return false;
    const auto found=std::find_if(std::begin(commands),std::end(commands),[&](const auto& c) { return name==c.name; });
    if(found==std::end(commands)) return false;
    const auto line="/say #"+std::string(name)+(args.empty() ? "" : " "+std::string(args));
    host_->do_command(host_->host,line.c_str()); return true;
}
bool ServerCommands::shortcut(std::string_view line) {
    if(!selected_ || !line.starts_with('/') || !safe(line)) return false;
    const auto end=line.find(' '); std::string root(line.substr(1,end==line.npos ? line.size()-1 : end-1));
    for(auto& c:root) if(c>='A' && c<='Z') c=static_cast<char>(c+'a'-'A');
    const auto target=destination(root); if(target.empty()) return false;
    send(target,end==line.npos ? std::string_view{} : line.substr(end+1)); return true;
}
int ServerCommands::callback(void* self,const HailstoneArgs* a,HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<ServerCommands*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(a,out); });
}
int ServerCommands::ui(const HailstoneArgs* a,HailstoneOut* out) {
    services::Output result{host_,out}; if(!result.valid()) return 0;
    const auto op=services::argument(host_,a,0,16); if(!op) return 0;
    if(*op=="send") {
        const auto name=services::argument(host_,a,1,64),args=services::argument(host_,a,2,768);
        result.begin(); result.boolean("ok",name && args && send(*name,*args)); result.end(); return 1;
    }
    if(*op!="catalog" || !selected_) return 0;
    result.begin(); result.array("commands");
    for(const auto& c:commands) {
        result.begin(); result.string("name",c.name); result.string("help",c.help);
        result.string("access",c.access); result.string("category",c.group);
        std::string_view alias;
        for(const auto& [from,to]:aliases) if(to==c.name) { alias=from; break; }
        result.string("shortcut",alias.empty() ? "" : "/"+std::string(alias)); result.end();
    }
    result.end(); result.end(); return 1;
}
}
