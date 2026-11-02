    .text
    .p2align 4, 0x90
    .globl _NtUserInitializeClientPfnArrays@4
_NtUserInitializeClientPfnArrays@4:
    movl $0x14aa, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserRegisterClassExWOW@20
_NtUserRegisterClassExWOW@20:
    movl $0x10a2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserUnregisterClass@8
_NtUserUnregisterClass@8:
    movl $0x10ad, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserGetClassInfoEx@20
_NtUserGetClassInfoEx@20:
    movl $0x10ab, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserSetClassLongPtr@16
_NtUserSetClassLongPtr@16:
    movl $0x15d9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserSetClassLong@16
_NtUserSetClassLong@16:
    movl $0x10b2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserSetClassWord@12
_NtUserSetClassWord@12:
    movl $0xc1549, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetClassName@12
_NtUserGetClassName@12:
    movl $0x1074, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserCreateWindowEx@52
_NtUserCreateWindowEx@52:
    movl $0x106f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $52
    .p2align 4, 0x90
    .globl _NtUserDestroyWindow@4
_NtUserDestroyWindow@4:
    movl $0x31095, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserShowWindow@8
_NtUserShowWindow@8:
    movl $0x81052, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserShowWindowAsync@8
_NtUserShowWindowAsync@8:
    movl $0x81106, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetWindowPos@28
_NtUserSetWindowPos@28:
    movl $0x1021, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtUserEnableWindow@8
_NtUserEnableWindow@8:
    movl $0x1416, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetParent@8
_NtUserSetParent@8:
    movl $0x81070, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserGetAncestor@8
_NtUserGetAncestor@8:
    movl $0x710a4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetWindowLongPtr@16
_NtUserSetWindowLongPtr@16:
    movl $0x15da, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserSetWindowLong@16
_NtUserSetWindowLong@16:
    movl $0x1056, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserDefSetText@8
_NtUserDefSetText@8:
    movl $0x1077, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserInternalGetWindowText@12
_NtUserInternalGetWindowText@12:
    movl $0xc105d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserFindWindowEx@20
_NtUserFindWindowEx@20:
    movl $0x1067, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserGetKeyboardState@4
_NtUserGetKeyboardState@4:
    movl $0x41071, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetGuiResources@8
_NtUserGetGuiResources@8:
    movl $0x71447, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetKeyboardState@4
_NtUserSetKeyboardState@4:
    movl $0x410dc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCreateWindowStation@28
_NtUserCreateWindowStation@28:
    movl $0x13db, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtUserOpenWindowStation@8
_NtUserOpenWindowStation@8:
    movl $0x1097, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserCreateDesktopEx@24
_NtUserCreateDesktopEx@24:
    movl $0x13d2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtUserOpenDesktop@12
_NtUserOpenDesktop@12:
    movl $0x109a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserOpenInputDesktop@12
_NtUserOpenInputDesktop@12:
    movl $0xa14e6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetProcessWindowStation@0
_NtUserGetProcessWindowStation@0:
    movl $0x1101f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserSetProcessWindowStation@4
_NtUserSetProcessWindowStation@4:
    movl $0x4109b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetThreadDesktop@8
_NtUserGetThreadDesktop@8:
    movl $0x5107b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetThreadDesktop@4
_NtUserSetThreadDesktop@4:
    movl $0x41089, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCloseWindowStation@4
_NtUserCloseWindowStation@4:
    movl $0x410a7, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCloseDesktop@4
_NtUserCloseDesktop@4:
    movl $0x41099, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSwitchDesktop@12
_NtUserSwitchDesktop@12:
    movl $0x515b1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetObjectInformation@20
_NtUserGetObjectInformation@20:
    movl $0x1065, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserSetObjectInformation@16
_NtUserSetObjectInformation@16:
    movl $0x1574, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserBuildNameList@16
_NtUserBuildNameList@16:
    movl $0x10a0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserBuildHwndList@28
_NtUserBuildHwndList@28:
    movl $0x101a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtUserGetWindowPlacement@8
_NtUserGetWindowPlacement@8:
    movl $0x710c2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetWindowPlacement@8
_NtUserSetWindowPlacement@8:
    movl $0x710cf, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserMessageCall@28
