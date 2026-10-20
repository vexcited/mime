/*
 * the system image lists: SHGetImageList and what SHGetFileInfo answers for SHGFI_SYSICONINDEX.
 *
 * there is one table of icon sources, and each list of a size has one image for each entry, in the same order, so an
 * index means the same icon in every size. An entry is added the first time a source is asked for, which adds an
 * image to every list that exists; a list made later is filled from the whole table. The first entries are the stock
 * icons of the old shell, whose numbers are the indexes of shell32's icons.
 */
#include "shell32_private.h"
#include <mime/win32/commctrl.h>

#define LIST_COUNT 5
/* the stock icons 0 to 17 hold their own numbers as indexes, and the arrow of a link follows them. */
#define RESERVED 18
#define OVERLAY_ENTRY RESERVED
#define FIXED_ENTRIES (RESERVED + 1)
/* a table of this many icons is as many as a process asks for, then new sources show the plain document. */
#define MAX_ENTRIES 2048

typedef struct {
    SH_ICON_SOURCE Source;
    DWORD Hash;
} ENTRY;

typedef HIMAGELIST(WINAPI *CREATE_LIST)(int, int, UINT, int, int);
typedef int(WINAPI *REPLACE_ICON)(HIMAGELIST, int, HICON);
typedef BOOL(WINAPI *SET_OVERLAY)(HIMAGELIST, int, int);
typedef BOOL(WINAPI *SET_COUNT)(HIMAGELIST, UINT);
typedef HRESULT(WINAPI *QUERY_LIST)(HIMAGELIST, REFIID, void **);

typedef struct {
    CREATE_LIST Create;
    REPLACE_ICON ReplaceIcon;
    SET_OVERLAY SetOverlayImage;
    SET_COUNT SetImageCount;
    QUERY_LIST QueryInterface;
} COMCTL;

static SRWLOCK g_lock = SRWLOCK_INIT;
static COMCTL g_comctl;
static BOOL g_comctl_tried;
static ENTRY *g_entries;
static int g_count, g_capacity;
static HIMAGELIST g_lists[LIST_COUNT];
static int g_sizes[LIST_COUNT];

static BOOL load_comctl(void)
{
    if (g_comctl.Create)
        return TRUE;
    if (g_comctl_tried)
        return FALSE;
    g_comctl_tried = TRUE;
    HMODULE module = LoadLibraryW(L"comctl32.dll");
    if (!module)
        return FALSE;
    COMCTL c;
    c.ReplaceIcon = (REPLACE_ICON)(void *)GetProcAddress(module, "ImageList_ReplaceIcon");
    c.SetOverlayImage = (SET_OVERLAY)(void *)GetProcAddress(module, "ImageList_SetOverlayImage");
    c.SetImageCount = (SET_COUNT)(void *)GetProcAddress(module, "ImageList_SetImageCount");
    c.QueryInterface = (QUERY_LIST)(void *)GetProcAddress(module, "HIMAGELIST_QueryInterface");
    c.Create = (CREATE_LIST)(void *)GetProcAddress(module, "ImageList_Create");
    if (!c.ReplaceIcon || !c.SetOverlayImage || !c.SetImageCount || !c.QueryInterface || !c.Create)
        return FALSE;
    g_comctl = c;
    return TRUE;
}

static DWORD hash_text(const WCHAR *text)
{
    DWORD h = 2166136261u;
    for (; *text; text++)
        h = (h ^ (DWORD)*text) * 16777619u;
    return h;
}

static int list_size(int which)
{
    int size;
    switch (which) {
    case SHIL_LARGE:
        size = GetSystemMetrics(SM_CXICON);
        return size > 0 ? size : 32;
    case SHIL_SMALL:
        return 16;
    case SHIL_EXTRALARGE:
        return 48;
    case SHIL_SYSSMALL:
        size = GetSystemMetrics(SM_CXSMICON);
        return size > 0 ? size : 16;
    default:
        return 256;
    }
}

/* puts the icon of an entry in a list, at the end, or leaves a blank where the icon could not be made. */
static void append_image(int which, int index)
{
    HICON icon = ShLoadIconSource(&g_entries[index].Source, g_sizes[which], g_sizes[which]);
    int placed = icon ? g_comctl.ReplaceIcon(g_lists[which], -1, icon) : -1;
    if (icon)
        DestroyIcon(icon);
    if (placed < 0)
        g_comctl.SetImageCount(g_lists[which], (UINT)index + 1);
}

