/*
 * the heap of the desktop, which user32 reads instead of asking: measured on windows 11 26100. win32k maps it read-only
 * in every program of the desktop, 0x1400000 bytes of which the head has pages, and a thread of the window manager
 * holds in its teb the desktop information at 0x1040 of it, the base of the view, and its own id as its thread
 * information. what points inside the heap is an offset from its start. the desktop information starts with the id of
 * the heap, which the entry of each handle of the desktop repeats, and the offset of the window of the desktop.
 *
 * a window is 0x160 bytes there:
 *
 *   0x00 the handle                   0x08 its own offset          0x10 the state, as the WF flags number it
 *   0x18 the extended style           0x1c the style               0x20 the module
 *   0x2a the id of a system class     0x30 the parent              0x38 the first child
 *   0x40 the owner                    0x48 the next sibling        0x50 the previous one
 *   0x58 the rectangle                0x68 the client rectangle    0x78 the procedure
 *   0x80 the class                    0xb0 the last active popup   0xb8 the text: sizes, then at 0xc0 its offset
 *   0xc8 the size of the extra bytes  0xd8 the user data           0x11c the dpi
 *   0x140 the id or the menu          0x148 the thread             0x14c the process
 *
 * an entry of the handle table is 0x20 bytes: the offset of the object, the thread that owns it, the id of the
 * heap, then the type, the flags and the 15 bits of the handle's high word. a freed entry keeps the next high word.
 *
 * the windows are written here when a thread of such a program lets go of the lock of the window manager, so that
 * what it reads next is what win32k holds. the class, the input context, the update region and the extra bytes are
 * not there yet.
 */
#include "win32k.h"
#include "menu.h"

#define DESK_RESERVED 0x1400000
#define DESK_COMMITTED 0x200000
#define DESK_INFO 0x1040
#define DESK_INFO_SIZE 0xa0
#define DESK_FIRST 0x1100
/*
 * the CLIENTTHREADINFO a thread's client information points at, at 0x60 of it, where user32 26300 reads without a
 * call: GetFocus is the window at 0x20, GetActiveWindow the one at 0x28, and the wake bits are at 4 and 8.
 * PeekMessage answers no message when no bit it wants is set, and GetRawInputBuffer answers no input unless
 * QS_RAWINPUT is set at +8. mime keeps no wake bits per thread there yet: all of them read as set, so user32 always
 * asks. a thread whose block could not be had points at the one of this offset, which names no window.
 */
#define DESK_THREAD_INFO 0x10e0
#define THREAD_INFO_SIZE 0x40
#define DESK_ID 0x667d
#define DESK_GRANULE 0x40
#define DESK_GRANULES (DESK_COMMITTED / DESK_GRANULE)

#define WND_SIZE 0x160
#define WND_HANDLE 0x00
#define WND_SELF 0x08
#define WND_STATE 0x10
#define WND_STATE2 0x14
#define WND_EXSTYLE 0x18
#define WND_STYLE 0x1c
#define WND_INSTANCE 0x20
#define WND_FNID 0x2a
#define WND_PARENT 0x30
#define WND_CHILD 0x38
#define WND_OWNER 0x40
#define WND_NEXT 0x48
#define WND_PREV 0x50
#define WND_RECT 0x58
#define WND_CLIENT 0x68
#define WND_PROC 0x78
#define WND_LAST_ACTIVE 0xb0
#define WND_TEXT_LENGTH 0xb8
#define WND_TEXT_MAXIMUM 0xbc
#define WND_TEXT 0xc0
#define WND_EXTRA_SIZE 0xc8
#define WND_USER_DATA 0xd8
#define WND_CLASS_BLOCK 0x80
#define WND_UPDATE 0x88
#define WND_SCROLL 0x90
#define WND_BAND 0xec
#define WND_DPI 0x11c
#define WND_EXTRA_BYTES 0x128
#define WND_ID 0x140
#define WND_TID 0x148
#define WND_PID 0x14c

/*
 * a class is read by GetClassLongPtr, GetClassNameA and DefWindowProc of user32 26300.9457, which name these fields:
 * the atom of the name of its version and the one of its name, the number of its procedure, the CSF flags it was
 * registered with, of which 1 says the procedure runs in the kernel and 2 that it is ansi, the style, the size of its extra bytes, the procedure, the menu name, the ansi name, the small icon, the
 * module, the brush, the size of the extra bytes of its windows, and its own extra bytes at 0x58.
 */
#define CLS_VERSION_ATOM 0x00
#define CLS_ATOM 0x02
#define CLS_FNID 0x04
#define CLS_FLAGS 0x06
#define CLS_STYLE 0x08
#define CLS_CLASS_EXTRA 0x0c
#define CLS_PROC 0x20
#define CLS_MENU 0x28
#define CLS_ANSI_NAME 0x30
#define CLS_ICON_SMALL 0x38
#define CLS_MODULE 0x40
#define CLS_BACKGROUND 0x48
#define CLS_WINDOW_EXTRA 0x50
#define CLS_EXTRA 0x58
#define CSF_SERVERSIDEPROC 0x0001
#define CSF_ANSIPROC 0x0002

