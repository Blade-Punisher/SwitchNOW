#pragma once
#include <functional>
#include <string>
#include <vector>
namespace opennow::gfn::cleanup {
struct Route { std::string base, source; };
enum class Result { Accepted, AlreadyAbsent };
std::vector<Route> Routes(const std::string& hint, const std::string& saved, const std::string& provider);
struct Reply { long status_code=0; std::string body; };
using Send=std::function<Reply(const Route&)>;
using Refresh=std::function<void()>;
using Observe=std::function<void(const Route&, const char*, int)>;
// All routes address the same caller-owned session. Failures never clear its record.
Result Close(const std::vector<Route>& routes, const Send& send, const Refresh& refresh,
    const Observe& observe={}, const std::function<bool()>& cancelled={});
}
