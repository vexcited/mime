#ifndef MIME_MSWSOCK_PRIVATE_H
#define MIME_MSWSOCK_PRIVATE_H

#include "../ws2_32/ws2_32_private.h"

#define WSPAPI WSAAPI

typedef struct _WSATHREADID {
    HANDLE ThreadHandle;
    DWORD_PTR Reserved;
} WSATHREADID, *LPWSATHREADID;

typedef struct _WSPDATA {
    WORD wVersion;
    WORD wHighVersion;
    WCHAR szDescription[WSADESCRIPTION_LEN + 1];
} WSPDATA, *LPWSPDATA;

/* the upcalls ws2_32 passes to WSPStartup, in the order of ws2spi.h. */
typedef struct _WSPUPCALLTABLE {
    PVOID lpWPUCloseEvent;
    PVOID lpWPUCloseSocketHandle;
    PVOID lpWPUCreateEvent;
    PVOID lpWPUCreateSocketHandle;
    PVOID lpWPUFDIsSet;
    PVOID lpWPUGetProviderPath;
    SOCKET(WSPAPI *lpWPUModifyIFSHandle)(DWORD catalog_entry, SOCKET proposed, LPINT error);
    PVOID lpWPUPostMessage;
    PVOID lpWPUQueryBlockingCallback;
    PVOID lpWPUQuerySocketHandleContext;
    PVOID lpWPUQueueApc;
    PVOID lpWPUResetEvent;
    PVOID lpWPUSetEvent;
    PVOID lpWPUOpenCurrentThread;
    PVOID lpWPUCloseThread;
} WSPUPCALLTABLE, *LPWSPUPCALLTABLE;

/* the procedures WSPStartup returns, in the order of ws2spi.h. ws2_32 refuses a table with an empty entry. */
typedef struct _WSPPROC_TABLE {
    PVOID lpWSPAccept;
    PVOID lpWSPAddressToString;
    PVOID lpWSPAsyncSelect;
    PVOID lpWSPBind;
    PVOID lpWSPCancelBlockingCall;
    PVOID lpWSPCleanup;
    PVOID lpWSPCloseSocket;
    PVOID lpWSPConnect;
    PVOID lpWSPDuplicateSocket;
    PVOID lpWSPEnumNetworkEvents;
    PVOID lpWSPEventSelect;
    PVOID lpWSPGetOverlappedResult;
    PVOID lpWSPGetPeerName;
    PVOID lpWSPGetSockName;
    PVOID lpWSPGetSockOpt;
    PVOID lpWSPGetQOSByName;
    PVOID lpWSPIoctl;
    PVOID lpWSPJoinLeaf;
    PVOID lpWSPListen;
    PVOID lpWSPRecv;
    PVOID lpWSPRecvDisconnect;
    PVOID lpWSPRecvFrom;
    PVOID lpWSPSelect;
    PVOID lpWSPSend;
    PVOID lpWSPSendDisconnect;
    PVOID lpWSPSendTo;
    PVOID lpWSPSetSockOpt;
    PVOID lpWSPShutdown;
    PVOID lpWSPSocket;
    PVOID lpWSPStringToAddress;
} WSPPROC_TABLE, *LPWSPPROC_TABLE;

int WSAAPI WSAConnect(SOCKET s, const struct sockaddr *name, int namelen, LPWSABUF caller_data, LPWSABUF callee_data,
                      void *sqos, void *gqos);

#endif
