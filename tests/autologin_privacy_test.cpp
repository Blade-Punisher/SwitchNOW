#include "stream_diagnostics.hpp"
#include <cassert>
int main() {
    using namespace opennow;
    for(bool diagnostics:{true,false}) {
        SetStreamDiagnosticsEnabled(diagnostics);
        SetSensitiveInputLoggingSuppressed(false);
        assert(CanLogInputPayload("key_down")&&CanLogInputPayload("key_up"));
        SetSensitiveInputLoggingSuppressed(true);
        assert(!CanLogInputPayload("key_down")&&!CanLogInputPayload("key_up"));
        // The privacy gate must not hide unrelated connection/controller diagnostics.
        assert(CanLogInputPayload("gamepad")&&CanLogInputPayload("input_heartbeat"));
        SetSensitiveInputLoggingSuppressed(false);
        assert(CanLogInputPayload("key_down"));
    }
}
