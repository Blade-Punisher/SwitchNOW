#pragma once
// MinGW source-backed RTP tests only. Production Switch and Linux use their
// native POSIX headers; no socket calls are replaced by this adapter.
#include <winsock2.h>
#include <ws2tcpip.h>
