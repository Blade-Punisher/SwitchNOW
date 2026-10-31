#include "session_recovery_policy.hpp"
#include <cassert>

int main() {
    using namespace opennow;
    using namespace std::chrono;
    assert(!recovery::NeedsOfferClaim(false,false,true,1));
    assert(recovery::NeedsOfferClaim(false,false,false,2));
    assert(!recovery::NeedsOfferClaim(false,false,false,1));
    assert(!recovery::NeedsOfferClaim(true,false,true,2));
    assert(!recovery::NeedsOfferClaim(true,true,true,2));
    // One fresh signaling attempt precedes at most one claim of the same rig.
    recovery::Budget missing_offer;
    assert(missing_offer.Take());
    assert(!recovery::NeedsOfferClaim(false,false,true,missing_offer.attempts()));
    assert(missing_offer.Take());
    assert(recovery::NeedsOfferClaim(false,false,false,missing_offer.attempts()));
    assert(!missing_offer.Take());
    assert(!recovery::NeedsOfferClaim(true,false,true,1));
    assert(!recovery::NeedsOfferClaim(true,true,true,1));
    assert(!recovery::NeedsOfferClaim(false,false,true,0));

    assert(recovery::Eligible(StreamEndReason::NetworkLost, true));
    assert(recovery::Eligible(StreamEndReason::ServerDisconnected, true));
    assert(recovery::Eligible(StreamEndReason::VideoTimedOut, true));
    for (auto reason : {StreamEndReason::None, StreamEndReason::FreeSessionEnded,
         StreamEndReason::StreamEnded, StreamEndReason::ConnectionFailed})
        assert(!recovery::Eligible(reason, true));
    assert(!recovery::Eligible(StreamEndReason::ServerDisconnected, false));
    assert(recovery::Eligible(StreamEndReason::StartupTimedOut, false));
    assert(recovery::OfferManualStartupRetry(StreamEndReason::ConnectionFailed, false));
    assert(!recovery::OfferManualStartupRetry(StreamEndReason::ConnectionFailed, true));
    assert(!recovery::OfferManualStartupRetry(StreamEndReason::StreamEnded, false));
    assert(!recovery::OfferManualStartupRetry(StreamEndReason::FreeSessionEnded, false));
    for (int status : {-1, 0, 1, 6, 99})
        assert(recovery::ClassifyCloudState(status, true) == recovery::CloudState::Wait);
    for (int status : {2, 3}) {
        assert(recovery::ClassifyCloudState(status, false) == recovery::CloudState::Wait);
        assert(recovery::ClassifyCloudState(status, true) == recovery::CloudState::Ready);
    }
    for (int status : {4, 5})
        assert(recovery::ClassifyCloudState(status, true) == recovery::CloudState::Ended);
    // R33 report: accepted RESUME -> 6 -> provisioning -> fresh ready endpoint.
    unsigned ready_count = 0;
    for (int status : {6, 6, 1, 2}) {
        const auto state = recovery::ClassifyCloudState(status, status == 2);
        assert(state != recovery::CloudState::Ended);
        ready_count += state == recovery::CloudState::Ready;
    }
    assert(ready_count == 1);
    recovery::Budget budget;
    const auto now = steady_clock::now();
    budget.Observe(true, now);
    budget.Observe(true, now + seconds(40));
    assert(budget.Take());
    const recovery::Identity identity {"user", "provider", "https://gfn.example/", "device", "client"};
    assert(recovery::Matches(identity, identity));
    for (int field = 0; field < 5; ++field) {
        auto changed = identity;
        switch (field) {
            case 0: changed.user = "another"; break;
            case 1: changed.provider = "another"; break;
            case 2: changed.provider_url = "https://other.example/"; break;
            case 3: changed.device = "another"; break;
            case 4: changed.client = "another"; break;
        }
        assert(!recovery::Matches(identity, changed));
    }
    assert(!recovery::Matches({}, {}));
    assert(recovery::ValidFormat(1280, 720, 60, "H264"));
    assert(!recovery::ValidFormat(1280, 720, 0, "H264"));
    assert(!recovery::ValidFormat(8192, 720, 60, "H264"));
    assert(!recovery::ValidFormat(1280, 720, 60, "bad"));
    budget.Observe(true, now + seconds(41)); // one packet does not reset budget
    assert(budget.attempts() == 1);
    assert(budget.Take());
    assert(!budget.Take());
    budget.Observe(true, now + seconds(42));
    budget.Observe(false, now + seconds(60)); // interrupted recovery
    budget.Observe(true, now + seconds(61));
    budget.Observe(true, now + seconds(90));
    assert(!budget.Take());
    budget.Observe(true, now + seconds(91));
    assert(budget.Take()); // only 30 seconds of continuous healthy video rearms
    assert(budget.Take());
    assert(!budget.Take());
    budget.ManualRetry();
    assert(budget.Take());
    return 0;
}
