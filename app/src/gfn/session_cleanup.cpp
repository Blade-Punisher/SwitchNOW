#include "session_cleanup.hpp"
#include "session_endpoints.hpp"
#include <jansson.h>
#include <algorithm>
#include <stdexcept>
#include <memory>

namespace opennow::gfn::cleanup {
std::vector<Route> Routes(const std::string& hint, const std::string& saved, const std::string& provider) {
    std::vector<Route> out;
    for(const auto& item : {Route{hint,"server_hint"},Route{saved,"saved_session"},Route{provider,"provider"}}) {
        const auto base=endpoints::ControlBaseUrl(item.base);
        if(base.empty() || std::any_of(out.begin(),out.end(),[&](const auto& r){return r.base==base;})) continue;
        out.push_back({base,item.source});
    }
    return out;
}
namespace {
enum class State { Accepted, Missing, Failed };
State Classify(const Reply& response) {
    if(response.status_code==404 || response.status_code==410) return State::Missing;
    std::unique_ptr<json_t,decltype(&json_decref)> root(json_loads(response.body.c_str(),0,nullptr),&json_decref);
    auto* status=json_object_get(root.get(),"requestStatus");
    auto* description=json_object_get(status,"statusDescription");
    const bool missing=((response.status_code>=200 && response.status_code<300) || response.status_code==400) &&
        json_is_string(description) &&
        std::string(json_string_value(description)).find("INVALID_SESSION_ID_NOT_FOUND")!=std::string::npos;
    auto* code=json_object_get(status,"statusCode");
    const bool refused=code && (!json_is_integer(code) || json_integer_value(code)!=1);
    if(missing) return State::Missing;
    if(response.status_code>=200 && response.status_code<300 && !refused) return State::Accepted;
    return State::Failed;
}
}
Result Close(const std::vector<Route>& routes, const Send& send, const Refresh& refresh,
    const Observe& observe, const std::function<bool()>& cancelled) {
    if(routes.empty() || routes.size()>3 || !send) throw std::runtime_error("No valid route for closing the previous cloud session");
    const auto check=[&] {
        if(cancelled && cancelled()) throw std::runtime_error("Previous cloud session cleanup cancelled or timed out");
    };
    const auto note=[&](const Route& r,const char* event,int status) {
        if(observe) try {observe(r,event,status);} catch (...) {} // Diagnostics cannot change the result.
    };
    bool refreshed=false;
    size_t missing=0;
    std::string failure="network request failed";
    for(const auto& route:routes) {
        check();
        // Defend the operation even if a caller bypasses Routes().
        if(endpoints::ControlBaseUrl(route.base)!=route.base) throw std::runtime_error("Invalid cloud session cleanup route");
        for(unsigned retry=0;retry<2;++retry) {
            check();note(route,"begin",0);
            Reply response;
            try {response=send(route);} catch(const std::exception&) {
                check();note(route,"transport_error",0);failure="network request failed";break;
            }
            check();
            if(response.status_code==401 && !refreshed && refresh) {
                refreshed=true;note(route,"refresh_auth",401);refresh();continue;
            }
            const auto state=Classify(response);
            if(state==State::Accepted) {note(route,"accepted",static_cast<int>(response.status_code));return Result::Accepted;}
            if(state==State::Missing) {++missing;note(route,"not_found",static_cast<int>(response.status_code));break;}
            note(route,response.status_code>=200 && response.status_code<300 ? "api_error" : "http_error",static_cast<int>(response.status_code));
            failure="HTTP "+std::to_string(response.status_code);
            if(response.status_code==429) throw std::runtime_error("Previous cloud session cleanup rate limited (HTTP 429)");
            break;
        }
    }
    if(missing==routes.size()) return Result::AlreadyAbsent;
    throw std::runtime_error("Unable to close the previous cloud session: "+failure);
}
}
