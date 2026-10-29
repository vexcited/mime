/* the device control string of a serial port: "baud=9600 parity=N data=8 stop=1" and the older "9600,n,8,1". */
#include "kernelbase_private.h"

#define DCB_FLAGS_OFFSET 8

/* the text being read: the part not read yet, where the last match ended and the text the last wildcard held. */
typedef struct COMM_TEXT {
    const char *Cursor;
    const char *End;
    const char *Start;
    const char *Last;
} COMM_TEXT;

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* a pattern is literal characters in either case, # for digits, * for any text up to a space and @ for one character. */
static BOOL match(COMM_TEXT *t, const char *pattern)
{
    const char *p = t->Cursor;
    for (; *pattern; pattern++) {
        switch (*pattern) {
        case '#':
            t->Start = p;
            while (*p >= '0' && *p <= '9')
                p++;
            if (p == t->Start)
                return FALSE;
            t->Last = p - 1;
            break;
        case '*':
            t->Start = p;
            while (*p && *p != ' ')
                p++;
            t->Last = p - 1;
            break;
        case '@':
            if (!*p)
                return FALSE;
            t->Start = t->Last = p++;
            break;
        default:
            if (!*p || upper(*p) != upper(*pattern))
                return FALSE;
            p++;
        }
    }
    t->End = p;
    return TRUE;
}

static void advance(COMM_TEXT *t)
{
    t->Cursor = t->End;
    while (*t->Cursor == ' ')
        t->Cursor++;
}

static DWORD number(const COMM_TEXT *t)
{
    DWORD value = 0;
    for (const char *p = t->Start; p <= t->Last; p++)
        value = value * 10 + (DWORD)(*p - '0');
    return value;
}

/* the short rates of the old syntax are the first digits of the rate. */
static DWORD baud_rate(DWORD rate)
{
    switch (rate) {
    case 11:
    case 110:
        return 110;
    case 12:
        return 1200;
    case 15:
        return 150;
    case 19:
        return 19200;
    case 24:
        return 2400;
    case 30:
        return 300;
    case 48:
        return 4800;
    case 60:
        return 600;
    case 96:
        return 9600;
    }
    return rate;
}

static BOOL parity_of(char c, BYTE *parity)
{
    switch (lower(c)) {
    case 'e':
        *parity = EVENPARITY;
        return TRUE;
    case 'm':
        *parity = MARKPARITY;
        return TRUE;
    case 'n':
        *parity = NOPARITY;
        return TRUE;
    case 'o':
        *parity = ODDPARITY;
        return TRUE;
    case 's':
        *parity = SPACEPARITY;
        return TRUE;
    }
    return FALSE;
}

/* "on", "off", "hs", and for the request to send also "tg", as the whole text that was held. */
static BOOL control_of(const COMM_TEXT *t, BOOL request, BYTE *control)
{
    SIZE_T length = (SIZE_T)(t->Last - t->Start + 1);
    const char *s = t->Start;
    if (length == 2 && lower(s[0]) == 'o' && lower(s[1]) == 'n') {
        *control = 1;
        return TRUE;
    }
    if (length == 3 && lower(s[0]) == 'o' && lower(s[1]) == 'f' && lower(s[2]) == 'f') {
        *control = 0;
        return TRUE;
    }
    if (length == 2 && lower(s[0]) == 'h' && lower(s[1]) == 's') {
        *control = 2;
        return TRUE;
    }
    if (request && length == 2 && lower(s[0]) == 't' && lower(s[1]) == 'g') {
        *control = 3;
        return TRUE;
    }
    return FALSE;
}

/* the ports the machine has are named by the values of the device map; a name in front of the text is skipped. */
static void skip_device_name(COMM_TEXT *t)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_QUERY_VALUE, &key))
        return;
    for (DWORD i = 0;; i++) {
        WCHAR name[64];
        BYTE data[128];
        DWORD name_length = RTL_NUMBER_OF(name), size = sizeof(data) - sizeof(WCHAR), type;
        if (RegEnumValueW(key, i, name, &name_length, nullptr, &type, data, &size))
            break;
        if (type != REG_SZ)
            continue;
        char port[64];
        ((WCHAR *)data)[size / sizeof(WCHAR)] = 0;
        if (!WideCharToMultiByte(CP_ACP, 0, (WCHAR *)data, -1, port, sizeof(port), nullptr, nullptr))
            continue;
        if (match(t, port)) {
            advance(t);
            if (match(t, ":"))
                advance(t);
        }
    }
    RegCloseKey(key);
}

