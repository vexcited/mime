/*
 * the system mouse: three axes and up to eight buttons in a 20 byte state,
 * always relative. exclusive acquisition hides the cursor in the middle of
 * the window and puts it back afterwards.
 */
#include "dinput_private.h"

static const DIOBJECT MOUSE_OBJECTS[] = {
    {&GUID_XAxis, 0, DIDFT_MAKEINSTANCE(0) | DIDFT_RELAXIS, DIDOI_ASPECTPOSITION},
    {&GUID_YAxis, 4, DIDFT_MAKEINSTANCE(1) | DIDFT_RELAXIS, DIDOI_ASPECTPOSITION},
    {&GUID_ZAxis, 8, DIDFT_MAKEINSTANCE(2) | DIDFT_RELAXIS, DIDOI_ASPECTPOSITION},
    {&GUID_Button, 12, DIDFT_MAKEINSTANCE(3) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 13, DIDFT_MAKEINSTANCE(4) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 14, DIDFT_MAKEINSTANCE(5) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 15, DIDFT_MAKEINSTANCE(6) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 16, DIDFT_MAKEINSTANCE(7) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 17, DIDFT_MAKEINSTANCE(8) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 18, DIDFT_MAKEINSTANCE(9) | DIDFT_PSHBUTTON, 0},
    {&GUID_Button, 19, DIDFT_MAKEINSTANCE(10) | DIDFT_PSHBUTTON, 0},
};

/* a wheel makes three axes; buttons as the system counts them, at least three with a wheel. */
static HRESULT mouse_init(DIDEV *dev)
{
    DWORD axes = GetSystemMetrics(SM_MOUSEWHEELPRESENT) ? 3 : 2;
    DWORD buttons = (DWORD)GetSystemMetrics(SM_CMOUSEBUTTONS);
    if (buttons > 8)
        buttons = 8;
    if (axes == 3 && buttons < 3)
        buttons = 3;
    dev->devtype = dev->di8 ? DI8DEVTYPE_MOUSE | DI8DEVTYPEMOUSE_UNKNOWN << 8 : DIDEVTYPE_MOUSE | DIDEVTYPEMOUSE_UNKNOWN << 8;
    dev->count = 0;
    for (DWORD i = 0; i < axes; i++)
        dev->objects[dev->count++] = MOUSE_OBJECTS[i];
    for (DWORD i = 0; i < buttons; i++)
        dev->objects[dev->count++] = MOUSE_OBJECTS[3 + i];
    dev->native_size = sizeof(DIMOUSE);
    dev->axes = axes;
    dev->buttons = buttons;
    dev->povs = 0;
    dev->relative = FALSE;
    DiCopyW(dev->name, DiString(16), DI_MAX_PATH);
    return DI_OK;
}

static HRESULT mouse_cooperate(DIDEV *dev, HWND window, DWORD flags)
{
    if ((flags & DISCL_EXCLUSIVE) && !(flags & DISCL_FOREGROUND))
        return DIERR_UNSUPPORTED;
    dev->em = (flags & DISCL_EXCLUSIVE) ? DIEM_EXCLUSIVE : 0;
    return DI_OK;
}

static void mouse_acquire(DIDEV *dev)
{
    if (!(dev->em & DIEM_EXCLUSIVE))
        return;
    GetCursorPos(&dev->saved_cursor);
    RECT r;
    if (GetWindowRect(dev->window, &r))
        SetCursorPos((r.left + r.right) >> 1, (r.top + r.bottom) >> 1);
    ShowCursor(FALSE);
    dev->hid_cursor = TRUE;
}

/* the cursor goes back where it was, unless the foreground window covers the whole desktop. */
static void mouse_unacquire(DIDEV *dev)
{
    if (!dev->hid_cursor)
        return;
    dev->hid_cursor = FALSE;
    RECT desktop, fg, rest;
    HWND foreground = GetForegroundWindow();
    GetWindowRect(GetDesktopWindow(), &desktop);
    if (!foreground || !GetWindowRect(foreground, &fg) || SubtractRect(&rest, &desktop, &fg))
        SetCursorPos(dev->saved_cursor.x, dev->saved_cursor.y);
    ShowCursor(TRUE);
}

static void mouse_state(DIDEV *dev, BYTE *native)
{
    memcpy(native, &DiMouseState, sizeof(DiMouseState));
}

static BOOL mouse_name(DIDEV *dev, const DIOBJECT *object, WCHAR *name)
{
    DiCopyW(name, DiString(256 + DIDFT_GETINSTANCE(object->type)), DI_MAX_PATH);
    return TRUE;
}

/* a wheel notch is WHEEL_DELTA. */
static HRESULT mouse_property(DIDEV *dev, DWORD id, const DIOBJECT *object, DIPROPHEADER *h)
{
    if (id == 3 && object && object->type == (DIDFT_MAKEINSTANCE(2) | DIDFT_RELAXIS)) {
        ((DIPROPDWORD *)h)->dwData = WHEEL_DELTA;
        return DI_OK;
    }
    return E_NOTIMPL;
}

const DICLASS DiMouseClass = {
    .keyboard = FALSE,
    .init = mouse_init,
    .cooperate = mouse_cooperate,
    .acquire = mouse_acquire,
    .unacquire = mouse_unacquire,
    .state = mouse_state,
    .name = mouse_name,
    .get_property = mouse_property,
    .control_panel = L"control.exe mouse",
};
