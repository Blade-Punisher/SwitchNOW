#pragma once
#include <string>
namespace opennow::gfn::ownership {
std::string RequestBody(const std::string& variant_id,bool owned=true);
void ValidateResponse(const std::string& body,const std::string& expected_app_id,bool owned=true);
}
