#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
struct json_t;
namespace opennow::signaling {
enum class EndReason { None, InitialOfferTimeout, InitialIceTimeout, RecoveryDeadline, RecoveryPeerTerminal, StopRequested };
const char* EndReasonName(EndReason reason);
const char* TransportErrorKind(const std::string& error);
std::string DescribePayload(json_t* payload, bool parsed, std::size_t bytes);
class Diagnostics {
public:
    std::string Observe(json_t* root, const std::string& expected_name);
    std::string Registration(std::int64_t request_id, bool written, std::size_t bytes);
    std::string Ack(bool self_skipped, bool serialized, bool written);
    std::string Summary(bool socket, bool offer, unsigned ice_remote, bool video, EndReason end,
                        const std::string& transport_error) const;
private:
    std::uint64_t rx_=0, peer_info_=0, identities_=0, unknown_=0, malformed_=0, errors_=0;
    std::uint64_t registration_tx_=0, registration_written_=0, registration_ack_=0;
    std::uint64_t ack_tx_=0, ack_written_=0, local_id_skipped_=0, ack_decisions_=0;
    std::int64_t registration_id_=0;
    bool have_registration_=false, identified_=false, last_registration_written_=false;
};
}
