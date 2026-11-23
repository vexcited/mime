/*
 * the device query calls: DevGetObjects, DevFreeObjects, DevCreateObjectQuery and DevCloseObjectQuery. windows asks
 * the kernel's device query driver, here the objects are the ones the other calls of this library know and the
 * filter runs in the caller.
 *
 * measured on windows 11 26100 for all of this unless a comment says read:
 *
 * - the objects of a device are every device with the root, of an interface every link, of the two class types the
 *   class keys by their guid in lower case, of a container the container ids of the devices. the type 0 and types
 *   past the last one give no object and no error.
 * - a requested property an object does not have comes back with the type DEVPROP_TYPE_EMPTY, no size and no buffer.
 *   a key of another store than the system's is always that. a key with a locale is refused.
 * - a filter is a row of comparisons, which must all hold, or one group. a group opens with one of the logical
 *   operators and closes with its partner, holds comparisons and groups, one only for a negation, and is refused
 *   when it is empty, left open or closed by another partner. what follows the closing of the first group is not
 *   read. a logical operator after a comparison at the top is refused.
 * - a comparison whose value has no buffer or not the size of its type is refused. one against a property of
 *   another type does not hold. strings compare by case unless the operator says otherwise.
 * - DevGetObjects takes the flags for all properties and for localization, the others are refused, and so is a
 *   count without its array. a failure leaves the outputs as they were.
 * - a query calls back with one addition per object and then with the state that says the enumeration is done. a
 *   query that was closed calls back no more.
 */
#define MIME_BUILD_CFGMGR32
#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/pnp.h>
#include <mime/win32/cfgmgr32.h>
#include "cfgmgr32_private.h"

/* the float and double properties compare as numbers. */
int _fltused = 0x9875;

#define QUERY_S_OK ((HRESULT)0)
#define QUERY_E_INVALIDARG ((HRESULT)0x80070057)
#define QUERY_E_OUTOFMEMORY ((HRESULT)0x8007000E)

enum {
    TYPE_INTERFACE = 1,
    TYPE_CONTAINER = 2,
    TYPE_DEVICE = 3,
    TYPE_INTERFACE_CLASS = 4,
    TYPE_INSTALLER_CLASS = 7,
};

enum {
    FLAG_UPDATE_RESULTS = 1,
    FLAG_ALL_PROPERTIES = 2,
    FLAG_LOCALIZE = 4,
    FLAG_ASYNC_CLOSE = 8,
};

enum {
    ACTION_STATE = 0,
    ACTION_ADD = 1,
    ACTION_UPDATE = 2,
    ACTION_REMOVE = 3,
    STATE_ENUM_COMPLETED = 1,
    STATE_ABORTED = 2,
    STATE_CLOSED = 3,
};

/* the operators of devfiltertypes.h. */
enum {
    OP_EXISTS = 1,
    OP_EQUALS = 2,
    OP_GREATER = 3,
    OP_LESS = 4,
    OP_GREATER_EQUALS = 5,
    OP_LESS_EQUALS = 6,
    OP_BITWISE_AND = 7,
    OP_BITWISE_OR = 8,
    OP_BEGINS_WITH = 9,
    OP_ENDS_WITH = 10,
    OP_CONTAINS = 11,
    OP_MASK_EVAL = 0x00000FFF,
    OP_LIST_CONTAINS = 0x00001000,
    OP_LIST_BEGINS_WITH = 0x00002000,
    OP_LIST_ENDS_WITH = 0x00003000,
    OP_LIST_ELEMENT_CONTAINS = 0x00004000,
    OP_MASK_LIST = 0x0000F000,
    OP_NOT = 0x00010000,
    OP_IGNORE_CASE = 0x00020000,
    OP_AND_OPEN = 0x00100000,
    OP_AND_CLOSE = 0x00200000,
    OP_OR_OPEN = 0x00300000,
    OP_OR_CLOSE = 0x00400000,
    OP_NOT_OPEN = 0x00500000,
    OP_NOT_CLOSE = 0x00600000,
    OP_MASK_LOGICAL = 0x0FF00000,
    OP_ARRAY_CONTAINS = 0x10000000,
};

typedef struct COMPOUND_KEY {
    DEVPROPKEY Key;
    ULONG Store;
    PCWSTR LocaleName;
} COMPOUND_KEY;

typedef struct PROPERTY {
    COMPOUND_KEY CompKey;
    DEVPROPTYPE Type;
    ULONG BufferSize;
    PVOID Buffer;
} PROPERTY;

typedef struct OBJECT {
    ULONG ObjectType;
    PCWSTR pszObjectId;
    ULONG cPropertyCount;
    const PROPERTY *pProperties;
} OBJECT;

typedef struct FILTER {
    ULONG Operator;
    PROPERTY Property;
} FILTER;

typedef struct ACTION_DATA {
    ULONG Action;
    union {
        ULONG State;
        OBJECT DeviceObject;
    } Data;
} ACTION_DATA;

typedef void(WINAPI *QUERY_CALLBACK)(HANDLE query, PVOID context, const ACTION_DATA *action);