/*
 * the state of a window, as the WNDS bits of windows name it. a top level window of the reference machine had
 * 0x40020048 and a child 0x00020000, with 0x80000700 and 0x80000710 after them.
 */
#define STATE_HASMENU 0x00000001u
#define STATE_HASCAPTION 0x00000008u
#define STATE_SENDSIZEMOVEMSGS 0x00000010u
#define STATE_MSGBOX 0x00000020u
#define STATE_ACTIVEFRAME 0x00000040u
/* what UpdateWindow, GetUpdateRect and GetUpdateRgn of user32 read before they ask: with none of them and no update
   region they answer that nothing is to be painted. */
#define STATE_SENDNCPAINT 0x00000200u
#define STATE_SENDERASEBKGND 0x00000800u
#define STATE_INTERNALPAINT 0x00001000u
#define STATE_HIDDENPOPUP 0x00004000u
#define STATE_DIALOGWINDOW 0x00010000u
#define STATE_HASCREATESTRUCTNAME 0x00020000u
#define STATE_SERVER_PROC 0x00040000u
#define STATE_ANSI_PROC 0x00080000u
#define STATE_MAXIMIZESTOFULLSCREEN 0x40000000u
#define STATE2_COMMON 0x80000700u
#define STATE2_CHILD 0x00000010u
#define WND_DPI_CONTEXT 0x120
#define WND_MENU 0x98

/*
 * a menu, read by GetMenuItemCount, GetMenuItemID, GetMenuState, GetSubMenu, GetMenuInfo, GetMenuDefaultItem,
 * GetMenuItemInfoInternalW, EnableMenuItem and CheckMenuItem of user32 26300.9457, the last two only calling the
 * kernel when the state they read differs: the handle, its own offset, the data, the brush, the offset of the items,
 * the flags, whose top six bits are the MNS style, the number of items, the help id and the largest height. an item
 * is 0x70 bytes: type, state, id, the offset of its submenu, the two check bitmaps, the offset of its text and its
 * length in characters, the data, and at 0x60 the bitmap.
 */
#define MENU_SIZE 0x60
#define MENU_HANDLE 0x00
#define MENU_SELF 0x08
#define MENU_DATA 0x10
#define MENU_BACKGROUND 0x18
#define MENU_ITEMS 0x20
#define MENU_FLAGS 0x28
#define MENU_COUNT 0x2c
#define MENU_HELP_ID 0x30
#define MENU_MAX_HEIGHT 0x34
#define MENU_FLAG_POPUP 0x1
#define ITEM_SIZE 0x70
#define ITEM_TYPE 0x00
#define ITEM_STATE 0x04
#define ITEM_ID 0x08
#define ITEM_SUBMENU 0x10
#define ITEM_CHECKED 0x18
#define ITEM_UNCHECKED 0x20
#define ITEM_TEXT 0x28
#define ITEM_TEXT_LENGTH 0x30
#define ITEM_DATA 0x38
#define ITEM_BITMAP 0x60


#define ENTRY_SIZE 0x20
#define ENTRY_OBJECT 0x00
#define ENTRY_OWNER 0x08
#define ENTRY_HEAP 0x10
#define ENTRY_TYPE 0x18
#define ENTRY_UNIQUE 0x1a

static PVOID g_section;
static PUCHAR g_heap;
static UCHAR g_used[DESK_GRANULES / 8];
static ULONG g_hint = DESK_FIRST / DESK_GRANULE;
static ULONG g_users; /* the programs that have the heap in view. */

static BOOLEAN used(ULONG granule)
{
    return (g_used[granule / 8] >> (granule % 8)) & 1;
}

static void mark(ULONG granule, ULONG count, BOOLEAN on)
{
    for (ULONG i = granule; i < granule + count; i++) {
        if (on)
            g_used[i / 8] |= (UCHAR)(1 << (i % 8));
        else
            g_used[i / 8] &= (UCHAR)~(1 << (i % 8));
    }
}

static ULONG granules(ULONG size)
{
    return (size + DESK_GRANULE - 1) / DESK_GRANULE;
}

/* the offset of a cleared block, 0 when the heap is full. */
static ULONG heap_alloc(ULONG size)
{
    ULONG count = granules(size), first = DESK_FIRST / DESK_GRANULE;
    for (ULONG pass = 0; pass < 2; pass++) {
        ULONG run = 0;
        for (ULONG i = pass ? first : g_hint; i < DESK_GRANULES; i++) {
            run = used(i) ? 0 : run + 1;
            if (run == count) {
                ULONG start = i + 1 - count;
                mark(start, count, TRUE);
                g_hint = i + 1;
                memset(g_heap + (SIZE_T)start * DESK_GRANULE, 0, (SIZE_T)count * DESK_GRANULE);
                return start * DESK_GRANULE;
            }
        }
    }
    return 0;
}

