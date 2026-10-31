#include "signaling_registration.hpp"
#include <jansson.h>
#include <climits>
namespace opennow::signaling {
std::optional<std::int64_t> IncomingAckId(json_t* envelope) {
    auto* id=json_object_get(envelope,"ackid");
    if(!json_is_integer(id) || json_integer_value(id)<0) return std::nullopt;
    return json_integer_value(id);
}
bool ObserveRegistration(RegistrationPolicy& state,json_t* root,const std::string& expected) {
    const bool before=state.Confirmed();
    if(!json_is_object(root)) return false;
    auto* info=json_object_get(root,"peer_info");
    auto* name=json_object_get(info,"name");
    auto* id=json_object_get(info,"id");
    if(json_is_string(name) && expected==json_string_value(name) && json_is_integer(id) &&
        json_integer_value(id)>=0 && json_integer_value(id)<=INT_MAX) state.Identity();
    auto* ack=json_object_get(root,"ack");
    if(json_is_integer(ack)) state.Ack(json_integer_value(ack));
    return !before && state.Confirmed();
}
}
