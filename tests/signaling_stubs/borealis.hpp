#pragma once
namespace brls { struct Logger {
    template<class... T> static void info(const char*,T&&...) {}
    template<class... T> static void error(const char*,T&&...) {}
}; }
