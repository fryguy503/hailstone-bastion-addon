// SPDX-License-Identifier: MIT
#include "firefell.h"
#include <array>
namespace bastion::firefell {
const char* label(std::uint16_t v) {
    constexpr const char* labels[]={"Firefell, Crucible of the Concord","Firefell: The First Claim",
        "Firefell: The Stolen Names","Firefell: The Borrowed Sun","Firefell: The Oathless Pact",
        "Firefell: The Ember Road","Firefell: The Last Signal"};
    return v>=249 && v<=255 ? labels[v-249] : nullptr;
}
void Adapter::retire() { context_.reset(); instance_=nonce_=version_=0; received_=false; attempts_=0; next_=last_=0; }
void Adapter::set_host(const PluginHostApi* h) {
    if(HAILSTONE_HOST_HAS(host_,remove_ui_function)) host_->remove_ui_function(host_->host,"firefell");
    host_=h; selected_=in_game_=false; retire();
}
void Adapter::initialize() { if(HAILSTONE_HOST_HAS(host_,add_ui_function)) host_->add_ui_function(host_->host,"firefell",callback,this); }
void Adapter::peer(bool value) { selected_=value; retire(); }
void Adapter::game_state(int value) { in_game_=value==GAMESTATE_INGAME; if(!in_game_) retire(); }
void Adapter::begin_zone() { in_game_=false; retire(); }
bool Adapter::synchronize() {
    auto c=services::context(host_,selected_,in_game_);
    HailstonePlayer p{}; p.size=sizeof(p);
    if(!c || !host_->get_player(host_->host,&p) || p.size<offsetof(HailstonePlayer,zone_instance)+sizeof(p.zone_instance) ||
        c->zone!=406 || !p.zone_instance) { retire(); return false; }
    if(!context_ || *context_!=*c || instance_!=p.zone_instance) {
        retire(); context_=std::move(c); instance_=p.zone_instance; nonce_=services::request_id();
    }
    return nonce_!=0;
}
void Adapter::pulse() {
    if(!synchronize()) return;
    const auto now=host_->monotonic_ms(host_->host);
    if(last_ && now<last_) { retire(); return; }
    last_=now;
    if(received_ || attempts_>=5 || now<next_) return;
    hailstone::ByteWriter w;
    w.le(std::uint32_t{0x315a4646}); w.le(std::uint16_t{1}); w.le(std::uint16_t{1}); w.le(nonce_);
    w.le(std::uint32_t{0}); w.le(std::uint32_t{0}); w.le(std::uint16_t{0}); w.le(std::uint16_t{0});
    for(unsigned n=0;n<128;++n) w.le(std::uint8_t{0});
    ++attempts_; next_=now+2000;
    host_->send_packet(host_->host,HAILSTONE_STREAM_ZONE,opcode,w.data().data(),w.size());
}
bool Adapter::packet(std::uint16_t op,const void* bytes,std::size_t count) {
    if(op!=opcode || !selected_) return false;
    if(!bytes || count!=152 || !synchronize()) return true;
    try {
        hailstone::ByteReader r(bytes,count);
        if(r.le<std::uint32_t>()!=0x315a4646 || r.le<std::uint16_t>()!=1) return true;
        const auto action=r.le<std::uint16_t>(); const auto nonce=r.le<std::uint32_t>();
        const auto zone=r.le<std::uint32_t>(),instance=r.le<std::uint32_t>();
        const auto version=r.le<std::uint16_t>(),reserved=r.le<std::uint16_t>();
        const auto text=services::text(r,128);
        if(!nonce || nonce!=nonce_ || zone!=context_->zone || instance!=instance_ || instance>65535 || reserved) return true;
        if(action==3 && version==0 && text.empty()) { version_=0; received_=true; return true; }
        const auto* canonical=label(version);
        if(action!=2 || !canonical) return true;
        std::string legacy(canonical); legacy.replace(0,8,"Firefall");
        if(text!=canonical && text!=legacy) return true;
        version_=version; received_=true;
    } catch(const std::exception&) {}
    return true;
}
const char* Adapter::name(std::uint32_t zone,std::uint32_t instance) {
    return synchronize() && received_ && context_->zone==zone && instance_==instance ? label(version_) : nullptr;
}
int Adapter::callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(args,out); });
}
int Adapter::ui(const HailstoneArgs* args,HailstoneOut* out) {
    services::Output result{host_,out}; if(!result.valid()) return 0;
    const auto op=services::argument(host_,args,0,16);
    if(!op || (*op!="state" && *op!="refresh")) return 0;
    const auto active=synchronize();
    if(*op=="refresh" && active) { received_=false; version_=0; attempts_=0; next_=0; nonce_=services::request_id(); pulse(); }
    result.begin(); result.boolean("active",selected_&&in_game_); result.boolean("available",active);
    result.boolean("ready",received_); result.number("instance",instance_); result.number("version",version_);
    result.number("attempts",attempts_); result.string("name",label(version_) ? label(version_) : ""); result.end(); return 1;
}
}
