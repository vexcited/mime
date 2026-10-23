/*
 * oleacc.dll: role and state texts and the proxies' strings, worded in English as windows' oleacc resource file has
 * them.
 */
#define OLEACCAPI DECLSPEC_EXPORT
#include <mime/win32/oleacc.h>
#include "oleacc_private.h"

static const WCHAR *const roles[] = {
    L"unknown object",
    L"title bar",
    L"menu bar",
    L"scroll bar",
    L"grip",
    L"sound",
    L"cursor",
    L"caret",
    L"alert",
    L"window",
    L"client",
    L"popup menu",
    L"menu item",
    L"tool tip",
    L"application",
    L"document",
    L"pane",
    L"chart",
    L"dialog",
    L"border",
    L"grouping",
    L"separator",
    L"tool bar",
    L"status bar",
    L"table",
    L"column header",
    L"row header",
    L"column",
    L"row",
    L"cell",
    L"link",
    L"help balloon",
    L"character",
    L"list",
    L"list item",
    L"outline",
    L"outline item",
    L"page tab",
    L"property page",
    L"indicator",
    L"graphic",
    L"text",
    L"editable text",
    L"push button",
    L"check box",
    L"radio button",
    L"combo box",
    L"drop down",
    L"progress bar",
    L"dial",
    L"hot key field",
    L"slider",
    L"spin box",
    L"diagram",
    L"animation",
    L"equation",
    L"drop down button",
    L"menu button",
    L"grid drop down button",
    L"white space",
    L"page tab list",
    L"clock",
    L"split button",
    L"IP address",
    L"outline button",
};

static const WCHAR *const states[] = {
    L"normal",
    L"unavailable",
    L"selected",
    L"focused",
    L"pressed",
    L"checked",
    L"mixed",
    L"read only",
    L"hot tracked",
    L"default",
    L"expanded",
    L"collapsed",
    L"busy",
    L"floating",
    L"marqueed",
    L"animated",
    L"invisible",
    L"offscreen",
    L"sizeable",
    L"moveable",
    L"self voicing",
    L"focusable",
    L"selectable",
    L"linked",
    L"traversed",
    L"multiple selectable",
    L"extended selectable",
    L"alert low",
    L"alert medium",
    L"alert high",
    L"protected",
    L"has popup",
};

static const struct {
    WORD id;
    const WCHAR *text;
} strings[] = {
    {109, L"null"},
    {110, L"Unknown"},
    {111, L"Normal"},
    {112, L"Edit"},
    {113, L"Wait"},
    {114, L"Graphic"},
    {115, L"Up"},
    {116, L"NWSE size"},
    {117, L"NESW size"},
    {118, L"Horizontal size"},
    {119, L"Vertical size"},
    {120, L"Move"},
    {121, L"Forbidden"},
    {122, L"App start"},
    {123, L"Help"},
    {124, L"Pen"},
    {142, L"Minimize"},
    {143, L"Maximize"},
    {144, L"Context help"},
    {145, L"Close"},
    {146, L"Restore"},
    {150, L"Displays the name of the window and contains controls to manipulate it"},
    {151, L"Brings up the keyboard when pressed"},
    {152, L"Moves the window out of the way"},
    {153, L"Makes the window full screen"},
    {154, L"Enters or leaves context sensitive help mode"},
    {155, L"Closes the window"},
    {156, L"Puts a minimized or maximized window back to normal"},
    {180, L"Vertical"},
    {181, L"Line up"},
    {182, L"Page up"},
    {183, L"Position"},
    {184, L"Page down"},
    {185, L"Line down"},
    {186, L"Horizontal"},
    {187, L"Column left"},
    {188, L"Page left"},
    {189, L"Position"},
    {190, L"Page right"},
    {191, L"Column right"},
    {192, L"Size box"},
    {200, L"Used to change the vertical viewing area"},
    {201, L"Moves the vertical position up one line"},
    {202, L"Moves the vertical position up a couple of lines"},
    {203, L"Indicates the current vertical position, and can be dragged to change it directly"},
    {204, L"Moves the vertical position down a couple of lines"},
    {205, L"Moves the vertical position down one line"},
    {206, L"Used to change the horizontal viewing area"},
    {207, L"Moves the horizontal position left one column"},
    {208, L"Moves the horizontal position left a couple of columns"},
    {209, L"Indicates the current horizontal position, and can be dragged to change it directly"},
    {210, L"Moves the horizontal position right a couple of columns"},
    {211, L"Moves the horizontal position right one column"},
    {212, L"Can be used to resize a window's width and height"},
    {251, L"System"},
    {254, L"Alt"},
    {255, L"Alt+%s"},
    {256, L"Space"},
    {257, L"Hyphen"},
    {258, L"Execute"},
    {265, L"Double Click"},
    {266, L"Click"},
    {285, L"Open"},
    {286, L"Close"},
    {305, L"Expand"},
    {306, L"Collapse"},
    {307, L"Jump"},
    {308, L"Press"},
    {309, L"Check"},
    {310, L"Uncheck"},
    {311, L"Toggle"},
    {321, L"normal"},
    {322, L"error"},
    {323, L"paused"},
};

