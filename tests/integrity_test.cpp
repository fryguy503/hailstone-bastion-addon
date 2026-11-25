// SPDX-License-Identifier: MIT
#include "integrity.h"
#include "citadel_test_host.h"
#include "../server/native_integrity.h"
#include <array>
#include <iostream>
#include <limits>

struct HailstoneArgs {};
struct HailstoneOut {
    std::string key;
    std::map<std::string,bool> booleans;
    std::map<std::string,std::string> strings;
};
namespace integrity=bastion::integrity;
namespace server=bastion_native_integrity;
using citadel_test::check;
namespace {
using Wire=std::vector<std::uint8_t>;
constexpr bool kNativeReportSupported = HAILSTONE_PLUGIN_ABI == integrity::native_report_abi;
void put(Wire& bytes,std::size_t at,std::uint64_t value,unsigned width=4) {
    for(unsigned n=0;n<width;++n) bytes.at(at+n)=static_cast<std::uint8_t>(value>>(n*8));
}
struct Rig:citadel_test::Host {
    integrity::Adapter addon;
    HailstoneBinaryMeasurement executable{},module{};
    std::vector<std::uint32_t> measured;
    std::function<void(std::uint32_t)> during_measure;
    unsigned fault=0;
    Rig() {
        api.abi=HAILSTONE_PLUGIN_ABI;
        api.arg=[](void*,const HailstoneArgs*,std::size_t index,HailstoneValue* out) {
            if(index!=0) return 0;
            *out={};out->kind=HAILSTONE_STRING;out->s="state";return 1;
        };
        api.out_begin_table=[](void*,HailstoneOut*) {};
        api.out_begin_array=[](void*,HailstoneOut*) {};
        api.out_end=[](void*,HailstoneOut*) {};
        api.out_key=[](void*,HailstoneOut* out,const char* key) {out->key=key;};
        api.out_value=[](void*,HailstoneOut* out,const HailstoneValue* value) {
            if(value->kind==HAILSTONE_BOOL) out->booleans[out->key]=value->i!=0;
            if(value->kind==HAILSTONE_STRING) out->strings[out->key]=value->s;
        };
        executable.size=module.size=sizeof(executable);
        executable.kind=HAILSTONE_BINARY_EXECUTABLE;module.kind=HAILSTONE_BINARY_CALLING_PLUGIN;
        executable.byte_count=0x112233;module.byte_count=0x445566;
        executable.fnv1a63=0x1122334455667788ull;module.fnv1a63=0x2233445566778899ull;
        for(std::size_t n=0;n<32;++n) {executable.sha256[n]=static_cast<std::uint8_t>(n+1);module.sha256[n]=static_cast<std::uint8_t>(n+65);}
        api.measure_binary=[](void* context,std::uint32_t kind,HailstoneBinaryMeasurement* out) -> int {
            auto& rig=*static_cast<Rig*>(static_cast<citadel_test::Host*>(context));
            check(out&&out->size==sizeof(*out),"binary measurement lost SDK output size");
            rig.measured.push_back(kind);
            if(rig.during_measure) rig.during_measure(kind);
            if(rig.fault==1) return 0;
            if(rig.fault==2) throw std::runtime_error("injected measurement failure");
            *out=kind==HAILSTONE_BINARY_EXECUTABLE?rig.executable:rig.module;
            if(rig.fault==3) out->kind=99;
            if(rig.fault==4) out->size=0;
            if(rig.fault==5) out->byte_count=0;
            if(rig.fault==6) out->fnv1a63=1ull<<63;
            if(rig.fault==7) std::fill(std::begin(out->sha256),std::end(out->sha256),std::uint8_t{0});
            if(rig.fault==8) out->byte_count=1024ull*1024*1024;
            if(rig.fault==9) out->fnv1a63=0;
            return 1;
        };
        api.send_packet=[](void* context,int stream,std::uint16_t opcode,const void* data,std::size_t count) -> int {
            auto& rig=*static_cast<Rig*>(static_cast<citadel_test::Host*>(context));
            check(stream==HAILSTONE_STREAM_WORLD&&opcode==integrity::opcode&&(count==16||count==168),"integrity report used the wrong stream/opcode/size");
            check(data!=nullptr,"integrity submission supplied no packet bytes");
            const auto wire=std::span(static_cast<const std::uint8_t*>(data),count);
            if(count==168) {
                server::Report decoded;
                check(server::decode(wire,decoded),"actual native send was not an exact valid168-byte v3 report");
            } else {
                check(server::read(wire,0,4)==integrity::magic&&server::read(wire,4,4)==2&&
                      server::read(wire,8,8)>0&&server::read(wire,8,8)<=INT64_MAX,
                      "actual legacy send was not the exact v2 header and nonzero FNV63");
            }
            rig.sent.push_back({opcode,{static_cast<const std::byte*>(data),static_cast<const std::byte*>(data)+count}});
            if(rig.during_send) rig.during_send();
            return rig.admit?1:0;
        };
        addon.set_host(&api);addon.initialize();
    }
    ~Rig() {addon.set_host(nullptr);}
    void select() {addon.peer(true);}
    HailstoneOut state() const {
        HailstoneArgs args;HailstoneOut out;
        const auto function=functions.at("integrity");
        check(function.fn(function.self,&args,&out)==1,"integrity state callback became unavailable");
        return out;
    }
    HailstoneOut state() {
        const auto function=functions.at("integrity");HailstoneArgs args;HailstoneOut out;
        check(function.fn(function.self,&args,&out)==1,"integrity state callback failed");return out;
    }
    bool deliver(const Wire& bytes,std::uint16_t op=integrity::opcode) {return addon.packet(op,bytes.data(),bytes.size());}
    void issue(std::uint64_t nonce=0x1122334455667788ull) {check(deliver(server::challenge(nonce)),"native challenge did not stay on its private route");}
    std::span<const std::uint8_t> report() const {
        check(!sent.empty(),"no report was captured");
        return {reinterpret_cast<const std::uint8_t*>(sent.back().bytes.data()),sent.back().bytes.size()};
    }
    std::string allowlist() const {
        server::Report report;check(server::decode(this->report(),report),"server could not decode actual addon report");
        return server::hex(report.executable.sha)+":"+server::hex(report.addon.sha);
    }
};
void actual_wire_and_server_acceptance() {
    Rig rig;
    rig.addon.pulse();check(rig.measured.empty()&&rig.sent.empty(),"unselected peer measured or reported automatically");
    rig.select();rig.addon.pulse();check(rig.measured.empty()&&rig.sent.empty(),"no-challenge login reported automatically");
    constexpr std::uint64_t nonce=0x1122334455667788ull;
    rig.issue(nonce);
    check(rig.addon.status()==integrity::Status::Queued&&!rig.addon.report_sent()&&rig.sent.empty(),"measurement was misrepresented as a sent report");
    check(rig.measured==std::vector<std::uint32_t>({HAILSTONE_BINARY_EXECUTABLE,HAILSTONE_BINARY_CALLING_PLUGIN}),"report did not independently measure actual executable and calling addon");
    rig.addon.pulse();
    check(rig.addon.status()==integrity::Status::Sent&&rig.addon.report_sent(),"successful send was not exposed as sent");
    server::Report decoded;
    check(server::decode(rig.report(),decoded),"portable server decoder rejected the actual 168-byte report");
    check(decoded.nonce==nonce&&decoded.features==0x3fff&&decoded.addon_version==integrity::addon_version,"report lost full nonce/features/version");
    check(decoded.executable.bytes==rig.executable.byte_count&&decoded.addon.bytes==rig.module.byte_count&&
          decoded.executable.fnv63==rig.executable.fnv1a63&&decoded.addon.fnv63==rig.module.fnv1a63,
          "binary records swapped counts, masked FNV or substituted another client binary");
    check(std::equal(decoded.executable.sha.begin(),decoded.executable.sha.end(),rig.executable.sha256)&&
          std::equal(decoded.addon.sha.begin(),decoded.addon.sha.end(),rig.module.sha256),"actual SDK SHA-256 bytes were replaced");
    server::Session connection;connection.issue(nonce,static_cast<std::uint32_t>(rig.now));
    check(connection.evaluate(rig.report(),static_cast<std::uint32_t>(rig.now),rig.allowlist(),decoded)==server::Status::Accepted&&connection.verified(),
          "real server pair-allowlist evaluator rejected the measured report");
    check(rig.addon.status()!=integrity::Status::Accepted,"send admission claimed server acceptance");
    rig.deliver(server::acknowledgement(nonce,server::Status::Accepted,"Approved executable and addon pair."));
    check(rig.addon.status()==integrity::Status::Accepted&&rig.addon.report_sent(),"valid current server acceptance was not recorded");
    rig.deliver(server::acknowledgement(nonce,server::Status::Rejected,"late duplicate"));
    check(rig.addon.status()==integrity::Status::Accepted,"late duplicate acknowledgement replaced a terminal outcome");
    check(rig.commands.empty(),"integrity reporting issued a gameplay command");
}
void parser_and_legacy_routing() {
    Rig rig;rig.select();
    const auto valid=server::challenge(99);
    for(std::size_t count=0;count<valid.size();++count) {auto short_packet=valid;short_packet.resize(count);rig.deliver(short_packet);}
    for(unsigned bad=0;bad<5;++bad) {
        auto bytes=valid;
        if(bad==0) bytes.push_back(0);
        if(bad==1) put(bytes,12,1);
        if(bad==2) put(bytes,16,0,8);
        if(bad==3) put(bytes,8,2);
        if(bad==4) put(bytes,0,0);
        rig.deliver(bytes);
    }
    auto legacy=valid;put(legacy,4,2);
    check(!rig.deliver(legacy)&&!rig.deliver(valid,0x1234),"legacy CIT1v2 or an unrelated asset opcode was intercepted");
    check(rig.measured.empty()&&rig.sent.empty(),"malformed/legacy packet triggered a v3 report");
    auto unknown=valid;put(unknown,24,integrity::features|(1ull<<63),8);rig.deliver(unknown);rig.addon.pulse();
    check(rig.addon.status()==integrity::Status::Unsupported&&rig.measured.empty()&&rig.sent.empty(),"unknown required capabilities were falsely reported as supported");
    rig.issue(100);rig.addon.pulse();
    const auto sent=rig.sent.size();rig.issue(100);rig.addon.pulse();
    check(rig.sent.size()==sent&&rig.measured.size()==2,"duplicate challenge extended or remeasured the same nonce");
}
void acknowledgement_binding() {
    for(unsigned result=1;result<=4;++result) {
        Rig rig;rig.select();rig.issue(50);rig.addon.pulse();
        rig.deliver(server::acknowledgement(51,server::Status::Accepted,"wrong nonce"));
        check(rig.addon.status()==integrity::Status::Sent,"wrong-nonce acknowledgement was accepted");
        auto wrong_flags=server::acknowledgement(50,server::Status::Accepted,"badflags");put(wrong_flags,12,0);rig.deliver(wrong_flags);
        check(rig.addon.status()==integrity::Status::Sent,"zero acknowledgement flags were accepted");
        auto bad_reason=server::acknowledgement(50,server::Status::Accepted,"control");bad_reason[24]=1;rig.deliver(bad_reason);
        bad_reason=server::acknowledgement(50,server::Status::Accepted,"x");bad_reason[26]='y';rig.deliver(bad_reason);
        bad_reason=server::acknowledgement(50,server::Status::Accepted,"");std::fill(bad_reason.begin()+24,bad_reason.end(),'x');rig.deliver(bad_reason);
        check(rig.addon.status()==integrity::Status::Sent,"invalid ASCII/termination/padding acknowledgement was accepted");
        const auto status=static_cast<server::Status>(result);
        rig.deliver(server::acknowledgement(50,status,"Server decision"));
        const std::array expected={integrity::Status::Accepted,integrity::Status::Rejected,integrity::Status::Unsupported,integrity::Status::Malformed};
        check(rig.addon.status()==expected[result-1]&&rig.addon.reason()=="Server decision","server acknowledgement outcome/reason changed");
    }
    Rig rig;rig.select();rig.issue(70);
    rig.deliver(server::acknowledgement(70,server::Status::Accepted,"before send"));
    check(rig.addon.status()==integrity::Status::Queued&&!rig.addon.report_sent(),"pre-send acknowledgement was accepted");
    rig.addon.pulse();rig.addon.peer(false);rig.select();
    rig.deliver(server::acknowledgement(70,server::Status::Accepted,"old account session"));
    check(rig.addon.status()==integrity::Status::Waiting&&!rig.addon.report_sent(),"old account/world acknowledgement survived peer reset");
    rig.issue(71);rig.addon.pulse();rig.addon.game_state(GAMESTATE_PRECHARSELECT);
    rig.deliver(server::acknowledgement(71,server::Status::Accepted,"disconnected"));
    check(rig.addon.status()==integrity::Status::Inactive,"pre-character-select retirement retained an integrity session");
}
void retry_timeout_and_reentrant_send() {
    Rig rig;rig.select();rig.admit=false;rig.issue(101);rig.addon.pulse();
    check(rig.addon.attempts()==1&&rig.addon.status()==integrity::Status::Queued&&!rig.addon.report_sent(),"failed send was recorded as a successful report");
    rig.now+=999;rig.addon.pulse();check(rig.sent.size()==1,"send failure retried before one second");
    ++rig.now;rig.addon.pulse();rig.now+=1000;rig.addon.pulse();rig.now+=10000;rig.addon.pulse();
    check(rig.sent.size()==3&&rig.addon.status()==integrity::Status::Failed&&rig.measured.size()==2,"failed sends exceeded three attempts or remeasured on retries");
    for(const auto& report:rig.sent) check(report.bytes==rig.sent.front().bytes,"retry changed the nonce or binary snapshot");
    Rig timeout;timeout.select();timeout.issue(102);timeout.addon.pulse();
    timeout.now+=integrity::ack_timeout_ms;timeout.addon.pulse();
    timeout.deliver(server::acknowledgement(102,server::Status::Accepted,"late"));
    check(timeout.addon.status()==integrity::Status::TimedOut&&timeout.sent.size()==1,"ack timeout retried a successful report or accepted a late response");
    Rig ttl;ttl.select();ttl.issue(103);ttl.now+=integrity::challenge_ttl_ms;ttl.addon.pulse();
    check(ttl.sent.empty()&&ttl.addon.status()==integrity::Status::TimedOut,"expired queued challenge produced a report");
    for(const bool admit:{false,true}) {
        Rig reentrant;reentrant.select();reentrant.issue(104);reentrant.admit=admit;
        reentrant.during_send=[&] {
            reentrant.deliver(server::acknowledgement(104,server::Status::Accepted,"during send"));
            check(reentrant.addon.status()==integrity::Status::Sending,"reentrant ACK bypassed send admission");
        };
        reentrant.addon.pulse();
        check(reentrant.addon.status()==(admit?integrity::Status::Accepted:integrity::Status::Queued)&&reentrant.addon.report_sent()==admit,
              "reentrant acknowledgement did not respect actual send result");
    }
    Rig replace;replace.select();replace.issue(105);replace.during_send=[&]{replace.issue(106);};replace.addon.pulse();
    check(replace.addon.status()==integrity::Status::Queued&&!replace.addon.report_sent(),"old send completion overwrote a replacement challenge");
}
void measurement_and_clock_failure() {
    for(unsigned fault=1;fault<=9;++fault) {
        Rig rig;rig.select();rig.fault=fault;rig.issue(200+fault);rig.addon.pulse();
        check(rig.sent.empty()&&rig.addon.status()==integrity::Status::Failed,"invalid/failed binary measurement produced an integrity report");
    }
    Rig old;old.select();old.api.measure_binary=nullptr;old.issue(220);old.addon.pulse();
    check(old.sent.empty()&&old.addon.status()==integrity::Status::Failed,"missing binary SDK service fabricated a measurement");
    Rig reset;reset.select();reset.during_measure=[&](std::uint32_t kind){if(kind==HAILSTONE_BINARY_EXECUTABLE) reset.addon.peer(false);};reset.issue(221);reset.addon.pulse();
    check(reset.sent.empty()&&reset.measured.size()==1&&reset.addon.status()==integrity::Status::Inactive,"measurement completion crossed an account/session reset");
    Rig rollback;rollback.select();rollback.issue(222);rollback.addon.pulse();--rollback.now;rollback.addon.pulse();
    rollback.deliver(server::acknowledgement(222,server::Status::Accepted,"old clock"));
    check(rollback.addon.status()==integrity::Status::Failed&&!rollback.addon.report_sent(),"clock rollback retained a report/ack session");
}
constexpr std::array<std::uint16_t,4> legacy_milestones={0x0f13,0x4b8d,0x298d,0x578f};
void outgoing_milestones(Rig& rig) {
    for(const auto op:legacy_milestones)rig.addon.world_outgoing(op);
}
void check_legacy_wire(const Rig& rig,std::uint64_t expected) {
    const auto bytes=rig.report();
    check(bytes.size()==16,"legacy report acquired padding, native fields or an asset CRC payload");
    check(server::read(bytes,0,4)==0x43495431&&server::read(bytes,4,4)==2&&server::read(bytes,8,8)==expected,
          "legacy report did not contain the actual calling plugin's exact FNV63");
    server::Report native;
    check(!server::decode(bytes,native),"legacy submission was represented as native v3 approval evidence");
}
void legacy_wire_and_milestones() {
    Rig unselected;outgoing_milestones(unselected);unselected.addon.pulse();
    check(unselected.measured.empty()&&unselected.sent.empty()&&!unselected.addon.legacy_submitted(),
          "unselected peer initiated a legacy integrity report");
    Rig unrelated;unrelated.select();
    for(const auto op:std::array<std::uint16_t,4>{0x7a09,0x7d1a,0x7d20,0x1234})unrelated.addon.world_outgoing(op);
    check(unrelated.measured.empty()&&unrelated.sent.empty(),"an unrelated outgoing opcode initiated legacy integrity");
    for(const auto first:legacy_milestones) {
        Rig rig;rig.select();rig.addon.world_outgoing(first);
        check(rig.sent.size()==1&&rig.addon.legacy_attempts()==1&&rig.addon.legacy_submitted(),
              "first eligible milestone did not synchronously submit one legacy report");
        check(rig.measured==std::vector<std::uint32_t>{HAILSTONE_BINARY_CALLING_PLUGIN},
              "legacy report measured the executable or used a path/hash substitute instead of the calling addon");
        check_legacy_wire(rig,rig.module.fnv1a63);
        constexpr std::array<std::uint8_t,16> golden={0x31,0x54,0x49,0x43,2,0,0,0,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22};
        check(std::equal(golden.begin(),golden.end(),rig.report().begin()),"legacy report changed the literal little-endian16-byte wire");
        check(rig.addon.status()==integrity::Status::LegacySubmitted&&!rig.addon.report_sent(),
              "legacy send admission claimed native-report delivery or server acceptance");
        outgoing_milestones(rig);rig.now+=10000;rig.addon.pulse();
        check(rig.sent.size()==1&&rig.measured.size()==1&&rig.addon.legacy_attempts()==1,
              "successful legacy submission repeated at another milestone or pulse");
        rig.deliver(server::acknowledgement(888,server::Status::Accepted,"not a legacy acknowledgement"));
        check(rig.addon.status()==integrity::Status::LegacySubmitted&&!rig.addon.report_sent(),
              "an unbound native ACK turned unacknowledged legacy submission into acceptance");
        check(rig.commands.empty(),"legacy integrity issued a gameplay command");
    }
}
void legacy_retries_and_snapshot() {
    Rig rig;rig.select();rig.admit=false;rig.addon.world_outgoing(legacy_milestones.front());
    check(rig.addon.status()==integrity::Status::LegacyQueued&&rig.addon.legacy_attempts()==1&&
          !rig.addon.legacy_submitted()&&!rig.addon.report_sent(),"failed legacy send was not left queued without success");
    const auto original=rig.sent.front().bytes;
    ++rig.module.fnv1a63;rig.module.byte_count+=128;
    outgoing_milestones(rig);rig.addon.pulse();
    check(rig.sent.size()==1&&rig.measured.size()==1,"another milestone bypassed the legacy retry delay or remeasured");
    rig.now+=999;rig.addon.pulse();check(rig.sent.size()==1,"legacy retry occurred before one second");
    ++rig.now;rig.addon.pulse();check(rig.sent.size()==2&&rig.addon.legacy_attempts()==2,"legacy retry did not run at the one-second boundary");
    rig.now+=1000;rig.addon.pulse();
    check(rig.sent.size()==3&&rig.addon.legacy_attempts()==3&&rig.addon.status()==integrity::Status::Failed,
          "legacy retries did not stop at three failed submissions");
    rig.now+=100000;outgoing_milestones(rig);rig.addon.pulse();
    check(rig.sent.size()==3&&rig.measured.size()==1&&!rig.addon.legacy_submitted()&&!rig.addon.report_sent(),
          "exhausted legacy retries restarted, remeasured or fabricated submission");
    for(const auto& packet:rig.sent)check(packet.bytes==original,"legacy retry changed the measured binary snapshot");
    Rig recovered;recovered.select();recovered.admit=false;recovered.addon.world_outgoing(legacy_milestones.front());
    recovered.admit=true;recovered.now+=1000;recovered.addon.pulse();
    check(recovered.sent.size()==2&&recovered.addon.legacy_submitted()&&recovered.addon.legacy_attempts()==2&&
          recovered.addon.status()==integrity::Status::LegacySubmitted&&!recovered.addon.report_sent(),
          "successful legacy retry did not retain its distinct unacknowledged outcome");
    recovered.now+=10000;outgoing_milestones(recovered);recovered.addon.pulse();
    check(recovered.sent.size()==2&&recovered.measured.size()==1,"successful retry continued transmitting legacy reports");
    Rig throwing;throwing.select();throwing.during_send=[]{throw std::runtime_error("injected legacy send failure");};
    throwing.addon.world_outgoing(legacy_milestones.front());throwing.now+=1000;throwing.addon.pulse();throwing.now+=1000;throwing.addon.pulse();
    check(throwing.sent.size()==3&&throwing.measured.size()==1&&throwing.addon.status()==integrity::Status::Failed&&
          !throwing.addon.legacy_submitted(),"throwing legacy sends escaped or exceeded the same bounded retry policy");
}
void legacy_measurement_failures_and_new_session() {
    for(unsigned fault=1;fault<=9;++fault) {
        Rig rig;rig.select();rig.fault=fault;outgoing_milestones(rig);rig.now+=10000;rig.addon.pulse();
        check(rig.sent.empty()&&rig.measured==std::vector<std::uint32_t>{HAILSTONE_BINARY_CALLING_PLUGIN}&&
              rig.addon.status()==integrity::Status::Failed&&!rig.addon.legacy_submitted()&&!rig.addon.report_sent(),
              "invalid/failed legacy measurement fabricated a hash or repeated measurement at later milestones");
    }
    Rig missing;missing.select();missing.api.measure_binary=nullptr;outgoing_milestones(missing);missing.addon.pulse();
    check(missing.measured.empty()&&missing.sent.empty()&&!missing.addon.legacy_submitted(),
          "missing native binary API fabricated legacy integrity");
    Rig unavailable;unavailable.select();unavailable.api.send_packet=nullptr;outgoing_milestones(unavailable);unavailable.addon.pulse();
    check(unavailable.sent.empty()&&!unavailable.addon.legacy_submitted()&&!unavailable.addon.report_sent(),
          "missing packet API claimed legacy or native submission");
    Rig session;session.select();session.addon.world_outgoing(legacy_milestones.front());
    const auto first=session.sent.front().bytes;
    session.addon.peer(false);outgoing_milestones(session);
    check(session.sent.size()==1&&!session.addon.legacy_submitted()&&session.addon.legacy_attempts()==0,
          "peer retirement retained or transmitted legacy session state");
    session.select();++session.module.fnv1a63;session.addon.world_outgoing(legacy_milestones.back());
    check(session.sent.size()==2&&session.measured.size()==2&&session.addon.legacy_submitted()&&session.sent.back().bytes!=first,
          "new peer session reused stale legacy measurement or suppressed its fresh report");
    check_legacy_wire(session,session.module.fnv1a63);
}
void valid_v3_prevents_legacy_downgrade() {
    for(const auto result:{server::Status::Accepted,server::Status::Rejected,server::Status::Unsupported,server::Status::Malformed}) {
        Rig rig;rig.select();rig.issue(600);outgoing_milestones(rig);
        check(rig.sent.empty()&&!rig.addon.legacy_submitted()&&rig.addon.legacy_attempts()==0,
              "valid queued v3 challenge also initiated a legacy report");
        rig.addon.pulse();rig.deliver(server::acknowledgement(600,result,"Native outcome"));
        outgoing_milestones(rig);rig.now+=10000;rig.addon.pulse();
        check(rig.sent.size()==1&&rig.report().size()==168&&rig.measured.size()==2&&!rig.addon.legacy_submitted()&&rig.addon.legacy_attempts()==0,
              "terminal native acceptance/rejection/unsupported/malformed outcome downgraded to legacy");
    }
    Rig unsupported;unsupported.select();auto challenge=server::challenge(601);put(challenge,24,integrity::features|(1ull<<63),8);
    unsupported.deliver(challenge);outgoing_milestones(unsupported);unsupported.addon.pulse();
    check(unsupported.addon.status()==integrity::Status::Unsupported&&unsupported.measured.empty()&&unsupported.sent.empty(),
          "valid v3 challenge with unsupported features fell back to legacy");
    Rig failed;failed.select();failed.fault=1;failed.issue(602);failed.fault=0;outgoing_milestones(failed);failed.addon.pulse();
    check(failed.sent.empty()&&failed.measured.size()==1&&!failed.addon.legacy_submitted(),
          "native measurement failure was bypassed through legacy fallback");
    failed.addon.peer(false);failed.select();failed.addon.world_outgoing(legacy_milestones.front());
    check(failed.sent.size()==1&&failed.report().size()==16&&failed.addon.legacy_submitted(),
          "new session could not use legacy after retiring its previous native challenge");
}
void unsupported_v3_sdk_preserves_login_protocol() {
    Rig rig;rig.select();rig.issue(603);rig.addon.pulse();outgoing_milestones(rig);
    check(rig.addon.status()==integrity::Status::Unsupported&&rig.sent.empty()&&rig.measured.empty()&&
          !rig.addon.legacy_submitted()&&rig.addon.legacy_attempts()==0,
          "the mismatched v3 SDK sent a misdeclared report or downgraded the challenged login");
    rig.addon.world_login(false);rig.select();rig.addon.world_outgoing(legacy_milestones.front());
    check(rig.addon.legacy_submitted(),"a fresh login using v2 lost its DLL verification");
    check_legacy_wire(rig,rig.module.fnv1a63);
}
void malformed_v3_does_not_disable_legacy() {
    const auto valid=server::challenge(700);
    for(std::size_t length=0;length<valid.size();++length) {
        Rig rig;rig.select();auto bytes=valid;bytes.resize(length);rig.deliver(bytes);rig.addon.world_outgoing(legacy_milestones.front());
        check(rig.sent.size()==1&&rig.report().size()==16&&rig.measured==std::vector<std::uint32_t>{HAILSTONE_BINARY_CALLING_PLUGIN},
              "truncated v3 header/challenge silently suppressed legacy support");
    }
    for(unsigned fault=0;fault<6;++fault) {
        Rig rig;rig.select();auto bytes=valid;
        if(fault==0)bytes.push_back(0);
        if(fault==1)put(bytes,12,1);
        if(fault==2)put(bytes,16,0,8);
        if(fault==3)put(bytes,8,2);
        if(fault==4)put(bytes,0,0);
        if(fault==5)put(bytes,4,2);
        rig.deliver(bytes);rig.addon.world_outgoing(legacy_milestones.front());
        check(rig.sent.size()==1&&rig.report().size()==16&&rig.addon.legacy_submitted(),
              "malformed/wrong-version v3 packet suppressed the legitimate legacy path");
    }
}
void legacy_reentrant_measurement() {
    Rig recursion;recursion.select();
    recursion.during_measure=[&](std::uint32_t){outgoing_milestones(recursion);recursion.addon.pulse();};
    recursion.addon.world_outgoing(legacy_milestones.front());
    check(recursion.measured.size()==1&&recursion.sent.size()==1&&recursion.addon.legacy_submitted(),
          "outgoing/pulse reentry during legacy measurement recursively measured or sent");
    Rig retired;retired.select();retired.during_measure=[&](std::uint32_t){retired.addon.peer(false);};
    retired.addon.world_outgoing(legacy_milestones.front());
    check(retired.measured.size()==1&&retired.sent.empty()&&!retired.addon.legacy_submitted()&&retired.addon.status()==integrity::Status::Inactive,
          "legacy measurement completion crossed peer retirement");
    Rig fresh;fresh.select();bool restarted=false;
    fresh.during_measure=[&](std::uint32_t){if(!restarted){restarted=true;fresh.addon.peer(false);fresh.select();}};
    fresh.addon.world_outgoing(legacy_milestones.front());
    check(fresh.sent.empty()&&fresh.addon.status()==integrity::Status::Waiting&&!fresh.addon.legacy_submitted(),
          "old legacy measurement completion crossed a retired then reselected account session");
    ++fresh.module.fnv1a63;fresh.addon.world_outgoing(legacy_milestones.back());
    check(fresh.sent.size()==1&&fresh.measured.size()==2,"reselected session could not measure and send its current module");
    check_legacy_wire(fresh,fresh.module.fnv1a63);
    Rig replacement;replacement.select();bool replaced=false;
    replacement.during_measure=[&](std::uint32_t){if(!replaced){replaced=true;replacement.issue(800);}};
    replacement.addon.world_outgoing(legacy_milestones.front());
    const auto expected=kNativeReportSupported?integrity::Status::Queued:integrity::Status::Unsupported;
    check(replacement.sent.empty()&&replacement.addon.status()==expected&&!replacement.addon.legacy_submitted(),
          "legacy measurement completion overwrote a reentrant native challenge");
    if (!kNativeReportSupported) {
        outgoing_milestones(replacement);replacement.addon.pulse();
        check(replacement.measured.size()==1&&replacement.sent.empty(),
              "a reentrant unsupported challenge retried the legacy report");
        return;
    }
    check(replacement.measured==std::vector<std::uint32_t>({HAILSTONE_BINARY_CALLING_PLUGIN,HAILSTONE_BINARY_EXECUTABLE,HAILSTONE_BINARY_CALLING_PLUGIN}),
          "replacement challenge did not independently measure native binaries");
    replacement.addon.pulse();server::Report native;
    check(replacement.sent.size()==1&&server::decode(replacement.report(),native)&&native.nonce==800,
          "replacement challenge sent stale legacy data instead of the current native report");
}
void legacy_reentrant_submission() {
    Rig recursion;recursion.select();
    recursion.during_send=[&]{recursion.addon.world_outgoing(integrity::opcode);outgoing_milestones(recursion);recursion.addon.pulse();};
    recursion.addon.world_outgoing(legacy_milestones.front());
    check(recursion.sent.size()==1&&recursion.measured.size()==1&&recursion.addon.legacy_attempts()==1&&recursion.addon.legacy_submitted(),
          "own outbound integrity or milestone/pulse reentry recursively submitted legacy data");
    for(const bool admit:{false,true}) {
        Rig retired;retired.select();retired.admit=admit;retired.during_send=[&]{retired.addon.peer(false);};
        retired.addon.world_outgoing(legacy_milestones.front());
        check(retired.addon.status()==integrity::Status::Inactive&&!retired.addon.legacy_submitted()&&retired.addon.legacy_attempts()==0,
              "legacy send completion restored state from a retired peer session");
    }
    Rig fresh;fresh.select();bool reset=false;
    fresh.during_send=[&]{if(!reset){reset=true;fresh.addon.peer(false);fresh.select();}};
    fresh.addon.world_outgoing(legacy_milestones.front());
    check(fresh.addon.status()==integrity::Status::Waiting&&!fresh.addon.legacy_submitted(),
          "old legacy completion overwrote a new selected session");
    fresh.addon.world_outgoing(legacy_milestones.back());
    check(fresh.sent.size()==2&&fresh.measured.size()==2&&fresh.addon.legacy_submitted(),
          "fresh session could not submit after reentrant retirement of its predecessor");
    Rig replacement;replacement.select();bool changed=false;
    replacement.during_send=[&]{if(!changed){changed=true;replacement.issue(801);}};
    replacement.addon.world_outgoing(legacy_milestones.front());
    const auto expected=kNativeReportSupported?integrity::Status::Queued:integrity::Status::Unsupported;
    check(replacement.sent.size()==1&&replacement.report().size()==16&&replacement.addon.status()==expected&&
          !replacement.addon.legacy_submitted()&&!replacement.addon.report_sent(),
          "legacy send completion defeated the current reentrant native challenge");
    replacement.addon.pulse();server::Report native;
    if (kNativeReportSupported)
        check(replacement.sent.size()==2&&server::decode(replacement.report(),native)&&native.nonce==801,
              "native challenge replacement did not send its actual current report after a legacy submission");
    outgoing_milestones(replacement);
    check(replacement.sent.size()==(kNativeReportSupported?2u:1u),
          "native replacement did not suppress later legacy milestones");
    Rig unsupported;unsupported.select();bool challenged=false;
    unsupported.during_send=[&]{if(!challenged){challenged=true;auto bytes=server::challenge(802);put(bytes,24,integrity::features|(1ull<<63),8);unsupported.deliver(bytes);}};
    unsupported.addon.world_outgoing(legacy_milestones.front());outgoing_milestones(unsupported);unsupported.addon.pulse();
    check(unsupported.sent.size()==1&&unsupported.addon.status()==integrity::Status::Unsupported&&
          !unsupported.addon.legacy_submitted()&&!unsupported.addon.report_sent(),
          "legacy send completion downgraded a reentrant valid unsupported native challenge");
}
void late_load_and_zoning_require_fresh_login() {
    Rig late;late.select();late.addon.game_state(GAMESTATE_INGAME);
    check(late.addon.status()==integrity::Status::ReconnectRequired&&late.state().booleans.at("requires_reconnect"),
          "late in-zone approval did not explain that a fresh login is required");
    check(late.measured.empty()&&late.sent.empty(),"late approval measured or reported during ordinary in-zone state");
    late.addon.world_login(true);late.addon.peer(false);late.addon.game_state(GAMESTATE_PRECHARSELECT);
    late.select();late.addon.game_state(GAMESTATE_INGAME);outgoing_milestones(late);late.now+=10000;late.addon.pulse();
    check(late.measured.empty()&&late.sent.empty()&&late.addon.status()==integrity::Status::ReconnectRequired&&
          late.state().booleans.at("requires_reconnect"),"zone transfer tried to repair a missed fresh-login report");
    late.addon.world_login(false);late.addon.peer(false);late.select();
    check(!late.state().booleans.at("requires_reconnect"),"fresh world login retained a stale reconnect requirement");
    late.addon.world_outgoing(legacy_milestones.front());
    check(late.measured.size()==1&&late.sent.size()==1&&late.addon.legacy_submitted(),
          "fresh login after late approval failed to submit the actual addon report");
}
void zoning_preserves_successful_receipts_without_resending() {
    Rig legacy;legacy.addon.world_login(false);legacy.select();legacy.addon.world_outgoing(legacy_milestones.front());
    const auto receipt=legacy.sent.front().bytes;++legacy.module.fnv1a63;
    legacy.addon.game_state(GAMESTATE_INGAME);
    for(unsigned n=0;n<3;++n) {
        legacy.addon.world_login(true);legacy.addon.peer(false);legacy.addon.game_state(GAMESTATE_PRECHARSELECT);
        legacy.select();outgoing_milestones(legacy);legacy.addon.game_state(GAMESTATE_INGAME);
        legacy.now+=100000;legacy.addon.pulse();
        const auto state=legacy.state();
        check(legacy.sent.size()==1&&legacy.measured.size()==1&&legacy.sent.front().bytes==receipt&&
              legacy.addon.legacy_submitted()&&legacy.addon.legacy_attempts()==1&&
              legacy.addon.status()==integrity::Status::LegacySubmitted&&!state.booleans.at("requires_reconnect")&&
              state.booleans.at("legacy_submitted")&&!state.booleans.at("accepted"),
              "zoning erased, remeasured or falsely upgraded the original unacknowledged DLL receipt");
    }
    legacy.addon.world_login(false);legacy.addon.peer(false);legacy.select();
    check(!legacy.addon.legacy_submitted()&&legacy.addon.status()==integrity::Status::Waiting,
          "fresh login retained the preceding login's legacy receipt");
    legacy.addon.world_outgoing(legacy_milestones.front());
    check(legacy.sent.size()==2&&legacy.measured.size()==2&&legacy.sent.back().bytes!=receipt,
          "fresh login reused the old report instead of measuring its current loaded addon");
    if constexpr (HAILSTONE_PLUGIN_ABI != integrity::native_report_abi) return;
    Rig native;native.addon.world_login(false);native.select();native.issue(901);native.addon.pulse();
    native.deliver(server::acknowledgement(901,server::Status::Accepted,"Current login approved"));
    native.addon.world_login(true);native.addon.peer(false);native.addon.game_state(GAMESTATE_PRECHARSELECT);
    native.select();native.addon.game_state(GAMESTATE_INGAME);outgoing_milestones(native);
    native.now+=100000;native.addon.pulse();
    check(native.sent.size()==1&&native.measured.size()==2&&native.addon.status()==integrity::Status::Accepted&&
          native.addon.report_sent()&&native.state().booleans.at("accepted")&&!native.state().booleans.at("requires_reconnect"),
          "zoning discarded a native server acceptance or resent it on the zoning connection");
    native.addon.world_login(false);native.addon.peer(false);native.select();
    check(native.addon.status()==integrity::Status::Waiting&&!native.addon.report_sent()&&
          !native.state().booleans.at("accepted"),"fresh login inherited a prior login's server acceptance");
}
void zoning_cancels_unsubmitted_retries_and_host_reset_retires_receipts() {
    Rig queued;queued.select();queued.admit=false;queued.addon.world_outgoing(legacy_milestones.front());
    check(queued.addon.status()==integrity::Status::LegacyQueued,"retry fixture failed to queue its first report");
    queued.addon.world_login(true);queued.addon.peer(false);queued.select();queued.admit=true;
    queued.now+=10000;outgoing_milestones(queued);queued.addon.pulse();queued.addon.game_state(GAMESTATE_INGAME);
    check(queued.sent.size()==1&&queued.measured.size()==1&&!queued.addon.legacy_submitted()&&
          queued.addon.status()==integrity::Status::ReconnectRequired&&queued.state().booleans.at("requires_reconnect"),
          "zoning retried an unsubmitted fresh-login report or presented its failed attempt as a receipt");
    if constexpr (HAILSTONE_PLUGIN_ABI == integrity::native_report_abi) {
        Rig native;native.select();native.issue(902);
        native.addon.world_login(true);native.addon.peer(false);native.select();native.addon.pulse();outgoing_milestones(native);
        check(native.sent.empty()&&native.measured.size()==2&&native.addon.status()==integrity::Status::ReconnectRequired,
              "a queued native report leaked onto a later zoning connection");
    }
    Rig reset;reset.select();reset.addon.world_outgoing(legacy_milestones.front());
    reset.addon.world_login(true);reset.addon.peer(false);reset.select();reset.addon.set_host(nullptr);
    check(reset.addon.status()==integrity::Status::Inactive&&!reset.addon.legacy_submitted()&&!reset.addon.report_sent(),
          "losing the host retained a previous login's submitted receipt");
    reset.addon.set_host(&reset.api);reset.addon.initialize();reset.select();reset.addon.game_state(GAMESTATE_INGAME);
    check(reset.addon.status()==integrity::Status::ReconnectRequired&&reset.state().booleans.at("requires_reconnect"),
          "new host late load inherited a submitted receipt from the retired host");
}
void initial_world_close_cancels_pending_and_fresh_login_recovers() {
    std::uint64_t nonce=1000;
    for(const int close_state:{GAMESTATE_LOGGINGIN,GAMESTATE_INGAME}) {
        Rig legacy;legacy.select();legacy.admit=false;legacy.addon.world_outgoing(legacy_milestones.front());
        check(legacy.addon.status()==integrity::Status::LegacyQueued&&legacy.sent.size()==1&&legacy.measured.size()==1,
              "initial-close fixture did not leave an unsubmitted legacy retry");
        legacy.addon.game_state(close_state);legacy.addon.game_state(GAMESTATE_INGAME);
        legacy.admit=true;legacy.now+=10000;legacy.addon.pulse();outgoing_milestones(legacy);
        check(legacy.sent.size()==1&&legacy.measured.size()==1&&!legacy.addon.legacy_submitted()&&
              legacy.addon.status()==integrity::Status::ReconnectRequired&&legacy.state().booleans.at("requires_reconnect"),
              "initial WORLD close let a pending legacy retry claim submission on a closed connection");
        ++legacy.module.fnv1a63;legacy.addon.world_login(false);legacy.select();
        check(!legacy.state().booleans.at("requires_reconnect"),"fresh login retained initial-close recovery state");
        legacy.addon.world_outgoing(legacy_milestones.front());
        check(legacy.sent.size()==2&&legacy.measured.size()==2&&legacy.addon.legacy_submitted(),
              "fresh full login failed to remeasure and submit after initial legacy cancellation");
        check_legacy_wire(legacy,legacy.module.fnv1a63);
        if constexpr (HAILSTONE_PLUGIN_ABI != integrity::native_report_abi) continue;
        for(const bool already_sent:{false,true}) {
            Rig native;native.select();const auto old_nonce=nonce++;native.issue(old_nonce);
            if(already_sent) native.addon.pulse();
            check(native.addon.status()==(already_sent?integrity::Status::Sent:integrity::Status::Queued),
                  "initial-close fixture did not reach the native queued or awaiting-ack state");
            const auto sends=native.sent.size(),measurements=native.measured.size();
            native.addon.game_state(close_state);native.addon.game_state(GAMESTATE_INGAME);
            native.deliver(server::acknowledgement(old_nonce,server::Status::Accepted,"Closed WORLD"));
            native.now+=10000;native.addon.pulse();outgoing_milestones(native);
            const auto closed=native.state();
            check(native.sent.size()==sends&&native.measured.size()==measurements&&!native.addon.report_sent()&&
                  !closed.booleans.at("accepted")&&closed.booleans.at("requires_reconnect")&&
                  native.addon.status()==integrity::Status::ReconnectRequired,
                  "initial WORLD close retried native data, accepted a retired nonce or downgraded to legacy");
            native.addon.world_login(false);native.select();const auto current_nonce=nonce++;native.issue(current_nonce);native.addon.pulse();
            native.deliver(server::acknowledgement(old_nonce,server::Status::Accepted,"Previous login"));
            check(native.addon.status()==integrity::Status::Sent&&!native.state().booleans.at("accepted"),
                  "fresh login accepted an initial-close predecessor's acknowledgement");
            native.deliver(server::acknowledgement(current_nonce,server::Status::Accepted,"Fresh login approved"));
            check(native.sent.size()==sends+1&&native.measured.size()==measurements+2&&native.addon.report_sent()&&
                  native.addon.status()==integrity::Status::Accepted&&native.state().booleans.at("accepted"),
                  "fresh native login failed to recover with its own current measurement and acknowledgement");
        }
    }
}
void initial_world_close_preserves_completed_receipts() {
    for(const int close_state:{GAMESTATE_LOGGINGIN,GAMESTATE_INGAME}) {
        Rig legacy;legacy.select();legacy.addon.world_outgoing(legacy_milestones.front());
        const auto wire=legacy.sent.front().bytes;const auto original=legacy.state();++legacy.module.fnv1a63;
        legacy.addon.game_state(close_state);legacy.addon.game_state(GAMESTATE_INGAME);
        legacy.now+=100000;legacy.addon.pulse();outgoing_milestones(legacy);
        const auto retained=legacy.state();
        check(legacy.sent.size()==1&&legacy.sent.front().bytes==wire&&legacy.measured.size()==1&&
              legacy.addon.status()==integrity::Status::LegacySubmitted&&legacy.addon.legacy_submitted()&&
              !retained.booleans.at("requires_reconnect")&&!retained.booleans.at("accepted")&&
              retained.strings.at("legacy_checksum")==original.strings.at("legacy_checksum")&&
              retained.strings.at("legacy_addon_bytes")==original.strings.at("legacy_addon_bytes"),
              "initial WORLD close erased, rehashed or upgraded a completed unacknowledged legacy receipt");
        if constexpr (HAILSTONE_PLUGIN_ABI != integrity::native_report_abi) continue;
        Rig native;native.select();native.issue(1100);native.addon.pulse();
        native.deliver(server::acknowledgement(1100,server::Status::Accepted,"Current login approved"));
        const auto accepted=native.state();
        native.addon.game_state(close_state);native.addon.game_state(GAMESTATE_INGAME);
        native.now+=100000;native.addon.pulse();outgoing_milestones(native);
        const auto current=native.state();
        check(native.sent.size()==1&&native.measured.size()==2&&native.addon.status()==integrity::Status::Accepted&&
              native.addon.report_sent()&&current.booleans.at("accepted")&&!current.booleans.at("requires_reconnect")&&
              current.strings.at("executable_bytes")==accepted.strings.at("executable_bytes")&&
              current.strings.at("addon_bytes")==accepted.strings.at("addon_bytes"),
              "initial WORLD close discarded a completed native acknowledgement or resent its report");
    }
}
void initial_world_close_during_send_cannot_restore_submission() {
    for(const int close_state:{GAMESTATE_LOGGINGIN,GAMESTATE_INGAME}) {
        for(const bool admit:{false,true}) {
            Rig legacy;legacy.select();legacy.admit=admit;
            legacy.during_send=[&]{legacy.addon.game_state(close_state);};
            legacy.addon.world_outgoing(legacy_milestones.front());
            legacy.now+=10000;legacy.addon.pulse();outgoing_milestones(legacy);
            check(legacy.sent.size()==1&&legacy.measured.size()==1&&!legacy.addon.legacy_submitted()&&
                  legacy.addon.status()==integrity::Status::ReconnectRequired&&legacy.state().booleans.at("requires_reconnect"),
                  "legacy host send completion restored queued/submitted state after reentrant initial WORLD close");
            if constexpr (HAILSTONE_PLUGIN_ABI != integrity::native_report_abi) continue;
            Rig native;native.select();native.issue(1200);native.admit=admit;
            native.during_send=[&]{
                native.deliver(server::acknowledgement(1200,server::Status::Accepted,"Reentrant acknowledgement"));
                native.addon.game_state(close_state);
            };
            native.addon.pulse();
            native.deliver(server::acknowledgement(1200,server::Status::Accepted,"After close"));
            native.now+=10000;native.addon.pulse();outgoing_milestones(native);
            const auto state=native.state();
            check(native.sent.size()==1&&native.measured.size()==2&&!native.addon.report_sent()&&
                  !state.booleans.at("accepted")&&state.booleans.at("requires_reconnect")&&
                  native.addon.status()==integrity::Status::ReconnectRequired,
                  "native host completion or deferred acknowledgement crossed reentrant initial WORLD close");
        }
    }
}
void initial_world_close_during_legacy_measurement_requires_reconnect() {
    for(const int close_state:{GAMESTATE_LOGGINGIN,GAMESTATE_INGAME}) {
        Rig legacy;legacy.select();
        legacy.during_measure=[&](std::uint32_t){legacy.addon.game_state(close_state);};
        legacy.addon.world_outgoing(legacy_milestones.front());
        legacy.now+=10000;legacy.addon.pulse();outgoing_milestones(legacy);
        const auto closed=legacy.state();
        check(legacy.measured.size()==1&&legacy.sent.empty()&&legacy.addon.status()==integrity::Status::ReconnectRequired&&
              closed.booleans.at("requires_reconnect")&&!closed.booleans.at("legacy_measured")&&
              !closed.booleans.at("legacy_submitted")&&!closed.booleans.at("accepted")&&
              closed.strings.at("legacy_checksum")=="0"&&closed.strings.at("legacy_addon_bytes")=="0",
              "legacy measurement completion claimed cached data or sent after reentrant initial WORLD close");
        legacy.during_measure={};++legacy.module.fnv1a63;legacy.addon.world_login(false);legacy.select();
        legacy.addon.world_outgoing(legacy_milestones.front());
        check(legacy.measured.size()==2&&legacy.sent.size()==1&&legacy.addon.legacy_submitted()&&
              !legacy.state().booleans.at("requires_reconnect"),
              "fresh full login failed to recover from reentrant initial-close measurement retirement");
        check_legacy_wire(legacy,legacy.module.fnv1a63);
    }
}
} // namespace
int main() {
    try {
        if (HAILSTONE_PLUGIN_ABI == integrity::native_report_abi) {
            actual_wire_and_server_acceptance();parser_and_legacy_routing();acknowledgement_binding();
            retry_timeout_and_reentrant_send();measurement_and_clock_failure();valid_v3_prevents_legacy_downgrade();
        } else {
            unsupported_v3_sdk_preserves_login_protocol();
        }
        legacy_wire_and_milestones();legacy_retries_and_snapshot();legacy_measurement_failures_and_new_session();
        malformed_v3_does_not_disable_legacy();legacy_reentrant_measurement();legacy_reentrant_submission();
        late_load_and_zoning_require_fresh_login();zoning_preserves_successful_receipts_without_resending();
        zoning_cancels_unsubmitted_retries_and_host_reset_retires_receipts();
        initial_world_close_cancels_pending_and_fresh_login_recovers();initial_world_close_preserves_completed_receipts();
        initial_world_close_during_send_cannot_restore_submission();
        initial_world_close_during_legacy_measurement_requires_reconnect();
        std::cout<<"Binary integrity: "<<citadel_test::checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"Binary integrity after "<<citadel_test::checks<<" checks: "<<error.what()<<'\n';return 1;}
}
