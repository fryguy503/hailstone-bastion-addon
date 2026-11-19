// SPDX-License-Identifier: MIT
#include "twist.h"
#include "citadel_services.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>

namespace bastion::twist {
namespace {
constexpr std::size_t maximum_songs=31;
constexpr std::array definitions={Preference{"delay",1,36000,33},Preference{"adjust",-1000,1000,1},
    Preference{"recast",0,36000,0},Preference{"quiet",0,1,0},Preference{"allow_nonbard",0,1,0}};
std::string lower(std::string_view s) {
    std::string out(s);for(char& c:out)if(c>='A'&&c<='Z')c=static_cast<char>(c+'a'-'A');return out;
}
std::vector<std::string_view> words(std::string_view s) {
    std::vector<std::string_view> result;
    while(!s.empty()) {
        const auto first=s.find_first_not_of(" \t");if(first==s.npos)break;s.remove_prefix(first);
        const auto end=s.find_first_of(" \t");result.push_back(s.substr(0,end));
        if(result.size()>maximum_songs+2)break;
        if(end==s.npos)break;
        s.remove_prefix(end);
    }
    return result;
}
std::optional<int> integer(std::string_view s) {
    int n=0;const auto r=std::from_chars(s.data(),s.data()+s.size(),n);
    return !s.empty()&&r.ec==std::errc{}&&r.ptr==s.data()+s.size()?std::optional(n):std::nullopt;
}
std::uint64_t later(std::uint64_t now,std::uint64_t delay) {
    return delay>UINT64_MAX-now?UINT64_MAX:now+delay;
}
bool interrupt_line(std::string_view text) {
    return (text.starts_with("You miss a note, bringing your ")&&text.ends_with(" to a close!"))||
        text=="You haven't recovered yet..."||(text.starts_with("Your ")&&text.ends_with(" spell is interrupted."));
}
} // namespace
std::optional<std::uint64_t> duration_ticks(int formula,std::uint32_t cap,std::uint32_t level) {
    const auto l=static_cast<std::uint64_t>(level),c=static_cast<std::uint64_t>(cap);
    if(formula==0)return 0;
    if(formula==1||formula==6)return std::min((l+1)/2,c);
    if(formula==2)return std::min((l*3+4)/5,c);
    if(formula==3||formula==4||formula==11||formula==12)return c?c:static_cast<std::uint64_t>(formula)*10;
    if(formula==5)return 3;
    if(formula==7)return std::min(l,c?c:l);
    if(formula>=8&&formula<=10)return std::min(l*static_cast<std::uint64_t>(formula-7)+10,c);
    if(formula==13)return c*6/10;
    if(formula==50)return UINT32_MAX;
    if(formula==3600)return 6000;
    return {};
}
Adapter::Adapter():preferences_("twist.cfg",definitions) {}
bool Adapter::can(std::uint32_t required) const {
    return HAILSTONE_HOST_HAS(host_,caps)&&(host_->caps(host_->host)&required)==required;
}
std::optional<Adapter::Observation> Adapter::observe(bool actions) const {
    if(!host_||!can(HAILSTONE_CAP_READ|(actions?(HAILSTONE_CAP_COMMANDS|HAILSTONE_CAP_ACTIONS):0u))||
       !HAILSTONE_HOST_HAS(host_,get_player)||!HAILSTONE_HOST_HAS(host_,get_spawn)||!HAILSTONE_HOST_HAS(host_,get_map_policy)||
       !HAILSTONE_HOST_HAS(host_,server_info))return {};
    Observation out;out.player.size=sizeof(out.player);
    if(!host_->get_player(host_->host,&out.player)||out.player.size<offsetof(HailstonePlayer,zone_instance)+sizeof(out.player.zone_instance)||
       !out.player.in_zone||!out.player.spawn_id)return {};
    HailstoneSpawn spawn{};spawn.size=sizeof(spawn);
    if(!host_->get_spawn(host_->host,out.player.spawn_id,&spawn)||
       spawn.size<offsetof(HailstoneSpawn,incarnation)+sizeof(spawn.incarnation)||!spawn.incarnation)return {};
    HailstoneMapPolicy policy{};policy.size=sizeof(policy);
    if(!host_->get_map_policy(host_->host,&policy)||policy.size<offsetof(HailstoneMapPolicy,context)+sizeof(policy.context)||!policy.context)return {};
    HailstoneServer server{};server.size=sizeof(server);
    if(!host_->server_info(host_->host,&server)||server.size<sizeof(server))return {};
    std::string server_key;
    for(const auto& part:{services::bounded(server.name),services::bounded(server.host),services::bounded(server.world),services::bounded(server.world_host)})
        server_key+=std::to_string(part.size())+":"+part;
    server_key+=':'+std::to_string(server.port);
    out.identity={out.player.spawn_id,out.player.zone_id,out.player.zone_instance,spawn.incarnation,policy.context,
                  services::bounded(out.player.name),std::move(server_key)};return out;
}
bool Adapter::authorized(const HailstonePlayer& p) const {
    return p.class_id==8||(p.class_mask&(1u<<7))!=0||settings_.allow_nonbard;
}
std::optional<std::string> Adapter::parse(std::string_view expression) const {
    if(!HAILSTONE_HOST_HAS(host_,parse))return {};
    std::array<char,128> text{};const std::string input(expression);
    const auto n=host_->parse(host_->host,input.c_str(),text.data(),text.size());
    if(n<0||static_cast<std::size_t>(n)>=text.size()||text[static_cast<std::size_t>(n)]!='\0')return {};
    return std::string(text.data(),static_cast<std::size_t>(n));
}
std::optional<bool> Adapter::flag(std::string_view expression) const {
    const auto value=parse(expression);if(!value)return {};
    const auto v=lower(*value);if(v=="true"||v=="1")return true;if(v=="false"||v=="0")return false;return {};
}
std::optional<std::uint64_t> Adapter::number(std::string_view expression) const {
    const auto value=parse(expression);if(!value||value->empty())return {};
    std::uint64_t n=0;const auto r=std::from_chars(value->data(),value->data()+value->size(),n);
    return r.ec==std::errc{}&&r.ptr==value->data()+value->size()?std::optional(n):std::nullopt;
}
void Adapter::registrations() {
    if(selected_&&in_game_&&!tlo_added_&&HAILSTONE_HOST_HAS(host_,add_tlo))
        tlo_added_=host_->add_tlo(host_->host,"Twist",tlo_callback,this)!=0;
    else if((!selected_||!in_game_)&&tlo_added_) {
        if(HAILSTONE_HOST_HAS(host_,remove_tlo))host_->remove_tlo(host_->host,"Twist");
        tlo_added_=false;
    }
}
void Adapter::set_host(const PluginHostApi* host) {
    try {stop_owned();}catch(...) {}
    if(tlo_added_&&HAILSTONE_HOST_HAS(host_,remove_tlo))host_->remove_tlo(host_->host,"Twist");
    if(HAILSTONE_HOST_HAS(host_,remove_ui_function))host_->remove_ui_function(host_->host,"twist");
    host_=host;selected_=in_game_=tlo_added_=zoning_=false;retire(true);owned_.reset();settings_={};
    preferences_.invalidate_context();error_.clear();message_.clear();
}
void Adapter::initialize() {
    if(HAILSTONE_HOST_HAS(host_,add_ui_function))host_->add_ui_function(host_->host,"twist",ui_callback,this);
    registrations();
}
void Adapter::retire(bool clear_queue) {
    ++generation_;running_=false;hold_=current_=0;identity_.reset();
    next_poll_=next_action_=last_clock_=0;clock_seen_=false;
    if(clear_queue) {rotation_={};saved_.reset();one_shot_=false;retained_profile_.reset();}
}
void Adapter::peer(bool selected) {
    retire(!(zoning_&&retained_profile_));selected_=selected;if(!selected)in_game_=false;
    settings_={};debug_=false;preferences_.invalidate_context();registrations();
    error_.clear();message_=rotation_.gems.empty()?"Twist is idle.":"Twist is stopped; use /twist to resume the saved queue.";
}
void Adapter::world_login(bool zoning) {
    zoning_=zoning;retire(!zoning);owned_.reset();
}
void Adapter::game_state(int value) {
    in_game_=value==GAMESTATE_INGAME;
    if(!in_game_) {retire(false);owned_.reset();preferences_.invalidate_context();}
    else if(selected_) {
        try {
            if(retained_profile_&&!rotation_.gems.empty()) {
                const auto live=observe(false);
                if(!live||std::pair{live->identity.character,live->identity.server}!=*retained_profile_) {
                    retire(true);error_.clear();message_="Twist is idle.";
                }
            }
            zoning_=false;refresh_settings();
        }catch(...) {retire(true);error_="Twist settings are unavailable.";}
    }
    registrations();
}
void Adapter::begin_zone() {
    in_game_=false;retire(false);owned_.reset();preferences_.invalidate_context();registrations();
    error_.clear();message_=rotation_.gems.empty()?"Twist is idle.":"Twist is stopped; use /twist to resume the saved queue.";
}
void Adapter::refresh_settings() {
    if(!selected_||!in_game_)return;
    const auto generation=generation_;preferences_.refresh(host_);if(generation!=generation_)return;
    settings_.delay=static_cast<int>(preferences_.get("delay"));settings_.adjust=static_cast<int>(preferences_.get("adjust"));
    settings_.recast=static_cast<int>(preferences_.get("recast"));settings_.quiet=preferences_.get("quiet")!=0;
    settings_.allow_nonbard=preferences_.get("allow_nonbard")!=0;
}
bool Adapter::setting(std::string_view key,int value) {
    if(!selected_||!in_game_)return false;
    const auto generation=generation_;const auto before=observe(false);
    if(!before||generation!=generation_)return false;
    refresh_settings();const auto after=observe(false);
    if(generation!=generation_||!after||after->identity!=before->identity)return false;
    if(!preferences_.set(key,value)) {error_=preferences_.error();return false;}
    refresh_settings();++generation_;
    if(running_&&!authorized(after->player))pause("Twist stopped: this character is not eligible.");
    return true;
}
void Adapter::message(std::string_view text) {
    message_=text;
    if(HAILSTONE_HOST_HAS(host_,write_chat)) {const auto line="[Twist] "+std::string(text);host_->write_chat(host_->host,line.c_str(),0);}
}
void Adapter::pause(std::string_view reason) {
    const bool announce=running_&&selected_&&error_!=reason;
    retire(false);error_=reason;message_=reason;
    if(announce&&HAILSTONE_HOST_HAS(host_,write_chat)) {
        try {const auto line="[Twist] "+std::string(reason);host_->write_chat(host_->host,line.c_str(),0);}catch(...) {}
    }
}
bool Adapter::dispatch(std::string_view command) {
    if(!HAILSTONE_HOST_HAS(host_,do_command)||!can(HAILSTONE_CAP_COMMANDS|HAILSTONE_CAP_ACTIONS))return false;
    struct Guard {bool& flag;bool old;~Guard(){flag=old;}} guard{dispatching_,dispatching_};dispatching_=true;
    const std::string text(command);host_->do_command(host_->host,text.c_str());return true;
}
void Adapter::stop_owned() {
    if(!owned_)return;
    const auto expected=*owned_;owned_.reset();const auto generation=generation_;
    const auto live=observe(true);
    if(live&&generation==generation_&&live->identity==expected.identity&&live->player.casting_spell_id==expected.spell)
        dispatch("/stopsong");
}
int Adapter::next() const {
    if(!running_)return 0;
    if(hold_)return hold_;
    return rotation_.gems.empty()?0:rotation_.gems[rotation_.cursor%rotation_.gems.size()];
}
std::string Adapter::queue() const {
    std::string result;for(int gem:rotation_.gems) {if(!result.empty())result+=' ';result+=std::to_string(gem);}return result;
}
void Adapter::observe_command(std::string_view line) {
    if(dispatching_)return;
    const auto tokens=words(line);if(tokens.empty())return;const auto name=lower(tokens[0]);
    if(name=="/cast"||name=="/melody"||name=="/stopsong"||name=="/stopcast") {retire(false);owned_.reset();}
}
bool Adapter::command(std::string_view line) {
    const auto tokens=words(line);if(tokens.empty())return false;
    const auto name=lower(tokens[0]);if(name!="/twist"&&name!="/sing"&&name!="/stoptwist")return false;
    if(!selected_)return false;
    if(line.size()>1024||line.find_first_of("\r\n\0",0,3)!=line.npos) {error_="Invalid twist command.";message(error_);return true;}
    if(tokens.size()>maximum_songs+2) {error_="Twist supports at most 31 entries.";message(error_);return true;}
    if(busy_) {retire(false);error_="Twist command interrupted an active observation.";return true;}
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{busy_};busy_=true;
    try {
        std::string args;
        if(name=="/sing")args="hold ";else if(name=="/stoptwist")args="stop ";
        const auto at=line.find_first_of(" \t");if(at!=line.npos)args+=line.substr(at+1);
        if(!execute(args)&&!error_.empty())message(error_);
    }catch(...) {pause("Twist host service failed.");}
    return true;
}
bool Adapter::execute(std::string_view args) {
    const auto tokens=words(args);
    if(tokens.size()>maximum_songs+1) {error_="Twist supports at most 31 entries.";return false;}
    const auto op=tokens.empty()?std::string{}:lower(tokens[0]);
    const bool silent=tokens.size()==2&&lower(tokens[1])=="silent";
    if(op=="stop"||op=="end"||op=="off"||op=="clear") {
        stop_owned();retire(op=="clear");error_.clear();if(!silent)message(op=="clear"?"Queue cleared.":"Stopped; queue retained.");return true;
    }
    if(!in_game_) {error_="Enter a Bastion character before using twist.";return false;}
    refresh_settings();const auto generation=generation_;const auto live=observe(true);
    if(!live||generation!=generation_) {error_="Twist needs a current character and read/action permissions.";return false;}
    if(!authorized(live->player)) {error_="Twist requires Bard eligibility or Allow non-bard twisting.";message(error_);return false;}
    if(op=="set"||op=="slots"||op=="list") {message("Item and AA twist entries are disabled in Citadel; use song gems 1–12.");return true;}
    if(op=="help"||(op.empty()&&rotation_.gems.empty())) {
        message("/twist 1 2 ...; once; hold N (/sing); start; stop (/stoptwist); clear; reset; delay; adjust; recast; quiet; reload.");return true;
    }
    if(op=="debug") {debug_=!debug_;message(debug_?"Debug enabled.":"Debug disabled.");return true;}
    if(op=="reload"||op=="load") {
        if(!preferences_.reload())error_=preferences_.error();else {refresh_settings();error_.clear();message("Settings reloaded.");}
        return true;
    }
    if(op=="delay"||op=="adjust"||op=="recast") {
        if(tokens.size()==1) {message(op+" = "+std::to_string(op=="delay"?settings_.delay:op=="adjust"?settings_.adjust:settings_.recast));return true;}
        const auto value=tokens.size()==2?integer(tokens[1]):std::nullopt;
        if(!value||!setting(op,*value)) {if(error_.empty())error_="Invalid twist setting.";return false;}
        error_.clear();message(op=="delay"&&*value<=30?"Delay saved; this is at or below the standard song cast time.":"Setting saved.");return true;
    }
    if(op=="quiet") {if(setting("quiet",settings_.quiet?0:1))message(settings_.quiet?"Quiet enabled.":"Quiet disabled.");return true;}
    if(op=="reset") {for(auto& due:rotation_.due)due=0;if(saved_)for(auto& due:saved_->due)due=0;message("Song timers reset.");return true;}
    if(op=="start"||op.empty()) {
        if(rotation_.gems.empty()) {error_="No twist queue is saved.";return false;}
        ++generation_;running_=true;hold_=0;identity_=live->identity;next_poll_=next_action_=0;clock_seen_=false;error_.clear();
        retained_profile_=std::pair{live->identity.character,live->identity.server};
        if(!silent)message("Resuming twist.");
        return true;
    }
    if(op=="hold") {
        const auto gem=tokens.size()==2?integer(tokens[1]):std::nullopt;
        if(!gem||*gem<1||*gem>12) {error_="Hold requires a gem from 1 to 12.";return false;}
        ++generation_;hold_=*gem;running_=true;identity_=live->identity;next_poll_=next_action_=0;clock_seen_=false;error_.clear();
        retained_profile_=std::pair{live->identity.character,live->identity.server};
        message("Holding gem "+std::to_string(*gem)+".");return true;
    }
    const bool once=op=="once";std::vector<int> replacement;
    const std::size_t first=once?1:0;
    if(tokens.size()-first>maximum_songs) {error_="Twist supports at most 31 entries.";return false;}
    for(std::size_t i=first;i<tokens.size();++i) {
        const auto gem=integer(tokens[i]);if(!gem||*gem<1||*gem>12) {error_="Twist entries must be song gems 1–12.";return false;}
        replacement.push_back(*gem);
    }
    if(replacement.empty()) {error_="Supply at least one song gem.";return false;}
    if(saved_) {rotation_=std::move(*saved_);saved_.reset();}
    if(once&&!rotation_.gems.empty())saved_=rotation_;
    one_shot_=once;
    rotation_={replacement,std::vector<std::uint64_t>(replacement.size()),0,0,false};
    ++generation_;running_=true;hold_=current_=0;identity_=live->identity;next_poll_=next_action_=0;clock_seen_=false;error_.clear();
    retained_profile_=std::pair{live->identity.character,live->identity.server};
    if(!settings_.quiet)message(once?"One-shot twist queued.":"Twist queued: "+queue());
    return true;
}
void Adapter::pulse() {
    if(!selected_||!in_game_||!running_||busy_)return;
    struct Guard {bool& flag;~Guard(){flag=false;}} guard{busy_};busy_=true;
    try {tick();}catch(...) {pause("Twist host observation failed; use /twist to resume.");}
}
void Adapter::tick() {
    if(!HAILSTONE_HOST_HAS(host_,monotonic_ms)) {pause("Twist clock service is unavailable.");return;}
    const auto generation=generation_;const auto now=host_->monotonic_ms(host_->host);
    if(generation!=generation_)return;
    if(clock_seen_&&now<last_clock_) {pause("Twist clock changed; use /twist to resume.");return;}
    last_clock_=now;clock_seen_=true;if(now<next_poll_)return;next_poll_=later(now,100);
    const auto live=observe(true);if(generation!=generation_)return;
    if(!live||!identity_||live->identity!=*identity_) {pause("Twist character context changed; use /twist to resume.");owned_.reset();return;}
    if(!authorized(live->player)) {pause("Twist character is no longer eligible.");return;}
    const auto dead=flag("${Me.Dead}"),stunned=flag("${Me.Stunned}"),feigning=flag("${Me.Feigning}");
    const auto casting=flag("${Window[CastingWindow].Open}");
    if(generation!=generation_)return;
    if(!dead||!stunned||!feigning||!casting) {pause("Twist needs current casting and character observations from Hailstone.");return;}
    if(*dead||live->player.stand_state==120||live->player.stand_state==110) {pause("Twist stopped while sitting or dead.");return;}
    if(*stunned||*casting)return;
    if(*feigning) {dispatch("/stand");return;}
    if(now<next_action_) {next_poll_=std::min(next_poll_,next_action_);return;}
    if(rotation_.completed&&!hold_) {
        if(saved_) {rotation_=std::move(*saved_);saved_.reset();one_shot_=false;if(!settings_.quiet)message("One-shot complete; normal twist resumed.");}
        else {retire(false);message_="One-shot complete.";return;}
    }
    int gem=hold_;std::size_t index=0;
    if(!gem) {
        if(rotation_.gems.empty()) {retire(false);return;}
        bool found=false;
        for(std::size_t i=0;i<rotation_.gems.size();++i) {
            index=rotation_.cursor;rotation_.cursor=(rotation_.cursor+1)%rotation_.gems.size();
            if(rotation_.due[index]<=now||(rotation_.gems.size()==1&&!one_shot_)) {gem=rotation_.gems[index];found=true;break;}
            if(one_shot_&&rotation_.cursor==0) {rotation_.completed=true;break;}
        }
        if(!found)return;
    }
    if(!HAILSTONE_HOST_HAS(host_,get_memorized_spell)||!HAILSTONE_HOST_HAS(host_,get_spell)) {pause("Twist spell services are unavailable.");return;}
    const auto spell_id=host_->get_memorized_spell(host_->host,static_cast<std::uint32_t>(gem-1));
    if(generation!=generation_)return;
    HailstoneSpell spell{};spell.size=sizeof(spell);
    if(!spell_id||!host_->get_spell(host_->host,spell_id,&spell)||spell.size<offsetof(HailstoneSpell,recast_time_ms)+sizeof(spell.recast_time_ms)) {
        pause("A queued song is not memorized; use /twist to resume.");return;
    }
    std::optional<std::uint64_t> duration=0;
    if(!hold_&&!one_shot_&&rotation_.gems.size()>1) {
        const auto prefix="${Spell["+std::to_string(spell_id)+"].";
        const auto formula=number(prefix+"DurationFormula}"),cap=number(prefix+"DurationCap}");
        if(generation!=generation_)return;
        if(!formula||!cap||*formula>INT_MAX||*cap>UINT32_MAX) {pause("Twist duration fields are unavailable; update Hailstone to use this scheduler.");return;}
        duration=duration_ticks(static_cast<int>(*formula),static_cast<std::uint32_t>(*cap),live->player.level);
        if(!duration) {pause("This song's duration formula is unsupported; twist paused.");return;}
    }
    const auto fresh=observe(true);if(generation!=generation_||!fresh||fresh->identity!=live->identity)return;
    const auto fresh_bar=flag("${Window[CastingWindow].Open}");
    if(generation!=generation_)return;
    if(!fresh_bar) {pause("Twist casting observation is unavailable.");return;}
    if(*fresh_bar)return;
    if(!dispatch("/stopsong")||generation!=generation_)return;
    const auto ready=flag("${Me.SpellReady["+std::to_string(gem)+"]}");
    const auto before_cast=observe(true);
    if(generation!=generation_||!before_cast||before_cast->identity!=live->identity)return;
    if(!ready) {pause("Twist gem readiness is unavailable.");return;}
    if(!*ready) {if(!hold_)rotation_.cursor=index;next_action_=later(now,100);return;}
    current_=gem;if(!hold_)rotation_.last=index;
    if(!dispatch("/cast "+std::to_string(gem))||generation!=generation_)return;
    owned_=Owned{live->identity,spell_id};current_=gem;next_action_=later(now,static_cast<std::uint64_t>(settings_.delay)*100);
    if(!hold_) {
        rotation_.last=index;
        const std::int64_t adjustment=static_cast<std::int64_t>(settings_.adjust)*6000;
        std::uint64_t extra=0;
        if(*duration>3) {
            const auto ms=*duration*6000;
            extra=adjustment>=0?(ms>static_cast<std::uint64_t>(adjustment)?ms-static_cast<std::uint64_t>(adjustment):0):ms+static_cast<std::uint64_t>(-adjustment);
        } else if(spell.recast_time_ms)extra=(static_cast<std::uint64_t>(spell.recast_time_ms)+100)/100*100+static_cast<std::uint64_t>(settings_.recast)*100;
        rotation_.due[index]=later(next_action_,extra);
        if(one_shot_&&rotation_.cursor==0)rotation_.completed=true;
    }
    if(debug_)message("Cast gem "+std::to_string(gem)+".");
}
int Adapter::incoming_chat(std::string_view text) {
    if(!running_||!selected_||current_==0)return 0;
    if(interrupt_line(text)||text=="You can't cast spells while stunned!") {
        ++generation_;
        if(!hold_&&!rotation_.gems.empty()) {rotation_.cursor=rotation_.last;rotation_.due[rotation_.last]=0;rotation_.completed=false;}
        next_poll_=0;next_action_=0;
        if(text=="You can't cast spells while stunned!"&&HAILSTONE_HOST_HAS(host_,monotonic_ms))
            try {next_action_=later(host_->monotonic_ms(host_->host),1000);}catch(...) {pause("Twist clock is unavailable.");}
    }
    return 0;
}
int Adapter::ui_callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if(!self)return 0;
    auto& owner=*static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_,[&]{return owner.ui(args,out);});
}
int Adapter::ui(const HailstoneArgs* args,HailstoneOut* out) {
    services::Output result{host_,out};if(!result.valid())return 0;
    const auto op=services::argument(host_,args,0,16);if(!op)return 0;
    if(*op=="command") {
        const auto text=services::argument(host_,args,1,1024);if(!text)return 0;command("/twist "+*text);
    }else if(*op=="set") {
        const auto key=services::argument(host_,args,1,32);HailstoneValue value{};
        if(!key||!HAILSTONE_HOST_HAS(host_,arg)||!host_->arg(host_->host,args,2,&value))return 0;
        std::optional<int> integer_value;
        if((value.kind==HAILSTONE_INT||value.kind==HAILSTONE_BOOL)&&value.i>=INT_MIN&&value.i<=INT_MAX)integer_value=static_cast<int>(value.i);
        else if(value.kind==HAILSTONE_FLOAT&&std::isfinite(value.f)&&value.f>=INT_MIN&&value.f<=INT_MAX&&std::floor(value.f)==value.f)integer_value=static_cast<int>(value.f);
        if(!integer_value||!setting(*key,*integer_value))return 0;
    }else if(*op!="state")return 0;
    result.begin();result.boolean("active",selected_&&in_game_);result.boolean("running",running_);
    result.string("queue",queue());result.number("current",current());result.number("next",next());
    result.boolean("once",one_shot_);result.number("hold",hold_);
    result.string("message",message_);result.string("error",error_);result.key("settings");result.begin();
    result.boolean("quiet",settings_.quiet);result.boolean("allow_nonbard",settings_.allow_nonbard);
    result.number("delay",settings_.delay);result.number("adjust",settings_.adjust);result.number("recast",settings_.recast);
    result.end();result.end();return 1;
}
int Adapter::tlo_callback(void* self,const char*,const char* member,const char*,HailstoneValue* out) noexcept {
    if(!self)return 0;
    auto& owner=*static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_,[&]{return owner.tlo(member,out);});
}
int Adapter::tlo(const char* member,HailstoneValue* out) {
    if(!out||!selected_||!in_game_||!can(HAILSTONE_CAP_READ))return 0;
    *out={};const auto name=member?lower(member):std::string{};
    if(name.empty()||name=="twisting") {out->kind=HAILSTONE_BOOL;out->i=running_?1:0;return 1;}
    if(name=="current"||name=="next") {out->kind=HAILSTONE_INT;out->i=name=="current"?current():next();return 1;}
    if(name=="list") {tlo_text_=queue();out->kind=HAILSTONE_STRING;out->s=tlo_text_.c_str();return 1;}
    return 0;
}
} // namespace bastion::twist
