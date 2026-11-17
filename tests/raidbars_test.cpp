#include "raidbars.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>

struct HailstoneArgs { std::vector<HailstoneValue> values; };
struct Node { std::string text;std::int64_t integer=0;std::map<std::string,Node> fields;std::vector<Node> array;bool is_array=false; };
struct HailstoneOut { Node root;std::vector<Node*> stack;std::string key; };
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
Node& next(HailstoneOut& out){
    if(out.stack.empty())return out.root;
    auto& parent=*out.stack.back();if(parent.is_array){parent.array.emplace_back();return parent.array.back();}
    return parent.fields[std::exchange(out.key,{})];
}
HailstoneValue text(const char* value){HailstoneValue out{};out.kind=HAILSTONE_STRING;out.s=value;return out;}
HailstoneValue number(std::int64_t value){HailstoneValue out{};out.kind=HAILSTONE_INT;out.i=value;return out;}
struct Host {
    PluginHostApi api{};HailstonePlayer player{};std::vector<HailstoneRaidMember> members;std::map<std::uint32_t,HailstoneSpawn> actors;
    std::uint64_t revision=9007199254740993ull,owner_incarnation=10;unsigned selected=0,moved=0;std::uint32_t selected_id=0;bool active=true,native_moves=true;std::vector<std::string> commands;
    std::uint32_t capabilities=HAILSTONE_CAP_READ|HAILSTONE_CAP_CHAT;
    HailstoneUiFn function=nullptr;void* user=nullptr;
    HailstoneUiFn stock_function=nullptr;void* stock_user=nullptr;
    bool accept_stock_function=true;
    bool directory_available=true,server_available=true;
    std::uint64_t context=71;
    unsigned roster_reads=0,member_reads=0,spawn_reads=0,walk_reads=0,directory_reads=0;
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("hailstone-raidbars-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path cleanup_root=path;
    Host(){
        std::filesystem::create_directories(path);api.size=sizeof(api);api.host=this;
        api.caps=[](void* h){return static_cast<Host*>(h)->capabilities;};
        player.size=sizeof(player);player.spawn_id=1;player.in_zone=1;player.game_state=GAMESTATE_INGAME;
        for(std::uint32_t id=1;id<=3;++id){
            HailstoneRaidMember member{};member.size=sizeof(member);member.group_id=1;member.class_id=id;member.spawn_id=id==3?0:id;
            member.incarnation=id==3?0:id*10;member.flags=id==1?HAILSTONE_RAID_LEADER:0;const std::string label=id==1?"Self":id==2?"Healer":"Off-zone member";std::memcpy(member.name,label.c_str(),label.size()+1);members.push_back(member);
        }
        HailstoneSpawn actor{};actor.size=sizeof(actor);actor.id=2;actor.incarnation=20;actor.hp_percent=0;
        actor.observation_flags=HAILSTONE_SPAWN_HP_KNOWN;actors.emplace(2,actor);
        api.get_player=[](void* h,HailstonePlayer* out){*out=static_cast<Host*>(h)->player;return 1;};
        api.get_raid_roster=[](void* h,HailstoneRaidRoster* out){auto& fixture=*static_cast<Host*>(h);++fixture.roster_reads;if(!fixture.active)return 0;out->size=sizeof(*out);out->revision=fixture.revision;out->owner_incarnation=fixture.owner_incarnation;out->count=static_cast<std::uint32_t>(fixture.members.size());return 1;};
        api.get_raid_member=[](void* h,std::uint64_t rev,std::uint32_t i,HailstoneRaidMember* out){auto& fixture=*static_cast<Host*>(h);++fixture.member_reads;if(rev!=fixture.revision||i>=fixture.members.size())return 0;*out=fixture.members[i];return 1;};
        api.get_spawn=[](void* h,std::uint32_t id,HailstoneSpawn* out){auto& fixture=*static_cast<Host*>(h);++fixture.spawn_reads;auto& received=fixture.actors;const auto it=received.find(id);if(it==received.end())return 0;*out=it->second;return 1;};
        api.next_spawn=[](void* h,std::uint32_t after,HailstoneSpawn* out){auto& fixture=*static_cast<Host*>(h);++fixture.walk_reads;auto& received=fixture.actors;const auto it=received.upper_bound(after);if(it==received.end())return 0;*out=it->second;return 1;};
        api.select_raid_member=[](void* h,std::uint64_t rev,std::uint32_t id,std::uint64_t incarnation){auto& fixture=*static_cast<Host*>(h);const auto found=fixture.actors.find(id);if(rev!=fixture.revision||found==fixture.actors.end()||incarnation!=found->second.incarnation)return 0;++fixture.selected;fixture.selected_id=id;fixture.player.target_id=id;return 1;};
        api.raid_action=[](void* h,std::uint64_t rev,const char*,std::uint32_t action,std::uint32_t group){auto& fixture=*static_cast<Host*>(h);if(rev!=fixture.revision||action!=1||group>12||!fixture.native_moves)return 0;++fixture.moved;return 1;};
        api.do_command=[](void* h,const char* line){static_cast<Host*>(h)->commands.emplace_back(line);};
        api.add_ui_function=[](void* h,const char* name,HailstoneUiFn fn,void* callback_user){auto& fixture=*static_cast<Host*>(h);
            if(std::string_view(name)=="raidbars"){fixture.function=fn;fixture.user=callback_user;}
            else{check(std::string_view(name)=="raid_manager_action","wrong UI function");if(!fixture.accept_stock_function)return 0;fixture.stock_function=fn;fixture.stock_user=callback_user;}return 1;};
        api.remove_ui_function=[](void* h,const char* name){auto& fixture=*static_cast<Host*>(h);
            if(std::string_view(name)=="raidbars")fixture.function=nullptr;else fixture.stock_function=nullptr;};
        api.arg=[](void*,const HailstoneArgs* args,std::size_t i,HailstoneValue* out){if(i>=args->values.size())return 0;*out=args->values[i];return 1;};
        api.out_begin_table=[](void*,HailstoneOut* out){out->stack.push_back(&next(*out));};
        api.out_begin_array=[](void*,HailstoneOut* out){auto& node=next(*out);node.is_array=true;out->stack.push_back(&node);};
        api.out_key=[](void*,HailstoneOut* out,const char* key){out->key=key;};
        api.out_value=[](void*,HailstoneOut* out,const HailstoneValue* value){auto& node=next(*out);node.integer=value->i;if(value->kind==HAILSTONE_STRING)node.text=value->s;};
        api.out_end=[](void*,HailstoneOut* out){out->stack.pop_back();};
        api.character_config_dir=[](void* h,char* out,std::size_t size){auto& fixture=*static_cast<Host*>(h);++fixture.directory_reads;if(!fixture.directory_available)return 0;const auto p=fixture.path.u8string();if(p.size()>=size)return 0;std::memcpy(out,p.c_str(),p.size()+1);return static_cast<int>(p.size());};
        api.server_info=[](void* h,HailstoneServer* out){if(!static_cast<Host*>(h)->server_available)return 0;*out={};out->size=sizeof(*out);return 1;};
        api.get_map_policy=[](void* h,HailstoneMapPolicy* out){*out={};out->size=sizeof(*out);out->context=static_cast<Host*>(h)->context;return 1;};
    }
    ~Host(){std::error_code error;std::filesystem::remove_all(cleanup_root,error);}
    Node call(std::initializer_list<HailstoneValue> values){HailstoneArgs args{values};HailstoneOut out;check(function&&function(user,&args,&out)==1,"missing UI result");return out.root;}
    Node stock(const char* action,const char* phase,const char* name="",const char* review_token="",const char* window="RaidWindow"){
        const auto id="bastion.manager."+std::string(action);HailstoneArgs args{{text(window),text(id.c_str()),text(phase),text(name),text(review_token)}};
        HailstoneOut out;check(stock_function&&stock_function(stock_user,&args,&out)==1,"missing stock action result");return out.root;
    }
};
std::string contents(const std::filesystem::path& file){
    std::ifstream input(file);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void settings_directory_lifetime(){
    Host fixture;fixture.path=fixture.cleanup_root/"profile-a";
    bastion::raidbars::Adapter adapter;
    adapter.set_host(&fixture.api);adapter.initialize();adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    const auto set=[&](std::int64_t threshold){return fixture.call({text("set"),text("threshold"),number(threshold)}).fields.at("ok").integer==1;};
    check(set(47),"profile A settings could not be primed");
    const auto first_file=fixture.path/"raidbars.cfg";
    const auto first=contents(first_file);
    check(!first.empty()&&first.find("threshold=47\n")!=std::string::npos,"profile A settings fixture missing");
    // All public names and actor/zone fields stay identical; only the opaque host context changes.
    fixture.path=fixture.cleanup_root/"profile-b";++fixture.context;
    fixture.directory_available=false;
    check(!set(52)&&contents(first_file)==first&&!std::filesystem::exists(fixture.path/"raidbars.cfg"),
          "failed profile B path resolution wrote profile A settings");
    fixture.directory_available=true;fixture.server_available=false;
    check(!set(53)&&contents(first_file)==first,"missing server scope reused profile A's writable path");
    fixture.server_available=true;
    check(set(62)&&contents(first_file)==first&&contents(fixture.path/"raidbars.cfg").find("threshold=62\n")!=std::string::npos,
          "profile B did not recover its own settings path");
    const auto second=contents(fixture.path/"raidbars.cfg");
    fixture.context=0;
    check(!set(64)&&contents(first_file)==first&&contents(fixture.path/"raidbars.cfg")==second,
          "zero host context reused a previously writable raid settings path");
    fixture.context=73;
    check(fixture.call({text("state")}).fields.at("settings").fields.at("threshold").integer==62,
          "restored context did not reload profile B settings");
    auto reads=fixture.directory_reads;
    adapter.begin_zone();adapter.game_state(GAMESTATE_INGAME);fixture.call({text("status")});
    check(fixture.directory_reads==reads+1,"zone lifecycle reused an old cached path without querying the host");
    reads=fixture.directory_reads;adapter.peer(false);adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    check(fixture.call({text("state")}).fields.at("settings").fields.at("threshold").integer==62&&fixture.directory_reads==reads+1,
          "peer lifecycle did not retire the path and reload current settings");
    reads=fixture.directory_reads;adapter.game_state(GAMESTATE_CHARSELECT);adapter.game_state(GAMESTATE_INGAME);
    fixture.call({text("status")});
    check(fixture.directory_reads==reads+1,"game-state lifecycle retained the previous path cache");
    reads=fixture.directory_reads;adapter.set_host(&fixture.api);adapter.initialize();adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    fixture.call({text("status")});
    check(fixture.directory_reads==reads+1,"same-pointer host rebinding retained the previous path cache");
    // Removal of this test-owned directory must not turn a valid cached path into a permanent write failure.
    std::filesystem::remove_all(fixture.path);
    check(set(63)&&contents(first_file)==first&&contents(fixture.path/"raidbars.cfg").find("threshold=63\n")!=std::string::npos,
          "explicit save did not recreate a removed profile directory");
    adapter.shutdown();
}
void status_polling(){
    Host fixture;bastion::raidbars::Adapter adapter;
    fixture.player.in_raid=1;
    adapter.set_host(&fixture.api);adapter.initialize();adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    for(unsigned i=0;i<120;++i){
        const auto state=fixture.call({text("status")});
        check(state.fields.at("available").integer==1&&state.fields.at("in_raid").integer==1,"light status lost current raid eligibility");
        check(!state.fields.contains("rows")&&!state.fields.contains("roster_members"),"light status serialized a hidden raid snapshot");
    }
    check(fixture.roster_reads==0&&fixture.member_reads==0&&fixture.spawn_reads==0&&fixture.walk_reads==0,
          "hidden status sampled raid members, resources or pets");
    check(fixture.directory_reads==1&&fixture.commands.empty(),"unchanged hidden status repeated directory work or sent commands");
    const auto state=fixture.call({text("state")});
    check(state.fields.at("roster_members").array.size()==3&&fixture.member_reads==3,"full state did not retrieve current members on demand");
    fixture.player.in_zone=0;
    check(fixture.call({text("status")}).fields.at("available").integer==0,"zoning retained light status admission");
    adapter.peer(false);
    check(fixture.call({text("status")}).fields.at("available").integer==0,"peer retirement retained light status admission");
}
void manager_requests(){
    Host fixture;bastion::raidbars::Adapter adapter;
    adapter.set_host(&fixture.api);adapter.initialize();adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    const auto request=[&](const char* action,const char* name=""){
        const auto revision=std::to_string(fixture.revision);
        return fixture.call({text("manager"),text(revision.c_str()),text(action),text(name)});
    };
    const auto review=[&]{return fixture.call({text("state")}).fields.at("manager_review");};
    auto state=fixture.call({text("state")});
    check(state.fields.at("manager_supported").integer==1&&state.fields.at("roster_members").array.size()==3,
          "delegation did not expose the full current roster and command capability");
    check(fixture.commands.empty()&&review().fields.at("active").integer==0,"opening delegation sent an unsolicited request");
    check(request("list").fields.at("ok").integer==1&&fixture.commands.back()=="/say #raidmanager list",
          "explicit list did not use the inspected server contract outside mutation mode");
    check(request("review_add","Healer").fields.at("ok").integer==0,"delegation reviewed while management was disabled");
    check(adapter.command("manage on"),"delegation management mode unavailable");
    auto sent=fixture.commands.size();
    check(request("confirm_add","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "grant reached the server without explicit review");
    check(request("review_add","hEaLeR").fields.at("ok").integer==1&&fixture.commands.size()==sent,
          "review sent a command or rejected a current case-folded character");
    auto pending=review();
    check(pending.fields.at("active").integer==1&&pending.fields.at("name").text=="Healer"&&
          pending.fields.at("action").text=="add"&&pending.fields.at("roster_revision").text==std::to_string(fixture.revision),
          "review lost the exact canonical character, action or 64-bit revision");
    check(request("confirm_add","HEALER").fields.at("ok").integer==1&&fixture.commands.back()=="/say #raidmanager add Healer",
          "confirmed grant did not preserve the current roster's character case");
    sent=fixture.commands.size();
    check(review().fields.at("active").integer==0&&fixture.members[1].flags==0,"grant invented a local manager status");
    check(request("confirm_add","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,"grant confirmation was replayable");
    check(request("review_remove","Healer").fields.at("ok").integer==1,"revoke review failed");
    check(request("confirm_add","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,"revoke review authorized a different grant action");
    check(review().fields.at("active").integer==0,"mismatched confirmation retained a reusable review");
    request("review_remove","Healer");check(request("cancel").fields.at("ok").integer==1,"review cancellation failed");
    check(request("confirm_remove","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,"cancelled review revoked a manager");
    request("review_remove","Healer");
    check(request("confirm_remove","healer").fields.at("ok").integer==1&&fixture.commands.back()=="/say #raidmanager remove Healer",
          "confirmed revoke did not use explicit remove syntax");
    sent=fixture.commands.size();
    check(request("review_add","Healer\n/say injected").fields.at("ok").integer==0&&
          request("review_add","Outsider").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "invalid or nonmember target reached a delegation command");
    check(adapter.command("groups off")&&adapter.command("never CLR PAL"),"delegation filter fixture unavailable");
    std::fill(std::begin(fixture.members[2].name),std::end(fixture.members[2].name),'\0');
    std::memcpy(fixture.members[2].name,"ReLaYn",7);++fixture.revision;
    state=fixture.call({text("state")});
    check(state.fields.at("rows").array.empty()&&state.fields.at("roster_members").array.size()==3,
          "overlay filters removed delegation targets from the full roster");
    check(request("review_add","relayn").fields.at("ok").integer==1&&
          request("confirm_add","ReLaYn").fields.at("ok").integer==1&&fixture.commands.back()=="/say #raidmanager add ReLaYn",
          "filtered off-zone member could not be explicitly delegated with preserved case");
    sent=fixture.commands.size();request("review_add","Healer");++fixture.revision;
    check(request("confirm_add","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "a changed roster retained a grant review");
    request("review_remove","Healer");++fixture.owner_incarnation;
    check(request("confirm_remove","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "a replacement raid owner retained a delegation review");
    request("review_add","Healer");fixture.members.erase(fixture.members.begin()+1);
    check(request("confirm_add","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "a departed member retained a delegation review even without a new revision");
    request("review_add","ReLaYn");adapter.begin_zone();
    check(request("confirm_add","ReLaYn").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "zoning retained a delegation review");
    request("review_add","ReLaYn");fixture.active=false;
    check(request("list").fields.at("ok").integer==0&&review().fields.at("active").integer==0&&fixture.commands.size()==sent,
          "leaving the raid retained delegation or sent a list request");
    fixture.active=true;fixture.members[0].flags=0;
    check(request("list").fields.at("ok").integer==1,
          "an unreliable client leader flag replaced the actual server permission check");
    sent=fixture.commands.size();request("review_remove","ReLaYn");
    const auto command=fixture.api.do_command;fixture.api.do_command=nullptr;
    check(request("confirm_remove","ReLaYn").fields.at("ok").integer==0&&
          fixture.call({text("state")}).fields.at("manager_supported").integer==0&&fixture.commands.size()==sent,
          "missing command capability admitted a delegation");
    fixture.api.do_command=command;
    request("review_add","ReLaYn");fixture.capabilities&=~HAILSTONE_CAP_CHAT;
    check(request("confirm_add","ReLaYn").fields.at("ok").integer==0&&
          fixture.call({text("state")}).fields.at("manager_supported").integer==0&&
          review().fields.at("active").integer==0&&fixture.commands.size()==sent,
          "revoked chat permission admitted or retained a delegation review");
    check(request("list").fields.at("ok").integer==0&&request("review_add","ReLaYn").fields.at("ok").integer==0&&
          fixture.commands.size()==sent,"revoked chat permission admitted list or review");
    fixture.capabilities|=HAILSTONE_CAP_CHAT;
    check(request("confirm_add","ReLaYn").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "restored chat permission revived a discarded review");
    request("review_remove","ReLaYn");const auto caps=fixture.api.caps;fixture.api.caps=nullptr;
    check(request("confirm_remove","ReLaYn").fields.at("ok").integer==0&&
          fixture.call({text("state")}).fields.at("manager_supported").integer==0&&
          review().fields.at("active").integer==0&&fixture.commands.size()==sent,
          "missing live capability API admitted or retained delegation");
    fixture.api.caps=caps;request("review_add","ReLaYn");adapter.command("manage off");
    check(request("confirm_add","ReLaYn").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "disabling management retained a delegation review");
}
void stock_manager_requests(){
    Host fixture;bastion::raidbars::Adapter adapter;adapter.set_host(&fixture.api);adapter.initialize();
    check(fixture.stock_function&&fixture.stock("list","invoke").fields.at("ok").integer==0&&fixture.commands.empty(),
          "stock delegation admitted a request before Bastion selection");
    adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    const auto initial=fixture.call({text("state")}).fields.at("settings");
    check(fixture.stock("list","invoke").fields.at("ok").integer==1&&fixture.commands.back()=="/say #raidmanager list",
          "stock List required Raid Bars management or lost the server contract");
    auto reviewed=fixture.stock("grant","review","hEaLeR");
    check(reviewed.fields.at("ok").integer==1&&!reviewed.fields.at("review_token").text.empty()&&
          reviewed.fields.at("message").text.find("Grant raid management to Healer?")!=std::string::npos,
          "stock review lost canonical character, concrete confirmation or opaque token");
    auto review_token=reviewed.fields.at("review_token").text;auto sent=fixture.commands.size();
    const auto settings=fixture.call({text("state")}).fields.at("settings");
    check(settings.fields.at("enabled").integer==initial.fields.at("enabled").integer&&settings.fields.at("manage").integer==0,
          "stock delegation enabled or required the Raid Bars overlay");
    check(fixture.stock("grant","invoke","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock Grant bypassed the review phase");
    check(fixture.stock("grant","confirm","Healer","").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock Grant confirmed without its one-use review token");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==1&&
          fixture.commands.back()=="/say #raidmanager add Healer","stock confirmation required Raid Bars management");
    const auto after_stock=fixture.call({text("state")}).fields.at("settings");
    check(after_stock.fields.at("manage").integer==0&&after_stock.fields.at("enabled").integer==initial.fields.at("enabled").integer,
          "stock confirmation changed Raid Bars management or overlay visibility");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;
    check(adapter.command("manage on"),"independent review fixture unavailable");
    const auto revision=std::to_string(fixture.revision);
    sent=fixture.commands.size();
    check(fixture.call({text("manager"),text(revision.c_str()),text("confirm_add"),text("Healer")}).fields.at("ok").integer==0&&
          fixture.commands.size()==sent,"a stock review authorized Raid Bars confirmation");
    fixture.call({text("manager"),text(revision.c_str()),text("review_remove"),text("Healer")});
    check(fixture.stock("grant","confirm","HEALER",review_token.c_str()).fields.at("ok").integer==1&&
          fixture.commands.back()=="/say #raidmanager add Healer","stock confirmed grant lost canonical case");
    check(fixture.call({text("state")}).fields.at("manager_review").fields.at("active").integer==1,
          "stock confirmation consumed the Raid Bars review");
    check(fixture.call({text("manager"),text(revision.c_str()),text("confirm_remove"),text("Healer")}).fields.at("ok").integer==1&&
          fixture.commands.back()=="/say #raidmanager remove Healer","Raid Bars review was not independently confirmable");
    sent=fixture.commands.size();
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock confirmation token was replayable");
    reviewed=fixture.stock("revoke","review","Healer");review_token=reviewed.fields.at("review_token").text;
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock revoke review authorized a grant");
    check(fixture.stock("revoke","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0,
          "mismatched stock confirmation retained a reusable review");
    reviewed=fixture.stock("revoke","review","Healer");review_token=reviewed.fields.at("review_token").text;
    check(fixture.stock("revoke","cancel","Healer",review_token.c_str()).fields.at("ok").integer==1&&
          fixture.stock("revoke","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "cancelled stock review sent a revoke");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;
    check(fixture.stock("grant","cancel","Healer","other-token").fields.at("ok").integer==0&&
          fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==1,
          "a stale cancellation consumed another stock review");
    sent=fixture.commands.size();
    check(fixture.stock("grant","review","Healer\n/say injected").fields.at("ok").integer==0&&
          fixture.stock("grant","review","Outsider").fields.at("ok").integer==0&&
          fixture.stock("grant","review","Healer","","OtherWindow").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock delegation admitted injection, nonmember or wrong window");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;++fixture.revision;
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock confirmation crossed a changed raid revision");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;++fixture.owner_incarnation;
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock confirmation crossed a changed raid owner");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;fixture.capabilities&=~HAILSTONE_CAP_CHAT;
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&
          fixture.stock("list","invoke").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock action survived revoked chat permission");
    fixture.capabilities|=HAILSTONE_CAP_CHAT;
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "restoring chat revived a retired stock review");
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;adapter.begin_zone();
    check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock confirmation crossed zoning");
    check(fixture.call({text("state")}).fields.at("in_zone").integer==0,
          "stock frontend lifecycle retained an in-zone state after begin-zone");
    check(fixture.stock("grant","review","Healer").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "stock delegation reviewed against a stale player during zoning");adapter.game_state(GAMESTATE_INGAME);
    reviewed=fixture.stock("grant","review","Healer");review_token=reviewed.fields.at("review_token").text;adapter.peer(false);
    check(fixture.stock("list","invoke").fields.at("ok").integer==0&&fixture.commands.size()==sent,
          "peer retirement left stock actions active");
    adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);check(fixture.stock("grant","confirm","Healer",review_token.c_str()).fields.at("ok").integer==0,
          "peer reactivation revived a stock review");
    adapter.shutdown();check(!fixture.function&&!fixture.stock_function,"unload left stock action callbacks");
    Host failed;failed.accept_stock_function=false;bastion::raidbars::Adapter partial;partial.set_host(&failed.api);partial.initialize();partial.peer(true);
    check(failed.function&&!failed.stock_function&&failed.commands.empty(),"failed stock callback registration replaced the Raid Bars service");partial.shutdown();
}
}
int main(){try{
    using namespace bastion::raidbars;
    Settings config;check(!config.set("threshold",101)&&!config.set("rows",0),"invalid settings admitted");
    check(config.set_priority("CLR WAR CLR")&&config.priority_text()=="CLR WAR","class priority did not preserve first unique order");
    check(!config.set_priority("CLR BAD")&&config.priority_text()=="CLR WAR","malformed class priority was partially published");
    std::vector<Row> rows(3);rows[0].name="Unknown";rows[0].class_id=2;rows[1].name="Zero";rows[1].class_id=2;rows[1].health={true,0};rows[2].name="Half";rows[2].class_id=2;rows[2].health={true,50};
    config.set("group_sort",0);config.set("show_all",0);config.set("filter",2);config.set("threshold",50);
    auto visible=visible_rows(rows,config);check(visible.size()==1&&visible[0].name=="Zero","unknown health or threshold boundary satisfied low-health filter");
    config.set("always",2);visible=visible_rows(rows,config);check(visible.size()==3,"always class did not include unknown roster members");
    config.set("show_all",1);config.set("never",2);check(visible_rows(rows,config).empty(),"never-class filter lost priority over show-all");
    config.set("group_sort",1);check(visible_rows(rows,config).size()==3,"default group mode did not preserve a complete roster");
    config.set("filter_groups",1);check(visible_rows(rows,config).empty(),"explicit group filters ignored");
    config=Settings{};rows[0].flags=HAILSTONE_RAID_GROUP_LEADER;rows[0].class_id=16;rows[0].name="Leader";
    check(visible_rows(rows,config).front().name=="Leader","group leader was not placed first");
    Host fixture;Adapter adapter;adapter.set_host(&fixture.api);adapter.initialize();adapter.peer(true);adapter.game_state(GAMESTATE_INGAME);
    auto state=fixture.call({text("state")});auto& observed=state.fields["rows"].array;
    check(observed.size()==3,"complete group roster lost self or off-zone member");
    check(state.fields.contains("group_counts"),"group occupancy missing from the current raid state");
    const auto& occupancy=state.fields.at("group_counts").array;
    check(occupancy.size()==13,"group occupancy did not include all twelve groups and ungrouped");
    check(occupancy.at(1).integer==3,"actual group capacity did not include the whole roster");
    const auto known=std::find_if(observed.begin(),observed.end(),[](const Node& row){return row.fields.at("name").text=="Healer";});
    const auto unknown=std::find_if(observed.begin(),observed.end(),[](const Node& row){return row.fields.at("name").text=="Off-zone member";});
    check(known!=observed.end()&&known->fields.at("health_known").integer==1&&known->fields.at("health").integer==0,"known zero was lost");
    check(unknown!=observed.end()&&unknown->fields.at("health_known").integer==0&&unknown->fields.at("incarnation").text=="0","off-zone health became known zero");
    const auto revision=state.fields["roster_revision"].text;
    adapter.pulse();check(fixture.selected==0,"pulse selected a raid actor");
    check(fixture.call({text("select"),text(revision.c_str()),number(2),text("20")}).fields["ok"].integer==1&&fixture.selected==1,"current explicit selection failed");
    HailstoneSpawn pet{};pet.size=sizeof(pet);pet.id=8;pet.owner_id=2;pet.incarnation=80;pet.hp_percent=75;pet.observation_flags=HAILSTONE_SPAWN_HP_KNOWN;std::memcpy(pet.name,"Companion",10);fixture.actors.emplace(8,pet);
    check(adapter.command("pets on")&&adapter.command("combine on"),"legacy pet commands failed");
    check(fixture.call({text("select"),text(revision.c_str()),number(2),text("20")}).fields["ok"].integer==1&&fixture.selected_id==8,"repeated owner click did not toggle to pet");
    check(adapter.command("combine off"),"separate pet command failed");
    const auto separate=fixture.call({text("state")}).fields["rows"].array;
    const auto owner=std::find_if(separate.begin(),separate.end(),[](const Node& row){return row.fields.at("name").text=="Healer";});
    check(owner!=separate.end()&&std::next(owner)!=separate.end()&&std::next(owner)->fields.at("pet").integer==1,"separate pet was not adjacent to owner");
    check(adapter.command("manage on"),"management compatibility command failed");
    check(fixture.call({text("action"),text(revision.c_str()),text("Healer"),number(1),number(2)}).fields["ok"].integer==1&&fixture.moved==1,"explicit native subgroup move failed");
    check(fixture.call({text("action"),text(revision.c_str()),text("Healer"),number(2),number(1)}).fields["ok"].integer==1&&fixture.commands==std::vector<std::string>{"/say #raidpromote Healer"},"promotion request did not preserve validated Bastion command");
    fixture.members[0].flags=0;
    check(fixture.call({text("action"),text(revision.c_str()),text("Healer"),number(2),number(1)}).fields["ok"].integer==1&&fixture.commands.size()==2,"delegated promotion request was blocked by an invented local permission");
    fixture.native_moves=false;
    check(fixture.call({text("action"),text(revision.c_str()),text("Healer"),number(1),number(2)}).fields["ok"].integer==1&&fixture.commands.back()=="/say #raidmove Healer 2","delegated move did not fall back to Bastion's explicit server request");
    const auto requested=fixture.commands.size();
    check(fixture.call({text("action"),text(revision.c_str()),text("Healer\n/say injected"),number(1),number(2)}).fields["ok"].integer==0&&fixture.commands.size()==requested,"nonmember or command injection reached the server request");
    ++fixture.revision;fixture.actors.at(2).incarnation=21;fixture.members[1].incarnation=21;
    check(fixture.call({text("select"),text(revision.c_str()),number(2),text("20")}).fields["ok"].integer==0&&fixture.selected==2,"stale roster/actor selected replacement");
    check(fixture.call({text("action"),text(revision.c_str()),text("Healer"),number(1),number(2)}).fields["ok"].integer==0&&fixture.moved==1,"stale roster moved replacement");
    fixture.active=false;
    check(fixture.call({text("state")}).fields["available"].integer==1,"out-of-raid settings unavailable");
    check(adapter.command("grid 6 3")&&adapter.command("position 40 60 800 600")&&adapter.command("always WAR CLR")&&adapter.command("threshold 45")&&adapter.command("clickthrough toggle"),"legacy layout/filter commands failed outside raid");
    const auto persisted=fixture.call({text("state")}).fields["settings"];
    check(persisted.fields.at("rows").integer==6&&persisted.fields.at("right").integer==800&&persisted.fields.at("always").integer==3,"settings command changed wrong values");
    check(!adapter.command("position 40 60 20 80")&&!adapter.command("grid 6 0"),"invalid compound layout admitted");
    check(fixture.call({text("state")}).fields["settings"].fields["right"].integer==800,"invalid command partially changed settings");
    adapter.shutdown();adapter.initialize();adapter.peer(false);adapter.peer(true);
    check(fixture.call({text("state")}).fields["settings"].fields["threshold"].integer==45,"character preferences did not reload");
    adapter.peer(false);check(fixture.call({text("state")}).fields["available"].integer==0,"peer retirement retained roster admission");
    adapter.shutdown();manager_requests();stock_manager_requests();status_polling();settings_directory_lifetime();std::cout<<"Raid bars: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