/* an object that passed the filter, with the properties it goes out with. */
typedef struct FOUND {
    WCHAR *Id;
    ULONG Count;
    PROPERTY *Values;
} FOUND;

typedef struct FOUND_LIST {
    ULONG Count, Room;
    FOUND *Items;
} FOUND_LIST;

static void *alloc(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
}

static void release(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* ------------------------------------------------------------------ the objects of a type */

static void lower(WCHAR *text)
{
    for (; *text; text++)
        if (*text >= L'A' && *text <= L'Z')
            *text = (WCHAR)(*text - L'A' + L'a');
}

static BOOL add_id(WCHAR **list, ULONG *used, ULONG *room, PCWSTR id)
{
    ULONG chars = (ULONG)lstrlenW(id) + 1;
    for (PCWSTR p = *list; p && *p; p += lstrlenW(p) + 1)
        if (!lstrcmpiW(p, id))
            return TRUE;
    if (*used + chars + 1 > *room) {
        ULONG wanted = (*used + chars + 1) * 2;
        WCHAR *grown = *list ? HeapReAlloc(GetProcessHeap(), 0, *list, wanted * sizeof(WCHAR)) : alloc(wanted * sizeof(WCHAR));
        if (!grown)
            return FALSE;
        *list = grown;
        *room = wanted;
    }
    memcpy(*list + *used, id, chars * sizeof(WCHAR));
    *used += chars;
    (*list)[*used] = 0;
    return TRUE;
}

static BOOL fetch(ULONG type, PCWSTR id, const DEVPROPKEY *key, DEVPROPTYPE *kind, BYTE **data, ULONG *size);

/* the ids of the objects of a type, a multi-sz the caller frees. */
static WCHAR *object_ids(ULONG type)
{
    WCHAR *list = nullptr;
    ULONG used = 0, room = 0;
    if (!add_id(&list, &used, &room, L""))
        return nullptr;
    used = 0;
    list[0] = 0;
    if (type == TYPE_DEVICE || type == TYPE_CONTAINER) {
        ULONG chars = 0;
        if (CM_Get_Device_ID_List_SizeW(&chars, nullptr, 0) || !chars)
            return list;
        WCHAR *devices = alloc(chars * sizeof(WCHAR));
        if (devices && !CM_Get_Device_ID_ListW(nullptr, devices, chars, 0)) {
            for (PCWSTR p = devices; *p; p += lstrlenW(p) + 1) {
                if (type == TYPE_DEVICE) {
                    add_id(&list, &used, &room, p);
                    continue;
                }
                DEVPROPTYPE kind;
                BYTE *value;
                ULONG size;
                if (!fetch(TYPE_DEVICE, p, &DEVPKEY_Device_ContainerId, &kind, &value, &size))
                    continue;
                if (kind == DEVPROP_TYPE_GUID && size == sizeof(GUID)) {
                    WCHAR text[MAX_GUID_STRING_LEN];
                    format_guid((const GUID *)value, text);
                    lower(text);
                    add_id(&list, &used, &room, text);
                }
                release(value);
            }
        }
        release(devices);
    } else if (type == TYPE_INTERFACE_CLASS || type == TYPE_INSTALLER_CLASS) {
        GUID klass;
        for (ULONG i = 0; !CM_Enumerate_Classes(i, &klass, type == TYPE_INTERFACE_CLASS); i++) {
            WCHAR text[MAX_GUID_STRING_LEN];
            format_guid(&klass, text);
            lower(text);
            add_id(&list, &used, &room, text);
        }
    } else if (type == TYPE_INTERFACE) {
        MIME_PNP_REQUEST *r;
        if (NT_SUCCESS(request(MIME_PNP_LIST_INTERFACES, 1, nullptr, nullptr, &r))) {
            for (PCWSTR p = r->Out; *p; p += lstrlenW(p) + 1)
                add_id(&list, &used, &room, p);
            release(r);
        }
        GUID klass;
        for (ULONG i = 0; !CM_Enumerate_Classes(i, &klass, 1); i++) {
            ULONG chars;
            WCHAR *registered = registered_interfaces(&klass, nullptr, &chars);
            for (PCWSTR p = registered; p && *p; p += lstrlenW(p) + 1)
                add_id(&list, &used, &room, p);
            release(registered);
        }
    }
    return list;
}

/* ------------------------------------------------------------------ the properties of an object */

static CONFIGRET fetch_into(ULONG type, PCWSTR id, const DEVPROPKEY *key, DEVPROPTYPE *kind, BYTE *buffer, ULONG *size)
{
    GUID klass;
    DEVINST node;
    switch (type) {
    case TYPE_DEVICE:
        if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_PHANTOM))
            return CR_NO_SUCH_DEVNODE;
        return CM_Get_DevNode_PropertyW(node, key, kind, buffer, size, 0);
    case TYPE_INTERFACE:
        return CM_Get_Device_Interface_PropertyW(id, key, kind, buffer, size, 0);
    case TYPE_INTERFACE_CLASS:
    case TYPE_INSTALLER_CLASS:
        if (!parse_guid(id, &klass))
            return CR_INVALID_DATA;
        return CM_Get_Class_PropertyW(&klass, key, kind, buffer, size, type == TYPE_INTERFACE_CLASS);
    default:
        return CR_NO_SUCH_VALUE;
    }
}

