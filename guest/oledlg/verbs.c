/*
 * the verb menu of an embedded object: the item of the Edit menu that opens the object, a popup of its verbs when it has
 * several, and the convert command.
 */
#define OLEDLGAPI
#include <mime/win32/oledlg.h>
#include <mime/win32/oleidl.h>
#include <mime/win32/winuser.h>

MIME_DEFINE_GUID(IID_IOleLink_, 0x0000011d, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

#define VERB_ON_CONTAINER_MENU 2

/* the methods of IOleObject up to the ones this file calls, in the order of the interface. */
struct IOleObject {
    const struct OleObjectVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(IOleObject *This, REFIID riid, void **object);
        ULONG(STDMETHODCALLTYPE *AddRef)(IOleObject *This);
        ULONG(STDMETHODCALLTYPE *Release)(IOleObject *This);
        void *SetClientSite, *GetClientSite, *SetHostNames, *Close, *SetMoniker, *GetMoniker, *InitFromData,
            *GetClipboardData, *DoVerb;
        HRESULT(STDMETHODCALLTYPE *EnumVerbs)(IOleObject *This, IEnumOLEVERB **verbs);
        void *Update, *IsUpToDate, *GetUserClassID;
        HRESULT(STDMETHODCALLTYPE *GetUserType)(IOleObject *This, DWORD form, LPOLESTR *type);
    } *lpVtbl;
};

/* the strings of the dialog library that name the object in the menu. */
#define TEXT_OBJECT L"&Object"
#define TEXT_LINKED_VERB L"%1 Linked %2 &Object"
#define TEXT_VERB L"%1 %2 &Object"
#define TEXT_LINKED_TYPE L"Linked %s &Object"
#define TEXT_TYPE L"%s &Object"
#define TEXT_UNKNOWN L"Unknown"
#define TEXT_CONVERT L"&Convert..."

/* the text with %1 and %2 given the two strings. */
static void format_text(WCHAR *out, const WCHAR *format, const WCHAR *first, const WCHAR *second, int size)
{
    int length = 0;
    for (; *format && length < size - 1; format++) {
        const WCHAR *insert = nullptr;
        if (format[0] == L'%' && format[1] == L'1')
            insert = first;
        else if (format[0] == L'%' && format[1] == L'2')
            insert = second;
        if (!insert) {
            out[length++] = *format;
            continue;
        }
        format++;
        for (; *insert && length < size - 1; insert++)
            out[length++] = *insert;
    }
    out[length] = 0;
}

static void format_type(WCHAR *out, const WCHAR *format, const WCHAR *type, int size)
{
    int length = 0;
    for (; *format && length < size - 1; format++) {
        if (format[0] == L'%' && format[1] == L's') {
            format++;
            for (const WCHAR *p = type; *p && length < size - 1; p++)
                out[length++] = *p;
        } else {
            out[length++] = *format;
        }
    }
    out[length] = 0;
}

/* the text without its ampersands, the character after one taken in its place. */
static void strip_accelerators(WCHAR *text)
{
    WCHAR *to = text;
    for (const WCHAR *from = text; *from; from++) {
        while (*from == L'&')
            from++;
        if (!*from)
            break;
        *to++ = *from;
    }
    *to = 0;
}