static void heap_free(ULONG offset, ULONG size)
{
    if (offset >= DESK_FIRST && offset % DESK_GRANULE == 0)
        mark(offset / DESK_GRANULE, granules(size), FALSE);
}

static void thread_info_init(ULONG offset)
{
    PULONG wake = (PULONG)(g_heap + offset);
    wake[1] = wake[2] = wake[4] = 0x1FFF; /* fsChangeBits, fsWakeBits and fsWakeMask: every QS_ bit. */
}

static NTSTATUS build(void)
{
    PVOID base;
    NTSTATUS status = MmCreateReservedSection(DESK_RESERVED, DESK_COMMITTED, &base, &g_section);
    if (!NT_SUCCESS(status))
        return status;
    g_heap = base;
    memset(g_heap, 0, DESK_COMMITTED);
    *(PULONG64)(g_heap + DESK_INFO) = DESK_ID;
    /*
     * the desktop is composited, as every desktop of windows since 8. user32 scales the monitors and the work area to
     * the dpi of a thread only then, so a program that is not dpi aware sees a screen of 96 dpi.
     */
    *(PULONG)(g_heap + DESK_INFO + 0x40) |= 1;
    thread_info_init(DESK_THREAD_INFO);
    return STATUS_SUCCESS;
}

static PUCHAR entry_of(const USER_OBJECT *object)
{
    PUCHAR table = UserSharedHandles();
    ULONG index = (ULONG)(ULONG_PTR)object->Handle & 0xFFFF;
    return table && index < UserSharedHandleCount() ? table + (SIZE_T)index * ENTRY_SIZE : nullptr;
}

static ULONG offset_of(const WND *w)
{
    return w && !w->Header.Destroyed ? w->Header.DeskOffset : 0;
}

/* the number of the full sync, which writes each class once however many windows it has. */
static ULONG g_sync;

static void write_class(WND_CLASS *c)
{
    if (c->DeskOffset && c->DeskWritten == g_sync)
        return;
    c->DeskWritten = g_sync;
    ULONG size = CLS_EXTRA + (ULONG)(c->ClassExtra > 0 ? c->ClassExtra : 0);
    if (c->DeskOffset && c->DeskSize != size) {
        heap_free(c->DeskOffset, c->DeskSize);
        c->DeskOffset = 0;
    }
    if (!c->DeskOffset) {
        c->DeskOffset = heap_alloc(size);
        c->DeskSize = c->DeskOffset ? size : 0;
        if (!c->DeskOffset)
            return;
    }
    PCWSTR name = c->PlainName ? c->PlainName : c->Name;
    /* a class registered by an integer atom is named by its number after a #. */
    WCHAR number[8];
    if (!name && c->Atom && c->Atom < 0xC000) {
        ULONG k = 0, v = c->Atom;
        WCHAR digits[6];
        while (v) {
            digits[k++] = (WCHAR)(L'0' + v % 10);
            v /= 10;
        }
        ULONG n = 0;
        number[n++] = L'#';
        while (k)
            number[n++] = digits[--k];
        number[n] = 0;
        name = number;
    }
    ULONG chars = name ? (ULONG)wcslen(name) : 0;
    if (!c->DeskName && name) {
        c->DeskName = heap_alloc(chars + 1);
        if (c->DeskName) {
            RtlUnicodeToMultiByteN((PCHAR)g_heap + c->DeskName, chars, nullptr, name, chars * sizeof(WCHAR));
            g_heap[c->DeskName + chars] = 0;
            c->DeskNameSize = chars + 1;
        }
    }
    PUCHAR block = g_heap + c->DeskOffset;
    UCHAR head[CLS_EXTRA] = {};
    *(PUSHORT)(head + CLS_VERSION_ATOM) = c->Atom;
    *(PUSHORT)(head + CLS_ATOM) = c->PlainAtom ? c->PlainAtom : c->Atom;
    *(PUSHORT)(head + CLS_FNID) = (USHORT)c->Fnid;
    WNDPROC proc = ClassProc(c, nullptr);
    *(PUSHORT)(head + CLS_FLAGS) = (USHORT)(c->CsfFlags | (proc ? 0 : CSF_SERVERSIDEPROC) | (c->Ansi ? CSF_ANSIPROC : 0));
    *(PULONG)(head + CLS_STYLE) = c->Style;
    *(PULONG)(head + CLS_CLASS_EXTRA) = (ULONG)(c->ClassExtra > 0 ? c->ClassExtra : 0);
    *(PULONG64)(head + CLS_PROC) = (ULONG64)(ULONG_PTR)proc;
    *(PULONG64)(head + CLS_MENU) = IS_INTRESOURCE(c->MenuName) ? (ULONG64)(ULONG_PTR)c->MenuName : 0;
    *(PULONG64)(head + CLS_ANSI_NAME) = c->DeskName;
    *(PULONG64)(head + CLS_ICON_SMALL) = (ULONG64)(ULONG_PTR)c->IconSmall;
    *(PULONG64)(head + CLS_MODULE) = (ULONG64)(ULONG_PTR)c->Instance;
    *(PULONG64)(head + CLS_BACKGROUND) = (ULONG64)(ULONG_PTR)c->Background;
    *(PULONG)(head + CLS_WINDOW_EXTRA) = (ULONG)(c->WindowExtra > 0 ? c->WindowExtra : 0);
    if (memcmp(block, head, sizeof(head)))
        memcpy(block, head, sizeof(head));
    if (c->Extra && c->ClassExtra > 0 && memcmp(block + CLS_EXTRA, c->Extra, (SIZE_T)c->ClassExtra))
        memcpy(block + CLS_EXTRA, c->Extra, (SIZE_T)c->ClassExtra);
}

