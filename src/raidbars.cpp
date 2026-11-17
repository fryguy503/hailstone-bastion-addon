#include "raidbars.h"
#include "citadel_services.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace bastion::raidbars {
namespace {
constexpr std::array<const char*, 16> classes{"WAR","CLR","PAL","RNG","SHD","DRU","MNK","BRD","ROG","SHM","NEC","WIZ","MAG","ENC","BST","BER"};
constexpr std::array<const char*,3> manager_action_ids{"bastion.manager.list","bastion.manager.grant","bastion.manager.revoke"};
std::string bounded(const char* text, std::size_t maximum) {
    if (!text) return {};
    std::size_t n = 0; while (n < maximum && text[n]) ++n;
    return {text, n};
}
template<std::size_t N> std::string bounded(const char (&text)[N]) { return bounded(text, N); }
std::optional<std::uint64_t> token(std::string_view text) {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0')) return {};
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value ? std::optional(value) : std::nullopt;
}
bool character_name(std::string_view name) {
    return !name.empty() && name.size() < 64 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    });
}
bool same_name(std::string_view left, std::string_view right) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
        [&](char a, char b) { return lower(a) == lower(b); });
}
bool manager_commands_available(const PluginHostApi* host) {
    // The callback stays installed when server permissions change. /say is held to live CHAT caps.
    return HAILSTONE_HOST_HAS(host,do_command) && HAILSTONE_HOST_HAS(host,caps) &&
           (host->caps(host->host)&HAILSTONE_CAP_CHAT)!=0;
}
void integer(const PluginHostApi& api, HailstoneOut* out, const char* key, std::int64_t value) {
    api.out_key(api.host, out, key); HailstoneValue v{}; v.kind = HAILSTONE_INT; v.i = value; api.out_value(api.host, out, &v);
}
void string(const PluginHostApi& api, HailstoneOut* out, const char* key, const std::string& text) {
    api.out_key(api.host, out, key); HailstoneValue v{}; v.kind = HAILSTONE_STRING; v.s = text.c_str(); api.out_value(api.host, out, &v);
}
void row_table(const PluginHostApi& h, HailstoneOut* out, const Row& row) {
    h.out_begin_table(h.host, out);
    string(h,out,"name",row.name); integer(h,out,"group",row.group); integer(h,out,"class_id",row.class_id);
    integer(h,out,"level",row.level);integer(h,out,"flags",row.flags);integer(h,out,"spawn_id",row.spawn_id);
    string(h,out,"incarnation",std::to_string(row.incarnation));integer(h,out,"pet",row.pet);
    integer(h,out,"health_known",row.health.known);integer(h,out,"health",row.health.percent);
    integer(h,out,"mana_known",row.mana.known);integer(h,out,"mana",row.mana.percent);
    integer(h,out,"endurance_known",row.endurance.known);integer(h,out,"endurance",row.endurance.percent);
    integer(h,out,"self",row.self);
    h.out_key(h.host,out,"pets");h.out_begin_array(h.host,out);
    for (const auto& pet : row.pets) row_table(h,out,pet);
    h.out_end(h.host,out);h.out_end(h.host,out);
}
} // namespace

