#pragma once
#include <string>
namespace brls {
inline std::string getStr(const std::string& key){return key=="hints/back"?"System Back":key=="hints/ok"?"System OK":key;}
}
