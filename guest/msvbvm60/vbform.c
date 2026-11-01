/*
 * forms and the intrinsic controls: the instance of a form is made from the tables the compiler of Visual Basic writes into the
 * program, its controls from the stream of properties of the form, and the windows from both. The object a compiled program
 * calls is a vtable of thunks, one for each slot the compiler of Visual Basic knows, which come here with the number of the slot.
 */
#include "vb.h"
#include "vbclass.h"

#define MAX_CONTROLS 160
#define MAX_PROPS 100
#define MAX_FORMS 32
#define MAX_SLOTS 480
#define ACCESSOR_BASE 190

typedef struct VbCtl VbCtl;

/* the vtable and the first words are those of a COM object, and the pointer at 0x10 is the same in the object of a form. */
struct VbCtl {
    void **vtable;
    LONG refs;
    DWORD magic;
    DWORD reserved;
    VbCtl *self;
    VbForm *form;
    int cls;
    int position;
    const BYTE *entry;
    char name[48];
    int control_index;
    HWND hwnd;
    HFONT font;
    VARIANT value[MAX_PROPS];
    BYTE set[MAX_PROPS];
    /* a menu */
    HMENU menu;
    UINT command;
    VbCtl *parent_menu;
    /* the control that contains it, none for the form. */
    VbCtl *parent;
    const BYTE *record;
    /* a windowless label */
    RECT bounds;
    char *font_name;
    float font_size;
    int font_weight;
    BOOL font_italic, font_underline, font_strike;
};

typedef struct VbObject {
    const BYTE *info;
    const BYTE *descriptor;
    const char *name;
    DWORD type;
    unsigned count;
    const BYTE *controls;
    int gui;
} VbObject;

struct VbForm {
    const VbObject *object;
    BYTE *instance;
    ULONG size;
    VbCtl *ctl[MAX_CONTROLS];
    int count;
    VbCtl *self;
    HMENU menu;
    BOOL loaded;
    BOOL loading;
    BOOL shown;
    int unloading;
    const BYTE *blob;
    ULONG blob_size;
    void **vtable;
    VbCtl *order[MAX_CONTROLS];
    int ordered;
    VbCtl *menus[MAX_CONTROLS];
    int menu_count;
};

static const VbHeader *g_header;
static VbObject g_objects[MAX_FORMS];
static int g_object_count;
static VbForm *g_forms[MAX_FORMS];
static int g_form_count;
static int g_dpi_x = 96, g_dpi_y = 96;

#define MAGIC 0x5642434C

/* ------------------------------------------------------------------ the tables of the program */

static BOOL readable(const void *p, SIZE_T n)
{
    return !IsBadReadPtr(p, n);
}

static int class_of_guid(const BYTE *guid)
{
    DWORD d1 = *(const DWORD *)guid;
    if (d1 < 0x33AD4ED0u || d1 >= 0x33AD5000u)
        return -1;
    int cls = (int)((d1 - 0x33AD4ED0u) >> 3);
    return cls < g_class_count && g_classes[cls].props ? cls : -1;
}

void VbLoadProject(const VbHeader *header)
{
    g_header = header;
    const BYTE *project = (const BYTE *)(ULONG_PTR)header->project_info;
    const BYTE *table = (const BYTE *)*(const DWORD *)(project + 4);
    unsigned count = *(const unsigned short *)(table + 42);
    const BYTE *objects = (const BYTE *)*(const DWORD *)(table + 48);
    int gui = 0;
    for (unsigned i = 0; i < count && g_object_count < MAX_FORMS; i++) {
        const BYTE *pod = objects + i * 0x30;
        VbObject *o = &g_objects[g_object_count++];
        o->descriptor = pod;
        o->info = (const BYTE *)*(const DWORD *)pod;
        o->name = *(const char *const *)(pod + 24);
        o->type = *(const DWORD *)(pod + 40);
        o->count = 0;
        o->gui = -1;
        if (o->type & 0x80) {
            const BYTE *opt = o->info + 0x38;
            o->count = *(const DWORD *)(opt + 0x20);
            o->controls = (const BYTE *)*(const DWORD *)(opt + 0x24);
            o->gui = gui++;
        }
    }
    HDC dc = GetDC(nullptr);
    g_dpi_x = GetDeviceCaps(dc, LOGPIXELSX);
    g_dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
}

/* a pixel is a whole number of twips, 7 at 192 dots to the inch where 7.5 would be exact. */
static int to_px_x(double twips) { return (int)(twips / (1440 / g_dpi_x) + (twips < 0 ? -0.5 : 0.5)); }
static int to_px_y(double twips) { return (int)(twips / (1440 / g_dpi_y) + (twips < 0 ? -0.5 : 0.5)); }

/* ------------------------------------------------------------------ the properties */

static const VBPROP *prop_of(const VbCtl *c, int k)
{
    return k >= 0 && k < g_classes[c->cls].nprops ? &g_classes[c->cls].props[k] : nullptr;
}

static int find_prop(const VbCtl *c, const char *name)
{
    const VBCLASS *cl = &g_classes[c->cls];
    for (int i = 0; i < cl->nprops; i++)
        if (cl->props[i].name && !lstrcmpA(cl->props[i].name, name))
            return i;
    return -1;
}

static BOOL type_is_number(int t)
{
    return t == 2 || t == 3 || t == 4 || t == 5 || t == 6 || t == 7 || (t >= 8 && t <= 11) || t == 0x11;
}

/* the value of a property as a double, whatever it is stored as: the default when it was never set. */
static double number_of(const VbCtl *c, int k)
{
    const VBPROP *p = prop_of(c, k);
    if (!p)
        return 0;
    if (!c->set[k]) {
        if (p->type == 7 || (p->type >= 8 && p->type <= 11)) {
            union { float f; DWORD d; } u = {.d = p->deflt};
            return u.f;
        }
        if (p->type == 4)
            return p->deflt ? -1 : 0;
        return (LONG)p->deflt;
    }
    const VARIANT *v = &c->value[k];
    switch (v->vt) {
    case VT_I2:
    case VT_BOOL: return v->iVal;
    case VT_I4: return v->lVal;
    case VT_R4: return v->fltVal;
    default: return 0;
    }
}

static const WCHAR *text_of(const VbCtl *c, int k)
{
    return k >= 0 && c->set[k] && c->value[k].vt == VT_BSTR && c->value[k].bstrVal ? c->value[k].bstrVal : L"";
}

static void set_number(VbCtl *c, int k, double d)
{
    const VBPROP *p = prop_of(c, k);
    if (!p)
        return;
    VARIANT *v = &c->value[k];
    VariantClear(v);
    switch (p->type) {
    case 3:
    case 5: v->vt = VT_I4; v->lVal = (LONG)d; break;
    case 4: v->vt = VT_BOOL; v->boolVal = d ? -1 : 0; break;
    case 7:
    case 8:
    case 9:
    case 10:
    case 11: v->vt = VT_R4; v->fltVal = (float)d; break;
    default: v->vt = VT_I2; v->iVal = (SHORT)d; break;
    }
    c->set[k] = 1;
}

static void set_text(VbCtl *c, int k, const WCHAR *text)
{
    VARIANT *v = &c->value[k];
    VariantClear(v);
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocString(text ? text : L"");
    c->set[k] = 1;
}

#define NUM(c, name) number_of((c), find_prop((c), name))

/* ------------------------------------------------------------------ fonts */

static void make_font(VbCtl *c)
{
    if (c->font) {
        DeleteObject(c->font);
        c->font = nullptr;
    }
    WCHAR face[LF_FACESIZE] = L"MS Sans Serif";
    if (c->font_name)
        MultiByteToWideChar(CP_ACP, 0, c->font_name, -1, face, LF_FACESIZE);
    LOGFONTW lf = {};
    float size = c->font_size > 0 ? c->font_size : 8.25f;
    lf.lfHeight = -(LONG)(size * g_dpi_y / 72.0f + 0.5f);
    lf.lfWeight = c->font_weight ? c->font_weight : FW_NORMAL;
    lf.lfItalic = c->font_italic;
    lf.lfUnderline = c->font_underline;
    lf.lfStrikeOut = c->font_strike;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    c->font = CreateFontIndirectW(&lf);
}

static HFONT font_of(VbCtl *c)
{
    if (!c->font)
        make_font(c);
    return c->font;
}