_NtUserMessageCall@28:
    movl $0x1005, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtUserPostMessage@16
_NtUserPostMessage@16:
    movl $0x100d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserPostThreadMessage@16
_NtUserPostThreadMessage@16:
    movl $0x1059, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserGetMessage@16
_NtUserGetMessage@16:
    movl $0x1004, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserPeekMessage@20
_NtUserPeekMessage@20:
    movl $0x1001, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserDispatchMessage@4
_NtUserDispatchMessage@4:
    movl $0x1031, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserTranslateMessage@8
_NtUserTranslateMessage@8:
    movl $0x100b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserWaitMessage@0
_NtUserWaitMessage@0:
    movl $0x1100a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserRegisterWindowMessage@4
_NtUserRegisterWindowMessage@4:
    movl $0x1032, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetThreadState@4
_NtUserGetThreadState@4:
    movl $0x41000, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetMessagePos@0
_NtUserGetMessagePos@0:
    movl $0x145e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserBeginPaint@8
_NtUserBeginPaint@8:
    movl $0x1015, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserEndPaint@8
_NtUserEndPaint@8:
    movl $0x1017, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserGetDC@4
_NtUserGetDC@4:
    movl $0x31008, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetDCEx@12
_NtUserGetDCEx@12:
    movl $0xc108a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetWindowDC@4
_NtUserGetWindowDC@4:
    movl $0x3105e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserReleaseDC@8
_NtUserReleaseDC@8:
    movl $0x151c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserInvalidateRect@12
_NtUserInvalidateRect@12:
    movl $0xc1003, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserValidateRect@8
_NtUserValidateRect@8:
    movl $0x710bb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserRedrawWindow@16
_NtUserRedrawWindow@16:
    movl $0x141011, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserGetUpdateRect@12
_NtUserGetUpdateRect@12:
    movl $0xc104e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserSetFocus@4
_NtUserSetFocus@4:
    movl $0x3104c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSetActiveWindow@4
_NtUserSetActiveWindow@4:
    movl $0x310cd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetForegroundWindow@0
_NtUserGetForegroundWindow@0:
    movl $0x11037, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserSetCapture@4
_NtUserSetCapture@4:
    movl $0x31044, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetKeyState@4
_NtUserGetKeyState@4:
    movl $0x31002, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSendInput@12
_NtUserSendInput@12:
    movl $0x107a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserSetWindowsHookEx@24
_NtUserSetWindowsHookEx@24:
    movl $0x1084, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtUserUnhookWindowsHookEx@4
_NtUserUnhookWindowsHookEx@4:
    movl $0x1069, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCallNextHookEx@16
_NtUserCallNextHookEx@16:
    movl $0x101c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserGetAsyncKeyState@4
_NtUserGetAsyncKeyState@4:
    movl $0x3103f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetCursorPos@4
_NtUserGetCursorPos@4:
    movl $0x1437, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSetTimer@20
_NtUserSetTimer@20:
    movl $0x1016, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserKillTimer@8
_NtUserKillTimer@8:
    movl $0x71019, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserTrackMouseEvent@4
_NtUserTrackMouseEvent@4:
    movl $0x10c8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSystemParametersInfo@16
_NtUserSystemParametersInfo@16:
    movl $0x103d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserEnumDisplaySettings@16
_NtUserEnumDisplaySettings@16:
    movl $0x10ff, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserChangeDisplaySettings@16
_NtUserChangeDisplaySettings@16:
    movl $0x13bc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserSystemParametersInfoForDpi@20
_NtUserSystemParametersInfoForDpi@20:
    movl $0x15b3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserForceWindowToDpiForTest@8
_NtUserForceWindowToDpiForTest@8:
    movl $0x1425, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserDeferWindowDpiChanges@4
_NtUserDeferWindowDpiChanges@4:
    movl $0x13e0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserRestoreWindowDpiChanges@4
_NtUserRestoreWindowDpiChanges@4:
    movl $0x1537, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSetDpiForWindow@8
_NtUserSetDpiForWindow@8:
    movl $0x1558, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserEnableChildWindowDpiMessage@8
_NtUserEnableChildWindowDpiMessage@8:
    movl $0x1408, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserIsChildWindowDpiMessageEnabled@4
_NtUserIsChildWindowDpiMessageEnabled@4:
    movl $0x14ba, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserIsWindowBroadcastingDpiToChildren@4
_NtUserIsWindowBroadcastingDpiToChildren@4:
    movl $0x14c3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserMessageBeep@4
_NtUserMessageBeep@4:
    movl $0x14dd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCreateEmptyCurObject@4
_NtUserCreateEmptyCurObject@4:
    movl $0x1705, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSetCursorIconData@16
_NtUserSetCursorIconData@16:
    movl $0x1098, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserDestroyCursor@8
_NtUserDestroyCursor@8:
    movl $0x51094, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserGetIconInfo@24
_NtUserGetIconInfo@24:
    movl $0x104a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtUserDrawIconEx@44
_NtUserDrawIconEx@44:
    movl $0x105a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $44
    .p2align 4, 0x90
    .globl _NtUserSetCursor@4
_NtUserSetCursor@4:
    movl $0x41018, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCreateMenu@0
_NtUserCreateMenu@0:
    movl $0x13d6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserCreatePopupMenu@0
_NtUserCreatePopupMenu@0:
    movl $0x13d8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserDestroyMenu@4
_NtUserDestroyMenu@4:
    movl $0x410ca, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserThunkedMenuItemInfo@24
_NtUserThunkedMenuItemInfo@24:
    movl $0x1090, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtUserThunkedMenuInfo@8
_NtUserThunkedMenuInfo@8:
    movl $0x10f4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserDeleteMenu@12
_NtUserDeleteMenu@12:
    movl $0xa10ae, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserRemoveMenu@12
_NtUserRemoveMenu@12:
    movl $0xa10e6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserEnableMenuItem@12
_NtUserEnableMenuItem@12:
    movl $0xa10bf, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserCheckMenuItem@12
_NtUserCheckMenuItem@12:
    movl $0xa10ef, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserSetMenuDefaultItem@12
_NtUserSetMenuDefaultItem@12:
    movl $0xa10ee, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserSetMenu@12
_NtUserSetMenu@12:
    movl $0xc156c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetSystemMenu@8
_NtUserGetSystemMenu@8:
    movl $0x7105b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserDrawMenuBar@4
_NtUserDrawMenuBar@4:
    movl $0x13ff, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserEndMenu@0
_NtUserEndMenu@0:
    movl $0x1141b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserMimeGetMenuItemInfo@16
_NtUserMimeGetMenuItemInfo@16:
    movl $0x1709, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserMimeCallMenu@12
_NtUserMimeCallMenu@12:
    movl $0x170a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserShutdownBlockReasonCreate@12
_NtUserShutdownBlockReasonCreate@12:
    movl $0x15a8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserShutdownReasonDestroy@4
_NtUserShutdownReasonDestroy@4:
    movl $0x15aa, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserShutdownBlockReasonQuery@12
_NtUserShutdownBlockReasonQuery@12:
    movl $0x15a9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserPrepareForLogoff@0
_NtUserPrepareForLogoff@0:
    movl $0x14ef, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserMimeExitWindows@8
_NtUserMimeExitWindows@8:
    movl $0x171e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiMimeGpuOpen@8
_NtGdiMimeGpuOpen@8:
    movl $0x170b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiMimeGpuAddBlock@12
_NtGdiMimeGpuAddBlock@12:
    movl $0x170c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiMimeGpuKick@0
_NtGdiMimeGpuKick@0:
    movl $0x170d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtGdiMimeGpuWait@12
_NtGdiMimeGpuWait@12:
    movl $0x170e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiMimeGpuGetSurface@8
_NtGdiMimeGpuGetSurface@8:
    movl $0x170f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiMimeAudioCreate@8
_NtGdiMimeAudioCreate@8:
    movl $0x1710, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiMimeAudioControl@8
_NtGdiMimeAudioControl@8:
    movl $0x1711, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiMimeVkOpen@4
_NtGdiMimeVkOpen@4:
    movl $0x1712, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiMimeVkAlloc@12
_NtGdiMimeVkAlloc@12:
    movl $0x1713, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiMimeVkFree@4
_NtGdiMimeVkFree@4:
    movl $0x1714, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiMimeVkKick@0
_NtGdiMimeVkKick@0:
    movl $0x1715, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtGdiMimeVkWait@12