static BOOL grow_entries(void)
{
    if (g_count < g_capacity)
        return TRUE;
    int capacity = g_capacity ? g_capacity * 2 : 64;
    ENTRY *entries = ShAlloc((SIZE_T)capacity * sizeof(ENTRY));
    if (!entries)
        return FALSE;
    if (g_entries) {
        memcpy(entries, g_entries, (SIZE_T)g_count * sizeof(ENTRY));
        ShFree(g_entries);
    }
    g_entries = entries;
    g_capacity = capacity;
    return TRUE;
}

static int append_entry(const SH_ICON_SOURCE *source, DWORD hash)
{
    if (!grow_entries())
        return -1;
    int index = g_count++;
    g_entries[index].Source = *source;
    g_entries[index].Hash = hash;
    for (int which = 0; which < LIST_COUNT; which++)
        if (g_lists[which])
            append_image(which, index);
    return index;
}

/* the table starts with the fixed entries, which are never looked up by name. */
static BOOL start_table(void)
{
    if (g_count)
        return TRUE;
    for (int i = 0; i < FIXED_ENTRIES; i++) {
        SH_ICON_SOURCE source;
        ShIconSourceStock(i == OVERLAY_ENTRY ? SIID_LINK : i, &source);
        WCHAR key[32];
        ShIconSourceKey(&source, key, RTL_NUMBER_OF(key));
        if (append_entry(&source, hash_text(key)) < 0)
            return FALSE;
    }
    return TRUE;
}

/* the list of a size, made and filled when first asked for. */
static HIMAGELIST list_locked(int which)
{
    if (g_lists[which])
        return g_lists[which];
    if (!load_comctl() || !start_table())
        return nullptr;
    int size = list_size(which);
    HIMAGELIST list = g_comctl.Create(size, size, ILC_COLOR32 | ILC_MASK, g_count + 16, 16);
    if (!list)
        return nullptr;
    /* a reference the table keeps, so that a program that destroys a list it was given leaves it standing. */
    void *extra;
    g_comctl.QueryInterface(list, &IID_IImageList2, &extra);
    g_lists[which] = list;
    g_sizes[which] = size;
    for (int i = 0; i < g_count; i++)
        append_image(which, i);
    g_comctl.SetOverlayImage(list, OVERLAY_ENTRY, 1);
    return list;
}

HIMAGELIST ShSystemList(int which)
{
    if (which < 0 || which >= LIST_COUNT)
        return nullptr;
    AcquireSRWLockExclusive(&g_lock);
    HIMAGELIST list = list_locked(which);
    ReleaseSRWLockExclusive(&g_lock);
    return list;
}

/*
 * the index of a source in the system lists, which makes the entry when there is none; the list of the large or the
 * small icons is made so that the index can be drawn from it.
 */
int ShSystemIconIndex(const SH_ICON_SOURCE *source, UINT small)
{
    WCHAR key[MAX_PATH + 32];
    ShIconSourceKey(source, key, RTL_NUMBER_OF(key));
    DWORD hash = hash_text(key);
    AcquireSRWLockExclusive(&g_lock);
    int index = -1;
    if (list_locked(small ? SHIL_SYSSMALL : SHIL_LARGE) && g_count) {
        for (int i = 0; i < g_count && index < 0; i++) {
            if (g_entries[i].Hash != hash)
                continue;
            WCHAR other[MAX_PATH + 32];
            ShIconSourceKey(&g_entries[i].Source, other, RTL_NUMBER_OF(other));
            if (!wcscmp(key, other))
                index = i;
        }
        if (index < 0 && g_count < MAX_ENTRIES)
            index = append_entry(source, hash);
        if (index < 0)
            index = SIID_DOCNOASSOC;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return index;
}

SHSTDAPI HRESULT WINAPI SHGetImageList(int which, REFIID riid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!riid)
        return E_INVALIDARG;
    if (which < 0 || which >= LIST_COUNT)
        return E_INVALIDARG;
    HIMAGELIST list = ShSystemList(which);
    if (!list)
        return HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION);
    return g_comctl.QueryInterface(list, riid, object);
}

/* the large and the small system lists, as handles. */
SHSTDAPI BOOL WINAPI Shell_GetImageLists(struct _IMAGELIST **large, struct _IMAGELIST **small)
{
    HIMAGELIST big = ShSystemList(SHIL_LARGE), little = ShSystemList(SHIL_SMALL);
    if (large)
        *large = big;
    if (small)
        *small = little;
    return big && little;
}

SHSTDAPI BOOL WINAPI FileIconInit(BOOL restore)
{
    return TRUE;
}