/* ------------------------------------------------------------------ the stream of a form */

static const BYTE *find_record(const BYTE *blob, ULONG size, int index, const char *name)
{
    SIZE_T n = lstrlenA(name);
    BYTE head = index == 0xFFFF ? 0 : (BYTE)index;
    for (ULONG i = 0; i + n + 4 < size; i++) {
        if (blob[i] != head || *(const USHORT *)(blob + i + 1) != n)
            continue;
        BOOL same = TRUE;
        for (SIZE_T j = 0; j < n && same; j++)
            same = blob[i + 3 + j] == (BYTE)name[j];
        if (same && blob[i + 3 + n] == 0)
            return blob + i + 3 + n + 1;
    }
    return nullptr;
}

static void store_item(VbCtl *c, int k, const BYTE *p, int type)
{
    switch (type) {
    case 1: {
        int len = *(const USHORT *)p;
        WCHAR *w = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
        MultiByteToWideChar(CP_ACP, 0, (const char *)p + 2, len, w, len);
        w[len] = 0;
        set_text(c, k, w);
        HeapFree(GetProcessHeap(), 0, w);
        break;
    }
    case 2:
    case 0x11: set_number(c, k, *(const SHORT *)p); break;
    case 3:
    case 5: set_number(c, k, *(const LONG *)p); break;
    case 4:
    case 6: set_number(c, k, type == 4 ? (*p ? -1 : 0) : (double)(signed char)*p); break;
    case 7: {
        union { DWORD d; float f; } u = {.d = *(const DWORD *)p};
        set_number(c, k, u.f);
        break;
    }
    }
}