typedef struct COMM_SETTINGS {
    BOOL BaudSet, ParitySet, DataSet, StopSet, TimeoutSet, XonSet, OdsrSet, IdsrSet, OctsSet, DtrSet, RtsSet;
    DWORD Baud;
    BYTE Parity, Data, Stop;
    BOOL TimeoutOn, Xon, Odsr, Idsr, Octs;
    BYTE Dtr, Rts;
    BOOL BaudKeyword;
} COMM_SETTINGS;

static BOOL parse_old(COMM_TEXT *t, COMM_SETTINGS *s)
{
    s->XonSet = s->DtrSet = s->RtsSet = s->OdsrSet = s->OctsSet = s->BaudSet = TRUE;
    s->Dtr = s->Rts = 1;
    s->Baud = baud_rate(number(t));
    advance(t);
    if (match(t, ",")) {
        advance(t);
        if (!match(t, ",") && match(t, "@")) {
            if (!parity_of(*t->Start, &s->Parity))
                return FALSE;
            s->ParitySet = TRUE;
            advance(t);
        }
        if (match(t, ",")) {
            advance(t);
            if (match(t, "#")) {
                DWORD bits = number(t);
                if (bits < 5 || bits > 8)
                    return FALSE;
                s->Data = (BYTE)bits;
                s->DataSet = TRUE;
                advance(t);
            }
            if (match(t, ",")) {
                advance(t);
                if (match(t, "1.5")) {
                    s->Stop = ONE5STOPBITS;
                    s->StopSet = TRUE;
                    advance(t);
                } else if (match(t, "#")) {
                    DWORD stop = number(t);
                    if (stop != 1 && stop != 2)
                        return FALSE;
                    s->Stop = stop == 1 ? ONESTOPBIT : TWOSTOPBITS;
                    s->StopSet = TRUE;
                    advance(t);
                }
                if (match(t, ",")) {
                    advance(t);
                    if (match(t, "x")) {
                        s->Xon = TRUE;
                        advance(t);
                    } else if (match(t, "p")) {
                        s->Dtr = s->Rts = 2;
                        s->Odsr = s->Octs = TRUE;
                        advance(t);
                    }
                }
            }
        }
    }
    return *t->Cursor == 0;
}

static BOOL parse_keywords(COMM_TEXT *t, COMM_SETTINGS *s)
{
    while (*t->Cursor) {
        if (match(t, "BAUD=#")) {
            s->Baud = baud_rate(number(t));
            s->BaudSet = s->BaudKeyword = TRUE;
        } else if (match(t, "PARITY=@")) {
            if (!parity_of(*t->Start, &s->Parity))
                return FALSE;
            s->ParitySet = TRUE;
        } else if (match(t, "DATA=#")) {
            DWORD bits = number(t);
            if (bits < 5 || bits > 8)
                return FALSE;
            s->Data = (BYTE)bits;
            s->DataSet = TRUE;
        } else if (match(t, "STOP=1.5")) {
            s->Stop = ONE5STOPBITS;
            s->StopSet = TRUE;
        } else if (match(t, "STOP=#")) {
            DWORD stop = number(t);
            if (stop != 1 && stop != 2)
                return FALSE;
            s->Stop = stop == 1 ? ONESTOPBIT : TWOSTOPBITS;
            s->StopSet = TRUE;
        } else if (match(t, "TO=ON") || match(t, "TO=OFF")) {
            s->TimeoutSet = TRUE;
            s->TimeoutOn = lower(t->End[-1]) == 'n';
        } else if (match(t, "XON=ON") || match(t, "XON=OFF")) {
            s->XonSet = TRUE;
            s->Xon = lower(t->End[-1]) == 'n';
        } else if (match(t, "ODSR=ON") || match(t, "ODSR=OFF")) {
            s->OdsrSet = TRUE;
            s->Odsr = lower(t->End[-1]) == 'n';
        } else if (match(t, "IDSR=ON") || match(t, "IDSR=OFF")) {
            s->IdsrSet = TRUE;
            s->Idsr = lower(t->End[-1]) == 'n';
        } else if (match(t, "OCTS=ON") || match(t, "OCTS=OFF")) {
            s->OctsSet = TRUE;
            s->Octs = lower(t->End[-1]) == 'n';
        } else if (match(t, "DTR=*")) {
            if (!control_of(t, FALSE, &s->Dtr))
                return FALSE;
            s->DtrSet = TRUE;
        } else if (match(t, "RTS=*")) {
            if (!control_of(t, TRUE, &s->Rts))
                return FALSE;
            s->RtsSet = TRUE;
        } else {
            return FALSE;
        }
        advance(t);
    }
    return TRUE;
}

