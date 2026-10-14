#ifndef MIME_WIN32_NB30_H
#define MIME_WIN32_NB30_H

#include <mime/win32/winbase.h>

#define NCBNAMSZ 16
#define MAX_LANA 254
#define NCBRESET 0x32
#define NCBASTAT 0x33
#define NCBENUM 0x37
#define ASYNCH 0x80
#define NRC_GOODRET 0x00
#define NRC_BUFLEN 0x01
#define NRC_ILLCMD 0x03
#define NRC_BADDR 0x07
#define NRC_BRIDGE 0x23
#define NRC_INVADDRESS 0x39

typedef struct _NCB {
    UCHAR ncb_command, ncb_retcode, ncb_lsn, ncb_num;
    PUCHAR ncb_buffer;
    WORD ncb_length;
    UCHAR ncb_callname[NCBNAMSZ], ncb_name[NCBNAMSZ];
    UCHAR ncb_rto, ncb_sto;
    void (CALLBACK *ncb_post)(struct _NCB *);
    UCHAR ncb_lana_num, ncb_cmd_cplt;
#ifdef _WIN64
    UCHAR ncb_reserve[18];
#else
    UCHAR ncb_reserve[10];
#endif
    HANDLE ncb_event;
} NCB, *PNCB;

typedef struct _LANA_ENUM {
    UCHAR length;
    UCHAR lana[MAX_LANA + 1];
} LANA_ENUM, *PLANA_ENUM;

#ifndef NETBIOSAPI
#define NETBIOSAPI DECLSPEC_IMPORT
#endif
NETBIOSAPI UCHAR WINAPI Netbios(PNCB ncb);

#endif
