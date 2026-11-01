/*
 * the forms and controls of vbform.c as the container of other libraries' controls in vbocx.c sees them.
 */
#ifndef VBCTL_H
#define VBCTL_H

#include "vb.h"
#include "vbclass.h"

#define MAX_CONTROLS 160
#define MAX_PROPS 100
#define MAX_FORMS 64
#define MAX_SLOTS 640
#define FORM_METHODS 446
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
    /* a control of another library: its properties from the stream, the data of the control itself and the container that holds it. */
    char progid[80];
    const BYTE *blob;
    ULONG blob_size;
    struct VbOcx *ocx;
    int stream_align;
};

typedef struct VbObject {
    const BYTE *info;
    const BYTE *descriptor;
    const char *name;
    DWORD type;
    unsigned count;
    const BYTE *controls;
    int gui;
    /* a class module: its instance size, the entries of its vtable after the ones the runtime gives, and the vtable. */
    BOOL is_class;
    ULONG instance_size;
    const BYTE *thunks;
    unsigned thunk_count;
    void **vtable;
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
    /* the window an MDI form keeps its child forms in. */
    HWND mdi_client;
};

#define MAGIC 0x5642434C

/* the class number of a control of another library, which has the properties of the container of objects. */
#define VBC_OCX 38
#define VBC_MDIFORM 20
static inline BOOL is_list(const VbCtl *c) { return c->cls == VBC_LISTBOX || c->cls == VBC_COMBOBOX; }
static inline BOOL is_scroll(const VbCtl *c) { return c->cls == VBC_HSCROLL || c->cls == VBC_VSCROLL; }
static inline BOOL is_form(const VbCtl *c) { return c->cls == VBC_FORM || c->cls == VBC_MDIFORM; }

extern const VbHeader *g_vb_header;

/* helpers of vbform.c the container uses. */
int VbPxX(double twips);
int VbPxY(double twips);
double VbCtlNumber(const VbCtl *c, const char *name);
void VbCtlSetNumber(VbCtl *c, const char *name, double value);
HWND VbCtlParentWindow(VbCtl *c);
void VbCtlPlace(VbCtl *c);

/* vbocx.c: the container. */

void VbOcxCreate(VbCtl *c);
void VbOcxDestroy(VbCtl *c);
void VbOcxMoved(VbCtl *c);
void VbOcxShow(VbCtl *c, BOOL visible);
int VbOcxAlign(VbCtl *c);
void VbFormDock(VbForm *f);
void **VbOcxVtable(void);
LONG VbOcxSlot(VbCtl *c, int slot, DWORD *args);
void *VbMakeThunk(int slot, int ret);

#endif
