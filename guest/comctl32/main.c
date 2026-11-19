/*
 * comctl32 entry point and control class registration.
 *
 * windows registers a class when an activation context names it, through RegisterClassNameW, or in
 * InitCommonControlsEx. the activation context mime generates names the classes user32 has too, so the other ones
 * are registered when the dll loads.
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

/* same order and icc bits as windows' table. */
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
    {ANIMATE_CLASSW, AnimateProc, CS_GLOBALCLASS | CS_DBLCLKS, COLOR_BTNFACE + 1, ICC_ANIMATE_CLASS},
    {PROGRESS_CLASSW, ProgressProc, CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW, COLOR_BTNFACE + 1, ICC_PROGRESS_CLASS},
    {REBARCLASSNAMEW, RebarProc, CS_GLOBALCLASS | CS_DBLCLKS, 0, ICC_COOL_CLASSES},
    {L"SysLink", LinkProc, CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW, 0, ICC_LINK_CLASS},
};

/* the classes that take the place of the ones of user32, and so cannot be registered before a program asks. */
static const WCHAR *const g_user_classes[] = {L"Static", L"Button", L"Edit", L"ListBox", L"ComboBox"};

static BOOL register_button(void)
{
    WNDCLASSW wc = {};
    wc.style = CS_GLOBALCLASS | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW | CS_PARENTDC;
    wc.lpfnWndProc = ButtonProc;
    wc.cbWndExtra = sizeof(void *);
    wc.hInstance = Comctl32Instance;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"Button";
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

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

/* ordinal 100: registers one class by name, as user32 does when an activation context redirects it. */
WINCOMMCTRLAPI BOOL WINAPI RegisterClassNameW(LPCWSTR name)
{
    if (!name)
        return FALSE;
    if (!_wcsicmp(name, L"Static"))
        return RegisterStaticClass();
    if (!_wcsicmp(name, L"Button"))
        return register_button();
    for (SIZE_T i = 2; i < RTL_NUMBER_OF(g_user_classes); i++)
        if (!_wcsicmp(name, g_user_classes[i]))
            return RegisterFieldClass(g_user_classes[i]);
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
    } else if (reason == DLL_PROCESS_DETACH && !reserved) {
        /* a program that frees the dll must not keep classes whose procedures went with it. */
        for (SIZE_T i = 0; i < sizeof(g_classes) / sizeof(g_classes[0]); i++)
            UnregisterClassW(g_classes[i].Name, Comctl32Instance);
        for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_user_classes); i++)
            UnregisterClassW(g_user_classes[i], Comctl32Instance);
        SubclassProcessFree();
    }
    return TRUE;
}
