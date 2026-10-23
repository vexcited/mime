#ifndef WINMM_MCI_PRIVATE_H
#define WINMM_MCI_PRIVATE_H

#define MCI_MAX_TABLES 20
#define MCI_BLOCK_SIZE 160
#define MCI_MAX_STRINGS 20
#define WM_MCI_RELAY 0x3CA
#define MCI_RELAY_SENTINEL 0xFF000000u

typedef struct MCI_TABLE {
    BOOL used;
    HMODULE module;      /* module containing resource return strings. */
    UINT type;           /* the device type a type table serves, 0 else. */
    BYTE *data;
    DWORD size;
    const BYTE **commands;
    UINT count;
} MCI_TABLE;

typedef struct MCI_DEVICE {
    MCIDEVICEID id;
    WCHAR *name;         /* the alias, none for element id opens. */
    WCHAR install[80];   /* the key of [MCI32] the driver came from. */
    DWORD open_flags, element_id;
    DWORD_PTR data;
    YIELDPROC yield;
    DWORD yield_data;
    UINT type, type_table, custom_table;
    DWORD owner, requester;
    HDRVR driver;
    HMODULE module;
    BOOL closing, closing_by_notify;
} MCI_DEVICE;

/* open arguments are parsed again with the device's command table after its driver loads. */
typedef struct MCI_REPARSE {
    const WCHAR *args;      /* the words after the device name. */
    const WCHAR *device;    /* the device word. */
    BOOL is_new;
    MCIERROR deferred;      /* an unknown word the core table left. */
    DWORD flags;
    BYTE block[MCI_BLOCK_SIZE];
    WCHAR *strings[MCI_MAX_STRINGS];
    UINT string_count;
} MCI_REPARSE;

extern RTL_CRITICAL_SECTION MciLock;
extern MCI_TABLE MciTables[MCI_MAX_TABLES];

BOOL MciInit(void);
MCI_DEVICE *MciDevice(MCIDEVICEID id);
MCIDEVICEID MciDeviceByName(const WCHAR *name, DWORD thread);
const BYTE *MciEntry(const BYTE *p, const WCHAR **word, DWORD *value, WORD *type);
const BYTE *MciFindCommand(MCIDEVICEID id, const WCHAR *verb, UINT *table);
const BYTE *MciFindMessage(MCIDEVICEID id, UINT message, UINT *table);
UINT MciTypeOf(const WCHAR *name);
MCIERROR MciCommand(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR parms, DWORD requester, MCI_REPARSE *reparse);
HWND MciNotifyWindow(void);
HMODULE MciStringModule(MCIDEVICEID id, BOOL driver);
MCIERROR MciRelay(const WCHAR *command, WCHAR *ret, UINT size, DWORD requester, BOOL force_notify);
BOOL MciOnWindowThread(void);

/* the string interface, in mcistring.c. */
MCIERROR MciSendString(const WCHAR *command, WCHAR *ret, UINT size, HWND callback, DWORD requester, BOOL relayed,
                       BOOL force_notify);
MCIERROR MciParseArgs(const BYTE *command, const WCHAR *args, BOOL open, MCI_REPARSE *out);

#endif
