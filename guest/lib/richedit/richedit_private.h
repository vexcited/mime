/*
 * the rich edit engine shared by riched20.dll and msftedit.dll: a story
 * of text with a character and a paragraph format per character, its
 * layout into lines, painting, editing and the window procedure.
 */
#ifndef MIME_RICHEDIT_PRIVATE_H
#define MIME_RICHEDIT_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/richedit.h>

WINUSERAPI BOOL WINAPI IsWindowUnicode(HWND hwnd);

/* paragraph marks: a carriage return ends a paragraph, a vertical tab a line in it. */
#define RE_CR 0x000D
#define RE_VT 0x000B
#define RE_FF 0x000C

#define RE_UNDO_DEFAULT 100
#define RE_SCROLL_TIMER 1
#define RE_LIMIT_DEFAULT 32767

/* a character format as the story keeps it, every field valid. */
typedef struct RE_FMT {
    CHARFORMAT2W cf;
    BYTE autolink;
} RE_FMT;

/* the font of a format at the current zoom, created when first measured. */
typedef struct RE_FONT {
    HFONT font;
    int ascent, descent, ave;
    int offset;
} RE_FONT;

enum { LINE_PARA_START = 1, LINE_PARA_END = 2 };

typedef struct RE_LINE {
    UINT cp, len;
    int y, height, ascent;
    int x, width;
    UINT flags;
    int number, bullet_x;
} RE_LINE;

/* one undoable replacement: what [cp, cp + old_len) held before it became [cp, cp + new_len). */
typedef struct RE_UNDO {
    UINT cp;
    UINT old_len, new_len;
    WCHAR *old_text, *new_text;
    WORD *old_cf, *new_cf, *old_pf, *new_pf;
    UINT sel_anchor, sel_active;
    int kind;
    BOOL group;
} RE_UNDO;

enum { UNDO_NONE, UNDO_TYPING, UNDO_DELETE, UNDO_DRAGDROP, UNDO_CUT, UNDO_PASTE, UNDO_FORMAT = 0x100 };

typedef struct RE {
    HWND hwnd, parent;
    UINT id;
    BOOL ansi, v1, v41;
    DWORD style, exstyle;

    /* the story, which always ends with a paragraph mark. */
    WCHAR *text;
    UINT len, cap;
    WORD *cfi, *pfi;
    RE_FMT *fmts;
    RE_FONT *fonts;
    UINT fmt_count, fmt_cap;
    PARAFORMAT2 *paras;
    UINT para_count, para_cap;
    WORD def_cf, def_pf;
    WORD ins_cf;
    BOOL ins_valid;
    BOOL plain;

    UINT anchor, active;
    BOOL at_line_end;
    int caret_x, caret_h;
    BOOL caret_x_valid;

    UINT limit;
    DWORD mask;
    BOOL multiline, readonly, wrap, nohidesel, autourl, autoword, wantreturn, selbar, disabled;
    BOOL modified, focus, in_dialog, redraw;
    WCHAR password;
    DWORD edit_style, lang_options, typography;
    COLORREF bk;
    BOOL bk_sys;
    EDITWORDBREAKPROCW wordbreak;
    void *ole, *ole_callback;

    /* layout. */
    RE_LINE *lines;
    UINT line_count, line_cap;
    int *xpos;
    UINT xpos_cap;
    int content_height, content_width;
    BOOL dirty;
    RECT view;
    BOOL rect_set;
    RECT fmt_rect;
    int inset_l, inset_t, inset_r, inset_b;
    int margin_l, margin_r;
    int yscroll, xscroll;
    int zoom_num, zoom_den;
    int dpi;
    LONG target;
    BOOL vbar, hbar;
    BOOL laying_out;

    /* undo. */
    RE_UNDO *undo, *redo;
    UINT undo_count, redo_count, undo_cap, redo_cap;
    UINT undo_limit;
    BOOL typing_group;

    /* notifications deferred to the end of the outermost message. */
    int depth;
    BOOL pend_change, pend_maxtext, pend_errspace;
    BOOL in_change, in_selchange;
    LONG sent_min, sent_max;
    int last_req_w, last_req_h;

    /* the mouse. */
    BOOL tracking;
    int select_mode;
    UINT word_min, word_max;
    POINT last_mouse;
    int wheel;
    DWORD last_click;
    POINT last_click_pt;
} RE;

