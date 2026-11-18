// SPDX-License-Identifier: MIT
#include "integrity.h"
#include <cstring>
#include <limits>

namespace bastion::integrity {
const char* label(Status status) {
    switch(status) {
    case Status::Inactive:return "Inactive";case Status::Waiting:return "Waiting for server challenge";
    case Status::Measuring:return "Measuring binaries";case Status::Queued:return "Report queued";
    case Status::Sending:return "Sending report";case Status::Sent:return "Report sent";
    case Status::Accepted:return "Server accepted";case Status::Rejected:return "Server rejected";
    case Status::Unsupported:return "Unsupported";case Status::Malformed:return "Server reported malformed data";
    case Status::TimedOut:return "Acknowledgement timed out";case Status::Failed:return "Report unavailable";
    case Status::LegacyQueued:return "Legacy DLL report queued";case Status::LegacySubmitted:return "Legacy DLL report submitted";
    }
    return "Unavailable";
}
void Adapter::retire(Status status,std::string_view reason) {
    ++generation_;nonce_=deadline_=ack_deadline_=next_=0;attempts_=0;sent_=measured_=false;
    host_bytes_=addon_bytes_=0;report_.fill(std::byte{});reentrant_ack_.reset();status_=status;reason_=reason;
}
void Adapter::reset_legacy() {
    native_protocol_=legacy_attempted_=legacy_measured_=legacy_pending_=legacy_sending_=legacy_submitted_=false;
    legacy_next_=legacy_bytes_=legacy_checksum_=0;legacy_attempts_=0;legacy_report_.fill(std::byte{});
}
void Adapter::set_host(const PluginHostApi* host) {
    if(HAILSTONE_HOST_HAS(host_,remove_ui_function)) host_->remove_ui_function(host_->host,"integrity");
    host_=host;selected_=false;has_clock_=false;seen_count_=0;reset_legacy();retire(Status::Inactive,"");
}
void Adapter::initialize() {
    if(HAILSTONE_HOST_HAS(host_,add_ui_function)) host_->add_ui_function(host_->host,"integrity",callback,this);
}
void Adapter::peer(bool value) {
    selected_=value;has_clock_=false;seen_count_=0;reset_legacy();
    retire(value?Status::Waiting:Status::Inactive,value?"Waiting for Bastion's binary integrity challenge.":"");
}
void Adapter::game_state(int value) {
    if(value==GAMESTATE_PRECHARSELECT||value==GAMESTATE_UNLOADING) peer(false);
}
bool Adapter::clock(std::uint64_t& now) {
    if(!selected_||!HAILSTONE_HOST_HAS(host_,monotonic_ms)) return false;
    now=host_->monotonic_ms(host_->host);
    if(has_clock_&&now<last_clock_) {legacy_pending_=false;retire(Status::Failed,"Session clock reset; reconnect or wait for a fresh integrity challenge.");last_clock_=now;return false;}
    has_clock_=true;last_clock_=now;return true;
}
bool Adapter::measure(std::uint32_t kind,HailstoneBinaryMeasurement& out) {
    if(!HAILSTONE_HOST_HAS(host_,measure_binary)||host_->abi!=HAILSTONE_PLUGIN_ABI) return false;
    out={};out.size=sizeof(out);
    const std::uint64_t maximum=kind==HAILSTONE_BINARY_EXECUTABLE?512ull*1024*1024:128ull*1024*1024;
    if(!host_->measure_binary(host_->host,kind,&out)||out.size<sizeof(out)||out.kind!=kind||!out.byte_count||out.byte_count>maximum||!out.fnv1a63||out.fnv1a63>INT64_MAX) return false;
    return std::any_of(std::begin(out.sha256),std::end(out.sha256),[](std::uint8_t byte){return byte!=0;});
}
void Adapter::world_outgoing(std::uint16_t op) {
    if(!login_checkpoint(op)) return;
    if(!selected_||native_protocol_||legacy_attempted_) return;
    // Guard before calling the host: measurement and packet hooks may reenter us.
    legacy_attempted_=true;
    const auto generation=generation_;
    std::uint64_t now=0;
    if(!clock(now)) {
        if(generation==generation_&&selected_&&!native_protocol_) {
            status_=Status::Failed;reason_="The legacy DLL report clock is unavailable.";
        }
        return;
    }
    HailstoneBinaryMeasurement addon{};
    try {
        if(!measure(HAILSTONE_BINARY_CALLING_PLUGIN,addon)) {
            if(generation==generation_&&selected_&&!native_protocol_) {
                status_=Status::Failed;reason_="The loaded Bastion addon could not be measured for the DLL check.";
            }
            return;
        }
    } catch(...) {
        if(generation==generation_&&selected_&&!native_protocol_) {
            status_=Status::Failed;reason_="The binary measurement service failed.";
        }
        return;
    }
    if(generation!=generation_||!selected_||native_protocol_) return;
    hailstone::ByteWriter writer;writer.reserve(legacy_report_.size());
    writer.le(magic);writer.le(std::uint32_t{2});writer.le(addon.fnv1a63);
    std::copy(writer.data().begin(),writer.data().end(),legacy_report_.begin());
    legacy_bytes_=addon.byte_count;legacy_checksum_=addon.fnv1a63;legacy_measured_=legacy_pending_=true;
    legacy_next_=now;status_=Status::LegacyQueued;
    // Pulse-only submission could follow automatic EnterWorld in the same update.
    send_legacy(now);
}
void Adapter::send_legacy(std::uint64_t now) {
    if(!selected_||native_protocol_||!legacy_pending_||legacy_sending_) return;
    if(!HAILSTONE_HOST_HAS(host_,send_packet)) {
        legacy_pending_=false;status_=Status::Failed;reason_="The world packet service is unavailable.";return;
    }
    const auto bytes=legacy_report_;const auto generation=generation_;
    ++legacy_attempts_;legacy_sending_=true;
    bool admitted=false;
    try {admitted=host_->send_packet(host_->host,HAILSTONE_STREAM_WORLD,opcode,bytes.data(),bytes.size())!=0;} catch(...) {}
    if(generation!=generation_||!selected_||native_protocol_) return;
    legacy_sending_=false;
    if(admitted) {
        legacy_pending_=false;legacy_submitted_=true;status_=Status::LegacySubmitted;
        reason_="Loaded bastion.dll checksum submitted; this server protocol has no acknowledgement.";
    } else {
        legacy_pending_=legacy_attempts_<3;
        legacy_next_=now>std::numeric_limits<std::uint64_t>::max()-1000?std::numeric_limits<std::uint64_t>::max():now+1000;
        status_=legacy_pending_?Status::LegacyQueued:Status::Failed;
        reason_=legacy_pending_?"DLL report send failed; a bounded retry is queued.":"The DLL integrity report could not be submitted.";
    }
}
void Adapter::challenge(std::uint64_t nonce,std::uint64_t required,std::uint64_t now) {
    if(std::find(seen_.begin(),seen_.begin()+static_cast<std::ptrdiff_t>(seen_count_),nonce)!=seen_.begin()+static_cast<std::ptrdiff_t>(seen_count_)) return;
    if(seen_count_==seen_.size()) {retire(Status::Failed,"Too many integrity challenges in this session.");return;}
    seen_[seen_count_++]=nonce;
    retire(Status::Measuring,"");nonce_=nonce;
    if((required&~features)!=0) {status_=Status::Unsupported;reason_="Bastion requested integrity features unavailable in this addon.";return;}
    if(now>std::numeric_limits<std::uint64_t>::max()-challenge_ttl_ms) {status_=Status::Failed;reason_="The integrity challenge clock is unavailable.";return;}
    deadline_=now+challenge_ttl_ms;
    const auto generation=generation_;
    HailstoneBinaryMeasurement executable{},addon{};
    try {
        if(!measure(HAILSTONE_BINARY_EXECUTABLE,executable)) {
            if(generation==generation_) {status_=Status::Failed;reason_="The current executable or addon binary could not be measured.";}
            return;
        }
        if(generation!=generation_||!selected_) return;
        if(!measure(HAILSTONE_BINARY_CALLING_PLUGIN,addon)) {
            if(generation==generation_) {status_=Status::Failed;reason_="The current executable or addon binary could not be measured.";}
            return;
        }
    } catch(...) {
        if(generation==generation_) {status_=Status::Failed;reason_="The binary measurement service failed.";}
        return;
    }
    if(generation!=generation_||!selected_) return;
    hailstone::ByteWriter writer;writer.reserve(report_.size());
    writer.le(magic);writer.le(version);writer.le(std::uint32_t{2});writer.le(std::uint32_t{0});writer.le(nonce_);
    writer.le(features);writer.le(std::uint32_t{HAILSTONE_PLUGIN_ABI});writer.le(std::uint32_t{0});
    for(const auto* binary:{&executable,&addon}) {
        writer.le(binary->byte_count);writer.le(binary->fnv1a63);
        writer.bytes({reinterpret_cast<const std::byte*>(binary->sha256),sizeof(binary->sha256)});
    }
    writer.fixed_string(addon_version,32);
    if(writer.size()!=report_.size()) {status_=Status::Failed;reason_="The integrity report could not be encoded.";return;}
    std::copy(writer.data().begin(),writer.data().end(),report_.begin());
    host_bytes_=executable.byte_count;addon_bytes_=addon.byte_count;measured_=true;status_=Status::Queued;next_=now;
    reason_="Current executable and addon measured; report queued.";
}
void Adapter::acknowledge(const Ack& ack) {
    switch(ack.flags) {
    case 1:status_=Status::Accepted;break;case 2:status_=Status::Rejected;break;
    case 3:status_=Status::Unsupported;break;case 4:status_=Status::Malformed;break;default:return;
    }
    reason_=ack.reason;ack_deadline_=0;reentrant_ack_.reset();
}
void Adapter::send(std::uint64_t now) {
    if(!HAILSTONE_HOST_HAS(host_,send_packet)) {status_=Status::Failed;reason_="The world packet service is unavailable.";return;}
    const auto bytes=report_;const auto generation=generation_;
    ++attempts_;status_=Status::Sending;reentrant_ack_.reset();
    bool admitted=false;
    try {admitted=host_->send_packet(host_->host,HAILSTONE_STREAM_WORLD,opcode,bytes.data(),bytes.size())!=0;} catch(...) {}
    if(generation!=generation_||!selected_) return;
    if(!admitted) {
        reentrant_ack_.reset();status_=attempts_>=3?Status::Failed:Status::Queued;next_=now+1000;
        reason_=attempts_>=3?"The binary integrity report could not be sent.":"Report send failed; a bounded retry is queued.";
        return;
    }
    sent_=true;status_=Status::Sent;ack_deadline_=std::min(deadline_,now+ack_timeout_ms);
    reason_="Report sent; waiting for Bastion's acknowledgement.";
    if(reentrant_ack_) {const auto ack=*reentrant_ack_;acknowledge(ack);}
}
void Adapter::pulse() {
    // No per-frame clock calls once the legacy report has been submitted.
    if(!selected_||(!native_protocol_&&!legacy_pending_)) return;
    std::uint64_t now=0;if(!clock(now)) return;
    if(!native_protocol_) {if(now>=legacy_next_) send_legacy(now);return;}
    if((status_==Status::Queued||status_==Status::Sending||status_==Status::Sent)&&now>=deadline_) {
        status_=Status::TimedOut;reentrant_ack_.reset();reason_="The integrity challenge expired before a current acknowledgement.";return;
    }
    if(status_==Status::Sent&&now>=ack_deadline_) {status_=Status::TimedOut;reason_="Report sent; Bastion did not acknowledge it in time.";return;}
    if(status_==Status::Queued&&now>=next_) send(now);
}
bool Adapter::packet(std::uint16_t op,const void* data,std::size_t count) {
    if(!selected_||op!=opcode) return false;
    if(!data||count<8) return true;
    try {
        hailstone::ByteReader reader(data,count);
        if(reader.le<std::uint32_t>()!=magic||reader.le<std::uint32_t>()!=version) return false;
        if(count<24) return true;
        const auto kind=reader.le<std::uint32_t>(),flags=reader.le<std::uint32_t>();
        const auto nonce=reader.le<std::uint64_t>();if(!nonce) return true;
        if(kind==1) {
            if(count==32&&flags==0) {
                const auto required=reader.le<std::uint64_t>();
                // A real challenge permanently selects v3 for this WORLD session,
                // including unsupported features and server rejection. Never downgrade it.
                if(!native_protocol_) {native_protocol_=true;legacy_pending_=false;++generation_;}
                std::uint64_t now=0;
                if(clock(now)) challenge(nonce,required,now);
                else if(status_!=Status::Failed) {retire(Status::Failed,"The integrity challenge clock is unavailable.");}
            }
            return true;
        }
        std::uint64_t now=0;if(!clock(now)) return true;
        if(kind!=3||count!=152||flags<1||flags>4||nonce!=nonce_||now>=deadline_||
           (status_!=Status::Sending&&status_!=Status::Sent)) return true;
        if(status_==Status::Sent&&now>=ack_deadline_) {status_=Status::TimedOut;reason_="Report acknowledgement arrived after its deadline.";return true;}
        const auto text=reader.bytes(128);const auto terminator=std::find(text.begin(),text.end(),std::byte{});
        if(terminator==text.end()) return true;
        for(auto it=text.begin();it!=terminator;++it) {const auto byte=std::to_integer<unsigned>(*it);if(byte<32||byte>126) return true;}
        if(std::any_of(terminator,text.end(),[](std::byte byte){return byte!=std::byte{};})) return true;
        Ack ack{flags,std::string(reinterpret_cast<const char*>(text.data()),static_cast<std::size_t>(terminator-text.begin()))};
        if(status_==Status::Sending) reentrant_ack_=std::move(ack);else acknowledge(ack);
    } catch(const std::out_of_range&) {}
    return true;
}
int Adapter::callback(void* self,const HailstoneArgs* args,HailstoneOut* out) noexcept {
    if(!self) return 0;
    auto& owner=*static_cast<Adapter*>(self);
    return services::guarded_ui(owner.host_,[&]{return owner.ui(args,out);});
}
int Adapter::ui(const HailstoneArgs* args,HailstoneOut* out) {
    services::Output result{host_,out};if(!result.valid()) return 0;
    const auto operation=services::argument(host_,args,0,16);if(!operation||*operation!="state") return 0;
    result.begin();result.boolean("active",selected_);result.boolean("measured",measured_);result.boolean("sent",sent_);
    result.boolean("accepted",selected_&&status_==Status::Accepted);result.string("status",label(status_));result.string("reason",reason_);
    result.string("addon_version",addon_version);result.number("attempts",attempts_);result.number("features",features);
    result.boolean("native_protocol",native_protocol_);result.boolean("legacy_measured",legacy_measured_);
    result.boolean("legacy_submitted",legacy_submitted_);result.number("legacy_attempts",legacy_attempts_);
    result.identity("legacy_addon_bytes",legacy_bytes_);result.identity("legacy_checksum",legacy_checksum_);
    result.identity("executable_bytes",host_bytes_);result.identity("addon_bytes",addon_bytes_);result.end();return 1;
}
} // namespace bastion::integrity
