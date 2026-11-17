// SPDX-License-Identifier: MIT
#include "integrity.h"
#include "citadel_test_host.h"
#include "../server/native_integrity.h"
#include <array>
#include <iostream>
#include <limits>

namespace integrity=bastion::integrity;
namespace server=bastion_native_integrity;
using citadel_test::check;
namespace {
using Wire=std::vector<std::uint8_t>;
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
            check(stream==HAILSTONE_STREAM_WORLD&&opcode==integrity::opcode&&count==168,"integrity report used the wrong stream/opcode/size");
            rig.sent.push_back({opcode,{static_cast<const std::byte*>(data),static_cast<const std::byte*>(data)+count}});
            if(rig.during_send) rig.during_send();
            return rig.admit?1:0;
        };
        addon.set_host(&api);addon.initialize();
    }
    ~Rig() {addon.set_host(nullptr);}
    void select() {addon.peer(true);}
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
} // namespace
int main() {
    try {
        actual_wire_and_server_acceptance();parser_and_legacy_routing();acknowledgement_binding();retry_timeout_and_reentrant_send();measurement_and_clock_failure();
        std::cout<<"Binary integrity: "<<citadel_test::checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"Binary integrity after "<<citadel_test::checks<<" checks: "<<error.what()<<'\n';return 1;}
}