const std::vector<Setting>& settings_definitions() {
    static const std::vector<Setting> values{
        {"enabled",0,1,0},{"edit",0,1,0},{"clickable",0,1,1},{"clickthrough",0,1,0},
        {"show_all",0,1,1},{"group_sort",0,1,1},{"show_pets",0,1,0},{"combine_pets",0,1,1},
        {"debug",0,1,0},{"background",0,100,80},{"threshold",0,100,100},
        {"bar_width",0,500,150},{"bar_height",0,80,28},{"pet_width",0,500,0},{"pet_height",0,50,12},
        {"left",0,16384,24},{"top",0,16384,160},{"rows",1,72,12},{"columns",1,72,6},
        {"always",0,65535,0},{"never",0,65535,0},{"filter",0,65535,0},{"mana",0,1,0},
        {"endurance",0,1,0},{"manage",0,1,0},{"filter_groups",0,1,0},{"class_colors",0,1,1},
        {"show_self",0,1,1},{"show_empty",0,1,1},{"right",0,16384,0},{"bottom",0,16384,0}
    };
    return values;
}
Settings::Settings() { for (const auto& def : settings_definitions()) values.emplace(def.key,def.initial); }
int Settings::get(std::string_view key) const { const auto found=values.find(key);return found==values.end()?0:found->second; }
bool Settings::set(std::string_view key,int value) {
    const auto found=std::find_if(settings_definitions().begin(),settings_definitions().end(),[&](const Setting& d){return key==d.key;});
    if(found==settings_definitions().end()||value<found->minimum||value>found->maximum)return false;
    values[std::string(key)]=value;return true;
}
bool Settings::set_priority(std::string_view names) {
    if(names.size()>128)return false;
    std::vector<std::uint32_t> next;std::istringstream input{std::string(names)};std::string word;
    while(input>>word){
        for(char& c:word)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto found=std::find(classes.begin(),classes.end(),word);
        if(found==classes.end())return false;
        const auto id=static_cast<std::uint32_t>(found-classes.begin()+1);
        if(std::find(next.begin(),next.end(),id)==next.end())next.push_back(id);
    }
    priority=std::move(next);return true;
}
std::string Settings::priority_text()const{
    std::string result;for(auto id:priority){if(!result.empty())result+=' ';result+=classes[id-1];}return result;
}
std::vector<Row> visible_rows(std::vector<Row> rows,const Settings& config){
    std::erase_if(rows,[&](const Row& row){
        if(row.self&&(!config.get("show_self")||!config.get("group_sort")))return true;
        // Citadel's group view is a complete roster; class rules are optional there.
        if(config.get("group_sort")&&!config.get("filter_groups"))return false;
        const auto bit=row.class_id>=1&&row.class_id<=16?1u<<(row.class_id-1):0u;
        if((static_cast<unsigned>(config.get("never"))&bit)!=0)return true;
        if(config.get("show_all")||(static_cast<unsigned>(config.get("always"))&bit)!=0)return false;
        const auto threshold=(static_cast<unsigned>(config.get("filter"))&bit)!=0?config.get("threshold"):99;
        return !row.health.known||row.health.percent>=static_cast<unsigned>(threshold);
    });
    constexpr std::array<std::uint32_t,16> defaults{1,3,5,14,12,7,4,9,15,8,2,10,6,13,11,16};
    const auto rank=[&](std::uint32_t id){const auto it=std::find(config.priority.begin(),config.priority.end(),id);
        if(it!=config.priority.end())return static_cast<std::size_t>(it-config.priority.begin());
        const auto fallback=std::find(defaults.begin(),defaults.end(),id);
        return config.priority.size()+static_cast<std::size_t>(fallback-defaults.begin());};
    std::stable_sort(rows.begin(),rows.end(),[&](const Row& a,const Row& b){
        if(config.get("group_sort")&&a.group!=b.group)return (a.group?a.group:13)<(b.group?b.group:13);
        if(config.get("group_sort")&&((a.flags&HAILSTONE_RAID_GROUP_LEADER)!=0)!=((b.flags&HAILSTONE_RAID_GROUP_LEADER)!=0))
            return (a.flags&HAILSTONE_RAID_GROUP_LEADER)!=0;
        if(rank(a.class_id)!=rank(b.class_id))return rank(a.class_id)<rank(b.class_id);
        return a.name<b.name;
    });
    if(config.get("show_pets")&&!config.get("combine_pets")){
        std::vector<Row> expanded;
        for(auto& row:rows){auto pets=std::move(row.pets);row.pets.clear();expanded.push_back(std::move(row));
            for(auto& pet:pets)expanded.push_back(std::move(pet));}
        return expanded;
    }
    return rows;
}
void Adapter::set_host(const PluginHostApi* api){shutdown();host_=api;}
void Adapter::initialize(){
    if(initialized_||!HAILSTONE_HOST_HAS(host_,add_ui_function))return;
    initialized_=host_->add_ui_function(host_->host,"raidbars",callback,this)!=0;
    stock_function_=host_->add_ui_function(host_->host,"raid_manager_action",stock_callback,this)!=0;
}
void Adapter::shutdown(){
    directory_cache_.reset();
    if(HAILSTONE_HOST_HAS(host_,remove_ui_function)){
        if(initialized_)host_->remove_ui_function(host_->host,"raidbars");
        if(stock_function_)host_->remove_ui_function(host_->host,"raid_manager_action");
    }
    initialized_=stock_function_=false;manager_review_.reset();stock_manager_review_.reset();roster_members_.clear();
}
void Adapter::peer(bool selected){directory_cache_.reset();selected_=selected;if(!selected){in_zone_=false;identity_.clear();file_.clear();settings_=Settings{};manager_review_.reset();stock_manager_review_.reset();roster_members_.clear();}}
void Adapter::game_state(int state){directory_cache_.reset();in_zone_=state==GAMESTATE_INGAME;if(!in_zone_){identity_.clear();file_.clear();manager_review_.reset();stock_manager_review_.reset();roster_members_.clear();}}
void Adapter::begin_zone(){directory_cache_.reset();in_zone_=false;manager_review_.reset();stock_manager_review_.reset();roster_members_.clear();}
void Adapter::load_settings(){
    const auto directory=directory_cache_.get(host_);
    if(directory.empty()){
        file_.clear();identity_.clear();settings_=Settings{};manager_review_.reset();stock_manager_review_.reset();
        error_="Character settings directory is unavailable.";return;
    }
    const auto file=directory/"raidbars.cfg";
    if(file.generic_string()==identity_)return;
    file_=file;identity_=file.generic_string();settings_=Settings{};error_.clear();manager_review_.reset();stock_manager_review_.reset();
    std::error_code error;const auto bytes=std::filesystem::file_size(file,error);
    if(error||bytes>8192)return;
    std::ifstream input(file);std::string line;Settings next;
    while(std::getline(input,line)){
        const auto at=line.find('=');if(at==std::string::npos)continue;
        const auto key=line.substr(0,at),value=line.substr(at+1);
        if(key=="priority"){if(!next.set_priority(value))return;}
        else{int number=0;const auto read=std::from_chars(value.data(),value.data()+value.size(),number);
            if(read.ec!=std::errc{}||read.ptr!=value.data()+value.size()||!next.set(key,number))return;}
    }
    if(!input.bad())settings_=std::move(next);
}
bool Adapter::save_settings(){
    if(file_.empty()){error_="Character settings are unavailable.";return false;}
    std::error_code directory_error;std::filesystem::create_directories(file_.parent_path(),directory_error);
    if(directory_error){error_="Character settings directory could not be created.";return false;}
    auto pending=file_;pending+=".pending";std::ofstream out(pending,std::ios::trunc);
    for(const auto& [key,value]:settings_.values)out<<key<<'='<<value<<'\n';
    out<<"priority="<<settings_.priority_text()<<'\n';out.close();
    if(!out){error_="Raid bar settings could not be saved.";return false;}
    std::error_code error;auto backup=file_;backup+=".previous";
    if(std::filesystem::exists(file_,error)){
        std::filesystem::remove(backup,error);error.clear();std::filesystem::rename(file_,backup,error);
        if(error){error_="Raid bar settings could not be replaced.";return false;}
    }
    std::filesystem::rename(pending,file_,error);
    if(error){std::error_code ignored;if(std::filesystem::exists(backup,ignored))std::filesystem::rename(backup,file_,ignored);
        error_="Raid bar settings could not be replaced.";return false;}
    error_.clear();return true;
}
bool Adapter::sample(HailstoneRaidRoster& roster,std::vector<Row>& rows){
    local_leader_=false;
    group_counts_.fill(0);
    roster_members_.clear();
    if(!selected_||!HAILSTONE_HOST_HAS(host_,get_raid_member)||!HAILSTONE_HOST_HAS(host_,get_raid_roster)||!HAILSTONE_HOST_HAS(host_,get_player))return false;
    HailstonePlayer player{};player.size=sizeof(player);
    if(!host_->get_player(host_->host,&player)||player.size<offsetof(HailstonePlayer,in_zone)+sizeof(player.in_zone)||
        !player.in_zone||player.game_state!=GAMESTATE_INGAME)return false;
    load_settings();
    roster={};roster.size=sizeof(roster);
    // Settings and the preview remain usable when the character is outside a raid.
    if(!host_->get_raid_roster(host_->host,&roster)){roster={};roster.size=sizeof(roster);return true;}
    if(!roster.revision||!roster.owner_incarnation||roster.count>72)return false;
    const auto vitals=[&](Row& row){
        if(!row.spawn_id||!row.incarnation||!HAILSTONE_HOST_HAS(host_,get_spawn))return;
        HailstoneSpawn spawn{};spawn.size=sizeof(spawn);
        if(!host_->get_spawn(host_->host,row.spawn_id,&spawn)||spawn.size<offsetof(HailstoneSpawn,mana_percent)+sizeof(spawn.mana_percent)||spawn.incarnation!=row.incarnation)return;
        row.health={(spawn.observation_flags&HAILSTONE_SPAWN_HP_KNOWN)!=0&&spawn.hp_percent<=100,spawn.hp_percent};
        row.mana={(spawn.observation_flags&HAILSTONE_SPAWN_MANA_KNOWN)!=0&&spawn.mana_percent<=100,spawn.mana_percent};
        if(spawn.size>=offsetof(HailstoneSpawn,endurance_percent)+sizeof(spawn.endurance_percent))
            row.endurance={(spawn.observation_flags&HAILSTONE_SPAWN_ENDURANCE_KNOWN)!=0&&spawn.endurance_percent<=100,spawn.endurance_percent};
    };
    for(std::uint32_t i=0;i<roster.count;++i){
        HailstoneRaidMember member{};member.size=sizeof(member);
        if(!host_->get_raid_member(host_->host,roster.revision,i,&member)||member.group_id>12)return false;
        ++group_counts_[member.group_id];
        Row row;row.name=bounded(member.name);row.group=member.group_id;row.class_id=member.class_id;row.level=member.level;
        row.self=member.spawn_id==player.spawn_id;
        if(row.self&&(member.flags&HAILSTONE_RAID_LEADER)!=0)local_leader_=true;
        row.flags=member.flags;row.spawn_id=member.spawn_id;row.incarnation=member.incarnation;vitals(row);rows.push_back(std::move(row));
    }
    if(settings_.get("show_pets")&&HAILSTONE_HOST_HAS(host_,next_spawn)){
        std::uint32_t after=0;
        for(unsigned work=0;work<4096;++work){
            HailstoneSpawn spawn{};spawn.size=sizeof(spawn);
            if(!host_->next_spawn(host_->host,after,&spawn)||spawn.size<offsetof(HailstoneSpawn,incarnation)+sizeof(spawn.incarnation)||spawn.id<=after)break;
            after=spawn.id;
            if(!spawn.owner_id||!spawn.incarnation||spawn.is_corpse)continue;
            auto owner=std::find_if(rows.begin(),rows.end(),[&](const Row& row){return !row.pet&&row.spawn_id==spawn.owner_id&&row.incarnation;});
            if(owner==rows.end())continue;
            Row pet;pet.name=bounded(spawn.name);pet.group=owner->group;pet.class_id=owner->class_id;pet.owner_id=owner->spawn_id;
            pet.spawn_id=spawn.id;pet.incarnation=spawn.incarnation;pet.pet=true;vitals(pet);
            if(owner->pets.size()<4)owner->pets.push_back(std::move(pet));
        }
    }
    HailstoneRaidRoster current{};current.size=sizeof(current);
    if(!host_->get_raid_roster(host_->host,&current)||current.revision!=roster.revision||current.owner_incarnation!=roster.owner_incarnation)return false;
    roster_members_.reserve(rows.size());
    for(const auto& row:rows)roster_members_.push_back({row.name,row.self});
    rows=visible_rows(std::move(rows),settings_);return true;
}
bool Adapter::select(const std::vector<Row>& rows,const HailstoneRaidRoster& roster,std::uint32_t id,std::uint64_t incarnation){
    const Row* found=nullptr;
    for(const auto& row:rows){
        if(row.spawn_id==id&&row.incarnation==incarnation)found=&row;
        for(const auto& pet:row.pets)if(pet.spawn_id==id&&pet.incarnation==incarnation)found=&pet;
    }
    if(!found||!roster.revision)return false;
    HailstonePlayer player{};player.size=sizeof(player);
    if(settings_.get("combine_pets")&&!found->pet&&!found->pets.empty()&&
       host_->get_player(host_->host,&player)&&player.target_id==id){
        const auto& pet=found->pets.front();id=pet.spawn_id;incarnation=pet.incarnation;
    }
    return host_->select_raid_member(host_->host,roster.revision,id,incarnation)!=0;
}
int Adapter::callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if (!self) return 0;
    auto &owner = *static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_, [&] { return owner.ui(args,out); });
}
int Adapter::stock_callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if(!self)return 0;
    auto& owner=*static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_,[&]{return owner.stock_action(args,out);});
}
void Adapter::retire_manager_reviews(const HailstoneRaidRoster& roster,bool available){
    const auto stale=[&](const ManagerReview& review){
        return !available||!roster.count||!manager_commands_available(host_)||review.revision!=roster.revision||
            review.owner_incarnation!=roster.owner_incarnation||
            std::none_of(roster_members_.begin(),roster_members_.end(),[&](const ManagerMember& row){return row.name==review.name;});
    };
    if(manager_review_&&(!settings_.get("manage")||stale(*manager_review_)))manager_review_.reset();
    if(stock_manager_review_&&stale(*stock_manager_review_))stock_manager_review_.reset();
}
bool Adapter::manager_request(const HailstoneRaidRoster& roster,bool available,std::optional<std::uint64_t> revision,
                             std::string_view verb,std::string_view target,bool stock,
                             std::string_view review_token,std::string& error){
    auto& pending=stock?stock_manager_review_:manager_review_;
    if(verb=="cancel"){
        if(!stock){pending.reset();return true;}
        if(pending&&pending->token==review_token&&same_name(pending->name,target)){pending.reset();return true;}
        error="This delegation review is no longer active.";return false;
    }
    if(!available||!roster.count||!revision||*revision!=roster.revision)
        error="The raid roster changed. Choose a current member and review the request again.";
    else if(!manager_commands_available(host_))error="Bastion raid manager requests require current chat permission.";
    else if(verb=="list"){
        // The server restricts List to the actual raid leader; its response is chat.
        host_->do_command(host_->host,"/say #raidmanager list");return true;
    }else if(!stock&&!settings_.get("manage"))error="Enable raid management before reviewing a delegation.";
    else{
        const auto member=std::find_if(roster_members_.begin(),roster_members_.end(),
            [&](const ManagerMember& row){return character_name(target)&&same_name(row.name,target);});
        const bool reviewing=verb=="review_add"||verb=="review_remove";
        const bool confirming=verb=="confirm_add"||verb=="confirm_remove";
        const auto requested=verb=="review_add"||verb=="confirm_add"?"add":"remove";
        if(member==roster_members_.end())error="Choose a current raid character with a valid name.";
        else if(reviewing){
            const auto id=stock?services::token():0;
            if(stock&&!id){error="A new delegation review could not be created.";return false;}
            pending=ManagerReview{roster.revision,roster.owner_incarnation,requested,member->name,stock?std::to_string(id):std::string{}};
            return true;
        }else if(confirming){
            const auto reviewed=std::exchange(pending,std::nullopt);
            if(reviewed&&reviewed->revision==roster.revision&&reviewed->owner_incarnation==roster.owner_incarnation&&
               reviewed->name==member->name&&reviewed->action==requested&&(!stock||reviewed->token==review_token)){
                // Delegates see a client leader alias. Real leader/member/bot checks remain server-authoritative.
                const auto command="/say #raidmanager "+reviewed->action+" "+member->name;
                host_->do_command(host_->host,command.c_str());return true;
            }
            error="Review this exact delegation before confirming it.";
        }else error="Unknown raid manager request.";
    }
    return false;
}
int Adapter::stock_action(const HailstoneArgs* args,HailstoneOut* out){
    services::Output result{host_,out};if(!result.valid())return 0;
    const auto window=services::argument(host_,args,0,32),id=services::argument(host_,args,1,64),
        phase=services::argument(host_,args,2,16),selection=services::argument(host_,args,3,64),
        review_token=services::argument(host_,args,4,128);
    HailstoneRaidRoster roster{};std::vector<Row> rows;const bool available=in_zone_&&sample(roster,rows);
    retire_manager_reviews(roster,available);
    bool okay=false;std::string message;std::string returned_token;
    if(!selected_||!stock_function_||!window||*window!="RaidWindow"||!id||!phase||!selection||!review_token)
        message="Bastion raid manager actions are unavailable.";
    else{
        const bool list=*id==manager_action_ids[0],grant=*id==manager_action_ids[1],revoke=*id==manager_action_ids[2];
        std::string verb;
        if(list&&*phase=="invoke")verb="list";
        else if((grant||revoke)&&(*phase=="review"||*phase=="confirm"))verb=*phase+(grant?"_add":"_remove");
        else if((grant||revoke)&&*phase=="cancel")verb="cancel";
        if(verb.empty())message="Unknown raid manager action.";
        else{
            okay=manager_request(roster,available,roster.revision?std::optional(roster.revision):std::nullopt,
                                 verb,*selection,true,*review_token,message);
            if(okay){
                if(verb=="list")message="Manager list requested. Read the server reply in chat.";
                else if(verb=="cancel")message="Delegation review cancelled.";
                else if(*phase=="review"&&stock_manager_review_){
                    returned_token=stock_manager_review_->token;
                    message=(grant?"Grant raid management to ":"Revoke raid management from ")+stock_manager_review_->name+
                        "? Only the actual raid leader can do this. Bastion checks membership and excludes bots and the raid leader.";
                }else message="Request sent. Check server chat for confirmation.";
            }
        }
    }
    result.begin();integer(*host_,out,"ok",okay);string(*host_,out,"message",message);
    if(!returned_token.empty())string(*host_,out,"review_token",returned_token);
    result.end();return 1;
}
int Adapter::ui(const HailstoneArgs* args,HailstoneOut* out){
    if(!HAILSTONE_HOST_HAS(host_,out_end)||!HAILSTONE_HOST_HAS(host_,out_begin_array)||!HAILSTONE_HOST_HAS(host_,arg))return 0;
    const auto arg=[&](std::size_t at){HailstoneValue v{};host_->arg(host_->host,args,at,&v);return v;};
    const auto op=arg(0);if(op.kind!=HAILSTONE_STRING||!op.s)return 0;
    const auto operation=bounded(op.s,16);
    if(operation=="status"){
        HailstonePlayer player{};player.size=sizeof(player);
        const bool available=selected_&&HAILSTONE_HOST_HAS(host_,get_player)&&HAILSTONE_HOST_HAS(host_,get_raid_roster)&&
            HAILSTONE_HOST_HAS(host_,get_raid_member)&&host_->get_player(host_->host,&player)&&
            player.size>=offsetof(HailstonePlayer,in_zone)+sizeof(player.in_zone)&&player.in_zone&&
            player.game_state==GAMESTATE_INGAME;
        if(available)load_settings();
        else { manager_review_.reset();stock_manager_review_.reset(); }
        host_->out_begin_table(host_->host,out);
        integer(*host_,out,"available",available);integer(*host_,out,"in_zone",available&&in_zone_);
        integer(*host_,out,"in_raid",available&&player.in_raid);
        integer(*host_,out,"manager_supported",available&&manager_commands_available(host_));
        host_->out_key(host_->host,out,"settings");host_->out_begin_table(host_->host,out);
        integer(*host_,out,"enabled",settings_.get("enabled"));host_->out_end(host_->host,out);
        host_->out_end(host_->host,out);return 1;
    }
    HailstoneRaidRoster roster{};std::vector<Row> rows;
    const bool available=sample(roster,rows);
    const bool manager_supported=manager_commands_available(host_);
    retire_manager_reviews(roster,available);
    if(operation=="state"){
        host_->out_begin_table(host_->host,out);integer(*host_,out,"available",available);string(*host_,out,"error",error_);
        string(*host_,out,"roster_revision",std::to_string(roster.revision));
        integer(*host_,out,"in_zone",in_zone_&&available);
        integer(*host_,out,"in_raid",roster.count>0);
        integer(*host_,out,"manage_supported",HAILSTONE_HOST_HAS(host_,raid_action)||HAILSTONE_HOST_HAS(host_,do_command));
        integer(*host_,out,"can_manage",local_leader_);
        integer(*host_,out,"manager_supported",manager_supported);
        host_->out_key(host_->host,out,"roster_members");host_->out_begin_array(host_->host,out);
        if(available)for(const auto& row:roster_members_){
            host_->out_begin_table(host_->host,out);string(*host_,out,"name",row.name);integer(*host_,out,"self",row.self);host_->out_end(host_->host,out);
        }
        host_->out_end(host_->host,out);
        host_->out_key(host_->host,out,"manager_review");host_->out_begin_table(host_->host,out);
        integer(*host_,out,"active",manager_review_.has_value());
        if(manager_review_){
            string(*host_,out,"name",manager_review_->name);string(*host_,out,"action",manager_review_->action);
            string(*host_,out,"roster_revision",std::to_string(manager_review_->revision));
        }
        host_->out_end(host_->host,out);
        host_->out_key(host_->host,out,"group_counts");host_->out_begin_array(host_->host,out);
        for(const auto count:group_counts_){HailstoneValue v{};v.kind=HAILSTONE_INT;v.i=count;host_->out_value(host_->host,out,&v);}
        host_->out_end(host_->host,out);
        host_->out_key(host_->host,out,"settings");host_->out_begin_table(host_->host,out);
        for(const auto& [key,value]:settings_.values)integer(*host_,out,key.c_str(),value);
        string(*host_,out,"priority",settings_.priority_text());host_->out_end(host_->host,out);
        host_->out_key(host_->host,out,"rows");host_->out_begin_array(host_->host,out);
        if(available)for(const auto& row:rows)row_table(*host_,out,row);
        host_->out_end(host_->host,out);host_->out_end(host_->host,out);return 1;
    }
    bool okay=false;std::string request_error;
    if(available&&operation=="set"){
        const auto key=arg(1),value=arg(2);const auto previous=settings_;
        if(key.kind==HAILSTONE_STRING&&key.s){
            const auto name=bounded(key.s,40);
            if(name=="priority"&&value.kind==HAILSTONE_STRING&&value.s)okay=settings_.set_priority(bounded(value.s,129));
            else if(value.kind==HAILSTONE_INT&&value.i>=0&&value.i<=std::numeric_limits<int>::max())okay=settings_.set(name,static_cast<int>(value.i));
            if(okay&&value.kind==HAILSTONE_INT&&value.i==1&&(name=="manage"||name=="edit"))settings_.set("enabled",1);
            if(okay&&value.kind==HAILSTONE_INT&&value.i==1&&name=="manage"){
                settings_.set("group_sort",1);settings_.set("show_all",1);settings_.set("filter_groups",0);
                settings_.set("clickable",1);settings_.set("clickthrough",0);
            }
            if(okay&&!save_settings()){settings_=previous;okay=false;}
        }
    }else if(available&&operation=="select"&&settings_.get("clickable")&&!settings_.get("clickthrough")&&HAILSTONE_HOST_HAS(host_,select_raid_member)){
        const auto rev=arg(1),id=arg(2),identity=arg(3);
        const auto expected=rev.kind==HAILSTONE_STRING&&rev.s?token(bounded(rev.s,21)):std::nullopt;
        const auto incarnation=identity.kind==HAILSTONE_STRING&&identity.s?token(bounded(identity.s,21)):std::nullopt;
        if(expected&&*expected==roster.revision&&incarnation&&id.kind==HAILSTONE_INT&&id.i>0&&id.i<=0xffffffffll){
            const auto exact=[&](const Row& r){return r.spawn_id==static_cast<std::uint32_t>(id.i)&&r.incarnation==*incarnation;};
            const bool found=std::any_of(rows.begin(),rows.end(),[&](const Row& r){return exact(r)||std::any_of(r.pets.begin(),r.pets.end(),exact);});
            if(found)okay=select(rows,roster,static_cast<std::uint32_t>(id.i),*incarnation);
        }
    }else if(operation=="manager"){
        const auto rev=arg(1),action=arg(2),name=arg(3);
        const auto verb=action.kind==HAILSTONE_STRING&&action.s?bounded(action.s,20):std::string{};
        const auto expected=rev.kind==HAILSTONE_STRING&&rev.s?token(bounded(rev.s,21)):std::nullopt;
        const auto target=name.kind==HAILSTONE_STRING&&name.s?bounded(name.s,65):std::string{};
        okay=manager_request(roster,available,expected,verb,target,false,{},request_error);
    }else if(available&&operation=="action"&&settings_.get("manage")){
        const auto rev=arg(1),name=arg(2),action=arg(3),group=arg(4);
        const auto expected=rev.kind==HAILSTONE_STRING&&rev.s?token(bounded(rev.s,21)):std::nullopt;
        if(expected&&*expected==roster.revision&&name.kind==HAILSTONE_STRING&&name.s&&
           action.kind==HAILSTONE_INT&&(action.i==1||action.i==2)&&group.kind==HAILSTONE_INT&&group.i>=0&&group.i<=12){
            const auto member=bounded(name.s,65);
            const auto current=std::find_if(rows.begin(),rows.end(),[&](const Row& row){return !row.pet&&row.name==member;});
            const auto destination=static_cast<std::uint32_t>(group.i);
            const bool room=action.i!=1||destination==0||group_counts_[destination]<6||
                            (current!=rows.end()&&current->group==destination);
            if(member.size()<64&&current!=rows.end()&&room){
                if(HAILSTONE_HOST_HAS(host_,raid_action))
                    okay=host_->raid_action(host_->host,*expected,member.c_str(),static_cast<std::uint32_t>(action.i),destination)!=0;
                // Bastion also authorizes delegated managers server-side (#raidmanager).
                // An explicit fallback preserves Citadel's requests without inventing a local permission bit.
                // Success here means requested, never server acceptance.
                if(!okay&&character_name(member)&&HAILSTONE_HOST_HAS(host_,do_command)){
                    const auto command=action.i==2?"/say #raidpromote "+member:
                                       "/say #raidmove "+member+" "+std::to_string(destination);
                    host_->do_command(host_->host,command.c_str());okay=true;
                }
            }
        }
    }
    host_->out_begin_table(host_->host,out);integer(*host_,out,"ok",okay);string(*host_,out,"error",okay?"":!request_error.empty()?request_error:error_.empty()?(operation=="action"?"Raid request refused. Check leadership, raid lock, group capacity and current membership.":"The roster, settings or selected actor changed."):error_);host_->out_end(host_->host,out);return 1;
}
bool Adapter::command(std::string_view args) {
    while(!args.empty()&&args.front()==' ')args.remove_prefix(1);
    while(!args.empty()&&args.back()==' ')args.remove_suffix(1);
    const auto say=[&](const std::string& message){if(HAILSTONE_HOST_HAS(host_,write_chat))host_->write_chat(host_->host,message.c_str(),0);};
    if(args.empty()||args=="help") {
        say("/raidbars on|off|toggle; edit|manage|groups|showall|pets|combine|clickable|clickthrough on|off|toggle");
        say("/raidbars position left top [right bottom]; grid rows columns; barwidth|barheight|petwidth|petheight|background|threshold value");
        say("/raidbars always|never|filter|priority <class codes>; status; set <setting> <integer>");
        return true;
    }
    HailstoneRaidRoster roster{};std::vector<Row> rows;if(!sample(roster,rows))return false;
    if(args=="status"){
        say(std::string("Raid bars ")+(settings_.get("enabled")?"on":"off")+", "+std::to_string(roster.count)+" raid members, "+std::to_string(rows.size())+" displayed rows; unknown resources remain unavailable.");return true;
    }
    if(args=="font"||args.starts_with("font ")){say("Raid bars use the active Hailstone UI font and scale.");return true;}
    const auto previous=settings_;bool okay=false;
    if(args=="on"||args=="off"||args=="toggle")okay=settings_.set("enabled",args=="toggle"?!settings_.get("enabled"):args=="on"?1:0);
    else if(args.starts_with("priority "))okay=settings_.set_priority(args.substr(9));
    else if(args.starts_with("set ")) {
        args.remove_prefix(4);const auto split=args.find(' ');
        if(split!=std::string_view::npos){const auto number=args.substr(split+1);int value=0;
            const auto parsed=std::from_chars(number.data(),number.data()+number.size(),value);
            if(parsed.ec==std::errc{}&&parsed.ptr==number.data()+number.size())okay=settings_.set(args.substr(0,split),value);}
    }else{
        std::istringstream input{std::string(args)};std::string key,value,extra;input>>key;
        const std::map<std::string,std::string,std::less<>> aliases{{"groups","group_sort"},{"showall","show_all"},{"pets","show_pets"},{"combine","combine_pets"},{"barwidth","bar_width"},{"barheight","bar_height"},{"petwidth","pet_width"},{"petheight","pet_height"}};
        if(const auto it=aliases.find(key);it!=aliases.end())key=it->second;
        if(key=="position"||key=="grid"){
            int a=0,b=0,c=0,d=0;
            if(input>>a>>b){
                if(key=="grid")okay=!(input>>extra)&&settings_.set("rows",a)&&settings_.set("columns",b);
                else{
                    okay=settings_.set("left",a)&&settings_.set("top",b);
                    if(input>>extra){
                        const auto read=std::from_chars(extra.data(),extra.data()+extra.size(),c);
                        okay=okay&&read.ec==std::errc{}&&read.ptr==extra.data()+extra.size()&&bool(input>>d)&&!(input>>extra)&&c>a&&d>b&&settings_.set("right",c)&&settings_.set("bottom",d);
                    }else if(okay){settings_.set("right",0);settings_.set("bottom",0);}
                }
            }
        }else if(key=="always"||key=="never"||key=="filter"){
            std::string names;std::getline(input,names);Settings parsed;
            if(names==" none"||names==" clear")names.clear();
            okay=parsed.set_priority(names);unsigned mask=0;
            if(okay){for(auto id:parsed.priority)mask|=1u<<(id-1);okay=settings_.set(key,static_cast<int>(mask));}
        }else if(input>>value&&!(input>>extra)){
            const auto definition=std::find_if(settings_definitions().begin(),settings_definitions().end(),[&](const Setting& v){return key==v.key;});
            if(definition!=settings_definitions().end()){
                int number=0;const auto read=std::from_chars(value.data(),value.data()+value.size(),number);
                if(definition->maximum==1&&(value=="on"||value=="off"||value=="toggle")){
                    number=value=="toggle"?!settings_.get(key):value=="on"?1:0;okay=settings_.set(key,number);
                }else if(read.ec==std::errc{}&&read.ptr==value.data()+value.size())okay=settings_.set(key,number);
            }
        }
        if(okay&&(key=="manage"||key=="edit")&&settings_.get(key))settings_.set("enabled",1);
        if(okay&&key=="manage"&&settings_.get(key)){
            settings_.set("group_sort",1);settings_.set("show_all",1);settings_.set("filter_groups",0);
            settings_.set("clickable",1);settings_.set("clickthrough",0);
        }
    }
    if(!okay){settings_=previous;say("Raid bar command was not understood. Use /raidbars help.");return false;}
    if(!save_settings()){settings_=previous;say(error_);return false;}
    return true;
}
} // namespace bastion::raidbars