/* a class goes away: its blocks in the heap with it. */
void DeskClassFreed(WND_CLASS *c)
{
    if (!g_heap)
        return;
    if (c->DeskOffset)
        heap_free(c->DeskOffset, c->DeskSize);
    if (c->DeskName)
        heap_free(c->DeskName, c->DeskNameSize);
    c->DeskOffset = c->DeskName = 0;
}

static ULONG menu_offset(HMENU handle)
{
    MENU *m = handle ? MenuLookup(handle) : nullptr;
    return m && !m->Header.Destroyed ? m->Header.DeskOffset : 0;
}

static void write_entry(PUCHAR entry, const USER_OBJECT *object, ULONG64 owner)
{
    UCHAR fresh[ENTRY_SIZE] = {};
    *(PULONG64)(fresh + ENTRY_OBJECT) = object->DeskOffset;
    *(PULONG64)(fresh + ENTRY_OWNER) = owner;
    *(PULONG64)(fresh + ENTRY_HEAP) = DESK_ID;
    fresh[ENTRY_TYPE] = object->Type;
    *(PUSHORT)(fresh + ENTRY_UNIQUE) = (USHORT)(((ULONG_PTR)object->Handle >> 16) & 0x7FFF);
    if (memcmp(entry, fresh, ENTRY_SIZE))
        memcpy(entry, fresh, ENTRY_SIZE);
}

static void write_menu(MENU *m)
{
    PUCHAR entry = entry_of(&m->Header);
    if (!entry)
        return;
    if (m->Header.Destroyed) {
        entry[ENTRY_TYPE] = 0;
        return;
    }
    if (!m->Header.DeskOffset && !(m->Header.DeskOffset = heap_alloc(MENU_SIZE)))
        return;
    /* the items, then the text of each, in one block. */
    ULONG size = m->Count * ITEM_SIZE;
    for (ULONG i = 0; i < m->Count; i++)
        if (m->Items[i].Text)
            size += (m->Items[i].TextLength + 1) * (ULONG)sizeof(WCHAR);
    if (m->DeskItems && m->DeskItemsSize != size) {
        heap_free(m->DeskItems, m->DeskItemsSize);
        m->DeskItems = m->DeskItemsSize = 0;
    }
    if (size && !m->DeskItems) {
        m->DeskItems = heap_alloc(size);
        m->DeskItemsSize = m->DeskItems ? size : 0;
    }
    ULONG count = m->DeskItems ? m->Count : 0;
    PUCHAR block = g_heap + m->DeskItems;
    ULONG text = count * ITEM_SIZE;
    for (ULONG i = 0; i < count; i++) {
        const MENU_ITEM *it = &m->Items[i];
        UCHAR image[ITEM_SIZE] = {};
        *(PULONG)(image + ITEM_TYPE) = it->Type;
        *(PULONG)(image + ITEM_STATE) = it->State;
        *(PULONG)(image + ITEM_ID) = it->Id;
        *(PULONG64)(image + ITEM_SUBMENU) = menu_offset(it->SubMenu);
        *(PULONG64)(image + ITEM_CHECKED) = (ULONG64)(ULONG_PTR)it->Checked;
        *(PULONG64)(image + ITEM_UNCHECKED) = (ULONG64)(ULONG_PTR)it->Unchecked;
        *(PULONG64)(image + ITEM_DATA) = it->Data;
        *(PULONG64)(image + ITEM_BITMAP) = (ULONG64)(ULONG_PTR)it->Bitmap;
        if (it->Text) {
            ULONG bytes = it->TextLength * (ULONG)sizeof(WCHAR);
            if (memcmp(block + text, it->Text, bytes) || *(PWCHAR)(block + text + bytes))
                memcpy(block + text, it->Text, bytes);
            *(PWCHAR)(block + text + bytes) = 0;
            *(PULONG64)(image + ITEM_TEXT) = m->DeskItems + text;
            *(PULONG)(image + ITEM_TEXT_LENGTH) = it->TextLength;
            text += bytes + (ULONG)sizeof(WCHAR);
        }
        if (memcmp(block + (SIZE_T)i * ITEM_SIZE, image, ITEM_SIZE))
            memcpy(block + (SIZE_T)i * ITEM_SIZE, image, ITEM_SIZE);
    }
    UCHAR head[MENU_SIZE] = {};
    *(PULONG64)(head + MENU_HANDLE) = (ULONG64)(ULONG_PTR)m->Header.Handle;
    *(PULONG64)(head + MENU_SELF) = m->Header.DeskOffset;
    *(PULONG64)(head + MENU_DATA) = m->MenuData;
    *(PULONG64)(head + MENU_BACKGROUND) = (ULONG64)(ULONG_PTR)m->Background;
    *(PULONG64)(head + MENU_ITEMS) = count ? m->DeskItems : 0;
    *(PULONG)(head + MENU_FLAGS) = (m->Style & 0xFC000000) | (m->Popup ? MENU_FLAG_POPUP : 0);
    *(PULONG)(head + MENU_COUNT) = count;
    *(PULONG)(head + MENU_HELP_ID) = m->HelpId;
    *(PULONG)(head + MENU_MAX_HEIGHT) = m->MaxHeight;
    if (memcmp(g_heap + m->Header.DeskOffset, head, MENU_SIZE))
        memcpy(g_heap + m->Header.DeskOffset, head, MENU_SIZE);
    write_entry(entry, &m->Header, 0);
}