const WCHAR *OaRoleName(DWORD role)
{
    return role < RTL_NUMBER_OF(roles) ? roles[role] : nullptr;
}

/* the text of the highest set state bit, "normal" for none. */
const WCHAR *OaStateName(DWORD state)
{
    DWORD index = 0;
    for (; state; state >>= 1)
        index++;
    return index < RTL_NUMBER_OF(states) ? states[index] : nullptr;
}

const WCHAR *OaString(DWORD id)
{
    for (DWORD i = 0; i < RTL_NUMBER_OF(strings); i++)
        if (strings[i].id == id)
            return strings[i].text;
    return L"";
}

/* LoadString rules: cut to the buffer, always terminated, returns the length. */
static UINT copy_text(const WCHAR *text, WCHAR *buffer, UINT length)
{
    if (!text)
        return 0;
    UINT chars = (UINT)wcslen(text);
    if (!buffer)
        return chars < 127 ? chars : 127;
    if (length == 1) {
        *buffer = 0;
        return 0;
    }
    if (!length)
        return 0;
    UINT n = chars < length - 1 ? chars : length - 1;
    memcpy(buffer, text, n * sizeof(WCHAR));
    buffer[n] = 0;
    return n;
}

OLEACCAPI UINT WINAPI GetRoleTextW(DWORD role, LPWSTR text, UINT length)
{
    return copy_text(OaRoleName(role), text, length);
}

OLEACCAPI UINT WINAPI GetStateTextW(DWORD state, LPWSTR text, UINT length)
{
    return copy_text(OaStateName(state), text, length);
}

/* the ansi forms convert the wide text, a buffer too small cuts it. */
static UINT ansi_text(UINT (WINAPI *wide)(DWORD, LPWSTR, UINT), DWORD value, LPSTR text, UINT length)
{
    WCHAR buffer[128];
    UINT chars = wide(value, buffer, RTL_NUMBER_OF(buffer));
    if (!text)
        return chars;
    if (!length)
        return 0;
    if (length == 1) {
        *text = 0;
        return 0;
    }
    char converted[384];
    int bytes = WideCharToMultiByte(CP_ACP, 0, buffer, (int)chars, converted, sizeof(converted), nullptr, nullptr);
    if (bytes < 0)
        bytes = 0;
    if ((UINT)bytes > length - 1)
        bytes = (int)length - 1;
    memcpy(text, converted, (UINT)bytes);
    text[bytes] = 0;
    return (UINT)bytes;
}

OLEACCAPI UINT WINAPI GetRoleTextA(DWORD role, LPSTR text, UINT length)
{
    return ansi_text(GetRoleTextW, role, text, length);
}

OLEACCAPI UINT WINAPI GetStateTextA(DWORD state, LPSTR text, UINT length)
{
    return ansi_text(GetStateTextW, state, text, length);
}
