#ifndef _LIBKOSAPI_NET_H
#define _LIBKOSAPI_NET_H

#ifndef _WIN32
#include <sys/cdefs.h>
#include <stdint.h>
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// fkosNetStateChanged will run speical thread.
// @ipv4 0x7301a8c0 <==> 192.168.1.115
typedef void (*fkosNetStateChanged)(const char* iface, bool connected, uint32_t ipv4, void* user);
void kosSetNetStateChanged(fkosNetStateChanged did, void* user);

// max_bytes require include terminate char: '\0'
int kosNetSendMsg(const char* msg, char* result, int maxBytes);

#ifdef __cplusplus
}
#endif

#endif /* _LIBKOSAPI_NET_H */
