#pragma once

/// @file ByteOrder.h
/// @brief Cross-platform byte order conversion (htonl, ntohl, htons, ntohs).

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