static void write_window(WND *w)
{
    PUCHAR entry = entry_of(&w->Header);
    if (!entry)
        return;
    if (w->Header.Destroyed) {
        /* the handle is gone for the programs while the last references of the window go. */
        entry[ENTRY_TYPE] = 0;
        return;
    }
    if (!w->Header.DeskOffset) {
        w->Header.DeskOffset = heap_alloc(WND_SIZE);
        if (!w->Header.DeskOffset)
            return;
    }
    UCHAR image[WND_SIZE] = {};
    ULONG self = w->Header.DeskOffset;
    BOOLEAN desktop = w == WndDesktop();
    *(PULONG64)(image + WND_HANDLE) = (ULONG64)(ULONG_PTR)w->Header.Handle;
    *(PULONG64)(image + WND_SELF) = self;
    /* a window of no procedure of user mode runs the one of its class in the kernel. */
    ULONG state = desktop || !w->Proc ? STATE_SERVER_PROC : (w->Ansi ? STATE_ANSI_PROC : 0);
    if (!desktop) {
        if ((w->Style & WS_CAPTION) == WS_CAPTION)
            state |= STATE_HASCAPTION;
        if (!(w->Style & WS_CHILD) && w->Id)
            state |= STATE_HASMENU;
        if (w->Flags & WNDF_SENDSIZEMOVE)
            state |= STATE_SENDSIZEMOVEMSGS;
        if (w->Flags & WNDF_ACTIVE_FRAME)
            state |= STATE_ACTIVEFRAME;
        if (w->Flags & WNDF_MSGBOX)
            state |= STATE_MSGBOX;
        if (w->Flags & WNDF_DLGPOINTER)
            state |= STATE_DIALOGWINDOW;
        if (w->Flags & WNDF_HIDDENPOPUP)
            state |= STATE_HIDDENPOPUP;
        if (w->Flags & WNDF_HASNAME)
            state |= STATE_HASCREATESTRUCTNAME;
        if (!(w->Style & (WS_CHILD | WS_POPUP)))
            state |= STATE_MAXIMIZESTOFULLSCREEN;
    }
    if (!desktop) {
        if (w->Flags & WNDF_FRAME_DIRTY)
            state |= STATE_SENDNCPAINT;
        if (w->Flags & WNDF_ERASE_PENDING)
            state |= STATE_SENDERASEBKGND;
        if (w->Flags & WNDF_INTERNAL_PAINT)
            state |= STATE_INTERNALPAINT;
        /* the handle of the update region, of which user32 only asks whether there is one: HRGN_FULL stands for it. */
        if (!RgnIsEmpty(&w->Update))
            *(PULONG64)(image + WND_UPDATE) = 1;
    }
    *(PULONG)(image + WND_STATE) = state;
    *(PULONG)(image + WND_STATE2) = STATE2_COMMON | (w->Style & WS_CHILD ? STATE2_CHILD : 0);
    *(PULONG)(image + WND_EXSTYLE) = w->ExStyle;
    *(PULONG)(image + WND_STYLE) = w->Style;
    *(PULONG64)(image + WND_INSTANCE) = (ULONG64)(ULONG_PTR)w->Instance;
    *(PUSHORT)(image + WND_FNID) = desktop ? FNID_DESKTOP : w->Fnid;
    *(PULONG64)(image + WND_PARENT) = offset_of(w->Parent);
    *(PULONG64)(image + WND_CHILD) = offset_of(w->FirstChild);
    *(PULONG64)(image + WND_OWNER) = offset_of(w->Owner);
    *(PULONG64)(image + WND_NEXT) = offset_of(w->Next);
    *(PULONG64)(image + WND_PREV) = offset_of(w->Prev);
    memcpy(image + WND_RECT, &w->WindowRect, sizeof(RECT));
    memcpy(image + WND_CLIENT, &w->ClientRect, sizeof(RECT));
    *(PULONG64)(image + WND_PROC) = desktop ? 1 : (ULONG64)(ULONG_PTR)w->Proc;
    *(PULONG64)(image + WND_LAST_ACTIVE) = desktop || WndIsTopLevel(w) ? self : 0;
    /* the text keeps its block while it fits. */
    PUCHAR old = g_heap + self;
    ULONG text = (ULONG)*(PULONG64)(old + WND_TEXT), room = *(PULONG)(old + WND_TEXT_MAXIMUM);
    ULONG bytes = w->Text ? w->TextLength * (ULONG)sizeof(WCHAR) : 0;
    if (bytes > 0xFFFC)
        bytes = 0xFFFC;
    if (text && bytes + sizeof(WCHAR) > room) {
        heap_free(text, room);
        text = 0;
    }
    if (!text && bytes) {
        room = bytes + (ULONG)sizeof(WCHAR);
        text = heap_alloc(room);
        if (!text)
            bytes = room = 0;
    }
    if (text) {
        if (memcmp(g_heap + text, w->Text ? (PVOID)w->Text : (PVOID)L"", bytes) || *(PWCHAR)(g_heap + text + bytes))
            memcpy(g_heap + text, w->Text, bytes);
        *(PWCHAR)(g_heap + text + bytes) = 0;
    }
    *(PULONG)(image + WND_TEXT_LENGTH) = bytes;
    *(PULONG)(image + WND_TEXT_MAXIMUM) = text ? room : 0;
    *(PULONG64)(image + WND_TEXT) = text;
    *(PULONG64)(image + WND_EXTRA_SIZE) = w->ExtraSize;
    *(PULONG64)(image + WND_USER_DATA) = (ULONG64)w->UserData;
    *(PULONG)(image + WND_DPI) = w->Dpi ? w->Dpi : 96;
    /* ZBID_DESKTOP, the band of every window of a desktop program. GetWindow of user32 skips a window of no band. */
    *(PULONG)(image + WND_BAND) = 1;
    /*
     * the dpi awareness context of the window, which user32 compares with the one of its thread before it maps a point.
     * measured on windows 11: 0x6010 unaware and 0x6011 system aware, the dpi in bits 8 to 16 over the awareness, and
     * 0x12 and 0x22 for the two per monitor awarenesses, which carry no dpi. with a dpi there the contexts of a per
     * monitor thread and its window were not equal.
     */
    static const UCHAR awareness_code[] = {0x10, 0x11, 0x12, 0x22};
    ULONG awareness = w->Awareness < sizeof(awareness_code) ? w->Awareness : 0;
    *(PULONG)(image + WND_DPI_CONTEXT) =
        (awareness >= DPI_AWARE_PER_MONITOR ? 0 : (awareness == DPI_AWARE_UNAWARE || !w->Dpi ? 96 : w->Dpi) << 8) |
        awareness_code[awareness];
    if (w->Class) {
        write_class(w->Class);
        *(PULONG64)(image + WND_CLASS_BLOCK) = w->Class->DeskOffset;
    }
    *(PULONG64)(image + WND_EXTRA_BYTES) = (ULONG64)(ULONG_PTR)w->UserExtra;
    /* the scroll bars of the frame, SBINFO, which GetScrollInfo reads when they are not drawn by user32. */
    if (w->Scroll) {
        if (!w->Header.DeskScroll)
            w->Header.DeskScroll = heap_alloc(sizeof(SCROLL_INFO));
        if (w->Header.DeskScroll) {
            if (memcmp(g_heap + w->Header.DeskScroll, w->Scroll, sizeof(SCROLL_INFO)))
                memcpy(g_heap + w->Header.DeskScroll, w->Scroll, sizeof(SCROLL_INFO));
            *(PULONG64)(image + WND_SCROLL) = w->Header.DeskScroll;
        }
    }
    *(PULONG64)(image + WND_ID) = (ULONG64)w->Id;
    if (!(w->Style & WS_CHILD) && w->Id)
        *(PULONG64)(image + WND_MENU) = menu_offset((HMENU)w->Id);
    ULONG pid = w->Thread && w->Thread->Process ? (ULONG)(ULONG_PTR)w->Thread->Process->Pid : 0;
    ULONG tid = w->Thread ? (ULONG)(ULONG_PTR)w->Thread->Tid : 0;
    if (desktop) {
        HANDLE owner, thread;
        PsQuerySubsystemIds(&owner, &thread);
        pid = (ULONG)(ULONG_PTR)owner;
        tid = (ULONG)(ULONG_PTR)thread;
    }
    *(PULONG)(image + WND_PID) = pid;
    *(PULONG)(image + WND_TID) = tid;
    if (memcmp(old, image, WND_SIZE))
        memcpy(old, image, WND_SIZE);
    UCHAR fresh[ENTRY_SIZE] = {};
    *(PULONG64)(fresh + ENTRY_OBJECT) = self;
    *(PULONG64)(fresh + ENTRY_OWNER) = tid;
    *(PULONG64)(fresh + ENTRY_HEAP) = DESK_ID;
    fresh[ENTRY_TYPE] = USER_TYPE_WINDOW;
    *(PUSHORT)(fresh + ENTRY_UNIQUE) = (USHORT)(((ULONG_PTR)w->Header.Handle >> 16) & 0x7FFF);
    if (memcmp(entry, fresh, ENTRY_SIZE))
        memcpy(entry, fresh, ENTRY_SIZE);
    if (desktop)
        *(PULONG64)(g_heap + DESK_INFO + 8) = self;
}