/* a property of an object, in memory the caller frees. FALSE when the object does not have it. */
static BOOL fetch(ULONG type, PCWSTR id, const DEVPROPKEY *key, DEVPROPTYPE *kind, BYTE **data, ULONG *size)
{
    BYTE small[512];
    ULONG bytes = sizeof(small);
    *kind = DEVPROP_TYPE_EMPTY;
    CONFIGRET cr = fetch_into(type, id, key, kind, small, &bytes);
    BYTE *copy = nullptr;
    if (cr == CR_BUFFER_SMALL) {
        copy = alloc(bytes);
        if (!copy)
            return FALSE;
        cr = fetch_into(type, id, key, kind, copy, &bytes);
    } else if (!cr) {
        copy = alloc(bytes);
        if (copy)
            memcpy(copy, small, bytes);
    }
    if (cr || !copy || *kind == DEVPROP_TYPE_EMPTY) {
        release(copy);
        return FALSE;
    }
    *data = copy;
    *size = bytes;
    return TRUE;
}

static ULONG all_keys(ULONG type, PCWSTR id, DEVPROPKEY *keys, ULONG room)
{
    ULONG count = room;
    GUID klass;
    DEVINST node;
    CONFIGRET cr = CR_FAILURE;
    if (type == TYPE_DEVICE && !CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_PHANTOM))
        cr = CM_Get_DevNode_Property_Keys(node, keys, &count, 0);
    else if (type == TYPE_INTERFACE)
        cr = CM_Get_Device_Interface_Property_KeysW(id, keys, &count, 0);
    else if ((type == TYPE_INTERFACE_CLASS || type == TYPE_INSTALLER_CLASS) && parse_guid(id, &klass))
        cr = CM_Get_Class_Property_Keys(&klass, keys, &count, type == TYPE_INTERFACE_CLASS);
    if (cr == CR_BUFFER_SMALL)
        return room;
    return cr ? 0 : count;
}

/* ------------------------------------------------------------------ the filter */

static ULONG fixed_size(DEVPROPTYPE type)
{
    static const BYTE sizes[] = {0, 0, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 16, 16, 8, 8, 8, 1, 0, 0, 0, 20, 4, 4, 4, 0};
    return type < RTL_NUMBER_OF(sizes) ? sizes[type] : 0;
}

static BOOL is_logical(ULONG op)
{
    return (op & OP_MASK_LOGICAL) != 0;
}

static BOOL comparison_valid(const FILTER *f)
{
    ULONG eval = f->Operator & OP_MASK_EVAL;
    if (f->Property.CompKey.LocaleName)
        return FALSE;
    if (eval == OP_EXISTS || !(f->Operator & (OP_MASK_EVAL | OP_MASK_LIST | OP_ARRAY_CONTAINS)))
        return TRUE;
    if (!f->Property.Buffer || !f->Property.BufferSize)
        return FALSE;
    ULONG fixed = fixed_size(f->Property.Type);
    if (fixed && f->Property.BufferSize != fixed)
        return FALSE;
    if (f->Property.Type == DEVPROP_TYPE_STRING && (f->Property.BufferSize & 1))
        return FALSE;
    return TRUE;
}

/* the end of the group that opens at start, or 0 when the rows from there are no well formed group. */
static ULONG group_end(const FILTER *filters, ULONG count, ULONG start)
{
    ULONG open = filters[start].Operator, members = 0, i = start + 1;
    if (open != OP_AND_OPEN && open != OP_OR_OPEN && open != OP_NOT_OPEN)
        return 0;
    for (; i < count; members++) {
        ULONG op = filters[i].Operator;
        if (op == open + 0x00100000)
            return members && (open != OP_NOT_OPEN || members == 1) ? i : 0;
        if (is_logical(op)) {
            ULONG end = group_end(filters, count, i);
            if (!end)
                return 0;
            i = end + 1;
        } else {
            if (!comparison_valid(&filters[i]))
                return 0;
            i++;
        }
    }
    return 0;
}

static BOOL filter_valid(const FILTER *filters, ULONG count)
{
    if (!count)
        return TRUE;
    if (is_logical(filters[0].Operator))
        return group_end(filters, count, 0) != 0;
    for (ULONG i = 0; i < count; i++)
        if (is_logical(filters[i].Operator) || !comparison_valid(&filters[i]))
            return FALSE;
    return TRUE;
}

static WCHAR fold(WCHAR c, BOOL ignore_case)
{
    return ignore_case ? RtlUpcaseUnicodeChar(c) : c;
}

static BOOL text_equal(PCWSTR a, PCWSTR b, ULONG chars, BOOL ignore_case)
{
    for (ULONG i = 0; i < chars; i++)
        if (fold(a[i], ignore_case) != fold(b[i], ignore_case))
            return FALSE;
    return TRUE;
}

