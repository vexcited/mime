/* what the color, font and print dialogs share, see dlgbase.h. */
#include "dlgbase.h"

static BYTE *put_word(BYTE *p, WORD v)
{
    *(WORD *)p = v;
    return p + 2;
}

static BYTE *put_dword(BYTE *p, DWORD v)
{
    *(DWORD *)p = v;
    return p + 4;
}

static BYTE *put_string(BYTE *p, LPCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    memcpy(p, text, bytes);
    return p + bytes;
}

static BYTE *align4(BYTE *start, BYTE *p)
{
    return start + (((SIZE_T)(p - start) + 3) & ~(SIZE_T)3);
}

LPDLGTEMPLATEW DlgBuild(LPCWSTR title, DWORD style, int width, int height, const CTL *controls, UINT count)
{
    static const WCHAR font[] = L"Segoe UI";
    SIZE_T size = 64 + (wcslen(title) + 1) * sizeof(WCHAR) + sizeof(font);
    for (UINT i = 0; i < count; i++)
        size += 32 + (controls[i].Text ? (wcslen(controls[i].Text) + 1) * sizeof(WCHAR) : 2);
    BYTE *start = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!start)
        return nullptr;
    BYTE *p = start;
    p = put_dword(p, style | DS_SETFONT);
    p = put_dword(p, 0);
    p = put_word(p, (WORD)count);
    p = put_word(p, 0);
    p = put_word(p, 0);
    p = put_word(p, (WORD)width);
    p = put_word(p, (WORD)height);
    p = put_word(p, 0);
    p = put_word(p, 0);
    p = put_string(p, title);
    p = put_word(p, 9);
    p = put_string(p, font);
    for (UINT i = 0; i < count; i++) {
        const CTL *c = &controls[i];
        p = align4(start, p);
        p = put_dword(p, c->Style | WS_CHILD);
        p = put_dword(p, 0);
        p = put_word(p, (WORD)c->X);
        p = put_word(p, (WORD)c->Y);
        p = put_word(p, (WORD)c->W);
        p = put_word(p, (WORD)c->H);
        p = put_word(p, c->Id);
        p = put_word(p, 0xFFFF);
        p = put_word(p, c->Class);
        p = put_string(p, c->Text ? c->Text : L"");
        p = put_word(p, 0);
    }
    return (LPDLGTEMPLATEW)start;
}

const void *DlgFindTemplate(const DLGSOURCE *source, DWORD *error)
{
    *error = 0;
    HGLOBAL global;
    if (source->Flags & source->TemplateFlag) {
        HRSRC found = source->Ansi ? FindResourceA(source->Instance, (LPCSTR)source->Name, (LPCSTR)RT_DIALOG)
                                   : FindResourceW(source->Instance, (LPCWSTR)source->Name, MAKEINTRESOURCEW(5));
        if (!found) {
            *error = CDERR_FINDRESFAILURE;
            return nullptr;
        }
        global = LoadResource(source->Instance, found);
        if (!global) {
            *error = CDERR_LOADRESFAILURE;
            return nullptr;
        }
    } else if (source->Flags & source->HandleFlag) {
        global = (HGLOBAL)source->Instance;
    } else {
        return nullptr;
    }
    const void *data = LockResource(global);
    if (!data)
        *error = CDERR_LOCKRESFAILURE;
    return data;
}

UINT DlgMessage(UINT *cache, LPCWSTR name)
{
    if (!*cache)
        *cache = RegisterWindowMessageW(name);
    return *cache;
}

BOOL DlgHookValid(DWORD flags, DWORD hook_flag, const void *hook, DWORD *error)
{
    if ((flags & hook_flag) && !hook) {
        *error = CDERR_NOHOOK;
        return FALSE;
    }
    return TRUE;
}

void DlgToWide(LPCSTR ansi, LPWSTR wide, int chars)
{
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, chars) && chars)
        wide[chars - 1] = 0;
}

void DlgToAnsi(LPCWSTR wide, LPSTR ansi, int chars)
{
    if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, ansi, chars, nullptr, nullptr) && chars)
        ansi[chars - 1] = 0;
}

void DlgFillRect(HDC dc, const RECT *rect, COLORREF color)
{
    SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
}
