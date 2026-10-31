#pragma once
namespace brls { struct Logger { template<class... T> static void warning(const char*,T&&...) {} }; }
