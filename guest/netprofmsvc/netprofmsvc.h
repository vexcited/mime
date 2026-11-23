#ifndef MIME_NETPROFMSVC_H
#define MIME_NETPROFMSVC_H

#include <mime/win32/winsock2.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winsvc.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/iphlpapi.h>

MIME_DEFINE_GUID(CLSID_NetworkListManagerService, 0xA47979D2, 0xC419, 0x11D9, 0xA5, 0xB4, 0x00, 0x11, 0x85, 0xAD, 0x2B,
                 0x89);
MIME_DEFINE_GUID(IID_INetworkListManagerPrivate, 0xD0074FFD, 0x570F, 0x4A9B, 0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72,
                 0x3B);
MIME_DEFINE_GUID(IID_IEnumNetworkInterfacePrivate, 0xBCD1DE7E, 0x2DB1, 0x418B, 0xB0, 0x47, 0x4A, 0x74, 0xE1, 0x01, 0xF8,
                 0xC1);
MIME_DEFINE_GUID(IID_INetworkInterfacePrivate, 0x2A1C9EB2, 0xDF62, 0x4154, 0xB8, 0x00, 0x63, 0x27, 0x8F, 0xCB, 0x80,
                 0x37);

/* The bits of a connectivity state, the ones of WNF_NLM_GLOBAL_CONNECTIVITY_STATE. */
#define NP_STATE_CONNECTED 0x01
#define NP_STATE_IPV4_LOCAL 0x04
#define NP_STATE_IPV4_INTERNET 0x08
#define NP_STATE_IPV6_LOCAL 0x10
#define NP_STATE_IPV6_INTERNET 0x20
#define NP_COST_UNRESTRICTED 1

HRESULT NpManagerCreate(REFIID iid, void **out);
/* The objects clients still hold are cut from them before the service leaves its dll. */
void NpDisconnectObjects(void);

#endif