_NtGdiMimeVkWait@12:
    movl $0x1716, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiMimeGfxShare@8
_NtGdiMimeGfxShare@8:
    movl $0x1760, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiMimeGamma@8
_NtGdiMimeGamma@8:
    movl $0x1717, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserMimeDnd@16
_NtUserMimeDnd@16:
    movl $0x1719, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserMimeDndData@24
_NtUserMimeDndData@24:
    movl $0x171a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtUserMimeIme@20
_NtUserMimeIme@20:
    movl $0x171c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserMimeTray@12
_NtUserMimeTray@12:
    movl $0x171d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserMimeShell@16
_NtUserMimeShell@16:
    movl $0x1770, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserMimeDndProvide@20
_NtUserMimeDndProvide@20:
    movl $0x171b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserMimeCapture@20
_NtUserMimeCapture@20:
    movl $0x1750, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserOpenClipboard@8
_NtUserOpenClipboard@8:
    movl $0x710bd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserCloseClipboard@0
_NtUserCloseClipboard@0:
    movl $0x110bc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserEmptyClipboard@0
_NtUserEmptyClipboard@0:
    movl $0x110e4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserSetClipboardData@12
_NtUserSetClipboardData@12:
    movl $0xf10be, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetClipboardData@8
_NtUserGetClipboardData@8:
    movl $0x10e5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserIsClipboardFormatAvailable@4
_NtUserIsClipboardFormatAvailable@4:
    movl $0x4102a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetUpdatedClipboardFormats@12
_NtUserGetUpdatedClipboardFormats@12:
    movl $0x148c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserCountClipboardFormats@0
_NtUserCountClipboardFormats@0:
    movl $0x110fa, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserEnumClipboardFormats@4
_NtUserEnumClipboardFormats@4:
    movl $0x1421, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserGetClipboardFormatName@12
_NtUserGetClipboardFormatName@12:
    movl $0xa10d6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetClipboardOwner@0
_NtUserGetClipboardOwner@0:
    movl $0x110b7, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserGetOpenClipboardWindow@0
_NtUserGetOpenClipboardWindow@0:
    movl $0x110c5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserGetClipboardViewer@0
_NtUserGetClipboardViewer@0:
    movl $0x11431, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserSetClipboardViewer@4
_NtUserSetClipboardViewer@4:
    movl $0x31105, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserChangeClipboardChain@8
_NtUserChangeClipboardChain@8:
    movl $0x81104, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserGetClipboardSequenceNumber@0
_NtUserGetClipboardSequenceNumber@0:
    movl $0x11050, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtUserAddClipboardFormatListener@4
_NtUserAddClipboardFormatListener@4:
    movl $0x13aa, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserRemoveClipboardFormatListener@4
_NtUserRemoveClipboardFormatListener@4:
    movl $0x152e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserSetLayeredWindowAttributes@16
_NtUserSetLayeredWindowAttributes@16:
    movl $0x141568, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserGetLayeredWindowAttributes@16
_NtUserGetLayeredWindowAttributes@16:
    movl $0x14145a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserUpdateLayeredWindow@40
_NtUserUpdateLayeredWindow@40:
    movl $0x15c5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $40
    .p2align 4, 0x90
    .globl _NtUserSetWindowRgn@12
_NtUserSetWindowRgn@12:
    movl $0xc10b8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetWindowRgnEx@12
_NtUserGetWindowRgnEx@12:
    movl $0x1497, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetCursorInfo@4
_NtUserGetCursorInfo@4:
    movl $0x1436, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserClipCursor@4
_NtUserClipCursor@4:
    movl $0x413c8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserMsgWaitForMultipleObjectsEx@20
_NtUserMsgWaitForMultipleObjectsEx@20:
    movl $0x14e1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserGetClipCursor@4
_NtUserGetClipCursor@4:
    movl $0x4142e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserRegisterRawInputDevices@12
_NtUserRegisterRawInputDevices@12:
    movl $0x150f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetRegisteredRawInputDevices@12
_NtUserGetRegisteredRawInputDevices@12:
    movl $0x147f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetRawInputData@20
_NtUserGetRawInputData@20:
    movl $0x147b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtUserGetRawInputBuffer@12
_NtUserGetRawInputBuffer@12:
    movl $0x147a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetRawInputDeviceList@12