/* reads the properties of a record to its end, as far as the types are known. returns the end. */
static const BYTE *load_properties(VbCtl *c, const BYTE *p, const BYTE *limit)
{
    const VBCLASS *cl = &g_classes[c->cls];
    int last = -1;
    while (p < limit) {
        int k = *p;
        if (k == 0xFF)
            return p + 1;
        if (k >= cl->nprops) {
            char text[100];
            wsprintfA(text, "msvbvm60: stream of %s stops at property %d after %d\n", c->name, k, last);
            VbOutputDebug(text);
            return nullptr;
        }
        const VBPROP *prop = &cl->props[k];
        int t = prop->type;
        p++;
        if (!lstrcmpA(prop->name, "Left") || !lstrcmpA(prop->name, "ClientLeft")) {
            int wide = !lstrcmpA(prop->name, "ClientLeft") ? 4 : 2;
            int n = 1;
            while (k + n < cl->nprops && n < 4 && cl->props[k + n].type >= 8 && cl->props[k + n].type <= 11)
                n++;
            for (int i = 0; i < n; i++) {
                LONG v = wide == 4 ? *(const LONG *)(p + i * 4) : *(const USHORT *)(p + i * 2);
                set_number(c, k + i, v);
            }
            p += n * wide;
            last = k + n - 1;
            continue;
        }
        if (t == 1 || t == 0x0d) {
            store_item(c, k, p, 1);
            p += 2 + *(const USHORT *)p + 1;
        } else if (t == 0x14) {
            /* a font: version, character set, attributes, weight, size in hundredths of a point, name. */
            BYTE attrs = p[3];
            c->font_weight = *(const USHORT *)(p + 4);
            c->font_size = (float)(*(const DWORD *)(p + 6)) / 10000.0f;
            c->font_italic = (attrs & 2) != 0;
            c->font_underline = (attrs & 4) != 0;
            c->font_strike = (attrs & 8) != 0;
            int len = p[10];
            c->font_name = HeapAlloc(GetProcessHeap(), 0, len + 1);
            memcpy(c->font_name, p + 11, len);
            c->font_name[len] = 0;
            p += 11 + len;
        } else if (t == 0x15) {
            DWORD n = *(const DWORD *)p;
            p += n == 0xFFFFFFFFu ? 4 : 4 + n;
        } else if (t == 0x16) {
            /* a data format, kept as one byte that says none is set. */
            p += 1;
        } else if (type_is_number(t)) {
            int size = t == 2 || t == 0x11 ? 2 : t == 4 || t == 6 ? 1 : 4;
            if (t == 6 && !lstrcmpA(prop->name, "ScaleMode"))
                size = 2;
            store_item(c, k, p, t);
            p += size;
        } else {
            char text[100];
            wsprintfA(text, "msvbvm60: property %s of type %d in the stream of %s is not known\n", prop->name, t, c->name);
            VbOutputDebug(text);
            return nullptr;
        }
        last = k;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ calling the compiled code */

static LONG call_stdcall(void *fn, DWORD *argv, int argc)
{
    LONG result;
    __asm__ volatile("movl %2, %%ecx\n"
                     "1:\n\ttestl %%ecx, %%ecx\n\tjz 2f\n\tpushl -4(%1,%%ecx,4)\n\tdecl %%ecx\n\tjmp 1b\n"
                     "2:\n\tcall *%3"
                     : "=a"(result)
                     : "r"(argv), "r"(argc), "r"(fn)
                     : "ecx", "edx", "memory", "cc");
    return result;
}

/* an event of a control goes to the procedure the program has for it, when it has one. */
static BOOL fire(VbCtl *c, const char *event, DWORD *args, int nargs)
{
    const VBCLASS *cl = &g_classes[c->cls];
    if (!c->entry || !cl->events)
        return FALSE;
    int index = -1;
    for (int i = 0; i < cl->nevents; i++)
        if (!lstrcmpA(cl->events[i], event)) {
            index = i;
            break;
        }
    if (index < 0)
        return FALSE;
    DWORD table = *(const DWORD *)(c->entry + 0x18);
    if (!table)
        return FALSE;
    void *handler = (void *)(ULONG_PTR) * (const DWORD *)(table + (6 + index) * 4);
    if (!handler || (ULONG_PTR)handler < 0x1000)
        return FALSE;
    DWORD argv[8];
    argv[0] = (DWORD)(ULONG_PTR)(c->form->instance + *(const DWORD *)(c->entry + 4));
    for (int i = 0; i < nargs && i < 7; i++)
        argv[1 + i] = args[i];
    call_stdcall(handler, argv, 1 + nargs);
    return TRUE;
}

#define FIRE(c, event) fire((c), (event), nullptr, 0)

/* ------------------------------------------------------------------ the windows */

static HWND form_window(VbCtl *c)
{
    return c->form->self->hwnd;
}

static void place(VbCtl *c)
{
    int left = to_px_x(NUM(c, "Left")), top = to_px_y(NUM(c, "Top"));
    int width = to_px_x(NUM(c, "Width")), height = to_px_y(NUM(c, "Height"));
    c->bounds.left = left;
    c->bounds.top = top;
    c->bounds.right = left + width;
    c->bounds.bottom = top + height;
    if (c->hwnd)
        SetWindowPos(c->hwnd, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void paint_windowless(VbCtl *c)
{
    HWND form = form_window(c);
    if (form && IsWindow(form)) {
        RECT r = c->bounds;
        InvalidateRect(form, &r, TRUE);
    }
}

static COLORREF color_of(VbCtl *c, int k, COLORREF fallback)
{
    LONG v = (LONG)number_of(c, k);
    if (v < 0) {
        int index = v & 0xFF;
        return GetSysColor(index);
    }
    (void)fallback;
    return (COLORREF)v;
}

static void apply_text(VbCtl *c)
{
    int k = find_prop(c, c->cls == VBC_TEXTBOX ? "Text" : "Caption");
    if (k < 0)
        return;
    if (c->cls == VBC_LABEL)
        paint_windowless(c);
    else if (c->hwnd)
        SetWindowTextW(c->hwnd, text_of(c, k));
}

static void apply_all(VbCtl *c)
{
    if (c->cls == VBC_FORM) {
        if (c->hwnd)
            SetWindowTextW(c->hwnd, text_of(c, find_prop(c, "Caption")));
        return;
    }
    place(c);
    apply_text(c);
    if (c->hwnd) {
        HFONT f = font_of(c);
        SendMessageW(c->hwnd, WM_SETFONT, (WPARAM)f, TRUE);
        EnableWindow(c->hwnd, NUM(c, "Enabled") != 0);
        ShowWindow(c->hwnd, NUM(c, "Visible") != 0 ? SW_SHOWNA : SW_HIDE);
        if (c->cls == VBC_CHECKBOX)
            SendMessageW(c->hwnd, BM_SETCHECK, (WPARAM)NUM(c, "Value"), 0);
    }
}

static const WCHAR *window_class(int cls)
{
    switch (cls) {
    case VBC_COMMAND: return L"ThunderRT6CommandButton";
    case VBC_CHECKBOX: return L"ThunderRT6CheckBox";
    case VBC_OPTION: return L"ThunderRT6OptionButton";
    case VBC_FRAME: return L"ThunderRT6Frame";
    case VBC_TEXTBOX: return L"ThunderRT6TextBox";
    default: return nullptr;
    }
}

static void create_child_window(VbCtl *c)
{
    const WCHAR *name = window_class(c->cls);
    HWND parent = c->parent && c->parent->hwnd ? c->parent->hwnd : form_window(c);
    if (!name || !parent)
        return;
    DWORD style = WS_CHILD | WS_CLIPSIBLINGS;
    DWORD ex = 0;
    if (c->cls != VBC_FRAME && NUM(c, "TabStop") != 0 && c->cls != VBC_TEXTBOX)
        style |= WS_TABSTOP;
    switch (c->cls) {
    case VBC_COMMAND: style |= BS_PUSHBUTTON | BS_MULTILINE; break;
    case VBC_CHECKBOX: style |= BS_AUTOCHECKBOX; break;
    case VBC_OPTION: style |= BS_AUTORADIOBUTTON; break;
    case VBC_TEXTBOX:
        style |= WS_TABSTOP | ES_AUTOHSCROLL;
        if (NUM(c, "MultiLine"))
            style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
        if (((int)NUM(c, "ScrollBars") & 2))
            style |= WS_VSCROLL;
        if (((int)NUM(c, "ScrollBars") & 1))
            style |= WS_HSCROLL;
        if (NUM(c, "Locked"))
            style |= ES_READONLY;
        if (NUM(c, "Alignment") == 1)
            style |= ES_RIGHT;
        else if (NUM(c, "Alignment") == 2)
            style |= ES_CENTER;
        if (NUM(c, "BorderStyle") != 0)
            ex |= WS_EX_CLIENTEDGE;
        break;
    }
    c->hwnd = CreateWindowExW(ex, name, text_of(c, find_prop(c, c->cls == VBC_TEXTBOX ? "Text" : "Caption")), style, 0, 0, 10, 10,
                              parent, (HMENU)(ULONG_PTR)(1000 + c->position), GetModuleHandleW(nullptr), nullptr);
    if (c->hwnd)
        SetWindowLongW(c->hwnd, GWL_USERDATA, (LONG)(LONG_PTR)c);
}

static void build_menus(VbForm *f)
{
    HMENU bar = nullptr;
    HMENU top = nullptr;
    VbCtl *top_ctl = nullptr;
    for (int i = 0; i < f->menu_count; i++) {
        VbCtl *c = f->menus[i];
        if (!bar)
            bar = CreateMenu();
        BOOL is_top = c->set[7] != 0 && number_of(c, 7) != 0;
        int caption = find_prop(c, "Caption");
        const WCHAR *text = text_of(c, caption);
        UINT flags = MF_STRING;
        if (NUM(c, "Enabled") == 0)
            flags |= MF_GRAYED;
        if (NUM(c, "Checked"))
            flags |= MF_CHECKED;
        c->command = 5000 + (UINT)c->position;
        if (is_top || !top) {
            top = CreatePopupMenu();
            top_ctl = c;
            c->menu = top;
            AppendMenuW(bar, flags | MF_POPUP, (UINT_PTR)top, text);
        } else {
            c->parent_menu = top_ctl;
            if (text[0] == L'-' && !text[1])
                AppendMenuW(top, MF_SEPARATOR, 0, nullptr);
            else
                AppendMenuW(top, flags, c->command, text);
        }
    }
    f->menu = bar;
    if (bar && f->self->hwnd)
        SetMenu(f->self->hwnd, bar);
}

static void menu_click(VbForm *f, UINT command)
{
    for (int i = 0; i < f->count; i++)
        if (f->ctl[i]->cls == VBC_MENU && f->ctl[i]->command == command) {
            FIRE(f->ctl[i], "Click");
            return;
        }
}

static void layout_form(VbForm *f)
{
    VbCtl *c = f->self;
    if (!c->hwnd)
        return;
    int cw = to_px_x(NUM(c, "ClientWidth")), ch = to_px_y(NUM(c, "ClientHeight"));
    RECT rc = {0, 0, cw, ch};
    DWORD style = GetWindowLongW(c->hwnd, GWL_STYLE);
    AdjustWindowRectEx(&rc, style, f->menu != nullptr, GetWindowLongW(c->hwnd, GWL_EXSTYLE));
    int x = to_px_x(NUM(c, "ClientLeft")), y = to_px_y(NUM(c, "ClientTop"));
    int position = (int)NUM(c, "StartUpPosition");
    if (position == 2) {
        x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
    } else if (position == 3) {
        x = CW_USEDEFAULT;
        y = CW_USEDEFAULT;
    }
    SetWindowPos(c->hwnd, nullptr, x == CW_USEDEFAULT ? 0 : x, y == CW_USEDEFAULT ? 0 : y, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | (x == CW_USEDEFAULT ? SWP_NOMOVE : 0));
}

/* ------------------------------------------------------------------ the object of a control */

static void *make_vtable(const VBCLASS *cl, int accessor_count, VbForm *form);

static VbCtl *new_ctl(VbForm *f, int cls, const BYTE *entry, int position)
{
    VbCtl *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    c->magic = MAGIC;
    c->refs = 1;
    c->self = c;
    c->form = f;
    c->cls = cls;
    c->entry = entry;
    c->position = position;
    if (entry) {
        const char *name = *(const char *const *)(entry + 0x20);
        lstrcpynA(c->name, name ? name : "", sizeof(c->name));
        c->control_index = *(const USHORT *)(entry + 0xc);
    }
    return c;
}

/* ------------------------------------------------------------------ slots */

static void ensure_loaded(VbForm *f);

static VbCtl *ctl_of(void *object)
{
    VbCtl *c = *(VbCtl **)((BYTE *)object + 0x10);
    return c && c->magic == MAGIC ? c : nullptr;
}

static void get_value(VbCtl *c, int k, DWORD *out)
{
    const VBPROP *p = prop_of(c, k);
    if (!p) {
        *out = 0;
        return;
    }
    switch (p->type) {
    case 1: {
        const WCHAR *s = c->set[k] ? text_of(c, k) : L"";
        *(BSTR *)out = SysAllocString(s);
        break;
    }
    case 2:
    case 4:
    case 6:
    case 0x11: *out = (USHORT)(SHORT)number_of(c, k); break;
    case 3:
    case 5: *(LONG *)out = (LONG)number_of(c, k); break;
    case 7:
    case 8:
    case 9:
    case 10:
    case 11: *(float *)out = (float)number_of(c, k); break;
    default: *out = 0; break;
    }
}

static void apply_property(VbCtl *c, int k)
{
    const VBPROP *p = prop_of(c, k);
    if (!p)
        return;
    const char *n = p->name;
    if (!lstrcmpA(n, "Caption") || !lstrcmpA(n, "Text")) {
        if (c->cls == VBC_FORM && c->hwnd)
            SetWindowTextW(c->hwnd, text_of(c, k));
        else
            apply_text(c);
    } else if (!lstrcmpA(n, "Left") || !lstrcmpA(n, "Top") || !lstrcmpA(n, "Width") || !lstrcmpA(n, "Height")) {
        if (c->cls != VBC_FORM)
            place(c);
    } else if (!lstrcmpA(n, "Visible")) {
        if (c->cls == VBC_FORM) {
            if (c->hwnd)
                ShowWindow(c->hwnd, NUM(c, "Visible") ? SW_SHOW : SW_HIDE);
        } else if (c->hwnd) {
            ShowWindow(c->hwnd, NUM(c, "Visible") ? SW_SHOWNA : SW_HIDE);
        } else {
            paint_windowless(c);
        }
    } else if (!lstrcmpA(n, "Enabled")) {
        if (c->hwnd)
            EnableWindow(c->hwnd, NUM(c, "Enabled") != 0);
        else if (c->cls == VBC_MENU && c->command)
            EnableMenuItem(GetMenu(form_window(c)), c->command, NUM(c, "Enabled") ? MF_ENABLED : MF_GRAYED);
    } else if (!lstrcmpA(n, "Value")) {
        if (c->hwnd && (c->cls == VBC_CHECKBOX || c->cls == VBC_OPTION))
            SendMessageW(c->hwnd, BM_SETCHECK, (WPARAM)NUM(c, "Value"), 0);
    } else if (!lstrcmpA(n, "Checked")) {
        if (c->cls == VBC_MENU && c->command)
            CheckMenuItem(GetMenu(form_window(c)), c->command, NUM(c, "Checked") ? MF_CHECKED : MF_UNCHECKED);
    } else if (!lstrcmpA(n, "ForeColor") || !lstrcmpA(n, "BackColor") || !lstrcmpA(n, "BackStyle") || !lstrcmpA(n, "Alignment")) {
        if (c->hwnd)
            InvalidateRect(c->hwnd, nullptr, TRUE);
        else if (c->cls == VBC_FORM && c->hwnd)
            InvalidateRect(c->hwnd, nullptr, TRUE);
        else
            paint_windowless(c);
    } else if (!lstrcmpA(n, "FontName") || !lstrcmpA(n, "FontSize") || !lstrcmpA(n, "FontBold") || !lstrcmpA(n, "FontItalic") ||
               !lstrcmpA(n, "FontUnderline") || !lstrcmpA(n, "FontStrikethru")) {
        if (!lstrcmpA(n, "FontName")) {
            if (c->font_name)
                HeapFree(GetProcessHeap(), 0, c->font_name);
            int len = lstrlenW(text_of(c, k));
            c->font_name = HeapAlloc(GetProcessHeap(), 0, len + 1);
            WideCharToMultiByte(CP_ACP, 0, text_of(c, k), -1, c->font_name, len + 1, nullptr, nullptr);
        } else if (!lstrcmpA(n, "FontSize")) {
            c->font_size = (float)NUM(c, "FontSize");
        } else if (!lstrcmpA(n, "FontBold")) {
            c->font_weight = NUM(c, "FontBold") ? FW_BOLD : FW_NORMAL;
        } else if (!lstrcmpA(n, "FontItalic")) {
            c->font_italic = NUM(c, "FontItalic") != 0;
        } else if (!lstrcmpA(n, "FontUnderline")) {
            c->font_underline = NUM(c, "FontUnderline") != 0;
        } else {
            c->font_strike = NUM(c, "FontStrikethru") != 0;
        }
        make_font(c);
        if (c->hwnd)
            SendMessageW(c->hwnd, WM_SETFONT, (WPARAM)c->font, TRUE);
        else
            paint_windowless(c);
    }
}

/* the window of a control that the user changed has its text and state in the properties as well. */
static void read_back(VbCtl *c)
{
    if (!c->hwnd)
        return;
    if (c->cls == VBC_TEXTBOX) {
        int len = GetWindowTextLengthW(c->hwnd);
        WCHAR *w = HeapAlloc(GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR));
        GetWindowTextW(c->hwnd, w, len + 1);
        set_text(c, find_prop(c, "Text"), w);
        HeapFree(GetProcessHeap(), 0, w);
    } else if (c->cls == VBC_CHECKBOX || c->cls == VBC_OPTION) {
        set_number(c, find_prop(c, "Value"), (double)SendMessageW(c->hwnd, BM_GETCHECK, 0, 0));
    }
}

static BOOL g_trace_unknown = TRUE;

static void report_slot(VbCtl *c, int slot, const char *what)
{
    if (!g_trace_unknown)
        return;
    char text[160];
    wsprintfA(text, "msvbvm60: %s slot %d of %s (%s) is not implemented\n", what, slot, g_classes[c->cls].name, c->name);
    VbOutputDebug(text);
}

/* methods of the controls and forms by their place in the table of the layout. */
static HRESULT call_method(VbCtl *c, int slot, const VBSLOT *s, DWORD *args)
{
    (void)args;
    HWND form = c->form->self->hwnd;
    if (c->cls == VBC_FORM) {
        switch (s->index) {
        case 4: /* Show */
            c->form->shown = TRUE;
            if (form) {
                ShowWindow(form, SW_SHOW);
                UpdateWindow(form);
            }
            return S_OK;
        case 5: /* Hide */
            if (form)
                ShowWindow(form, SW_HIDE);
            return S_OK;
        case 0: /* Refresh */
            if (form)
                InvalidateRect(form, nullptr, TRUE);
            return S_OK;
        case 2: /* SetFocus */
            if (form)
                SetFocus(form);
            return S_OK;
        }
    } else if (s->kind == 3 && s->index == 0) {
        return S_OK;
    }
    report_slot(c, slot, "method");
    return S_OK;
}

LONG __cdecl vb_dispatch(int slot, DWORD *frame)
{
    void *object = (void *)(ULONG_PTR)frame[0];
    VbCtl *c = ctl_of(object);
    if (!c)
        return E_POINTER;
    const VBCLASS *cl = &g_classes[c->cls];
    DWORD *args = frame + 1;
    ensure_loaded(c->form);
    if (slot >= ACCESSOR_BASE && c->cls == VBC_FORM) {
        int index = slot - ACCESSOR_BASE;
        for (int i = 0; i < c->form->count; i++) {
            VbCtl *t = c->form->ctl[i];
            if (t->control_index == index && t != c->form->self && t->cls >= 0) {
                InterlockedIncrement(&t->refs);
                return (LONG)(LONG_PTR)t;
            }
        }
        return 0;
    }
    if (slot < 0 || slot >= cl->nslots) {
        report_slot(c, slot, "member");
        return E_NOTIMPL;
    }
    const VBSLOT *s = &cl->slots[slot];
    switch (s->kind) {
    case 1: {
        DWORD *out = (DWORD *)(ULONG_PTR)args[0];
        if (c->hwnd && c->cls == VBC_TEXTBOX && !lstrcmpA(cl->props[s->index].name, "Text"))
            read_back(c);
        if (c->hwnd && (c->cls == VBC_CHECKBOX || c->cls == VBC_OPTION) && !lstrcmpA(cl->props[s->index].name, "Value"))
            read_back(c);
        get_value(c, s->index, out);
        return S_OK;
    }
    case 2: {
        int k = s->index;
        const VBPROP *p = prop_of(c, k);
        if (!p)
            return E_NOTIMPL;
        VARIANT *v = &c->value[k];
        VariantClear(v);
        switch (p->type) {
        case 1: {
            BSTR b = (BSTR)(ULONG_PTR)args[0];
            v->vt = VT_BSTR;
            v->bstrVal = SysAllocStringLen(b, b ? SysStringLen(b) : 0);
            break;
        }
        case 3:
        case 5: v->vt = VT_I4; v->lVal = (LONG)args[0]; break;
        case 4: v->vt = VT_BOOL; v->boolVal = (SHORT)args[0] ? -1 : 0; break;
        case 7:
        case 8:
        case 9:
        case 10:
        case 11: {
            union { DWORD d; float f; } u = {.d = args[0]};
            v->vt = VT_R4;
            v->fltVal = u.f;
            break;
        }
        case 2:
        case 6:
        case 0x11: v->vt = VT_I2; v->iVal = (SHORT)args[0]; break;
        default: return S_OK;
        }
        c->set[k] = 1;
        apply_property(c, k);
        return S_OK;
    }
    case 3:
    case 4: return call_method(c, slot, s, args);
    }
    if (slot > 6)
        report_slot(c, slot, "member");
    return E_NOTIMPL;
}

/* IUnknown of the objects. */
static LONG __stdcall obj_query(void *self, const IID *iid, void **out)
{
    VbCtl *c = ctl_of(self);
    if (c && (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDispatch))) {
        *out = self;
        InterlockedIncrement(&c->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG __stdcall obj_addref(void *self)
{
    VbCtl *c = ctl_of(self);
    return c ? (ULONG)InterlockedIncrement(&c->refs) : 1;
}

static ULONG __stdcall obj_release(void *self)
{
    VbCtl *c = ctl_of(self);
    if (!c)
        return 0;
    LONG n = InterlockedDecrement(&c->refs);
    return n > 0 ? (ULONG)n : 1;
}

static LONG __stdcall obj_count(void *self, UINT *n)
{
    *n = 0;
    return S_OK;
}

static LONG __stdcall obj_info(void *self, UINT i, LCID l, void **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static LONG __stdcall obj_names(void *self, const IID *iid, LPOLESTR *names, UINT n, LCID l, DISPID *ids)
{
    VbCtl *c = ctl_of(self);
    if (!c || n < 1)
        return E_INVALIDARG;
    char narrow[80];
    WideCharToMultiByte(CP_ACP, 0, names[0], -1, narrow, sizeof(narrow), nullptr, nullptr);
    int k = find_prop(c, narrow);
    if (k < 0)
        return DISP_E_UNKNOWNNAME;
    ids[0] = 0x10000 + k;
    return S_OK;
}

static LONG __stdcall obj_invoke(void *self, DISPID id, const IID *iid, LCID l, WORD flags, DISPPARAMS *params, VARIANT *result,
                                 EXCEPINFO *ei, UINT *bad)
{
    VbCtl *c = ctl_of(self);
    int k = id - 0x10000;
    if (!c || k < 0 || k >= g_classes[c->cls].nprops)
        return DISP_E_MEMBERNOTFOUND;
    const VBPROP *p = prop_of(c, k);
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        if (!params->cArgs)
            return DISP_E_BADPARAMCOUNT;
        VARIANT t;
        VariantInit(&t);
        VARTYPE vt = p->type == 1 ? VT_BSTR : p->type == 4 ? VT_BOOL : p->type == 3 || p->type == 5 ? VT_I4 : p->type >= 7 && p->type <= 11 ? VT_R4 : VT_I2;
        if (FAILED(VariantChangeType(&t, &params->rgvarg[0], 0, vt)))
            return DISP_E_TYPEMISMATCH;
        VariantClear(&c->value[k]);
        c->value[k] = t;
        c->set[k] = 1;
        apply_property(c, k);
        return S_OK;
    }
    if (result) {
        read_back(c);
        VariantClear(result);
        if (p->type == 1) {
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(text_of(c, k));
        } else if (type_is_number(p->type)) {
            result->vt = VT_R8;
            result->dblVal = number_of(c, k);
            if (p->type == 4) {
                result->vt = VT_BOOL;
                result->boolVal = (SHORT)result->dblVal ? -1 : 0;
            } else if (p->type != 7 && !(p->type >= 8 && p->type <= 11)) {
                result->vt = VT_I4;
                result->lVal = (LONG)number_of(c, k);
            }
        }
    }
    return S_OK;
}

#define COMMON(N)                                                                                                         \
    __attribute__((naked)) static void common_##N(void)                                                                   \
    {                                                                                                                     \
        __asm__("leal 8(%esp), %eax\n\tpushl %eax\n\tpushl 4(%esp)\n\tcall _vb_dispatch\n\taddl $8, %esp\n\taddl $4, %esp\n\tret $" #N); \
    }

COMMON(0) COMMON(4) COMMON(8) COMMON(12) COMMON(16) COMMON(20) COMMON(24) COMMON(28) COMMON(32) COMMON(36) COMMON(40)
COMMON(44) COMMON(48) COMMON(52) COMMON(56) COMMON(60) COMMON(64) COMMON(68) COMMON(72) COMMON(76) COMMON(80) COMMON(84)
COMMON(88) COMMON(92) COMMON(96) COMMON(100) COMMON(104) COMMON(108) COMMON(112) COMMON(116) COMMON(120) COMMON(124)
COMMON(128) COMMON(132) COMMON(136) COMMON(140) COMMON(144) COMMON(148) COMMON(152) COMMON(156) COMMON(160)

#define ENTRY(N) common_##N
static void (*const g_common[])(void) = {
    ENTRY(0), ENTRY(4), ENTRY(8), ENTRY(12), ENTRY(16), ENTRY(20), ENTRY(24), ENTRY(28), ENTRY(32), ENTRY(36), ENTRY(40),
    ENTRY(44), ENTRY(48), ENTRY(52), ENTRY(56), ENTRY(60), ENTRY(64), ENTRY(68), ENTRY(72), ENTRY(76), ENTRY(80), ENTRY(84),
    ENTRY(88), ENTRY(92), ENTRY(96), ENTRY(100), ENTRY(104), ENTRY(108), ENTRY(112), ENTRY(116), ENTRY(120), ENTRY(124),
    ENTRY(128), ENTRY(132), ENTRY(136), ENTRY(140), ENTRY(144), ENTRY(148), ENTRY(152), ENTRY(156), ENTRY(160),
};

/* a thunk that pushes its slot and goes to the routine of the size of the arguments it takes. */
static void *make_thunk(int slot, int ret)
{
    static BYTE *page;
    static int used;
    if (!page || used + 16 > 0x10000) {
        page = VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        used = 0;
    }
    BYTE *p = page + used;
    used += 16;
    int n = ret / 4;
    if (n < 0 || n >= (int)(sizeof(g_common) / sizeof(g_common[0])))
        n = 2;
    p[0] = 0x68;
    *(DWORD *)(p + 1) = (DWORD)slot;
    p[5] = 0xB8;
    *(DWORD *)(p + 6) = (DWORD)(ULONG_PTR)g_common[n];
    p[10] = 0xFF;
    p[11] = 0xE0;
    return p;
}

static void *make_vtable(const VBCLASS *cl, int total, VbForm *form)
{
    void **vt = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (total + 8) * sizeof(void *));
    (void)form;
    vt[0] = obj_query;
    vt[1] = obj_addref;
    vt[2] = obj_release;
    vt[3] = obj_count;
    vt[4] = obj_info;
    vt[5] = obj_names;
    vt[6] = obj_invoke;
    for (int i = 7; i < total; i++) {
        int ret = 8;
        if (cl == &g_classes[VBC_FORM] && i >= ACCESSOR_BASE)
            ret = 4;
        else if (i < cl->nslots) {
            ret = cl->slots[i].ret;
            if (cl->slots[i].kind == 0 && ret == 0 && i > 16)
                ret = 8;
        }
        vt[i] = make_thunk(i, ret);
    }
    return vt;
}

/* ------------------------------------------------------------------ the windows procedures */

static VbCtl *ctl_of_window(HWND hwnd)
{
    VbCtl *c = (VbCtl *)(LONG_PTR)GetWindowLongW(hwnd, GWL_USERDATA);
    return c && c->magic == MAGIC ? c : nullptr;
}

static VbCtl *label_at(VbForm *f, int x, int y)
{
    for (int i = f->count - 1; i >= 0; i--) {
        VbCtl *c = f->ctl[i];
        if (c->cls == VBC_LABEL && NUM(c, "Visible") && x >= c->bounds.left && x < c->bounds.right && y >= c->bounds.top && y < c->bounds.bottom)
            return c;
    }
    return nullptr;
}

static void paint_labels(VbForm *f, HDC dc)
{
    for (int i = 0; i < f->count; i++) {
        VbCtl *c = f->ctl[i];
        if (c->cls != VBC_LABEL || c->parent || !NUM(c, "Visible"))
            continue;
        RECT r = c->bounds;
        HFONT old = SelectObject(dc, font_of(c));
        if (NUM(c, "BackStyle") != 0) {
            HBRUSH b = CreateSolidBrush(color_of(c, find_prop(c, "BackColor"), 0));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color_of(c, find_prop(c, "ForeColor"), 0));
        UINT flags = DT_NOPREFIX;
        if (NUM(c, "UseMnemonic") != 0)
            flags &= ~DT_NOPREFIX;
        if (NUM(c, "WordWrap"))
            flags |= DT_WORDBREAK;
        int align = (int)NUM(c, "Alignment");
        flags |= align == 1 ? DT_RIGHT : align == 2 ? DT_CENTER : DT_LEFT;
        if (NUM(c, "BorderStyle") == 1)
            DrawEdge(dc, &r, BDR_SUNKENOUTER, BF_RECT | BF_ADJUST);
        DrawTextW(dc, text_of(c, find_prop(c, "Caption")), -1, &r, flags);
        SelectObject(dc, old);
    }
}

int VbFormCount(void) { return g_form_count; }

static BOOL form_alive(VbForm *f)
{
    for (int i = 0; i < g_form_count; i++)
        if (g_forms[i] == f)
            return TRUE;
    return FALSE;
}

static void unload_form(VbForm *f, int mode)
{
    if (f->unloading)
        return;
    f->unloading = 1;
    SHORT cancel = 0;
    DWORD args[2] = {(DWORD)(ULONG_PTR)&cancel, (DWORD)mode};
    fire(f->self, "QueryUnload", args, 2);
    if (!cancel) {
        DWORD a[1] = {(DWORD)(ULONG_PTR)&cancel};
        fire(f->self, "Unload", a, 1);
    }
    if (cancel) {
        f->unloading = 0;
        return;
    }
    HWND h = f->self->hwnd;
    f->self->hwnd = nullptr;
    if (h)
        DestroyWindow(h);
    for (int i = 0; i < f->count; i++)
        f->ctl[i]->hwnd = nullptr;
    f->loaded = FALSE;
    f->shown = FALSE;
    f->unloading = 0;
    fire(f->self, "Terminate", nullptr, 0);
    for (int i = 0; i < g_form_count; i++)
        if (g_forms[i] == f) {
            g_forms[i] = g_forms[--g_form_count];
            break;
        }
    if (!g_form_count)
        PostQuitMessage(0);
}

static LRESULT CALLBACK form_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    VbCtl *self = ctl_of_window(hwnd);
    VbForm *f = self ? self->form : nullptr;
    if (!f || !form_alive(f))
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CLOSE:
        unload_form(f, 0);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(hwnd, &r);
        HBRUSH b = CreateSolidBrush(color_of(self, find_prop(self, "BackColor"), 0));
        FillRect((HDC)wp, &r, b);
        DeleteObject(b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_labels(f, dc);
        EndPaint(hwnd, &ps);
        FIRE(self, "Paint");
        return 0;
    }
    case WM_SIZE:
        if (f->loaded && wp != SIZE_MINIMIZED)
            FIRE(self, "Resize");
        return 0;
    case WM_ACTIVATE:
        if (f->loaded)
            FIRE(self, LOWORD(wp) ? "Activate" : "Deactivate");
        break;
    case WM_SETFOCUS:
        break;
    case WM_LBUTTONUP: {
        VbCtl *l = label_at(f, (short)LOWORD(lp), (short)HIWORD(lp));
        FIRE(l ? l : self, "Click");
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        VbCtl *l = label_at(f, (short)LOWORD(lp), (short)HIWORD(lp));
        FIRE(l ? l : self, "DblClick");
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
        VbCtl *c = ctl_of_window((HWND)lp);
        if (c && (c->cls == VBC_TEXTBOX || c->cls == VBC_FRAME || c->cls == VBC_CHECKBOX || c->cls == VBC_OPTION)) {
            HDC dc = (HDC)wp;
            SetTextColor(dc, color_of(c, find_prop(c, "ForeColor"), 0));
            COLORREF back = color_of(c, find_prop(c, "BackColor"), 0);
            SetBkColor(dc, back);
            static HBRUSH brush;
            if (brush)
                DeleteObject(brush);
            brush = CreateSolidBrush(back);
            return (LRESULT)brush;
        }
        break;
    }
    case WM_COMMAND: {
        HWND child = (HWND)lp;
        if (!child) {
            if (HIWORD(wp) == 0)
                menu_click(f, LOWORD(wp));
            return 0;
        }
        VbCtl *c = ctl_of_window(child);
        if (!c)
            break;
        switch (HIWORD(wp)) {
        case BN_CLICKED:
            read_back(c);
            FIRE(c, "Click");
            break;
        case EN_CHANGE:
            read_back(c);
            FIRE(c, "Change");
            break;
        case BN_SETFOCUS:
        case EN_SETFOCUS: FIRE(c, "GotFocus"); break;
        case BN_KILLFOCUS:
        case EN_KILLFOCUS: FIRE(c, "LostFocus"); break;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ making a form */

/* a frame draws its own border and caption, over the colour the control has. */
static void paint_frame(HWND hwnd, HDC dc, VbCtl *c)
{
    RECT r;
    GetClientRect(hwnd, &r);
    COLORREF back = color_of(c, find_prop(c, "BackColor"), 0);
    HBRUSH brush = CreateSolidBrush(back);
    FillRect(dc, &r, brush);
    HFONT old = SelectObject(dc, font_of(c));
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    RECT edge = r;
    edge.top += tm.tmHeight / 2;
    DrawEdge(dc, &edge, EDGE_ETCHED, BF_RECT);
    WCHAR text[200];
    int length = GetWindowTextW(hwnd, text, 200);
    if (length) {
        SIZE size;
        GetTextExtentPoint32W(dc, text, length, &size);
        RECT label = {6, 0, 6 + size.cx + 4, tm.tmHeight};
        FillRect(dc, &label, brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color_of(c, find_prop(c, "ForeColor"), 0));
        label.left += 2;
        DrawTextW(dc, text, length, &label, DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, old);
    DeleteObject(brush);
}

static LRESULT CALLBACK frame_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    VbCtl *c = ctl_of_window(hwnd);
    if (!c)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_frame(hwnd, dc, c);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paint_frame(hwnd, (HDC)wp, c);
        return 0;
    case WM_SETTEXT:
    case WM_SETFONT: {
        LRESULT result = DefWindowProcW(hwnd, msg, wp, lp);
        InvalidateRect(hwnd, nullptr, TRUE);
        return result;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, color_of(c, find_prop(c, "BackColor"), 0));
        static HBRUSH brush;
        static COLORREF made = (COLORREF)-1;
        COLORREF back = color_of(c, find_prop(c, "BackColor"), 0);
        if (!brush || made != back) {
            if (brush)
                DeleteObject(brush);
            brush = CreateSolidBrush(back);
            made = back;
        }
        return (LRESULT)brush;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void register_control_classes(HINSTANCE module)
{
    WNDCLASSW frame = {};
    frame.style = CS_HREDRAW | CS_VREDRAW;
    frame.lpfnWndProc = frame_proc;
    frame.hInstance = module;
    frame.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    frame.lpszClassName = L"ThunderRT6Frame";
    RegisterClassW(&frame);
    static const struct {
        const WCHAR *name;
        const WCHAR *base;
    } classes[] = {{L"ThunderRT6CommandButton", L"BUTTON"}, {L"ThunderRT6CheckBox", L"BUTTON"}, {L"ThunderRT6OptionButton", L"BUTTON"},
                   {L"ThunderRT6TextBox", L"EDIT"}};
    for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        WNDCLASSW wc;
        if (!GetClassInfoW(nullptr, classes[i].base, &wc))
            continue;
        wc.lpszClassName = classes[i].name;
        wc.hInstance = module;
        RegisterClassW(&wc);
    }
    WNDCLASSW form = {};
    form.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    form.lpfnWndProc = form_proc;
    form.hInstance = module;
    form.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    form.lpszClassName = L"ThunderRT6FormDC";
    RegisterClassW(&form);
    form.lpszClassName = L"ThunderRT6Form";
    RegisterClassW(&form);
}

static const BYTE *gui_blob(const VbObject *o, ULONG *size)
{
    const BYTE *table = (const BYTE *)(ULONG_PTR)g_header->gui_table;
    const BYTE *entry = table + o->gui * 0x50;
    *size = *(const DWORD *)(entry + 0x40);
    return (const BYTE *)*(const DWORD *)(entry + 0x48);
}

VbForm *VbCreateForm(const VbObject *o)
{
    VbForm *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    f->object = o;
    const BYTE *priv = (const BYTE *)*(const DWORD *)(o->info + 0xC);
    f->size = priv && readable(priv + 0x38, 4) ? *(const DWORD *)(priv + 0x38) : 0;
    ULONG need = 0;
    for (unsigned i = 0; i < o->count; i++) {
        ULONG off = *(const DWORD *)(o->controls + i * 0x28 + 4);
        if (off + 8 > need)
            need = off + 8;
    }
    if (f->size < need)
        f->size = need;
    f->instance = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, f->size + 16);
    f->blob = gui_blob(o, &f->blob_size);
    /* the controls, the form among them. */
    for (unsigned i = 0; i < o->count && f->count < MAX_CONTROLS; i++) {
        const BYTE *entry = o->controls + i * 0x28;
        int cls = class_of_guid(*(const BYTE *const *)(entry + 8));
        VbCtl *c = new_ctl(f, cls < 0 ? VBC_COMMAND : cls, entry, f->count);
        if (cls < 0) {
            char text[120];
            wsprintfA(text, "msvbvm60: the control %s is not one of Visual Basic and has no class here\n", c->name);
            VbOutputDebug(text);
            c->cls = -1;
        }
        f->ctl[f->count++] = c;
        if (cls == VBC_FORM)
            f->self = c;
        DWORD sink = *(const DWORD *)(entry + 0x18);
        *(DWORD *)(f->instance + *(const DWORD *)(entry + 4)) = sink;
    }
    if (!f->self)
        return nullptr;
    /* the object of the form, whose vtable is the interface of the form followed by the accessors of its controls. */
    f->vtable = make_vtable(&g_classes[VBC_FORM], MAX_SLOTS, f);
    *(void ***)f->instance = f->vtable;
    *(LONG *)(f->instance + 4) = 1;
    *(VbCtl **)(f->instance + 0x10) = f->self;
    f->self->vtable = f->vtable;
    f->self->hwnd = nullptr;
    for (int i = 0; i < f->count; i++) {
        VbCtl *c = f->ctl[i];
        if (c->cls < 0)
            continue;
        if (c != f->self)
            c->vtable = make_vtable(&g_classes[c->cls], g_classes[c->cls].nslots, f);
    }
    return f;
}

/*
 * the records of the stream follow each other in the order the controls were drawn, and the bytes in front of the size of a record
 * say where it belongs: 1 puts it inside the record before it, 3 beside it, and a 2 in front of the 3 closes the container.
 */
static void find_parents(VbForm *f)
{
    VbCtl *list[MAX_CONTROLS];
    int n = 0;
    for (int i = 0; i < f->count; i++) {
        VbCtl *c = f->ctl[i];
        c->parent = nullptr;
        c->record = nullptr;
        if (c->cls < 0)
            continue;
        const char *name = c == f->self ? f->object->name : c->name;
        const BYTE *p = find_record(f->blob, f->blob_size, c->control_index, name);
        if (!p)
            continue;
        c->record = p - lstrlenA(name) - 4;
        list[n++] = c;
    }
    for (int i = 1; i < n; i++) {
        VbCtl *c = list[i];
        int j = i - 1;
        while (j >= 0 && list[j]->record > c->record) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = c;
    }
    VbCtl *stack[MAX_CONTROLS];
    int depth = 0;
    VbCtl *previous = nullptr;
    f->ordered = 0;
    f->menu_count = 0;
    for (int i = 0; i < n; i++) {
        VbCtl *c = list[i];
        if (c->cls == VBC_MENU || c == f->self) {
            if (c == f->self)
                previous = c;
            else
                f->menus[f->menu_count++] = c;
            continue;
        }
        const BYTE *rec = c->record;
        BYTE last = rec - 5 >= f->blob ? rec[-5] : 0, before = rec - 6 >= f->blob ? rec[-6] : 0;
        if (last == 1 && previous && depth < MAX_CONTROLS - 1)
            stack[depth++] = previous;
        else if (last == 3 && before == 2 && depth > 0)
            depth--;
        c->parent = depth > 0 && stack[depth - 1] != f->self ? stack[depth - 1] : nullptr;
        previous = c;
        f->order[f->ordered++] = c;
    }
    /* a control the stream has no record for is made last, with none of its properties. */
    for (int i = 0; i < f->count; i++) {
        VbCtl *c = f->ctl[i];
        BOOL listed = FALSE;
        for (int k = 0; k < f->ordered; k++)
            listed |= f->order[k] == c;
        if (!listed && c != f->self && c->cls >= 0 && c->cls != VBC_MENU && f->ordered < MAX_CONTROLS)
            f->order[f->ordered++] = c;
    }
}

static void load_form_properties(VbForm *f)
{
    for (int i = 0; i < f->count; i++) {
        VbCtl *c = f->ctl[i];
        if (c->cls < 0)
            continue;
        const BYTE *p = find_record(f->blob, f->blob_size, c->control_index, c == f->self ? f->object->name : c->name);
        if (!p)
            continue;
        p++;
        load_properties(c, p, f->blob + f->blob_size);
    }
    find_parents(f);
}

static void create_windows(VbForm *f)
{
    VbCtl *self = f->self;
    int style_caption = (int)NUM(self, "BorderStyle");
    DWORD style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    DWORD ex = 0;
    if (style_caption != 0) {
        style |= WS_CAPTION;
        if (NUM(self, "ControlBox") != 0)
            style |= WS_SYSMENU;
        if (style_caption == 2 || style_caption == 5)
            style |= WS_THICKFRAME;
        if (NUM(self, "MinButton") != 0)
            style |= WS_MINIMIZEBOX;
        if (NUM(self, "MaxButton") != 0 && style_caption == 2)
            style |= WS_MAXIMIZEBOX;
    } else {
        style |= WS_POPUP;
    }
    if (style_caption == 4 || style_caption == 5)
        ex |= WS_EX_TOOLWINDOW;
    self->hwnd = CreateWindowExW(ex, L"ThunderRT6FormDC", text_of(self, find_prop(self, "Caption")), style, 0, 0, 100, 100, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    if (!self->hwnd)
        return;
    SetWindowLongW(self->hwnd, GWL_USERDATA, (LONG)(LONG_PTR)self);
    for (int i = 0; i < f->ordered; i++) {
        VbCtl *c = f->order[i];
        if (c == self || c->cls < 0)
            continue;
        create_child_window(c);
        apply_all(c);
    }
    build_menus(f);
    layout_form(f);
}

/* the form is made with its first event when a program says New, and loaded when something asks for it. */
static VbForm *new_form(const VbObject *o)
{
    VbForm *f = VbCreateForm(o);
    if (f)
        FIRE(f->self, "Initialize");
    return f;
}

static void ensure_loaded(VbForm *f)
{
    if (f->loaded || f->loading || f->unloading)
        return;
    f->loading = TRUE;
    if (g_form_count < MAX_FORMS)
        g_forms[g_form_count++] = f;
    load_form_properties(f);
    create_windows(f);
    f->loaded = TRUE;
    FIRE(f->self, "Load");
    f->loading = FALSE;
}

VbForm *VbLoadForm(const VbObject *o)
{
    VbForm *f = new_form(o);
    if (!f)
        return nullptr;
    ensure_loaded(f);
    return f;
}

/* the object of a New on the description of a class of the project, which is a form here. */
void *VbNewProjectObject(const void *info)
{
    for (int i = 0; i < g_object_count; i++) {
        const VbObject *o = &g_objects[i];
        if (o->info != info)
            continue;
        if (!(o->type & 0x80))
            return nullptr;
        VbForm *f = new_form(o);
        if (!f)
            return nullptr;
        InterlockedIncrement(&f->self->refs);
        return f->instance;
    }
    return nullptr;
}

void VbShowForm(VbForm *f)
{
    if (!f || !f->self->hwnd)
        return;
    f->shown = TRUE;
    ShowWindow(f->self->hwnd, SW_SHOWNORMAL);
    UpdateWindow(f->self->hwnd);
    FIRE(f->self, "Activate");
}

/* the form a program starts with: the first of its forms, when it has no Sub Main. */
BOOL VbStartForms(HINSTANCE module)
{
    register_control_classes(module);
    for (int i = 0; i < g_object_count; i++) {
        if (!(g_objects[i].type & 0x80))
            continue;
        VbForm *f = new_form(&g_objects[i]);
        if (!f)
            return FALSE;
        /* the form is the default instance of its name, which the compiled code keeps in the data of the project. */
        const BYTE *data = (const BYTE *)*(const DWORD *)(g_objects[i].info + 0x1C);
        if (data && readable(data + 8, 4))
            *(void **)(data + 8) = f->instance;
        ensure_loaded(f);
        if (!f->self->hwnd)
            return FALSE;
        VbShowForm(f);
        return TRUE;
    }
    return FALSE;
}

BOOL VbHasForms(void)
{
    return g_form_count > 0;
}

/* ------------------------------------------------------------------ the objects of the library: Global, App, Screen */

static VbForm g_lib_form;
static VbCtl g_lib_self;
static VbCtl *g_app, *g_screen, *g_clipboard;

/* a string of the version resource of the program, found by its name. */
static void version_string(const BYTE *blob, DWORD size, const WCHAR *key, WCHAR *out, int count)
{
    int length = lstrlenW(key);
    out[0] = 0;
    for (DWORD i = 6; i + (DWORD)(length + 1) * 2 <= size; i += 2) {
        const WCHAR *p = (const WCHAR *)(blob + i);
        if (p[length] || memcmp(p, key, length * 2))
            continue;
        const BYTE *start = blob + i - 6;
        DWORD value_chars = *(const WORD *)(start + 2);
        DWORD offset = (i + (length + 1) * 2 + 3) & ~3u;
        if (offset + value_chars * 2 <= size && value_chars) {
            lstrcpynW(out, (const WCHAR *)(blob + offset), count);
            return;
        }
    }
}

static void fill_app(VbCtl *c)
{
    WCHAR path[MAX_PATH], text[256];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    WCHAR *slash = path + lstrlenW(path);
    while (slash > path && slash[-1] != L'\\')
        slash--;
    WCHAR name[MAX_PATH];
    lstrcpyW(name, slash);
    WCHAR *dot = name + lstrlenW(name);
    while (dot > name && *dot != L'.')
        dot--;
    if (dot > name)
        *dot = 0;
    if (slash > path + 3)
        slash[-1] = 0;
    else
        *slash = 0;
    set_text(c, find_prop(c, "Path"), path);
    set_text(c, find_prop(c, "EXEName"), name);
    WCHAR title[64];
    VbProjectTitle(title, 64);
    set_text(c, find_prop(c, "Title"), title);
    set_number(c, find_prop(c, "PrevInstance"), 0);
    set_number(c, find_prop(c, "StartMode"), 0);
    set_number(c, find_prop(c, "TaskVisible"), -1);
    set_number(c, find_prop(c, "hInstance"), (double)(LONG)(LONG_PTR)GetModuleHandleW(nullptr));
    set_number(c, find_prop(c, "ThreadID"), (double)GetCurrentThreadId());
    set_number(c, find_prop(c, "NonModalAllowed"), -1);
    set_number(c, find_prop(c, "RetainedProject"), 0);
    set_number(c, find_prop(c, "UnattendedApp"), 0);
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
    if (!r)
        return;
    const BYTE *blob = LockResource(LoadResource(nullptr, r));
    DWORD size = SizeofResource(nullptr, r);
    if (!blob || size < 0x5C)
        return;
    const DWORD *fixed = (const DWORD *)(blob + 0x28);
    if (fixed[0] == 0xFEEF04BD) {
        set_number(c, find_prop(c, "Major"), fixed[4] >> 16);
        set_number(c, find_prop(c, "Minor"), fixed[4] & 0xFFFF);
        set_number(c, find_prop(c, "Revision"), fixed[5] & 0xFFFF);
    }
    static const char *const names[] = {"Comments", "CompanyName", "FileDescription", "LegalCopyright", "LegalTrademarks", "ProductName"};
    for (int i = 0; i < 6; i++) {
        WCHAR key[32];
        MultiByteToWideChar(CP_ACP, 0, names[i], -1, key, 32);
        version_string(blob, size, key, text, 256);
        set_text(c, find_prop(c, names[i]), text);
    }
}

static void fill_screen(VbCtl *c)
{
    int tx = 1440 / g_dpi_x, ty = 1440 / g_dpi_y;
    set_number(c, find_prop(c, "TwipsPerPixelX"), tx);
    set_number(c, find_prop(c, "TwipsPerPixelY"), ty);
    set_number(c, find_prop(c, "Width"), (double)GetSystemMetrics(SM_CXSCREEN) * tx);
    set_number(c, find_prop(c, "Height"), (double)GetSystemMetrics(SM_CYSCREEN) * ty);
    set_number(c, find_prop(c, "MousePointer"), 0);
}

static VbCtl *library_object(int cls, VbCtl **slot)
{
    if (*slot)
        return *slot;
    if (!g_lib_form.self) {
        g_lib_self.magic = MAGIC;
        g_lib_self.refs = 1;
        g_lib_self.self = &g_lib_self;
        g_lib_self.cls = -1;
        g_lib_form.self = &g_lib_self;
        g_lib_form.loaded = TRUE;
    }
    VbCtl *c = new_ctl(&g_lib_form, cls, nullptr, 0);
    c->vtable = make_vtable(&g_classes[cls], g_classes[cls].nslots, &g_lib_form);
    if (cls == VBC_APP)
        fill_app(c);
    else if (cls == VBC_SCREEN)
        fill_screen(c);
    *slot = c;
    return c;
}

typedef struct VbGlobal {
    void **vtable;
    LONG refs;
} VbGlobal;

static LONG __stdcall global_query(void *self, const IID *iid, void **out)
{
    /* the interface of the library has a number of its own, which a caller that made the object by its class asks for. */
    if (IsEqualGUID(iid, &IID_IUnknown) || ((const DWORD *)iid)[0] == 0xFCFB3D22u) {
        *out = self;
        InterlockedIncrement(&((VbGlobal *)self)->refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG __stdcall global_addref(void *self) { return (ULONG)InterlockedIncrement(&((VbGlobal *)self)->refs); }
static ULONG __stdcall global_release(void *self) { return (ULONG)InterlockedDecrement(&((VbGlobal *)self)->refs); }

/* Load and Unload take the form they are told to. */
static LONG __stdcall global_load(void *self, void *form)
{
    VbCtl *c = form ? ctl_of(form) : nullptr;
    if (!c)
        return E_POINTER;
    ensure_loaded(c->form);
    return S_OK;
}

static LONG __stdcall global_unload(void *self, void *form)
{
    VbCtl *c = form ? ctl_of(form) : nullptr;
    if (!c)
        return E_POINTER;
    if (c->form->loaded)
        unload_form(c->form, 0);
    return S_OK;
}

static LONG library_result(int cls, VbCtl **slot, void **out)
{
    VbCtl *c = library_object(cls, slot);
    InterlockedIncrement(&c->refs);
    *out = c;
    return S_OK;
}

static LONG __stdcall global_app(void *self, void **out) { return library_result(VBC_APP, &g_app, out); }
static LONG __stdcall global_screen(void *self, void **out) { return library_result(VBC_SCREEN, &g_screen, out); }
static LONG __stdcall global_clipboard(void *self, void **out) { return library_result(VBC_CLIPBOARD, &g_clipboard, out); }
static LONG global_unsupported(int slot)
{
    char text[100];
    wsprintfA(text, "msvbvm60: slot %d of the global object of the library is not implemented\n", slot);
    VbOutputDebug(text);
    return E_NOTIMPL;
}

#define UNSUPPORTED(N) static LONG __stdcall global_unsupported_##N(void *self) { return global_unsupported(N); }
UNSUPPORTED(8) UNSUPPORTED(9) UNSUPPORTED(10) UNSUPPORTED(11) UNSUPPORTED(12) UNSUPPORTED(13) UNSUPPORTED(14) UNSUPPORTED(15)
UNSUPPORTED(16) UNSUPPORTED(17) UNSUPPORTED(18) UNSUPPORTED(19) UNSUPPORTED(20) UNSUPPORTED(21) UNSUPPORTED(22) UNSUPPORTED(23)

const GUID CLSID_VbGlobal = {0xFCFB3D23, 0xA0FA, 0x1068, {0xA7, 0x38, 0x08, 0x00, 0x2B, 0x33, 0x71, 0xB5}};

void *VbNewGlobal(void)
{
    static VbGlobal global;
    static void *vtable[32];
    if (!global.vtable) {
        vtable[0] = global_query;
        vtable[1] = global_addref;
        vtable[2] = global_release;
        vtable[3] = global_load;
        vtable[4] = global_unload;
        vtable[5] = global_app;
        vtable[6] = global_screen;
        vtable[7] = global_clipboard;
        static void *const unsupported[] = {global_unsupported_8,  global_unsupported_9,  global_unsupported_10, global_unsupported_11,
                                            global_unsupported_12, global_unsupported_13, global_unsupported_14, global_unsupported_15,
                                            global_unsupported_16, global_unsupported_17, global_unsupported_18, global_unsupported_19,
                                            global_unsupported_20, global_unsupported_21, global_unsupported_22, global_unsupported_23};
        for (int i = 8; i < 32; i++)
            vtable[i] = i < 24 ? unsupported[i - 8] : (void *)global_unsupported_23;
        global.vtable = vtable;
        global.refs = 1;
    }
    return &global;
}
