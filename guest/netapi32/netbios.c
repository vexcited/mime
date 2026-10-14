#define NETBIOSAPI DECLSPEC_EXPORT
#include <mime/win32/nb30.h>

UCHAR WINAPI Netbios(PNCB ncb)
{
    if (!ncb)
        return NRC_INVADDRESS;
    UCHAR status;
    switch (ncb->ncb_command) {
    case NCBENUM:
        if (!ncb->ncb_buffer)
            status = NRC_BADDR;
        else if (ncb->ncb_length < sizeof(LANA_ENUM))
            status = NRC_BUFLEN;
        else {
            memset(ncb->ncb_buffer, 0, sizeof(LANA_ENUM));
            status = NRC_GOODRET;
        }
        break;
    case NCBRESET:
    case NCBASTAT:
        status = NRC_BRIDGE;
        break;
    default:
        status = NRC_ILLCMD;
        break;
    }
    ncb->ncb_retcode = status;
    ncb->ncb_cmd_cplt = status;
    return status;
}