_NtUserGetRawInputDeviceList@12:
    movl $0x147d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetRawInputDeviceInfo@16
_NtUserGetRawInputDeviceInfo@16:
    movl $0x147c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserAttachThreadInput@12
_NtUserAttachThreadInput@12:
    movl $0xa10d9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserIsTouchWindow@8
_NtUserIsTouchWindow@8:
    movl $0x14c1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserSetProp@12
_NtUserSetProp@12:
    movl $0x101047, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserGetProp@8
_NtUserGetProp@8:
    movl $0x100c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserRemoveProp@8
_NtUserRemoveProp@8:
    movl $0x71041, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserBuildPropList@16
_NtUserBuildPropList@16:
    movl $0x13b8, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtUserCallNoParam@4
_NtUserCallNoParam@4:
    movl $0x1700, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtUserCallOneParam@8
_NtUserCallOneParam@8:
    movl $0x1701, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserCallTwoParam@12
_NtUserCallTwoParam@12:
    movl $0x1702, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtUserCallHwnd@8
_NtUserCallHwnd@8:
    movl $0x1703, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtUserCallHwndParam@12
_NtUserCallHwndParam@12:
    movl $0x1704, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetStockObject@4
_NtGdiGetStockObject@4:
    movl $0x1706, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiDeleteObjectApp@4
_NtGdiDeleteObjectApp@4:
    movl $0x31020, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiExtGetObjectW@12
_NtGdiExtGetObjectW@12:
    movl $0x104d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetDCObject@8
_NtGdiGetDCObject@8:
    movl $0x81030, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiCreateCompatibleDC@4
_NtGdiCreateCompatibleDC@4:
    movl $0x3104f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiOpenDCW@32
_NtGdiOpenDCW@32:
    movl $0x10c7, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $32
    .p2align 4, 0x90
    .globl _NtGdiSaveDC@4
_NtGdiSaveDC@4:
    movl $0x31036, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiRestoreDC@8
_NtGdiRestoreDC@8:
    movl $0x81035, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiGetDeviceCaps@8
_NtGdiGetDeviceCaps@8:
    movl $0x812bf, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiSelectBitmap@8
_NtGdiSelectBitmap@8:
    movl $0x71009, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiSelectBrush@8
_NtGdiSelectBrush@8:
    movl $0x71312, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiSelectPen@8
_NtGdiSelectPen@8:
    movl $0x71314, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiSelectFont@8
_NtGdiSelectFont@8:
    movl $0x71034, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiGetAndSetDCDword@16
_NtGdiGetAndSetDCDword@16:
    movl $0x141061, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiGetDCDword@12
_NtGdiGetDCDword@12:
    movl $0xc103a, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetDCPoint@12
_NtGdiGetDCPoint@12:
    movl $0xc106c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiMoveTo@16
_NtGdiMoveTo@16:
    movl $0x1412f6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiSetViewportOrgEx@16
_NtGdiSetViewportOrgEx@16:
    movl $0x1707, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiSetWindowOrgEx@16
_NtGdiSetWindowOrgEx@16:
    movl $0x1708, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiTransformPoints@20
_NtGdiTransformPoints@20:
    movl $0x106b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiFlush@0
_NtGdiFlush@0:
    movl $0x11010, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $0
    .p2align 4, 0x90
    .globl _NtGdiCreateSolidBrush@8
_NtGdiCreateSolidBrush@8:
    movl $0x510aa, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiCreatePen@16
_NtGdiCreatePen@16:
    movl $0x111051, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiHfontCreate@20
_NtGdiHfontCreate@20:
    movl $0x1057, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiCreateBitmap@20
_NtGdiCreateBitmap@20:
    movl $0x1066, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiCreateCompatibleBitmap@12
_NtGdiCreateCompatibleBitmap@12:
    movl $0xb1046, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiDdDDICreateDCFromMemory@4
_NtGdiDdDDICreateDCFromMemory@4:
    movl $0x11c9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiDdDDIDestroyDCFromMemory@4
_NtGdiDdDDIDestroyDCFromMemory@4:
    movl $0x11d9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiCreateDIBSection@36
_NtGdiCreateDIBSection@36:
    movl $0x1092, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtUserWaitForInputIdle@12