static BOOLEAN g_dirty = TRUE;
static ULONG g_serial = 1;

/* something the heap shows may have changed under the lock: the next sync rewrites the objects. */
void DeskTouch(void)
{
    if (!g_dirty) {
        g_dirty = TRUE;
        g_serial++;
    }
}

/* the number of the state of the window manager while nothing changed it since the last sync, 0 otherwise. */
ULONG DeskCleanSerial(void)
{
    return g_dirty ? 0 : g_serial;
}

static void sync_thread(W32THREAD *t)
{
    if (!t || !t->DeskInfo)
        return;
    /* bit 1 of the flags is what InSendMessage answers with no call. */
    *(PULONG)(g_heap + t->DeskInfo) = MsgInSendFlags(t) ? 2 : 0;
    PULONG64 windows = (PULONG64)(g_heap + t->DeskInfo + 0x20);
    windows[0] = (ULONG64)(ULONG_PTR)t->Focus;
    windows[1] = (ULONG64)(ULONG_PTR)t->Active;
}

static void sync_threads(void)
{
    for (W32THREAD *t = W32NextThread(nullptr); t; t = W32NextThread(t))
        sync_thread(t);
}

/* called with the lock held, as its last owner lets go of it. */
void DeskSync(void)
{
    if (!g_users || !g_heap)
        return;
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    if (!p || !p->DeskBase)
        return;
    if (!g_dirty) {
        /*
         * the message a thread answers changes under this lock too, and only for the thread itself. not W32GetThread,
         * which makes a thread of the window manager of one that is not.
         */
        sync_thread(PsGetCurrentThreadWin32Thread());
        return;
    }
    g_dirty = FALSE;
    WndDesktop();
    g_sync++;
    /* windows and menus name their neighbours and submenus by offsets, so each has its block before any is written. */
    for (USER_OBJECT *o = nullptr; (o = UserNextLive(o, USER_TYPE_MENU)) != nullptr;)
        if (!o->Destroyed && !o->DeskOffset)
            o->DeskOffset = heap_alloc(MENU_SIZE);
    for (USER_OBJECT *o = nullptr; (o = UserNextLive(o, USER_TYPE_MENU)) != nullptr;)
        write_menu((MENU *)o);
    for (USER_OBJECT *o = nullptr; (o = UserNextLive(o, USER_TYPE_WINDOW)) != nullptr;)
        if (!o->Destroyed && !o->DeskOffset)
            o->DeskOffset = heap_alloc(WND_SIZE);
    for (USER_OBJECT *o = nullptr; (o = UserNextLive(o, USER_TYPE_WINDOW)) != nullptr;)
        write_window((WND *)o);
    /* spwndShell, which GetShellWindow reads. */
    *(PULONG64)(g_heap + DESK_INFO + 0x18) = offset_of(WndShell);
    sync_threads();
}

