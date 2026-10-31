#include "gfn/session_cleanup.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace opennow;
using namespace opennow::gfn::cleanup;
template<class F> bool Throws(F f) {try {f();} catch(const std::exception&) {return true;}return false;}
int main() {
    const auto reported=Routes(".cloudmatchbeta.nvidiagrid.net","https://np-ams-06.cloudmatchbeta.nvidiagrid.net/","https://prod.cloudmatchbeta.nvidiagrid.net/");
    assert(reported.size()==2 && reported[0].source=="saved_session" && reported[1].source=="provider");
    unsigned calls=0;
    assert(Close(reported,[&](const Route& route) {
        ++calls;assert(route.base==reported[0].base);return Reply{204,{}};
    },{})==Result::Accepted && calls==1); // Malformed hint never reaches the transport.
    const auto routes=Routes("rig.example","https://RIG.EXAMPLE:443/","https://provider.example/api/");
    assert(routes.size()==2); // Canonical default port/case dedupe.
    const std::string owned_id="only-owned-session";
    std::vector<std::string> requested;
    assert(Close(routes,[&](const Route& r)->Reply {
        requested.push_back(r.base+"v2/session/"+owned_id);
        if(r.source=="server_hint") throw std::runtime_error("simulated DNS failure");
        return {200,R"({"requestStatus":{"statusCode":1}})"};
    },{})==Result::Accepted);
    assert(requested.size()==2);
    for(const auto& url:requested) assert(url.ends_with("v2/session/"+owned_id));
    calls=0;
    assert(Close(routes,[&](const Route&)->Reply {return {++calls==1 ? 404L : 204L,{}};},{})==Result::Accepted && calls==2);
    calls=0;
    assert(Close(routes,[&](const Route&)->Reply {++calls;return {404,{}};},{})==Result::AlreadyAbsent && calls==2);
    assert(Throws([&] {Close(routes,[](const Route& r)->Reply {
        if(r.source=="server_hint") throw std::runtime_error("unreachable");
        return {404,{}};
    },{});})); // A partial failure must not be treated as confirmed absence.
    assert(Throws([&] {Close(routes,[](const Route&)->Reply {return {200,R"({"requestStatus":{"statusCode":2}})"};},{});}));
    assert(Throws([&] {Close(routes,[](const Route&)->Reply {return {200,R"({"requestStatus":{"statusCode":"unknown"}})"};},{});}));
    assert(Close(routes,[](const Route&)->Reply {return {400,R"({"requestStatus":{"statusCode":27,"statusDescription":"INVALID_SESSION_ID_NOT_FOUND"}})"};},{})==Result::AlreadyAbsent);
    calls=0;unsigned refreshes=0;
    assert(Close(routes,[&](const Route&)->Reply {return {++calls==1 ? 401L : 204L,{}};},[&]{++refreshes;})==Result::Accepted && calls==2 && refreshes==1);
    calls=0;refreshes=0;
    assert(Throws([&] {Close(routes,[&](const Route&)->Reply {++calls;return {401,{}};},[&]{++refreshes;});}));
    assert(calls==3 && refreshes==1); // At most one token refresh for the whole operation.
    calls=0;
    assert(Throws([&] {Close(routes,[&](const Route&)->Reply {++calls;return {429,{}};},{});}));assert(calls==1);
    calls=0;
    assert(Throws([&] {Close(routes,[&](const Route&)->Reply {++calls;return {204,{}};},{},{},[]{return true;});}));assert(calls==0);
    calls=0;bool cancel=false;
    assert(Throws([&] {Close(routes,[&](const Route&)->Reply {++calls;cancel=true;throw std::runtime_error("cancel");},{},{},[&]{return cancel;});}));assert(calls==1);
    calls=0;
    assert(Throws([&] {Close({{"https://.cloudmatchbeta.nvidiagrid.net/","bypass"}},[&](const Route&)->Reply {++calls;return {204,{}};},{});}));assert(calls==0);
    assert(Throws([&] {Close({},[](const Route&)->Reply {return {204,{}};},{});}));
    assert(Close(routes,[](const Route&)->Reply {return {204,{}};},{},[](const Route&,const char*,int){throw std::runtime_error("logging");})==Result::Accepted);
    std::cout<<"Production cloud session cleanup route/fallback/cancellation tests passed.\n";
}
