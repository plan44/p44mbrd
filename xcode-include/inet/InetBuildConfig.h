// Copied from Linux generated build config

#ifndef INET_INETBUILDCONFIG_H_
#define INET_INETBUILDCONFIG_H_

#define INET_CONFIG_TEST 0
#define INET_CONFIG_ENABLE_IPV4 1
#define INET_CONFIG_ENABLE_TCP_ENDPOINT 1
#define INET_CONFIG_ENABLE_UDP_ENDPOINT 1
#define HAVE_LWIP_RAW_BIND_NETIF 1
#define INET_PLATFORM_CONFIG_INCLUDE <platform/Darwin/InetPlatformConfig.h>
#define INET_TCP_END_POINT_IMPL_CONFIG_FILE <inet/TCPEndPointImplSockets.h>
#define INET_UDP_END_POINT_IMPL_CONFIG_FILE <inet/UDPEndPointImplSockets.h>

// Hacks for compiling mostly Linux code on macOS
//#ifdef __APPLE__
//#define HAVE_SO_BINDTODEVICE 0
//#endif

#endif  // INET_INETBUILDCONFIG_H_