void DeskThreadFreed(W32THREAD *t)
{
    if (t->DeskInfo && g_heap) {
        memset(g_heap + t->DeskInfo, 0, THREAD_INFO_SIZE);
        heap_free(t->DeskInfo, THREAD_INFO_SIZE);
    }
    t->DeskInfo = 0;
}

/* the handle of an object is freed: its entry keeps the high word the next handle there gets. */
void DeskObjectFreed(USER_OBJECT *object)
{
    PUCHAR entry = g_heap ? entry_of(object) : nullptr;
    if (entry) {
        USHORT next = (USHORT)((((ULONG_PTR)object->Handle >> 16) + 1) & 0x7FFF);
        memset(entry, 0, ENTRY_SIZE);
        *(PUSHORT)(entry + ENTRY_UNIQUE) = next;
    }
    if (!object->DeskOffset || !g_heap)
        return;
    if (object->DeskScroll) {
        heap_free(object->DeskScroll, sizeof(SCROLL_INFO));
        object->DeskScroll = 0;
    }
    if (object->Type == USER_TYPE_WINDOW) {
        PUCHAR old = g_heap + object->DeskOffset;
        heap_free((ULONG)*(PULONG64)(old + WND_TEXT), *(PULONG)(old + WND_TEXT_MAXIMUM));
        heap_free(object->DeskOffset, WND_SIZE);
    } else if (object->Type == USER_TYPE_MENU) {
        MENU *m = (MENU *)object;
        if (m->DeskItems)
            heap_free(m->DeskItems, m->DeskItemsSize);
        m->DeskItems = m->DeskItemsSize = 0;
        heap_free(object->DeskOffset, MENU_SIZE);
    }
    object->DeskOffset = 0;
}

