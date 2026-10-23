/* list box and combo box state, shared so a combo box can drive its own list box directly. */
#ifndef USER32_LISTBOX_H
#define USER32_LISTBOX_H

#include "user32_private.h"

/* private messages between a combo box and its list, and the case converting add variants. */
#define LBCB_CARETON 0x01A3
#define LBCB_CARETOFF 0x01A4
#define LB_INSERTSTRINGUPPER 0x01AA
#define LB_INSERTSTRINGLOWER 0x01AB
#define LB_ADDSTRINGUPPER 0x01AC
#define LB_ADDSTRINGLOWER 0x01AD
#define LBCB_STARTTRACK 0x01AE
#define LBCB_ENDTRACK 0x01AF
#define ES_COMBOBOX 0x0200L
#ifndef INT_MAX
#define INT_MAX 0x7FFFFFFF
#endif

typedef struct COMBO COMBO;

enum { LB_SINGLE, LB_MULTIPLE, LB_EXTENDED };

typedef struct LISTBOX {
    HWND hwnd, parent;
    COMBO *combo;              /* the owning combo box, for LBS_COMBOBOX lists. */
    int top, sel, caret, anchor, last_move, last_selection;
    int count, capacity;
    LPWSTR *text;              /* only with strings. */
    ULONG_PTR *data;           /* without LBS_NODATA. */
    BYTE *state;               /* multiple selection: selected in the low nibble, shown in the high one. */
    BYTE *heights;             /* LBS_OWNERDRAWVARIABLE. */
    int cx_char, cy_char;
    int cx_column, per_column, columns;
    int full_max;              /* items fully visible, one page. */
    int x_origin, extent;
    int *tabs;
    int tab_count;
    HFONT font;
    LCID locale;
    WCHAR typed[256];
    int typed_count;
    POINT prev;
    int owner_draw;            /* 0, 1 fixed or 2 variable. */
    int mode;
    BOOL redraw, deferred, notify, sort, use_tabs, want_keys, no_integral, multicolumn, disable_noscroll, nosel;
    BOOL has_strings, has_data, vbar, hbar, right, rtl;
    BOOL caret_enabled, caret_on, mouse_down, captured, double_click, new_state, add_mode, in_insert;
    int last_vbar[3], last_hbar[3]; /* max, page and position last given to the bars. */
} LISTBOX;

struct COMBO {
    HWND hwnd, parent, edit, list;
    LISTBOX *lb;
    BOOL editable, droppable, has_edit;
    RECT edit_rect, button_rect;
    int cx_combo, cy_combo, cx_drop, cy_drop;
    BOOL focus, no_redraw, mouse_down, button_pressed, list_visible, keyboard_sel, extended_ui;
    BOOL upper, lower, right, rtl, destroyed, owner_draw;
    HFONT font;
    DWORD list_scroll;         /* WS_VSCROLL and WS_HSCROLL the list gets. */
};

/* listbox.c, for the combo box. */
LISTBOX *ListBoxOf(HWND hwnd);
int ListBoxPrefixSearch(LISTBOX *lb, int start, LPCWSTR text);
int ListBoxExactSearch(LISTBOX *lb, int start, LPCWSTR text);
int ListBoxItemHeight(LISTBOX *lb, int index);
LRESULT ListBoxDir(LISTBOX *lb, UINT attributes, LPCWSTR spec);

/* combo.c, for the list box. */
void ComboShowList(COMBO *cb, BOOL track);
BOOL ComboHideList(COMBO *cb, BOOL notify, BOOL accept);
void ComboRefreshClosed(COMBO *cb);

#endif