_NtUserWaitForInputIdle@12:
    movl $0xc15d1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiDoPalette@24
_NtGdiDoPalette@24:
    movl $0x1042, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiGetDIBitsInternal@36
_NtGdiGetDIBitsInternal@36:
    movl $0x107d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtGdiSetDIBitsToDeviceInternal@64
_NtGdiSetDIBitsToDeviceInternal@64:
    movl $0x1025, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $64
    .p2align 4, 0x90
    .globl _NtGdiStretchDIBitsInternal@64
_NtGdiStretchDIBitsInternal@64:
    movl $0x1079, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $64
    .p2align 4, 0x90
    .globl _NtGdiBitBlt@44
_NtGdiBitBlt@44:
    movl $0x1006, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $44
    .p2align 4, 0x90
    .globl _NtGdiStretchBlt@48
_NtGdiStretchBlt@48:
    movl $0x102c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $48
    .p2align 4, 0x90
    .globl _NtGdiPatBlt@24
_NtGdiPatBlt@24:
    movl $0x1054, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiAlphaBlend@48
_NtGdiAlphaBlend@48:
    movl $0x1075, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $48
    .p2align 4, 0x90
    .globl _NtGdiLineTo@12
_NtGdiLineTo@12:
    movl $0xc103c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiRectangle@20
_NtGdiRectangle@20:
    movl $0x1088, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiRoundRect@28
_NtGdiRoundRect@28:
    movl $0x1308, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
    .p2align 4, 0x90
    .globl _NtGdiEllipse@20
_NtGdiEllipse@20:
    movl $0x127d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiPolyPolyDraw@20
_NtGdiPolyPolyDraw@20:
    movl $0x1043, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiSetPixel@16
_NtGdiSetPixel@16:
    movl $0x1410a1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiGetPixel@12
_NtGdiGetPixel@12:
    movl $0xb10b1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiExtTextOutW@36
_NtGdiExtTextOutW@36:
    movl $0x1033, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $36
    .p2align 4, 0x90
    .globl _NtGdiGetTextExtentExW@32
_NtGdiGetTextExtentExW@32:
    movl $0x12e4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $32
    .p2align 4, 0x90
    .globl _NtGdiGetTextMetricsW@12
_NtGdiGetTextMetricsW@12:
    movl $0xc106e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetTextFaceW@16
_NtGdiGetTextFaceW@16:
    movl $0x141078, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiGetCharWidthW@24
_NtGdiGetCharWidthW@24:
    movl $0x10b5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiGetCharABCWidthsW@24
_NtGdiGetCharABCWidthsW@24:
    movl $0x12b9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiAddFontResourceW@24
_NtGdiAddFontResourceW@24:
    movl $0x1189, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiGetFontData@20
_NtGdiGetFontData@20:
    movl $0x10c4, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiRemoveFontResourceW@24
_NtGdiRemoveFontResourceW@24:
    movl $0x1304, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiAddFontMemResourceEx@20
_NtGdiAddFontMemResourceEx@20:
    movl $0x10fb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiRemoveFontMemResourceEx@4
_NtGdiRemoveFontMemResourceEx@4:
    movl $0x310fe, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiEnumFonts@32
_NtGdiEnumFonts@32:
    movl $0x12a1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $32
    .p2align 4, 0x90
    .globl _NtGdiGetOutlineTextMetricsInternalW@16
_NtGdiGetOutlineTextMetricsInternalW@16:
    movl $0x10a5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiGetFontUnicodeRanges@8
_NtGdiGetFontUnicodeRanges@8:
    movl $0x712cb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiGetGlyphOutline@32
_NtGdiGetGlyphOutline@32:
    movl $0x12ce, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $32
    .p2align 4, 0x90
    .globl _NtGdiGetGlyphIndicesW@20
_NtGdiGetGlyphIndicesW@20:
    movl $0x12cc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiBeginPath@4
_NtGdiBeginPath@4:
    movl $0x310f5, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiEndPath@4
_NtGdiEndPath@4:
    movl $0x310f6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiAbortPath@4
_NtGdiAbortPath@4:
    movl $0x31187, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiCloseFigure@4
_NtGdiCloseFigure@4:
    movl $0x310ec, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiFlattenPath@4