/* the calling thread of a program of user32 learns where the heap is. called with the lock held. */
NTSTATUS DeskAttachThread(W32PROCESS *p)
{
    if (!p || !p->SharedBase)
        return STATUS_SUCCESS;
    NTSTATUS status = g_section ? STATUS_SUCCESS : build();
    if (!NT_SUCCESS(status))
        return status;
    if (!p->DeskBase) {
        status = MmMapReadOnlySection(g_section, p->Process, &p->DeskBase);
        if (!NT_SUCCESS(status))
            return status;
        g_users++;
    }
    PUCHAR teb = PsGetCurrentThreadTeb();
    if (!teb || PsGetCurrentProcess() != p->Process)
        return STATUS_SUCCESS;
    ULONG64 thread = (ULONG64)(ULONG_PTR)PsGetCurrentThreadId();
    ULONG64 desktop[2] = {(ULONG64)(ULONG_PTR)p->DeskBase + DESK_INFO, (ULONG64)(ULONG_PTR)p->DeskBase};
    MimeCopyToUser(teb + FIELD_OFFSET(TEB, Win32ThreadInfo), &thread, sizeof(thread));
    MimeCopyToUser(teb + FIELD_OFFSET(TEB, Win32ClientInfo) + 4 * sizeof(ULONG64), desktop, sizeof(desktop));
    W32THREAD *t = W32GetThread();
    if (t && !t->DeskInfo && (t->DeskInfo = heap_alloc(THREAD_INFO_SIZE)) != 0)
        thread_info_init(t->DeskInfo);
    ULONG64 thread_info = (ULONG64)(ULONG_PTR)p->DeskBase + (t && t->DeskInfo ? t->DeskInfo : DESK_THREAD_INFO);
    MimeCopyToUser(teb + FIELD_OFFSET(TEB, Win32ClientInfo) + 0x60, &thread_info, sizeof(thread_info));
    /*
     * at 0x10 the version of windows the program was made for, the subsystem version of its image: user32 keeps old
     * behaviours below 4.0, like the cursors IDC_SIZE and IDC_ICON. measured on windows.
     */
    PPEB peb = PsGetProcessPeb(p->Process);
    ULONG version[2];
    if (peb && NT_SUCCESS(MimeCopyFromUser(version, &peb->ImageSubsystemMajorVersion, sizeof(version)))) {
        ULONG expected = (version[0] & 0xFF) << 8 | (version[1] & 0xFF);
        MimeCopyToUser(teb + FIELD_OFFSET(TEB, Win32ClientInfo) + 0x10, &expected, sizeof(expected));
    }
    return STATUS_SUCCESS;
}

void DeskProcessExited(W32PROCESS *p)
{
    if (p && p->DeskBase && g_users) {
        p->DeskBase = nullptr;
        g_users--;
    }
}

/* the address of a window in the view of the heap of the calling program, 0 when it has none. */
ULONG64 DeskUserWindow(WND *w)
{
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    if (!w || w->Header.Destroyed || !p || !p->DeskBase || !g_heap)
        return 0;
    if (!w->Header.DeskOffset)
        w->Header.DeskOffset = heap_alloc(WND_SIZE);
    return w->Header.DeskOffset ? (ULONG64)(ULONG_PTR)p->DeskBase + w->Header.DeskOffset : 0;
}

/*
 * user32 asks for the address of a window or menu when the thread has no view of the heap yet, as a new thread at its
 * first use of another thread's window, or when the entry names another heap. the call itself made the thread one of
 * the window manager, so the next lookups read the table.
 */
ULONG64 NTAPI NtUserMapDesktopObject(HANDLE handle)
{
    UserEnterRead();
    ULONG64 address = 0;
    USER_OBJECT *o = UserLookup(handle, 0);
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    if (o && p && p->DeskBase && g_heap && (o->Type == USER_TYPE_WINDOW || o->Type == USER_TYPE_MENU)) {
        if (o->Type == USER_TYPE_WINDOW) {
            if (!o->DeskOffset)
                DeskTouch();
            address = DeskUserWindow((WND *)o);
        }
        else if (o->DeskOffset)
            address = (ULONG64)(ULONG_PTR)p->DeskBase + o->DeskOffset;
    }
    UserLeave();
    if (!address)
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return address;
}
