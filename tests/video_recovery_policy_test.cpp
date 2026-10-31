#include "stream/VideoRecoveryPolicy.hpp"
#include <cassert>
#include <cstdint>
using Policy=opennow::video::VideoRecoveryPolicy;
int main() {
    std::atomic<bool> reset{true};
    for(unsigned i=0;i<8;++i) {
        assert(!opennow::video::ConsumeResetForIdr(false,reset,false));
        assert(reset.load()); // Healthy pre-loss queue never consumes a reset.
    }
    assert(opennow::video::ConsumeResetForIdr(true,reset,false));
    assert(!reset.load() && !opennow::video::ConsumeResetForIdr(true,reset,false));
    assert(opennow::video::ConsumeResetForIdr(true,reset,true));
    using Output=Policy::Output;
    Policy p;
    assert(p.Required() && !p.Queue(500,false));
    assert(p.Queue(1000,true));
    assert(p.Queue(2500,false)); // Feed later packets even if IDR has no output yet.
    assert(!p.Begin(2500,false)); // It must not overtake the queued IDR.
    assert(p.Frame(true,1000)==Output::Drop); // Queued is not submitted/decoded.
    assert(p.Begin(1000,true));
    assert(p.Begin(2500,false));
    assert(p.Required()); // A successful send_packet / zero output is not recovery.
    assert(p.Frame(true,2500)==Output::Drop);
    assert(p.Frame(false,0)==Output::Drop);
    assert(p.Frame(true,1000)==Output::ConfirmedIdr && !p.Required());
    for(std::uint32_t t=2500;t<100000;t+=1500) {
        assert(p.Queue(t,false) && p.Begin(t,false));
        assert(p.Frame(true,t)==Output::Present);
    }
    assert(p.Fault(true,101000));
    const auto generation=p.Generation();
    assert(!p.Fault(true,101000) && p.Generation()==generation);
    assert(p.Begin(100000,false)); // Preserve healthy pre-loss work.
    assert(p.Frame(true,100000)==Output::Present);
    assert(!p.Begin(102500,false)); // Old queued dependent picture is rechecked.
    assert(p.Queue(104000,true) && p.Begin(104000,true));
    assert(p.Fault(true,107000) && p.Generation()>generation);
    assert(p.Frame(true,104000)==Output::Drop); // Old IDR cannot cancel newer loss.
    assert(!p.Begin(105500,false)); // Keep earliest unsafe boundary across losses.
    assert(p.Frame(true,105500)==Output::Drop);
    assert(p.Queue(108500,true) && p.Begin(108500,true));
    assert(p.Begin(110000,false));
    assert(p.Frame(true,108500)==Output::ConfirmedIdr);
    assert(p.Frame(true,110000)==Output::Present);
    assert(!p.Fault(true,107000)); // A late damage report already covered by IDR.
    assert(p.Frame(true,105500)==Output::Drop); // Delayed old output cannot rewind.
    assert(p.Fault(false,0));
    assert(!p.Begin(111500,false) && p.Frame(false,0)==Output::Drop);
    assert(p.Queue(113000,true) && p.Begin(113000,true));
    assert(p.Frame(true,113000)==Output::ConfirmedIdr);
    assert(!p.Begin(110000,false)); // Never feed an old dependent chain after a newer IDR.
    assert(p.Fault(false,0));
    assert(!p.Queue(113000,true) && !p.Begin(113000,true)); // Unknown fault needs a newer IDR.
    assert(p.Queue(114500,true) && p.Begin(114500,true));
    assert(p.Frame(true,114500)==Output::ConfirmedIdr);

    Policy wrap;
    assert(wrap.Queue(0xfffff000u,true) && wrap.Begin(0xfffff000u,true));
    assert(wrap.Frame(true,0xfffff000u)==Output::ConfirmedIdr);
    assert(wrap.Fault(true,0xfffffa00u));
    assert(wrap.Queue(0x100u,true) && wrap.Begin(0x100u,true));
    assert(wrap.Frame(true,0x100u)==Output::ConfirmedIdr);
    assert(!wrap.Fault(true,0xfffffa00u));
    assert(wrap.Frame(true,0x700u)==Output::Present);

    // A second, older loss expands the unsafe range without making an IDR
    // before the newest loss eligible to end recovery.
    Policy reverse;
    reverse.Fault(true,9000); reverse.Fault(true,6000);
    assert(!reverse.Begin(7500,false));
    assert(!reverse.Queue(7500,true));
    assert(reverse.Queue(10500,true) && reverse.Begin(10500,true));
    assert(reverse.Frame(true,10500)==Output::ConfirmedIdr);

    Policy delayed_damage;
    assert(delayed_damage.Queue(9000,true) && delayed_damage.Begin(9000,true));
    assert(delayed_damage.Fault(true,7500));
    assert(delayed_damage.Frame(true,9000)==Output::ConfirmedIdr);
    assert(!delayed_damage.Required()); // Avoid a needless second IDR / stall.

    // Candidate metadata stays bounded while the decoder delays output.
    Policy burst;
    for(std::uint32_t i=1;i<=40;++i) assert(burst.Queue(i*1500,true));
    assert(burst.Begin(60000,true));
    assert(burst.Frame(true,60000)==Output::ConfirmedIdr);
}
