// SPDX-License-Identifier: MIT
#include "fast_camp.h"
#include <array>
#include <cctype>

namespace bastion::fast_camp {
namespace {
constexpr std::array<Preference,1> preferences{{{"enabled",0,1,0}}};
constexpr std::uint32_t action_caps=HAILSTONE_CAP_READ|HAILSTONE_CAP_ACTIONS|HAILSTONE_CAP_EXTEND|HAILSTONE_CAP_PACKETS;
constexpr std::string_view idle_message="Use /fastcamp for one camp. Ordinary camping is unchanged by default.";
constexpr std::string_view popup_unavailable="Fast-camp offers require a matching Hailstone host. Use /camp for the normal countdown.";
bool popup_services(const PluginHostApi* host) noexcept {
#if defined(BASTION_OPTIONAL_CAMP_DIALOG) && BASTION_OPTIONAL_CAMP_DIALOG
    return HAILSTONE_HOST_HAS(host,get_large_dialog)&&HAILSTONE_HOST_HAS(host,answer_large_dialog);
#else
    (void)host;
    return false;
#endif
}
bool valid_text(std::string_view value,std::size_t maximum) {
    return value.size()<=maximum&&std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;});
}
std::string normalized(std::string_view value) {
    const auto first=value.find_first_not_of(" \t");
    if(first==value.npos)return {};
    const auto last=value.find_last_not_of(" \t");
    std::string result(value.substr(first,last-first+1));
    for(char& c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
std::uint64_t after(std::uint64_t now,std::uint64_t delay) {
    return now>UINT64_MAX-delay?UINT64_MAX:now+delay;
}
}
Adapter::Adapter():settings_("fast-camp.cfg",preferences) {}
void Adapter::retire(std::string_view message) {
    generation_=services::token();armed_=starting_=observed_camp_=explicit_=false;
    scope_.reset();started_=deadline_=next_poll_=0;
    if(!message.empty())message_=message;
}
void Adapter::set_host(const PluginHostApi* host) { shutdown();host_=host; }
void Adapter::initialize() {
    if(HAILSTONE_HOST_HAS(host_,add_ui_function))host_->add_ui_function(host_->host,"fast_camp",callback,this);
}
void Adapter::shutdown() {
    if(HAILSTONE_HOST_HAS(host_,remove_ui_function))host_->remove_ui_function(host_->host,"fast_camp");
    selected_=in_game_=false;retire(idle_message);settings_.invalidate_context();error_.clear();
}
void Adapter::peer(bool value) {
    selected_=value;retire(idle_message);settings_.invalidate_context();error_.clear();
}
void Adapter::game_state(int value) {
    in_game_=value==GAMESTATE_INGAME;
    if(!in_game_) { retire(idle_message);settings_.invalidate_context();error_.clear();return; }
    if(selected_)refresh_settings();
}
void Adapter::begin_zone() { in_game_=false;retire(idle_message);settings_.invalidate_context();error_.clear(); }
bool Adapter::allowed(std::uint32_t caps) const noexcept {
    try {return HAILSTONE_HOST_HAS(host_,caps)&&(host_->caps(host_->host)&caps)==caps;}catch(...){return false;}
}
bool Adapter::supported() const noexcept {
    return popup_services(host_)&&HAILSTONE_HOST_HAS(host_,get_map_policy)&&allowed(action_caps);
}
std::string_view Adapter::message() const {
    if(message_==idle_message&&!popup_services(host_))return popup_unavailable;
    return message_;
}
std::optional<Adapter::Live> Adapter::live() const {
    if(!active()||!allowed(HAILSTONE_CAP_READ)||!HAILSTONE_HOST_HAS(host_,get_player)||!HAILSTONE_HOST_HAS(host_,get_spawn)||
       !HAILSTONE_HOST_HAS(host_,get_map_policy))return {};
    HailstonePlayer player{};player.size=sizeof(player);
    if(!host_->get_player(host_->host,&player)||player.size<offsetof(HailstonePlayer,observation_flags)+sizeof(player.observation_flags)||
       !player.in_zone||!player.spawn_id||!player.zone_id)return {};
    HailstoneSpawn spawn{};spawn.size=sizeof(spawn);
    if(!host_->get_spawn(host_->host,player.spawn_id,&spawn)||spawn.size<offsetof(HailstoneSpawn,incarnation)+sizeof(spawn.incarnation)||
       spawn.id!=player.spawn_id||!spawn.incarnation||spawn.is_corpse)return {};
    HailstoneMapPolicy policy{};policy.size=sizeof(policy);
    if(!host_->get_map_policy(host_->host,&policy)||policy.size<offsetof(HailstoneMapPolicy,context)+sizeof(policy.context)||
       !policy.context)return {};
    const auto name=services::bounded(player.name);if(name.empty())return {};
    bool camping=false;
#if defined(BASTION_OPTIONAL_CAMP_DIALOG) && BASTION_OPTIONAL_CAMP_DIALOG
    camping=(player.observation_flags&HAILSTONE_PLAYER_CAMPING)!=0;
#endif
    return Live{{player.spawn_id,player.zone_id,player.zone_instance,policy.context,spawn.incarnation,name},camping};
}
bool Adapter::refresh_settings() {
    const auto generation=generation_;
    try {
        const auto current=live();if(!current)return false;
        if(generation!=generation_)return false;
        settings_.refresh(host_);
        if(generation!=generation_||!active())return false;
        const auto fresh=live();
        if(generation!=generation_||!fresh||fresh->scope!=current->scope)return false;
        error_=settings_.error();return true;
    }catch(...){error_="Fast-camp settings are unavailable.";return false;}
}
bool Adapter::set_enabled(bool value) {
    error_.clear();
    if(value&&!popup_services(host_)) {error_=popup_unavailable;return false;}
    const auto generation=generation_;
    const auto before=live();
    if((value&&!supported())||!refresh_settings()||!allowed(HAILSTONE_CAP_READ|HAILSTONE_CAP_ACTIONS)) {
        error_="Fast-camp settings are unavailable for this character or permission.";return false;
    }
    const auto fresh=live();
    if(generation!=generation_||!before||!fresh||before->scope!=fresh->scope) {
        error_="The character changed before fast-camp settings could be saved.";return false;
    }
    if(!settings_.set("enabled",value?1:0)){error_=settings_.error();return false;}
    if(!value)retire();
    if(value)message_="Eligible server fast-camp offers will be accepted for new camps.";
    else if(popup_services(host_))message_="Ordinary camping restored. /fastcamp remains available for one camp.";
    else message_=popup_unavailable;
    return true;
}
bool Adapter::arm(const Live& current,bool explicit_request) {
    if(!supported()||!HAILSTONE_HOST_HAS(host_,monotonic_ms))return false;
    const auto generation=generation_;
    const auto now=host_->monotonic_ms(host_->host);
    if(generation!=generation_)return false;
    const auto fresh=live();
    if(generation!=generation_||!fresh||fresh->scope!=current.scope||!active()||!supported()||generation!=generation_)return false;
    retire();if(!generation_)return false;
    scope_=current.scope;armed_=true;explicit_=explicit_request;
    started_=now;deadline_=after(now,30000);next_poll_=now;
    message_="Camping normally; waiting for an eligible server fast-camp offer.";
    error_.clear();return true;
}
bool Adapter::start(std::string_view destination) {
    if(!valid_text(destination,32)) {error_="The fast-camp destination is invalid.";return false;}
    const auto argument=normalized(destination);
    if(!argument.empty()&&argument!="desk"&&argument!="desktop"&&argument!="server") {
        error_="Use /fastcamp [desktop|server], or /fastcamp cancel to stop accepting an offer.";return false;
    }
    if(!popup_services(host_)) {retire();error_=popup_unavailable;return false;}
    try {
        if(!supported()||!allowed(HAILSTONE_CAP_COMMANDS)||!HAILSTONE_HOST_HAS(host_,do_command)||!refresh_settings()) {
            retire();error_="Fast camp requires the matching client update, permission and a current Bastion character.";return false;
        }
        const auto current=live();if(!current)return false;
        if(current->camping) {
            retire();error_="You are already camping. Use /camp to cancel the current camp.";return false;
        }
        if(!arm(*current,true))return false;
        const auto generation=generation_;starting_=true;
        const std::string line=argument.empty()?"/camp":argument=="server"?"/camp server":"/camp desktop";
        if(!supported()||!allowed(HAILSTONE_CAP_COMMANDS)||generation!=generation_||!active()) {retire();return false;}
        const auto dispatch=live();
        if(generation!=generation_||!dispatch||dispatch->scope!=current->scope||dispatch->camping) {retire();return false;}
        host_->do_command(host_->host,line.c_str());
        if(generation!=generation_)return false;
        starting_=false;
        if(!observed_camp_) {
            retire("No new camp started. The normal camp restrictions and cancellation behavior still apply.");return false;
        }
        return true;
    }catch(...){retire();error_="The camp request could not be started.";return false;}
}
void Adapter::cancel() { retire("Fast-camp request cancelled. Use the normal camp controls to cancel camping itself.");error_.clear(); }
void Adapter::observe_command(std::string_view line) {
    if(!armed_||starting_)return;
    const auto value=normalized(line);
    const auto split=value.find_first_of(" \t");const auto command=value.substr(0,split);
    if(command=="/camp"||command=="/stand"||command=="/sit"||command=="/duck")cancel();
}
bool Adapter::command(std::string_view line) {
    const auto head=normalized(line.substr(0,std::min<std::size_t>(line.size(),128)));
    if(!head.starts_with("/fastcamp")||(head.size()>9&&static_cast<unsigned char>(head[9])>32))return false;
    bool ok=true;
    if(!valid_text(line,128)) {error_="Fast-camp commands must be one short line without control characters.";ok=false;}
    if(!ok) {if(HAILSTONE_HOST_HAS(host_,write_chat))host_->write_chat(host_->host,error_.c_str(),0);return true;}
    const auto value=normalized(line);const auto split=value.find_first_of(" \t");
    const auto args=split==value.npos?std::string{}:normalized(std::string_view(value).substr(split));
    if(args=="cancel")cancel();
    else if(args=="on")ok=set_enabled(true);
    else if(args=="off")ok=set_enabled(false);
    else ok=start(args);
    if(!ok&&HAILSTONE_HOST_HAS(host_,write_chat))host_->write_chat(host_->host,error_.empty()?message_.c_str():error_.c_str(),0);
    return true;
}
void Adapter::outgoing(std::uint16_t opcode) {
    if(opcode!=camp_opcode||!active()||!popup_services(host_))return;
    try {
        if(starting_&&armed_&&explicit_) {
            const auto generation=generation_;const auto current=live();
            if(generation==generation_&&current&&scope_&&current->scope==*scope_)observed_camp_=true;
            return;
        }
        // A later camp cannot inherit an explicit request from a cancelled one.
        retire();
        if(!refresh_settings()||!enabled())return;
        const auto current=live();if(current&&arm(*current,false))observed_camp_=true;
    }catch(...){retire();error_="The camp request could not be observed.";}
}
void Adapter::pulse() {
#if defined(BASTION_OPTIONAL_CAMP_DIALOG) && BASTION_OPTIONAL_CAMP_DIALOG
    if(!armed_)return; // No idle clock, character, popup or filesystem reads.
    try {
        if(!active()||!supported()||!HAILSTONE_HOST_HAS(host_,monotonic_ms)) {retire();return;}
        const auto generation=generation_;const auto now=host_->monotonic_ms(host_->host);
        if(generation!=generation_)return;
        if(now<started_) {retire("The camp request expired after the session clock changed.");return;}
        if(now>=deadline_) {retire("No eligible fast-camp offer arrived. Normal camping continues.");return;}
        if(now<next_poll_)return;
        next_poll_=after(now,100);
        const auto current=live();
        if(generation!=generation_)return;
        if(!current||!current->camping||!scope_||current->scope!=*scope_) {cancel();return;}
        HailstoneLargeDialog dialog{};dialog.size=sizeof(dialog);
        if(!host_->get_large_dialog(host_->host,&dialog))return;
        if(generation!=generation_||!armed_)return;
        if(dialog.size<sizeof(dialog)||!dialog.token||!dialog.camp_token||!dialog.remaining_ms||
           (dialog.flags&(HAILSTONE_LARGE_DIALOG_YES_NO|HAILSTONE_LARGE_DIALOG_CAMPING))!=
                         (HAILSTONE_LARGE_DIALOG_YES_NO|HAILSTONE_LARGE_DIALOG_CAMPING)||
           dialog.popup_id!=accept_popup||dialog.negative_id!=decline_popup)return;
        const auto fresh=live();
        if(generation!=generation_||!armed_)return;
        if(!fresh||!fresh->camping||!scope_||fresh->scope!=*scope_||!supported()) {cancel();return;}
        armed_=starting_=false; // Consume the local request before calling a reentrant host.
        const bool answered=host_->answer_large_dialog(host_->host,dialog.token,dialog.camp_token,1,
                                                       HAILSTONE_DIALOG_EXPECT_CAMP_DISCONNECT)!=0;
        if(generation!=generation_)return;
        message_=answered?"Fast-camp offer accepted locally; waiting for the server to finish camping.":
                          "The fast-camp offer changed or could not be answered. Normal camping continues.";
    }catch(...){retire();error_="The fast-camp offer could not be checked. Normal camping continues.";}
#endif
}
int Adapter::callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if(!self)return 0;
    auto& owner=*static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_,[&]{return owner.call(args,out);});
}
int Adapter::call(const HailstoneArgs* args,HailstoneOut* out) {
    services::Output result{host_,out};if(!result.valid())return 0;
    const auto operation=services::argument(host_,args,0,16);if(!operation)return 0;
    bool ok=true;
    if(*operation=="set") {
        const auto key=services::argument(host_,args,1,16);HailstoneValue value{};
        ok=key&&*key=="enabled"&&host_->arg(host_->host,args,2,&value)&&value.kind==HAILSTONE_INT&&
           (value.i==0||value.i==1)&&set_enabled(value.i==1);
        if(!ok&&error_.empty())error_="Fast-camp enabled must be 0 or 1.";
    } else if(*operation=="start") {
        HailstoneValue value{};
        if(host_->arg(host_->host,args,1,&value)) {
            const auto destination=services::argument(host_,args,1,16);
            if(!destination) {ok=false;error_="The fast-camp destination must be text.";}
            else ok=start(*destination);
        } else ok=start();
    } else if(*operation=="cancel")cancel();
    else if(*operation!="state")return 0;
    result.begin();result.boolean("ok",ok);result.boolean("active",active());result.boolean("supported",supported());
    result.boolean("enabled",enabled());result.boolean("armed",armed_);result.string("message",message());
    result.string("error",error_.empty()?settings_.error():error_);result.end();return 1;
}
} // namespace bastion::fast_camp