_NtGdiFlattenPath@4:
    movl $0x312ad, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiFillPath@4
_NtGdiFillPath@4:
    movl $0x310f7, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiStrokePath@4
_NtGdiStrokePath@4:
    movl $0x31328, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiStrokeAndFillPath@4
_NtGdiStrokeAndFillPath@4:
    movl $0x31327, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiSelectClipPath@8
_NtGdiSelectClipPath@8:
    movl $0x81313, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiPathToRegion@4
_NtGdiPathToRegion@4:
    movl $0x312fd, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiGetPath@16
_NtGdiGetPath@16:
    movl $0x1412d7, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiModifyWorldTransform@12
_NtGdiModifyWorldTransform@12:
    movl $0xc10c3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetTransform@12
_NtGdiGetTransform@12:
    movl $0xc10c9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetBitmapBits@12
_NtGdiGetBitmapBits@12:
    movl $0xa10cb, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiSetBitmapBits@12
_NtGdiSetBitmapBits@12:
    movl $0xa10a6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiSetPixelFormat@8
_NtGdiSetPixelFormat@8:
    movl $0x8131f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiCreateRectRgn@16
_NtGdiCreateRectRgn@16:
    movl $0x11107c, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiCreateRoundRectRgn@24
_NtGdiCreateRoundRectRgn@24:
    movl $0x11a9, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $24
    .p2align 4, 0x90
    .globl _NtGdiExtCreateRegion@12
_NtGdiExtCreateRegion@12:
    movl $0xa1082, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiCombineRgn@16
_NtGdiCombineRgn@16:
    movl $0x11102f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $16
    .p2align 4, 0x90
    .globl _NtGdiSetRectRgn@20
_NtGdiSetRectRgn@20:
    movl $0x1320, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiOffsetRgn@12
_NtGdiOffsetRgn@12:
    movl $0xa1076, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetRgnBox@8
_NtGdiGetRgnBox@8:
    movl $0x51060, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiPtInRegion@12
_NtGdiPtInRegion@12:
    movl $0xa1301, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiRectInRegion@8
_NtGdiRectInRegion@8:
    movl $0x510af, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiEqualRgn@8
_NtGdiEqualRgn@8:
    movl $0x510fc, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiGetRegionData@12
_NtGdiGetRegionData@12:
    movl $0xa103b, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiFillRgn@12
_NtGdiFillRgn@12:
    movl $0x10c1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiFrameRgn@20
_NtGdiFrameRgn@20:
    movl $0x12b0, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiInvertRgn@8
_NtGdiInvertRgn@8:
    movl $0x7105f, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiExtSelectClipRgn@12
_NtGdiExtSelectClipRgn@12:
    movl $0x101029, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetAppClipBox@8
_NtGdiGetAppClipBox@8:
    movl $0x7103e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiGetRandomRgn@12
_NtGdiGetRandomRgn@12:
    movl $0x101026, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiIntersectClipRect@20
_NtGdiIntersectClipRect@20:
    movl $0x101e, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiExcludeClipRect@20
_NtGdiExcludeClipRect@20:
    movl $0x1091, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiSetMetaRgn@4
_NtGdiSetMetaRgn@4:
    movl $0x310d1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiCreateMetafileDC@4
_NtGdiCreateMetafileDC@4:
    movl $0x311a6, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiMimeSetExtent@20
_NtGdiMimeSetExtent@20:
    movl $0x1718, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiSetVirtualResolution@20
_NtGdiSetVirtualResolution@20:
    movl $0x10d3, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $20
    .p2align 4, 0x90
    .globl _NtGdiSetMiterLimit@12
_NtGdiSetMiterLimit@12:
    movl $0xc10d2, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $12
    .p2align 4, 0x90
    .globl _NtGdiGetMiterLimit@8
_NtGdiGetMiterLimit@8:
    movl $0x712d1, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $8
    .p2align 4, 0x90
    .globl _NtGdiWidenPath@4
_NtGdiWidenPath@4:
    movl $0x31331, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $4
    .p2align 4, 0x90
    .globl _NtGdiExtCreatePen@28
_NtGdiExtCreatePen@28:
    movl $0x109d, %eax
    movl $_Wow64SystemServiceCall, %edx
    calll *%edx
    retl $28