OLEDLGAPI BOOL WINAPI OleUIAddVerbMenuW(IOleObject *object, LPCWSTR short_type, HMENU menu, UINT position, UINT id_min,
                                        UINT id_max, BOOL add_convert, UINT id_convert, HMENU *popup)
{
    UINT new_id = id_min, flags = MF_BYPOSITION;
    WCHAR text[256];
    LPWSTR verb_name = nullptr, type = (LPWSTR)short_type;
    IEnumOLEVERB *verbs = nullptr;
    int count = 0;
    BOOL result = FALSE, linked = FALSE, convert = add_convert && id_convert != 0;

    DeleteMenu(menu, position, MF_BYPOSITION);
    /* the popup is cleared before the object is looked at, so it is empty when there is none. */
    if (popup)
        *popup = nullptr;
    if (popup && object) {
        if (!short_type)
            object->lpVtbl->GetUserType(object, USERCLASSTYPE_SHORT, &type);
        IOleObject *link = nullptr;
        if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IOleLink_, (void **)&link)) && link) {
            link->lpVtbl->Release(link);
            linked = TRUE;
        }
        object->lpVtbl->EnumVerbs(object, &verbs);
        *popup = CreatePopupMenu();
        BOOL failed = *popup == nullptr;
        while (!failed && verbs) {
            OLEVERB verb;
            if (verbs->lpVtbl->Next(verbs, 1, &verb, nullptr) != S_OK)
                break;
            if (verb.lVerb >= 0 && (verb.grfAttribs & VERB_ON_CONTAINER_MENU)) {
                CoTaskMemFree(verb_name);
                verb_name = verb.lpszVerbName;
                if (!id_max || id_max >= (UINT)verb.lVerb + id_min) {
                    if (!InsertMenuW(*popup, 0xFFFFFFFF, verb.fuFlags | MF_BYPOSITION, verb.lVerb + id_min, verb.lpszVerbName))
                        failed = TRUE;
                    else
                        count++;
                }
            } else {
                CoTaskMemFree(verb.lpszVerbName);
            }
        }
        if (!failed && convert) {
            UINT id;
            if (count) {
                new_id = id_min;
                failed = !InsertMenuW(*popup, 0xFFFFFFFF, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
                id = id_convert;
            } else {
                /* the only item is the conversion, and the name in the menu is the command without its dots. */
                new_id = id_convert;
                id = id_convert;
                CoTaskMemFree(verb_name);
                verb_name = CoTaskMemAlloc(sizeof(TEXT_CONVERT));
                if (verb_name) {
                    memcpy(verb_name, TEXT_CONVERT, sizeof(TEXT_CONVERT));
                    for (WCHAR *p = verb_name; *p; p++)
                        if (*p == L'.') {
                            *p = 0;
                            break;
                        }
                }
            }
            if (!failed) {
                failed = !InsertMenuW(*popup, 0xFFFFFFFF, MF_BYPOSITION, id, TEXT_CONVERT);
                if (!failed)
                    count++;
            }
        }
        if (!failed) {
            const WCHAR *object_type = type ? type : TEXT_UNKNOWN;
            UINT item_id;
            if (count == 1) {
                if (verb_name)
                    strip_accelerators(verb_name);
                format_text(text, linked ? TEXT_LINKED_VERB : TEXT_VERB, verb_name ? verb_name : L"", type ? type : L"", 256);
                if (convert)
                    wcscat(text, L"...");
                DestroyMenu(*popup);
                *popup = nullptr;
                item_id = new_id;
                result = TRUE;
            } else if (count > 1) {
                format_type(text, linked ? TEXT_LINKED_TYPE : TEXT_TYPE, object_type, 256);
                item_id = (UINT)(ULONG_PTR)*popup;
                new_id = item_id;
                flags |= MF_POPUP;
                result = TRUE;
            } else {
                format_type(text, linked ? TEXT_LINKED_TYPE : TEXT_TYPE, object_type, 256);
                flags |= MF_GRAYED;
                DestroyMenu(*popup);
                *popup = nullptr;
                item_id = new_id;
            }
            if (!InsertMenuW(menu, position, flags, item_id, text)) {
                result = FALSE;
                failed = TRUE;
            }
        }
        if (failed) {
            result = FALSE;
            InsertMenuW(menu, position, (flags & ~MF_POPUP) | MF_GRAYED, new_id, TEXT_OBJECT);
        }
    } else {
        InsertMenuW(menu, position, flags | MF_GRAYED, new_id, TEXT_OBJECT);
    }
    HWND active = GetActiveWindow();
    if (GetMenu(active) == menu)
        DrawMenuBar(active);
    CoTaskMemFree(verb_name);
    if (!short_type && type)
        CoTaskMemFree(type);
    if (verbs)
        verbs->lpVtbl->Release(verbs);
    return result;
}

OLEDLGAPI BOOL WINAPI OleUIAddVerbMenuA(IOleObject *object, LPCSTR short_type, HMENU menu, UINT position, UINT id_min,
                                        UINT id_max, BOOL add_convert, UINT id_convert, HMENU *popup)
{
    WCHAR *wide = nullptr;
    if (short_type) {
        int count = MultiByteToWideChar(CP_ACP, 0, short_type, -1, nullptr, 0);
        wide = CoTaskMemAlloc(count * sizeof(WCHAR));
        if (wide)
            MultiByteToWideChar(CP_ACP, 0, short_type, -1, wide, count);
    }
    BOOL result = OleUIAddVerbMenuW(object, wide, menu, position, id_min, id_max, add_convert, id_convert, popup);
    CoTaskMemFree(wide);
    return result;
}