/* how the text a relates to the text b: the comparison is one of equals, begins, ends and contains. */
static BOOL text_test(ULONG eval, PCWSTR a, PCWSTR b, BOOL ignore_case)
{
    ULONG la = (ULONG)lstrlenW(a), lb = (ULONG)lstrlenW(b);
    if (lb > la)
        return FALSE;
    switch (eval) {
    case OP_EQUALS:
        return la == lb && text_equal(a, b, la, ignore_case);
    case OP_BEGINS_WITH:
        return text_equal(a, b, lb, ignore_case);
    case OP_ENDS_WITH:
        return text_equal(a + la - lb, b, lb, ignore_case);
    case OP_CONTAINS:
        for (ULONG i = 0; i + lb <= la; i++)
            if (text_equal(a + i, b, lb, ignore_case))
                return TRUE;
        return FALSE;
    default:
        return FALSE;
    }
}

static int text_order(PCWSTR a, PCWSTR b, BOOL ignore_case)
{
    for (;; a++, b++) {
        WCHAR x = fold(*a, ignore_case), y = fold(*b, ignore_case);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

/* the order of two numbers of a type, read: the signed types compare signed, the others as unsigned values. */
static int number_order(DEVPROPTYPE type, const BYTE *a, const BYTE *b, ULONG size)
{
    LONGLONG x = 0, y = 0;
    memcpy(&x, a, size);
    memcpy(&y, b, size);
    if (type == 2 || type == 4 || type == 6 || type == 8 || type == 0x0E) {
        ULONG shift = 64 - size * 8;
        x = (LONGLONG)((ULONGLONG)x << shift) >> shift;
        y = (LONGLONG)((ULONGLONG)y << shift) >> shift;
        return x < y ? -1 : x > y;
    }
    if (type == 0x0A) {
        float fx, fy;
        memcpy(&fx, a, 4);
        memcpy(&fy, b, 4);
        return fx < fy ? -1 : fx > fy;
    }
    if (type == 0x0B || type == 0x0F) {
        double dx, dy;
        memcpy(&dx, a, 8);
        memcpy(&dy, b, 8);
        return dx < dy ? -1 : dx > dy;
    }
    return (ULONGLONG)x < (ULONGLONG)y ? -1 : (ULONGLONG)x > (ULONGLONG)y;
}

static BOOL ordered(ULONG eval, int order)
{
    return eval == OP_GREATER ? order > 0 : eval == OP_LESS ? order < 0 : eval == OP_GREATER_EQUALS ? order >= 0 : order <= 0;
}

/* whether a value an object has passes one comparison. */
static BOOL value_test(ULONG op, DEVPROPTYPE kind, const BYTE *data, ULONG size, const PROPERTY *wanted)
{
    ULONG eval = op & OP_MASK_EVAL, list = op & OP_MASK_LIST;
    BOOL ignore_case = (op & OP_IGNORE_CASE) != 0;
    const BYTE *value = wanted->Buffer;
    if (op & OP_ARRAY_CONTAINS) {
        /* read: an array property holds an element equal to the value. */
        ULONG element = fixed_size(wanted->Type);
        if (kind != (wanted->Type | DEVPROP_TYPEMOD_ARRAY) || !element)
            return FALSE;
        for (ULONG at = 0; at + element <= size; at += element)
            if (!memcmp(data + at, value, element))
                return TRUE;
        return FALSE;
    }
    if (list) {
        /* read: one string of a list equals, begins with, ends with or contains the value. */
        if (kind != DEVPROP_TYPE_STRING_LIST || wanted->Type != DEVPROP_TYPE_STRING)
            return FALSE;
        ULONG how = list == OP_LIST_CONTAINS ? OP_EQUALS : list == OP_LIST_BEGINS_WITH ? OP_BEGINS_WITH
                    : list == OP_LIST_ENDS_WITH ? OP_ENDS_WITH : OP_CONTAINS;
        for (PCWSTR p = (PCWSTR)data; (const BYTE *)p < data + size && *p; p += lstrlenW(p) + 1)
            if (text_test(how, p, (PCWSTR)value, ignore_case))
                return TRUE;
        return FALSE;
    }
    if (kind != wanted->Type)
        return FALSE;
    if (kind == DEVPROP_TYPE_STRING) {
        if (eval == OP_EQUALS || (eval >= OP_BEGINS_WITH && eval <= OP_CONTAINS))
            return text_test(eval, (PCWSTR)data, (PCWSTR)value, ignore_case);
        if (eval >= OP_GREATER && eval <= OP_LESS_EQUALS)
            return ordered(eval, text_order((PCWSTR)data, (PCWSTR)value, ignore_case));
        return FALSE;
    }
    if (eval == OP_EQUALS)
        return size == wanted->BufferSize && !memcmp(data, value, size);
    ULONG fixed = fixed_size(kind);
    if (!fixed || fixed > 8 || size != fixed || kind == DEVPROP_TYPE_GUID)
        return FALSE;
    if (eval >= OP_GREATER && eval <= OP_LESS_EQUALS)
        return ordered(eval, number_order(kind, data, value, fixed));
    if (eval == OP_BITWISE_AND || eval == OP_BITWISE_OR) {
        /* read: the bits the two have in common, or the bits of either, are not none. */
        ULONGLONG x = 0, y = 0;
        memcpy(&x, data, fixed);
        memcpy(&y, value, fixed);
        return eval == OP_BITWISE_AND ? (x & y) != 0 : (x | y) != 0;
    }
    return FALSE;
}

static BOOL comparison_holds(ULONG type, PCWSTR id, const FILTER *f)
{
    DEVPROPTYPE kind;
    BYTE *data = nullptr;
    ULONG size = 0;
    ULONG op = f->Operator, eval = op & OP_MASK_EVAL;
    BOOL has = !f->Property.CompKey.Store && fetch(type, id, &f->Property.CompKey.Key, &kind, &data, &size);
    BOOL result;
    if (eval == OP_EXISTS && !(op & (OP_MASK_LIST | OP_ARRAY_CONTAINS)))
        result = has;
    else if (!(op & (OP_MASK_EVAL | OP_MASK_LIST | OP_ARRAY_CONTAINS)))
        return release(data), FALSE;
    else
        result = has && value_test(op, kind, data, size, &f->Property);
    release(data);
    return (op & OP_NOT) ? !result : result;
}

static BOOL group_holds(ULONG type, PCWSTR id, const FILTER *filters, ULONG count, ULONG start, ULONG *next)
{
    ULONG open = filters[start].Operator, i = start + 1;
    BOOL result = open != OP_OR_OPEN;
    while (i < count && filters[i].Operator != open + 0x00100000) {
        BOOL member;
        if (is_logical(filters[i].Operator)) {
            member = group_holds(type, id, filters, count, i, &i);
        } else {
            member = comparison_holds(type, id, &filters[i]);
            i++;
        }
        if (open == OP_OR_OPEN)
            result = result || member;
        else if (open == OP_NOT_OPEN)
            result = !member;
        else
            result = result && member;
    }
    *next = i + 1;
    return result;
}

static BOOL filter_holds(ULONG type, PCWSTR id, const FILTER *filters, ULONG count)
{
    if (!count)
        return TRUE;
    if (is_logical(filters[0].Operator)) {
        ULONG next;
        return group_holds(type, id, filters, count, 0, &next);
    }
    for (ULONG i = 0; i < count; i++)
        if (!comparison_holds(type, id, &filters[i]))
            return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ collecting */

static void found_free(FOUND *item)
{
    for (ULONG i = 0; i < item->Count; i++)
        release(item->Values[i].Buffer);
    release(item->Values);
    release(item->Id);
}

static void list_free(FOUND_LIST *list)
{
    for (ULONG i = 0; i < list->Count; i++)
        found_free(&list->Items[i]);
    release(list->Items);
    *list = (FOUND_LIST){0};
}

/* the properties an object goes out with: the requested ones in their order, or every one it has. */
static BOOL found_fill(FOUND *item, ULONG type, PCWSTR id, ULONG flags, ULONG key_count, const COMPOUND_KEY *keys)
{
    DEVPROPKEY every[128];
    ULONG count = key_count;
    if (flags & FLAG_ALL_PROPERTIES)
        count = all_keys(type, id, every, RTL_NUMBER_OF(every));
    ULONG chars = (ULONG)lstrlenW(id) + 1;
    item->Id = alloc(chars * sizeof(WCHAR));
    item->Values = alloc(count * sizeof(PROPERTY));
    if (!item->Id || !item->Values)
        return FALSE;
    memcpy(item->Id, id, chars * sizeof(WCHAR));
    item->Count = count;
    for (ULONG i = 0; i < count; i++) {
        PROPERTY *p = &item->Values[i];
        if (flags & FLAG_ALL_PROPERTIES)
            p->CompKey.Key = every[i];
        else
            p->CompKey = keys[i];
        p->CompKey.LocaleName = nullptr;
        BYTE *data;
        if (!p->CompKey.Store && fetch(type, id, &p->CompKey.Key, &p->Type, &data, &p->BufferSize))
            p->Buffer = data;
        else
            p->Type = DEVPROP_TYPE_EMPTY, p->BufferSize = 0;
    }
    return TRUE;
}

static HRESULT collect(ULONG type, ULONG flags, ULONG key_count, const COMPOUND_KEY *keys, ULONG filter_count,
                       const FILTER *filters, FOUND_LIST *out)
{
    *out = (FOUND_LIST){0};
    WCHAR *ids = object_ids(type);
    if (!ids)
        return QUERY_E_OUTOFMEMORY;
    HRESULT hr = QUERY_S_OK;
    for (PCWSTR id = ids; *id && hr == QUERY_S_OK; id += lstrlenW(id) + 1) {
        if (!filter_holds(type, id, filters, filter_count))
            continue;
        if (out->Count == out->Room) {
            ULONG room = out->Room ? out->Room * 2 : 32;
            FOUND *grown = out->Items ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, out->Items, room * sizeof(FOUND))
                                      : alloc(room * sizeof(FOUND));
            if (!grown) {
                hr = QUERY_E_OUTOFMEMORY;
                break;
            }
            out->Items = grown;
            out->Room = room;
        }
        if (!found_fill(&out->Items[out->Count++], type, id, flags, key_count, keys))
            hr = QUERY_E_OUTOFMEMORY;
    }
    release(ids);
    if (hr != QUERY_S_OK)
        list_free(out);
    return hr;
}

static ULONG round8(ULONG n)
{
    return (n + 7) & ~7u;
}

/* the objects as one block: the array, then the property arrays, then the ids and the values. */
static OBJECT *pack(ULONG type, const FOUND *items, ULONG count)
{
    ULONG head = count * sizeof(OBJECT), properties = 0, data = 0;
    for (ULONG i = 0; i < count; i++) {
        properties += items[i].Count * sizeof(PROPERTY);
        data += round8((ULONG)(lstrlenW(items[i].Id) + 1) * sizeof(WCHAR));
        for (ULONG j = 0; j < items[i].Count; j++)
            data += round8(items[i].Values[j].BufferSize);
    }
    BYTE *block = alloc(head + properties + data);
    if (!block)
        return nullptr;
    OBJECT *objects = (OBJECT *)block;
    PROPERTY *next_property = (PROPERTY *)(block + head);
    BYTE *next_data = block + head + properties;
    for (ULONG i = 0; i < count; i++) {
        ULONG id_bytes = (ULONG)(lstrlenW(items[i].Id) + 1) * sizeof(WCHAR);
        memcpy(next_data, items[i].Id, id_bytes);
        objects[i].ObjectType = type;
        objects[i].pszObjectId = (PCWSTR)next_data;
        next_data += round8(id_bytes);
        objects[i].cPropertyCount = items[i].Count;
        objects[i].pProperties = items[i].Count ? next_property : nullptr;
        for (ULONG j = 0; j < items[i].Count; j++, next_property++) {
            *next_property = items[i].Values[j];
            if (next_property->Buffer) {
                memcpy(next_data, items[i].Values[j].Buffer, next_property->BufferSize);
                next_property->Buffer = next_data;
                next_data += round8(next_property->BufferSize);
            }
        }
    }
    return objects;
}

static BOOL arguments_valid(ULONG flags, ULONG allowed, ULONG key_count, const COMPOUND_KEY *keys, ULONG filter_count,
                            const FILTER *filters)
{
    if ((flags & ~allowed) || (key_count && !keys) || (filter_count && !filters))
        return FALSE;
    /* read: every property is asked for with no list of properties. */
    if ((flags & FLAG_ALL_PROPERTIES) && key_count)
        return FALSE;
    for (ULONG i = 0; i < key_count; i++)
        if (keys[i].LocaleName)
            return FALSE;
    return filter_valid(filters, filter_count);
}

CMAPI HRESULT WINAPI DevGetObjects(ULONG type, ULONG flags, ULONG key_count, const COMPOUND_KEY *keys, ULONG filter_count,
                                   const FILTER *filters, ULONG *count, const OBJECT **objects)
{
    if (!count || !objects)
        return QUERY_E_INVALIDARG;
    if (!arguments_valid(flags, FLAG_ALL_PROPERTIES | FLAG_LOCALIZE, key_count, keys, filter_count, filters))
        return QUERY_E_INVALIDARG;
    FOUND_LIST list;
    HRESULT hr = collect(type, flags, key_count, keys, filter_count, filters, &list);
    if (hr != QUERY_S_OK)
        return hr;
    OBJECT *block = list.Count ? pack(type, list.Items, list.Count) : nullptr;
    if (list.Count && !block)
        hr = QUERY_E_OUTOFMEMORY;
    if (hr == QUERY_S_OK) {
        *count = list.Count;
        *objects = block;
    }
    list_free(&list);
    return hr;
}

CMAPI void WINAPI DevFreeObjects(ULONG count, const OBJECT *objects)
{
    release((void *)objects);
}

/* ------------------------------------------------------------------ queries that call back */

#define QUERY_SIGNATURE 0x79725144

typedef struct QUERY {
    ULONG Signature;
    ULONG Type, Flags;
    ULONG KeyCount, FilterCount;
    COMPOUND_KEY *Keys;
    FILTER *Filters;
    QUERY_CALLBACK Callback;
    PVOID Context;
    HANDLE Thread;
    ULONG ThreadId;
    volatile LONG Closing;
} QUERY;

static void query_free(QUERY *q)
{
    for (ULONG i = 0; i < q->FilterCount; i++)
        release(q->Filters[i].Property.Buffer);
    release(q->Filters);
    release(q->Keys);
    if (q->Thread)
        CloseHandle(q->Thread);
    q->Signature = 0;
    release(q);
}

static void call_state(QUERY *q, ULONG state)
{
    ACTION_DATA action = {ACTION_STATE, {.State = state}};
    q->Callback(q, q->Context, &action);
}

static void call_object(QUERY *q, ULONG kind, const FOUND *item)
{
    OBJECT *object = pack(q->Type, item, 1);
    if (!object)
        return;
    ACTION_DATA action = {kind, {.DeviceObject = *object}};
    q->Callback(q, q->Context, &action);
    release(object);
}

static const FOUND *found_in(const FOUND_LIST *list, PCWSTR id)
{
    for (ULONG i = 0; i < list->Count; i++)
        if (!lstrcmpiW(list->Items[i].Id, id))
            return &list->Items[i];
    return nullptr;
}

static BOOL found_same(const FOUND *a, const FOUND *b)
{
    if (a->Count != b->Count)
        return FALSE;
    for (ULONG i = 0; i < a->Count; i++) {
        const PROPERTY *x = &a->Values[i], *y = &b->Values[i];
        if (x->Type != y->Type || x->BufferSize != y->BufferSize ||
            memcmp(&x->CompKey.Key, &y->CompKey.Key, sizeof(DEVPROPKEY)) ||
            (x->BufferSize && memcmp(x->Buffer, y->Buffer, x->BufferSize)))
            return FALSE;
    }
    return TRUE;
}

/*
 * the thread of a query: the objects there are, the state that ends the enumeration, and for a query that asked for
 * updates what came, changed and went each time the kernel's device tree moves. read, the header of the calls: a
 * removal names the object without properties, and a query closed without waiting ends with the closed state.
 */
static DWORD WINAPI query_thread(void *param)
{
    QUERY *q = param;
    FOUND_LIST seen;
    HRESULT hr = collect(q->Type, q->Flags, q->KeyCount, q->Keys, q->FilterCount, q->Filters, &seen);
    for (ULONG i = 0; hr == QUERY_S_OK && i < seen.Count && !q->Closing; i++)
        call_object(q, ACTION_ADD, &seen.Items[i]);
    if (!q->Closing)
        call_state(q, hr == QUERY_S_OK ? STATE_ENUM_COMPLETED : STATE_ABORTED);
    ULONG generation = 0xFFFFFFFF;
    while (hr == QUERY_S_OK && (q->Flags & FLAG_UPDATE_RESULTS) && !q->Closing) {
        MIME_PNP_REQUEST *r;
        NTSTATUS status = request(MIME_PNP_WAIT_CHANGE, generation, nullptr, nullptr, &r);
        if (!NT_SUCCESS(status)) {
            if (status == STATUS_ALERTED || status == STATUS_USER_APC)
                continue;
            break;
        }
        BOOL first = generation == 0xFFFFFFFF;
        generation = r->Flags;
        release(r);
        if (first || q->Closing)
            continue;
        FOUND_LIST now;
        if (collect(q->Type, q->Flags, q->KeyCount, q->Keys, q->FilterCount, q->Filters, &now) != QUERY_S_OK)
            continue;
        for (ULONG i = 0; i < seen.Count && !q->Closing; i++)
            if (!found_in(&now, seen.Items[i].Id)) {
                FOUND gone = {seen.Items[i].Id, 0, nullptr};
                call_object(q, ACTION_REMOVE, &gone);
            }
        for (ULONG i = 0; i < now.Count && !q->Closing; i++) {
            const FOUND *before = found_in(&seen, now.Items[i].Id);
            if (!before)
                call_object(q, ACTION_ADD, &now.Items[i]);
            else if (!found_same(before, &now.Items[i]))
                call_object(q, ACTION_UPDATE, &now.Items[i]);
        }
        list_free(&seen);
        seen = now;
    }
    list_free(&seen);
    if (q->Flags & FLAG_ASYNC_CLOSE) {
        /* nobody waits for this thread: it tells the closing itself and takes the query with it. */
        while (!q->Closing)
            NtDelayExecution(TRUE, &(LARGE_INTEGER){.QuadPart = -10000000});
        call_state(q, STATE_CLOSED);
        query_free(q);
    }
    return 0;
}

CMAPI HRESULT WINAPI DevCreateObjectQuery(ULONG type, ULONG flags, ULONG key_count, const COMPOUND_KEY *keys,
                                          ULONG filter_count, const FILTER *filters, QUERY_CALLBACK callback, PVOID context,
                                          HANDLE *query)
{
    if (!callback || !query)
        return QUERY_E_INVALIDARG;
    if (!arguments_valid(flags, FLAG_UPDATE_RESULTS | FLAG_ALL_PROPERTIES | FLAG_LOCALIZE | FLAG_ASYNC_CLOSE, key_count, keys,
                         filter_count, filters))
        return QUERY_E_INVALIDARG;
    QUERY *q = alloc(sizeof(*q));
    if (!q)
        return QUERY_E_OUTOFMEMORY;
    q->Signature = QUERY_SIGNATURE;
    q->Type = type;
    q->Flags = flags;
    q->Callback = callback;
    q->Context = context;
    q->Keys = alloc(key_count * sizeof(COMPOUND_KEY));
    q->Filters = alloc(filter_count * sizeof(FILTER));
    BOOL ok = q->Keys && q->Filters;
    if (ok) {
        if (key_count)
            memcpy(q->Keys, keys, key_count * sizeof(COMPOUND_KEY));
        q->KeyCount = key_count;
        for (ULONG i = 0; i < filter_count && ok; i++) {
            q->Filters[i] = filters[i];
            q->Filters[i].Property.Buffer = nullptr;
            q->FilterCount = i + 1;
            if (filters[i].Property.Buffer && filters[i].Property.BufferSize && !is_logical(filters[i].Operator)) {
                q->Filters[i].Property.Buffer = alloc(filters[i].Property.BufferSize);
                ok = q->Filters[i].Property.Buffer != nullptr;
                if (ok)
                    memcpy(q->Filters[i].Property.Buffer, filters[i].Property.Buffer, filters[i].Property.BufferSize);
            }
        }
    }
    if (ok) {
        /* the thread runs in this library, so a program that frees its last user must not unload it. */
        HMODULE self;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (PCWSTR)query_thread, &self);
        *query = q;
        q->Thread = CreateThread(nullptr, 0, query_thread, q, 0, &q->ThreadId);
        ok = q->Thread != nullptr;
    }
    if (!ok) {
        query_free(q);
        return QUERY_E_OUTOFMEMORY;
    }
    return QUERY_S_OK;
}

CMAPI void WINAPI DevCloseObjectQuery(HANDLE query)
{
    QUERY *q = query;
    if (!q || q->Signature != QUERY_SIGNATURE)
        return;
    InterlockedExchange(&q->Closing, 1);
    NtAlertThread(q->Thread);
    if (q->Flags & FLAG_ASYNC_CLOSE)
        return;
    /* a close from the callback itself cannot wait for its own thread, which frees the query when it returns. */
    if (GetCurrentThreadId() == q->ThreadId) {
        q->Flags |= FLAG_ASYNC_CLOSE;
        return;
    }
    WaitForSingleObject(q->Thread, INFINITE);
    query_free(q);
}

/* ------------------------------------------------------------------ the properties of one object */

#define QUERY_E_NOT_FOUND ((HRESULT)0x80070002)

static BOOL object_exists(ULONG type, PCWSTR id)
{
    WCHAR *ids = object_ids(type);
    BOOL found = FALSE;
    for (PCWSTR p = ids; p && *p && !found; p += lstrlenW(p) + 1)
        found = !lstrcmpiW(p, id);
    release(ids);
    return found;
}

/*
 * measured on windows 11: the requested properties in their order, the ones the object has not as empty ones, or
 * every property with the flag. an object that is not there, of any type, is the error of a file that is not found
 * and leaves the outputs alone. no list of properties gives none.
 */
CMAPI HRESULT WINAPI DevGetObjectProperties(ULONG type, PCWSTR id, ULONG flags, ULONG key_count, const COMPOUND_KEY *keys,
                                            ULONG *count, const PROPERTY **properties)
{
    if (!id || !count || !properties)
        return QUERY_E_INVALIDARG;
    if (!arguments_valid(flags, FLAG_ALL_PROPERTIES | FLAG_LOCALIZE, key_count, keys, 0, nullptr))
        return QUERY_E_INVALIDARG;
    if (!object_exists(type, id))
        return QUERY_E_NOT_FOUND;
    FOUND item = {0};
    if (!found_fill(&item, type, id, flags, key_count, keys)) {
        found_free(&item);
        return QUERY_E_OUTOFMEMORY;
    }
    ULONG data = 0;
    for (ULONG i = 0; i < item.Count; i++)
        data += round8(item.Values[i].BufferSize);
    PROPERTY *block = item.Count ? alloc(item.Count * sizeof(PROPERTY) + data) : nullptr;
    if (item.Count && !block) {
        found_free(&item);
        return QUERY_E_OUTOFMEMORY;
    }
    BYTE *next = (BYTE *)(block + item.Count);
    for (ULONG i = 0; i < item.Count; i++) {
        block[i] = item.Values[i];
        if (block[i].Buffer) {
            memcpy(next, item.Values[i].Buffer, block[i].BufferSize);
            block[i].Buffer = next;
            next += round8(block[i].BufferSize);
        }
    }
    *count = item.Count;
    *properties = block;
    found_free(&item);
    return QUERY_S_OK;
}

CMAPI void WINAPI DevFreeObjectProperties(ULONG count, const PROPERTY *properties)
{
    release((void *)properties);
}

/* the entry of a key in a list of properties: the key, the store and the locale all match. an empty entry counts. */
CMAPI const PROPERTY *WINAPI DevFindProperty(const DEVPROPKEY *key, ULONG store, PCWSTR locale, ULONG count,
                                             const PROPERTY *properties)
{
    if (!key || !properties)
        return nullptr;
    for (ULONG i = 0; i < count; i++) {
        const COMPOUND_KEY *have = &properties[i].CompKey;
        if (memcmp(&have->Key, key, sizeof(*key)) || have->Store != store)
            continue;
        if (!locale ? have->LocaleName != nullptr : !have->LocaleName || lstrcmpiW(locale, have->LocaleName))
            continue;
        return &properties[i];
    }
    return nullptr;
}