/* story.c */
RE *ReCreate(HWND hwnd, const CREATESTRUCTW *cs, BOOL ansi, BOOL v1, BOOL v41);
void ReDestroy(RE *e);
WORD ReAddFormat(RE *e, const RE_FMT *f);
WORD ReAddPara(RE *e, const PARAFORMAT2 *p);
void ReDefaultFormat(RE *e, HFONT font, RE_FMT *out);
UINT ReTextLength(RE *e);
WORD ReFormatAt(RE *e, UINT cp);
WORD ReInsertFormat(RE *e);
UINT ReParaStart(RE *e, UINT cp);
UINT ReParaEnd(RE *e, UINT cp);
BOOL ReReplace(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, const WORD *cf, const WORD *pf, int undo_kind);
BOOL ReReplaceText(RE *e, UINT cp, UINT count, const WCHAR *s, UINT n, WORD cf, int undo_kind);
void ReSetFormatRange(RE *e, UINT from, UINT to, const CHARFORMAT2W *cf, DWORD mask, BOOL undo);
void ReSetParaRange(RE *e, UINT from, UINT to, const PARAFORMAT2 *pf, DWORD mask, BOOL undo);
void ReApplyCharFormat(CHARFORMAT2W *dst, const CHARFORMAT2W *src, DWORD mask);
void ReApplyParaFormat(PARAFORMAT2 *dst, const PARAFORMAT2 *src, DWORD mask);
DWORD ReRangeCharFormat(RE *e, UINT from, UINT to, CHARFORMAT2W *out);
DWORD ReRangeParaFormat(RE *e, UINT from, UINT to, PARAFORMAT2 *out);
void ReClearUndo(RE *e);
BOOL ReUndo(RE *e, BOOL redo);
void ReStopGrouping(RE *e);
void ReScanUrls(RE *e);
BOOL ReIsWordChar(WCHAR c);
UINT ReWordBreak(RE *e, UINT cp, int code);
void *ReAlloc(SIZE_T size);
void *ReRealloc(void *p, SIZE_T size);
void ReFree(void *p);

/* layout.c */
void ReLayout(RE *e);
void ReInvalidateLayout(RE *e);
void ReUpdateView(RE *e);
void ReFlushFonts(RE *e);
RE_FONT *ReFont(RE *e, WORD fmt);
int ReTwipsToPixels(RE *e, LONG twips);
int ReTwipsX(RE *e, LONG twips);
UINT ReLineOf(RE *e, UINT cp, BOOL at_end);
POINT RePosFromCp(RE *e, UINT cp, BOOL at_end);
UINT ReCpFromPoint(RE *e, int x, int y, BOOL *at_end);
void ReUpdateScrollBars(RE *e);
void ReScrollTo(RE *e, int x, int y);
void ReScrollCaret(RE *e);
LRESULT ReVScroll(RE *e, int code, int pos);
void ReHScroll(RE *e, int code, int pos);
void ReRequestResize(RE *e, BOOL force);
void ReComputeView(RE *e);

/* paint.c */
void RePaint(RE *e, HDC dc, const RECT *update);
void RePlaceCaret(RE *e);
void ReCreateCaret(RE *e);
void ReInvalidate(RE *e);

/* control.c */
void ReNotifyCommand(RE *e, UINT code);
LRESULT ReNotify(RE *e, UINT code, NMHDR *hdr);
void ReSetSelection(RE *e, UINT anchor, UINT active);
void ReSelChanged(RE *e);
void ReChanged(RE *e);
void ReBeep(RE *e);
UINT ReSelMin(RE *e);
UINT ReSelMax(RE *e);
LRESULT ReDispatch(RE *e, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ReWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi, BOOL v1, BOOL v41);
BOOL ReSendLink(RE *e, UINT msg, WPARAM wparam, LPARAM lparam, UINT cp);

/* input.c */
void ReKeyDown(RE *e, UINT vk);
void ReChar(RE *e, WCHAR c);
void ReMouseDown(RE *e, int x, int y, UINT keys, BOOL dbl);
void ReMouseMove(RE *e, int x, int y, UINT keys);
void ReMouseUp(RE *e, int x, int y);
void ReMouseTimer(RE *e);
LRESULT ReWheel(RE *e, WPARAM wparam);
BOOL ReCopy(RE *e);
BOOL ReCut(RE *e);
BOOL RePaste(RE *e, UINT format);
BOOL ReCanPaste(RE *e, UINT format);
BOOL ReInsertUser(RE *e, const WCHAR *s, UINT n, int kind);
BOOL ReDeleteSelection(RE *e, int kind);
WCHAR *ReRangeText(RE *e, UINT from, UINT to, BOOL crlf, UINT *count);
BOOL ReSetCursor(RE *e);

/* exports.c */
BOOL ReRegisterClasses(HINSTANCE module, const WCHAR *wide, WNDPROC wide_proc, const char *ansi, WNDPROC ansi_proc);

/* rtfread.c and rtfwrite.c */
LONG ReStreamIn(RE *e, UINT flags, EDITSTREAM *es);
LONG ReStreamOut(RE *e, UINT flags, EDITSTREAM *es);
void ReBulletText(RE *e, const PARAFORMAT2 *pf, int number, WCHAR *out, UINT cap);
LONG ReReadRtf(RE *e, const char *data, UINT size, UINT cp, UINT count, LONG *error);
char *ReWriteRtf(RE *e, UINT from, UINT to, UINT *size);

#endif
