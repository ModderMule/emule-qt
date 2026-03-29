#pragma once

/// @file ByteOrder.h
/// @brief Cross-platform byte order conversion (htonl, ntohl, htons, ntohs).

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
