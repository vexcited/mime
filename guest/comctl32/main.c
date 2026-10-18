/*
 * comctl32 entry point and the registration of the control classes.
 *
 * windows registers a class when an activation context names it, through
 * RegisterClassNameW, or in InitCommonControlsEx. mime has no class
 * redirection, so every class this dll implements is registered when it
 * loads.
 */
#include "comctl32_private.h"

HINSTANCE Comctl32Instance;

typedef struct CLASS_ENTRY {
    LPCWSTR Name;
    WNDPROC Proc;
    UINT Style;
    INT_PTR Background;
    DWORD Icc;
} CLASS_ENTRY;

/* the order and icc bits of windows' table. */
static const CLASS_ENTRY g_classes[] = {
    {TOOLBARCLASSNAMEW, ToolbarProc, CS_GLOBALCLASS | CS_DBLCLKS, COLOR_BTNFACE + 1, ICC_BAR_CLASSES},
    {TOOLTIPS_CLASSW, TooltipsProc, CS_DROPSHADOW | CS_GLOBALCLASS | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, 0,
     ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES},
    {STATUSCLASSNAMEW, StatusProc, CS_GLOBALCLASS | CS_DBLCLKS | CS_VREDRAW, COLOR_BTNFACE + 1, ICC_BAR_CLASSES},
    {WC_LISTVIEWW, ListViewProc, CS_GLOBALCLASS | CS_DBLCLKS, COLOR_WINDOW + 1, ICC_LISTVIEW_CLASSES},
    {WC_HEADERW, HeaderProc, CS_GLOBALCLASS | CS_DBLCLKS, COLOR_BTNFACE + 1, ICC_LISTVIEW_CLASSES},
    {WC_TABCONTROLW, TabProc, CS_GLOBALCLASS | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE + 1,
     ICC_TAB_CLASSES},
    {WC_TREEVIEWW, TreeViewProc, CS_GLOBALCLASS | CS_DBLCLKS, 0, ICC_TREEVIEW_CLASSES},
    {TRACKBAR_CLASSW, TrackbarProc, CS_GLOBALCLASS, COLOR_BTNFACE + 1, ICC_BAR_CLASSES},
    {UPDOWN_CLASSW, UpDownProc, CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE + 1, ICC_UPDOWN_CLASS},
    {PROGRESS_CLASSW, ProgressProc, CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE + 1, ICC_PROGRESS_CLASS},
};

static BOOL register_class(const CLASS_ENTRY *c)
{
    WNDCLASSW wc = {};
    wc.style = c->Style;
    wc.lpfnWndProc = c->Proc;
    wc.cbWndExtra = sizeof(void *);
    wc.hInstance = Comctl32Instance;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)c->Background;
    wc.lpszClassName = c->Name;
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void RegisterControlClasses(DWORD icc)
{
    for (SIZE_T i = 0; i < sizeof(g_classes) / sizeof(g_classes[0]); i++)
        if (g_classes[i].Icc & icc)
            register_class(&g_classes[i]);
}

/* ordinal 100: registers one class by name, as user32 does for a class an activation context redirects. */
WINCOMMCTRLAPI BOOL WINAPI RegisterClassNameW(LPCWSTR name)
{
    if (!name)
        return FALSE;
    for (SIZE_T i = 0; i < sizeof(g_classes) / sizeof(g_classes[0]); i++)
        if (!_wcsicmp(name, g_classes[i].Name)) {
            register_class(&g_classes[i]);
            return TRUE;
        }
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        Comctl32Instance = instance;
        SubclassProcessAttach();
        RegisterControlClasses(0xFFFFFFFF);
    }
    return TRUE;
}