static BOOL build_dcb(LPCSTR definition, LPDCB dcb, LPCOMMTIMEOUTS timeouts)
{
    COMM_TEXT text = {definition, definition, nullptr, nullptr};
    COMM_SETTINGS s = {};
    if (!definition || !dcb)
        return FALSE;
    skip_device_name(&text);
    text.Cursor = text.End = text.Cursor;
    BOOL ok = match(&text, "#") ? parse_old(&text, &s) : parse_keywords(&text, &s);
    if (!ok)
        return FALSE;
    BYTE *raw = (BYTE *)dcb;
    DWORD *flags = (DWORD *)(raw + DCB_FLAGS_OFFSET);
    if (s.BaudSet)
        *(DWORD *)(raw + 4) = s.Baud;
    if (s.DataSet)
        raw[18] = s.Data;
    if (s.StopSet)
        raw[20] = s.Stop;
    else
        raw[20] = s.BaudSet && s.Baud == 110 ? TWOSTOPBITS : ONESTOPBIT;
    if (s.ParitySet)
        raw[19] = s.Parity;
    if (s.XonSet)
        *flags = s.Xon ? *flags | 0x300 : *flags & ~0x300u;
    if (s.OctsSet)
        *flags = s.Octs ? *flags | 0x4 : *flags & ~0x4u;
    if (s.OdsrSet)
        *flags = s.Odsr ? *flags | 0x8 : *flags & ~0x8u;
    if (s.IdsrSet)
        *flags = s.Idsr ? *flags | 0x40 : *flags & ~0x40u;
    if (s.DtrSet)
        *flags = (*flags & ~0x30u) | (((DWORD)s.Dtr << 4) & 0x30);
    if (s.RtsSet)
        *flags = (*flags & ~0x3000u) | (((DWORD)s.Rts << 12) & 0x3000);
    if (s.TimeoutSet && timeouts) {
        memset(timeouts, 0, sizeof(*timeouts));
        timeouts->WriteTotalTimeoutConstant = s.TimeoutOn ? 60000 : 0;
    }
    return TRUE;
}

BOOL WINAPI BuildCommDCBAndTimeoutsA(LPCSTR definition, LPDCB dcb, LPCOMMTIMEOUTS timeouts)
{
    COMMTIMEOUTS ignored;
    if (!build_dcb(definition, dcb, timeouts ? timeouts : &ignored)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI BuildCommDCBA(LPCSTR definition, LPDCB dcb)
{
    return BuildCommDCBAndTimeoutsA(definition, dcb, nullptr);
}

BOOL WINAPI BuildCommDCBAndTimeoutsW(LPCWSTR definition, LPDCB dcb, LPCOMMTIMEOUTS timeouts)
{
    PSTR ansi = KbWideToAnsi(definition);
    if (!ansi)
        return FALSE;
    BOOL ok = BuildCommDCBAndTimeoutsA(ansi, dcb, timeouts);
    KbFree(ansi);
    return ok;
}

BOOL WINAPI BuildCommDCBW(LPCWSTR definition, LPDCB dcb)
{
    return BuildCommDCBAndTimeoutsW(definition, dcb, nullptr);
}
