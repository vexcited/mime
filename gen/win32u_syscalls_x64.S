/* generated, do not edit. */
    .text
    .globl NtUserGetThreadState
    .p2align 5
NtUserGetThreadState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1000, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPeekMessage
    .p2align 5
NtUserPeekMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1001, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyState
    .p2align 5
NtUserGetKeyState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1002, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInvalidateRect
    .p2align 5
NtUserInvalidateRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1003, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMessage
    .p2align 5
NtUserGetMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1004, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMessageCall
    .p2align 5
NtUserMessageCall:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1005, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBitBlt
    .p2align 5
NtGdiBitBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1006, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCharSet
    .p2align 5
NtGdiGetCharSet:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1007, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDC
    .p2align 5
NtUserGetDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1008, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSelectBitmap
    .p2align 5
NtGdiSelectBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1009, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWaitMessage
    .p2align 5
NtUserWaitMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTranslateMessage
    .p2align 5
NtUserTranslateMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetProp
    .p2align 5
NtUserGetProp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPostMessage
    .p2align 5
NtUserPostMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserQueryWindow
    .p2align 5
NtUserQueryWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTranslateAccelerator
    .p2align 5
NtUserTranslateAccelerator:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x100F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFlush
    .p2align 5
NtGdiFlush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1010, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRedrawWindow
    .p2align 5
NtUserRedrawWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1011, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWindowFromPoint
    .p2align 5
NtUserWindowFromPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1012, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallMsgFilter
    .p2align 5
NtUserCallMsgFilter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1013, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserValidateTimerCallback
    .p2align 5
NtUserValidateTimerCallback:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1014, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBeginPaint
    .p2align 5
NtUserBeginPaint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1015, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetTimer
    .p2align 5
NtUserSetTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1016, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEndPaint
    .p2align 5
NtUserEndPaint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1017, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCursor
    .p2align 5
NtUserSetCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1018, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserKillTimer
    .p2align 5
NtUserKillTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1019, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBuildHwndList
    .p2align 5
NtUserBuildHwndList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSelectPalette
    .p2align 5
NtUserSelectPalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallNextHookEx
    .p2align 5
NtUserCallNextHookEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHideCaret
    .p2align 5
NtUserHideCaret:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiIntersectClipRect
    .p2align 5
NtGdiIntersectClipRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetProcessWindowStation
    .p2align 5
NtUserGetProcessWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x101F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDeleteObjectApp
    .p2align 5
NtGdiDeleteObjectApp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1020, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowPos
    .p2align 5
NtUserSetWindowPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1021, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowCaret
    .p2align 5
NtUserShowCaret:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1022, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEndDeferWindowPosEx
    .p2align 5
NtUserEndDeferWindowPosEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1023, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserVkKeyScanEx
    .p2align 5
NtUserVkKeyScanEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1024, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetDIBitsToDeviceInternal
    .p2align 5
NtGdiSetDIBitsToDeviceInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1025, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetRandomRgn
    .p2align 5
NtGdiGetRandomRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1026, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCopyAcceleratorTable
    .p2align 5
NtUserCopyAcceleratorTable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1027, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserNotifyWinEvent
    .p2align 5
NtUserNotifyWinEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1028, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtSelectClipRgn
    .p2align 5
NtGdiExtSelectClipRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1029, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsClipboardFormatAvailable
    .p2align 5
NtUserIsClipboardFormatAvailable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetScrollInfo
    .p2align 5
NtUserSetScrollInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiStretchBlt
    .p2align 5
NtGdiStretchBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateCaret
    .p2align 5
NtUserCreateCaret:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRectVisible
    .p2align 5
NtGdiRectVisible:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCombineRgn
    .p2align 5
NtGdiCombineRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x102F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDCObject
    .p2align 5
NtGdiGetDCObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1030, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDispatchMessage
    .p2align 5
NtUserDispatchMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1031, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterWindowMessage
    .p2align 5
NtUserRegisterWindowMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1032, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtTextOutW
    .p2align 5
NtGdiExtTextOutW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1033, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSelectFont
    .p2align 5
NtGdiSelectFont:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1034, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRestoreDC
    .p2align 5
NtGdiRestoreDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1035, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSaveDC
    .p2align 5
NtGdiSaveDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1036, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetForegroundWindow
    .p2align 5
NtUserGetForegroundWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1037, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowScrollBar
    .p2align 5
NtUserShowScrollBar:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1038, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserFindExistingCursorIcon
    .p2align 5
NtUserFindExistingCursorIcon:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1039, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDCDword
    .p2align 5
NtGdiGetDCDword:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetRegionData
    .p2align 5
NtGdiGetRegionData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiLineTo
    .p2align 5
NtGdiLineTo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSystemParametersInfo
    .p2align 5
NtUserSystemParametersInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetAppClipBox
    .p2align 5
NtGdiGetAppClipBox:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetAsyncKeyState
    .p2align 5
NtUserGetAsyncKeyState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x103F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCPD
    .p2align 5
NtUserGetCPD:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1040, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoveProp
    .p2align 5
NtUserRemoveProp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1041, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDoPalette
    .p2align 5
NtGdiDoPalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1042, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPolyPolyDraw
    .p2align 5
NtGdiPolyPolyDraw:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1043, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCapture
    .p2align 5
NtUserSetCapture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1044, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnumDisplayMonitors
    .p2align 5
NtUserEnumDisplayMonitors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1045, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateCompatibleBitmap
    .p2align 5
NtGdiCreateCompatibleBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1046, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProp
    .p2align 5
NtUserSetProp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1047, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetTextCharsetInfo
    .p2align 5
NtGdiGetTextCharsetInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1048, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSBGetParms
    .p2align 5
NtUserSBGetParms:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1049, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetIconInfo
    .p2align 5
NtUserGetIconInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserExcludeUpdateRgn
    .p2align 5
NtUserExcludeUpdateRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetFocus
    .p2align 5
NtUserSetFocus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtGetObjectW
    .p2align 5
NtGdiExtGetObjectW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetUpdateRect
    .p2align 5
NtUserGetUpdateRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateCompatibleDC
    .p2align 5
NtGdiCreateCompatibleDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x104F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardSequenceNumber
    .p2align 5
NtUserGetClipboardSequenceNumber:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1050, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreatePen
    .p2align 5
NtGdiCreatePen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1051, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowWindow
    .p2align 5
NtUserShowWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1052, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyboardLayoutList
    .p2align 5
NtUserGetKeyboardLayoutList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1053, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPatBlt
    .p2align 5
NtGdiPatBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1054, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMapVirtualKeyEx
    .p2align 5
NtUserMapVirtualKeyEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1055, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowLong
    .p2align 5
NtUserSetWindowLong:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1056, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiHfontCreate
    .p2align 5
NtGdiHfontCreate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1057, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMoveWindow
    .p2align 5
NtUserMoveWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1058, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPostThreadMessage
    .p2align 5
NtUserPostThreadMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1059, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrawIconEx
    .p2align 5
NtUserDrawIconEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetSystemMenu
    .p2align 5
NtUserGetSystemMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDrawStream
    .p2align 5
NtGdiDrawStream:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInternalGetWindowText
    .p2align 5
NtUserInternalGetWindowText:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowDC
    .p2align 5
NtUserGetWindowDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiInvertRgn
    .p2align 5
NtGdiInvertRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x105F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetRgnBox
    .p2align 5
NtGdiGetRgnBox:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1060, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetAndSetDCDword
    .p2align 5
NtGdiGetAndSetDCDword:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1061, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMaskBlt
    .p2align 5
NtGdiMaskBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1062, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetWidthTable
    .p2align 5
NtGdiGetWidthTable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1063, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserScrollDC
    .p2align 5
NtUserScrollDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1064, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetObjectInformation
    .p2align 5
NtUserGetObjectInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1065, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateBitmap
    .p2align 5
NtGdiCreateBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1066, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserFindWindowEx
    .p2align 5
NtUserFindWindowEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1067, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPolyPatBlt
    .p2align 5
NtGdiPolyPatBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1068, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnhookWindowsHookEx
    .p2align 5
NtUserUnhookWindowsHookEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1069, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetNearestColor
    .p2align 5
NtGdiGetNearestColor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiTransformPoints
    .p2align 5
NtGdiTransformPoints:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDCPoint
    .p2align 5
NtGdiGetDCPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateDIBBrush
    .p2align 5
NtGdiCreateDIBBrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetTextMetricsW
    .p2align 5
NtGdiGetTextMetricsW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateWindowEx
    .p2align 5
NtUserCreateWindowEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x106F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetParent
    .p2align 5
NtUserSetParent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1070, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyboardState
    .p2align 5
NtUserGetKeyboardState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1071, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserToUnicodeEx
    .p2align 5
NtUserToUnicodeEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1072, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetControlBrush
    .p2align 5
NtUserGetControlBrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1073, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClassName
    .p2align 5
NtUserGetClassName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1074, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAlphaBlend
    .p2align 5
NtGdiAlphaBlend:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1075, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiOffsetRgn
    .p2align 5
NtGdiOffsetRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1076, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDefSetText
    .p2align 5
NtUserDefSetText:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1077, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetTextFaceW
    .p2align 5
NtGdiGetTextFaceW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1078, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiStretchDIBitsInternal
    .p2align 5
NtGdiStretchDIBitsInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1079, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSendInput
    .p2align 5
NtUserSendInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetThreadDesktop
    .p2align 5
NtUserGetThreadDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateRectRgn
    .p2align 5
NtGdiCreateRectRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDIBitsInternal
    .p2align 5
NtGdiGetDIBitsInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetUpdateRgn
    .p2align 5
NtUserGetUpdateRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDeleteClientObj
    .p2align 5
NtGdiDeleteClientObj:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x107F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetIconSize
    .p2align 5
NtUserGetIconSize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1080, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserFillWindow
    .p2align 5
NtUserFillWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1081, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtCreateRegion
    .p2align 5
NtGdiExtCreateRegion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1082, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiComputeXformCoefficients
    .p2align 5
NtGdiComputeXformCoefficients:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1083, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowsHookEx
    .p2align 5
NtUserSetWindowsHookEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1084, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserNotifyProcessCreate
    .p2align 5
NtUserNotifyProcessCreate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1085, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiUnrealizeObject
    .p2align 5
NtGdiUnrealizeObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1086, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetTitleBarInfo
    .p2align 5
NtUserGetTitleBarInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1087, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRectangle
    .p2align 5
NtGdiRectangle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1088, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetThreadDesktop
    .p2align 5
NtUserSetThreadDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1089, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDCEx
    .p2align 5
NtUserGetDCEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetScrollBarInfo
    .p2align 5
NtUserGetScrollBarInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetTextExtent
    .p2align 5
NtGdiGetTextExtent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowFNID
    .p2align 5
NtUserSetWindowFNID:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetLayout
    .p2align 5
NtGdiSetLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCalcMenuBar
    .p2align 5
NtUserCalcMenuBar:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x108F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserThunkedMenuItemInfo
    .p2align 5
NtUserThunkedMenuItemInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1090, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExcludeClipRect
    .p2align 5
NtGdiExcludeClipRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1091, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateDIBSection
    .p2align 5
NtGdiCreateDIBSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1092, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDCforBitmap
    .p2align 5
NtGdiGetDCforBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1093, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyCursor
    .p2align 5
NtUserDestroyCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1094, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyWindow
    .p2align 5
NtUserDestroyWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1095, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateDIBitmapInternal
    .p2align 5
NtGdiCreateDIBitmapInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1096, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserOpenWindowStation
    .p2align 5
NtUserOpenWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1097, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCursorIconData
    .p2align 5
NtUserSetCursorIconData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1098, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCloseDesktop
    .p2align 5
NtUserCloseDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1099, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserOpenDesktop
    .p2align 5
NtUserOpenDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessWindowStation
    .p2align 5
NtUserSetProcessWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetAtomName
    .p2align 5
NtUserGetAtomName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtCreatePen
    .p2align 5
NtGdiExtCreatePen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreatePaletteInternal
    .p2align 5
NtGdiCreatePaletteInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetBrushOrg
    .p2align 5
NtGdiSetBrushOrg:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x109F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBuildNameList
    .p2align 5
NtUserBuildNameList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetPixel
    .p2align 5
NtGdiSetPixel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterClassExWOW
    .p2align 5
NtUserRegisterClassExWOW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreatePatternBrushInternal
    .p2align 5
NtGdiCreatePatternBrushInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetAncestor
    .p2align 5
NtUserGetAncestor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetOutlineTextMetricsInternalW
    .p2align 5
NtGdiGetOutlineTextMetricsInternalW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetBitmapBits
    .p2align 5
NtGdiSetBitmapBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCloseWindowStation
    .p2align 5
NtUserCloseWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDoubleClickTime
    .p2align 5
NtUserGetDoubleClickTime:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableScrollBar
    .p2align 5
NtUserEnableScrollBar:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateSolidBrush
    .p2align 5
NtGdiCreateSolidBrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClassInfoEx
    .p2align 5
NtUserGetClassInfoEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateClientObj
    .p2align 5
NtGdiCreateClientObj:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnregisterClass
    .p2align 5
NtUserUnregisterClass:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDeleteMenu
    .p2align 5
NtUserDeleteMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRectInRegion
    .p2align 5
NtGdiRectInRegion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserScrollWindowEx
    .p2align 5
NtUserScrollWindowEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPixel
    .p2align 5
NtGdiGetPixel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetClassLong
    .p2align 5
NtUserSetClassLong:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMenuBarInfo
    .p2align 5
NtUserGetMenuBarInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetNearestPaletteIndex
    .p2align 5
NtGdiGetNearestPaletteIndex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCharWidthW
    .p2align 5
NtGdiGetCharWidthW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInvalidateRgn
    .p2align 5
NtUserInvalidateRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardOwner
    .p2align 5
NtUserGetClipboardOwner:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowRgn
    .p2align 5
NtUserSetWindowRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBitBltSysBmp
    .p2align 5
NtUserBitBltSysBmp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCharWidthInfo
    .p2align 5
NtGdiGetCharWidthInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserValidateRect
    .p2align 5
NtUserValidateRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCloseClipboard
    .p2align 5
NtUserCloseClipboard:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserOpenClipboard
    .p2align 5
NtUserOpenClipboard:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetClipboardData
    .p2align 5
NtUserSetClipboardData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableMenuItem
    .p2align 5
NtUserEnableMenuItem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAlterWindowStyle
    .p2align 5
NtUserAlterWindowStyle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFillRgn
    .p2align 5
NtGdiFillRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowPlacement
    .p2align 5
NtUserGetWindowPlacement:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiModifyWorldTransform
    .p2align 5
NtGdiModifyWorldTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetFontData
    .p2align 5
NtGdiGetFontData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetOpenClipboardWindow
    .p2align 5
NtUserGetOpenClipboardWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetThreadState
    .p2align 5
NtUserSetThreadState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiOpenDCW
    .p2align 5
NtGdiOpenDCW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTrackMouseEvent
    .p2align 5
NtUserTrackMouseEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetTransform
    .p2align 5
NtGdiGetTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyMenu
    .p2align 5
NtUserDestroyMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetBitmapBits
    .p2align 5
NtGdiGetBitmapBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserConsoleControl
    .p2align 5
NtUserConsoleControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetActiveWindow
    .p2align 5
NtUserSetActiveWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetInformationThread
    .p2align 5
NtUserSetInformationThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowPlacement
    .p2align 5
NtUserSetWindowPlacement:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetControlColor
    .p2align 5
NtUserGetControlColor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetMetaRgn
    .p2align 5
NtGdiSetMetaRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetMiterLimit
    .p2align 5
NtGdiSetMiterLimit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetVirtualResolution
    .p2align 5
NtGdiSetVirtualResolution:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetRasterizerCaps
    .p2align 5
NtGdiGetRasterizerCaps:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowWord
    .p2align 5
NtUserSetWindowWord:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardFormatName
    .p2align 5
NtUserGetClipboardFormatName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRealInternalGetMessage
    .p2align 5
NtUserRealInternalGetMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateLocalMemHandle
    .p2align 5
NtUserCreateLocalMemHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAttachThreadInput
    .p2align 5
NtUserAttachThreadInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateHalftonePalette
    .p2align 5
NtGdiCreateHalftonePalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPaintMenuBar
    .p2align 5
NtUserPaintMenuBar:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10DB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetKeyboardState
    .p2align 5
NtUserSetKeyboardState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10DC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCombineTransform
    .p2align 5
NtGdiCombineTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10DD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateAcceleratorTable
    .p2align 5
NtUserCreateAcceleratorTable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10DE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCursorFrameInfo
    .p2align 5
NtUserGetCursorFrameInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10DF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCaretBlinkTime
    .p2align 5
NtUserGetCaretBlinkTime:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiQueryFontAssocInfo
    .p2align 5
NtGdiQueryFontAssocInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserProcessConnect
    .p2align 5
NtUserProcessConnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnumDisplayDevices
    .p2align 5
NtUserEnumDisplayDevices:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEmptyClipboard
    .p2align 5
NtUserEmptyClipboard:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardData
    .p2align 5
NtUserGetClipboardData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoveMenu
    .p2align 5
NtUserRemoveMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetBoundsRect
    .p2align 5
NtGdiSetBoundsRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetBitmapDimension
    .p2align 5
NtGdiGetBitmapDimension:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserConvertMemHandle
    .p2align 5
NtUserConvertMemHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10E9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyAcceleratorTable
    .p2align 5
NtUserDestroyAcceleratorTable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10EA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetGUIThreadInfo
    .p2align 5
NtUserGetGUIThreadInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10EB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCloseFigure
    .p2align 5
NtGdiCloseFigure:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10EC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowsHookAW
    .p2align 5
NtUserSetWindowsHookAW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10ED, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMenuDefaultItem
    .p2align 5
NtUserSetMenuDefaultItem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10EE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCheckMenuItem
    .p2align 5
NtUserCheckMenuItem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10EF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWinEventHook
    .p2align 5
NtUserSetWinEventHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnhookWinEvent
    .p2align 5
NtUserUnhookWinEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLockWindowUpdate
    .p2align 5
NtUserLockWindowUpdate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSystemMenu
    .p2align 5
NtUserSetSystemMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserThunkedMenuInfo
    .p2align 5
NtUserThunkedMenuInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBeginPath
    .p2align 5
NtGdiBeginPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEndPath
    .p2align 5
NtGdiEndPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFillPath
    .p2align 5
NtGdiFillPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDdeInitialize
    .p2align 5
NtUserDdeInitialize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserModifyUserStartupInfoFlags
    .p2align 5
NtUserModifyUserStartupInfoFlags:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10F9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCountClipboardFormats
    .p2align 5
NtUserCountClipboardFormats:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10FA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAddFontMemResourceEx
    .p2align 5
NtGdiAddFontMemResourceEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10FB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEqualRgn
    .p2align 5
NtGdiEqualRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10FC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetSystemPaletteUse
    .p2align 5
NtGdiGetSystemPaletteUse:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10FD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRemoveFontMemResourceEx
    .p2align 5
NtGdiRemoveFontMemResourceEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10FE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnumDisplaySettings
    .p2align 5
NtUserEnumDisplaySettings:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x10FF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPaintDesktop
    .p2align 5
NtUserPaintDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1100, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtEscape
    .p2align 5
NtGdiExtEscape:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1101, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetBitmapDimension
    .p2align 5
NtGdiSetBitmapDimension:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1102, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetFontEnumeration
    .p2align 5
NtGdiSetFontEnumeration:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1103, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserChangeClipboardChain
    .p2align 5
NtUserChangeClipboardChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1104, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetClipboardViewer
    .p2align 5
NtUserSetClipboardViewer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1105, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowWindowAsync
    .p2align 5
NtUserShowWindowAsync:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1106, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateColorSpace
    .p2align 5
NtGdiCreateColorSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1107, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDeleteColorSpace
    .p2align 5
NtGdiDeleteColorSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1108, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserActivateKeyboardLayout
    .p2align 5
NtUserActivateKeyboardLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1109, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtBindCompositionSurface
    .p2align 5
NtBindCompositionSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCloseCompositionInputSink
    .p2align 5
NtCloseCompositionInputSink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompositionSetDropTarget
    .p2align 5
NtCompositionSetDropTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCompositorNotifyExitWindows
    .p2align 5
NtCompositorNotifyExitWindows:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtConfigureInputSpace
    .p2align 5
NtConfigureInputSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtConfirmCompositionSurfaceIndependentFlipEntry
    .p2align 5
NtConfirmCompositionSurfaceIndependentFlipEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x110F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateCompositionInputSink
    .p2align 5
NtCreateCompositionInputSink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1110, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateCompositionSurfaceHandle
    .p2align 5
NtCreateCompositionSurfaceHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1111, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtCreateImplicitCompositionInputSink
    .p2align 5
NtCreateImplicitCompositionInputSink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1112, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionAddCrossDeviceVisualChild
    .p2align 5
NtDCompositionAddCrossDeviceVisualChild:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1113, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionBeginFrame
    .p2align 5
NtDCompositionBeginFrame:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1114, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionBoostCompositorClock
    .p2align 5
NtDCompositionBoostCompositorClock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1115, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCommitChannel
    .p2align 5
NtDCompositionCommitChannel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1116, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCommitSynchronizationObject
    .p2align 5
NtDCompositionCommitSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1117, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionConfirmFrame
    .p2align 5
NtDCompositionConfirmFrame:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1118, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionConnectPipe
    .p2align 5
NtDCompositionConnectPipe:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1119, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCreateAndBindSharedSection
    .p2align 5
NtDCompositionCreateAndBindSharedSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCreateBufferCollection
    .p2align 5
NtDCompositionCreateBufferCollection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCreateChannel
    .p2align 5
NtDCompositionCreateChannel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCreateConnection
    .p2align 5
NtDCompositionCreateConnection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCreateSharedResourceHandle
    .p2align 5
NtDCompositionCreateSharedResourceHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionCreateSynchronizationObject
    .p2align 5
NtDCompositionCreateSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x111F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionDestroyChannel
    .p2align 5
NtDCompositionDestroyChannel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1120, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionDestroyConnection
    .p2align 5
NtDCompositionDestroyConnection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1121, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionDuplicateHandleToProcess
    .p2align 5
NtDCompositionDuplicateHandleToProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1122, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionDuplicateSwapchainHandleToDwm
    .p2align 5
NtDCompositionDuplicateSwapchainHandleToDwm:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1123, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionEnableMMCSS
    .p2align 5
NtDCompositionEnableMMCSS:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1124, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetBatchId
    .p2align 5
NtDCompositionGetBatchId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1125, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetChannels
    .p2align 5
NtDCompositionGetChannels:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1126, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetConnectionBatch
    .p2align 5
NtDCompositionGetConnectionBatch:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1127, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetDeletedResources
    .p2align 5
NtDCompositionGetDeletedResources:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1128, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetFrameId
    .p2align 5
NtDCompositionGetFrameId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1129, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetFrameIdFromBatchId
    .p2align 5
NtDCompositionGetFrameIdFromBatchId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetFrameLegacyTokens
    .p2align 5
NtDCompositionGetFrameLegacyTokens:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetFrameStatistics
    .p2align 5
NtDCompositionGetFrameStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetFrameSurfaceUpdates
    .p2align 5
NtDCompositionGetFrameSurfaceUpdates:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetStatistics
    .p2align 5
NtDCompositionGetStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionGetTargetStatistics
    .p2align 5
NtDCompositionGetTargetStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x112F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionNotifySuperWetInkWork
    .p2align 5
NtDCompositionNotifySuperWetInkWork:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1130, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionProcessChannelBatchBuffer
    .p2align 5
NtDCompositionProcessChannelBatchBuffer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1131, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionRegisterThumbnailVisual
    .p2align 5
NtDCompositionRegisterThumbnailVisual:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1132, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionRegisterVirtualDesktopVisual
    .p2align 5
NtDCompositionRegisterVirtualDesktopVisual:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1133, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionReleaseAllResources
    .p2align 5
NtDCompositionReleaseAllResources:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1134, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionRemoveCrossDeviceVisualChild
    .p2align 5
NtDCompositionRemoveCrossDeviceVisualChild:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1135, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSendDwmLpcMessage
    .p2align 5
NtDCompositionSendDwmLpcMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1136, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSetBlurredWallpaperSurface
    .p2align 5
NtDCompositionSetBlurredWallpaperSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1137, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSetChannelCommitCompletionEvent
    .p2align 5
NtDCompositionSetChannelCommitCompletionEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1138, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSetChannelConnectionId
    .p2align 5
NtDCompositionSetChannelConnectionId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1139, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSetChildRootVisual
    .p2align 5
NtDCompositionSetChildRootVisual:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSetDebugCounter
    .p2align 5
NtDCompositionSetDebugCounter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSuspendAnimations
    .p2align 5
NtDCompositionSuspendAnimations:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSyncWait
    .p2align 5
NtDCompositionSyncWait:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionSynchronize
    .p2align 5
NtDCompositionSynchronize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionTelemetrySetApplicationId
    .p2align 5
NtDCompositionTelemetrySetApplicationId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x113F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionUpdatePointerCapture
    .p2align 5
NtDCompositionUpdatePointerCapture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1140, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionWaitForChannel
    .p2align 5
NtDCompositionWaitForChannel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1141, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDCompositionWaitForCompositorClock
    .p2align 5
NtDCompositionWaitForCompositorClock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1142, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDesktopCaptureBits
    .p2align 5
NtDesktopCaptureBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1143, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDuplicateCompositionInputSink
    .p2align 5
NtDuplicateCompositionInputSink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1144, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkAcquireExclusiveEngineAccess
    .p2align 5
NtDxgkAcquireExclusiveEngineAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1145, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkCancelPresents
    .p2align 5
NtDxgkCancelPresents:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1146, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkCheckEngineAccess
    .p2align 5
NtDxgkCheckEngineAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1147, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkCheckSinglePlaneForMultiPlaneOverlaySupport
    .p2align 5
NtDxgkCheckSinglePlaneForMultiPlaneOverlaySupport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1148, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkConnectDoorbell
    .p2align 5
NtDxgkConnectDoorbell:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1149, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkCreateDoorbell
    .p2align 5
NtDxgkCreateDoorbell:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkCreateNativeFence
    .p2align 5
NtDxgkCreateNativeFence:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkCreateTrackedWorkload
    .p2align 5
NtDxgkCreateTrackedWorkload:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDDisplayEnum2
    .p2align 5
NtDxgkDDisplayEnum2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDestroyDoorbell
    .p2align 5
NtDxgkDestroyDoorbell:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDestroyTrackedWorkload
    .p2align 5
NtDxgkDestroyTrackedWorkload:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x114F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDisableProcessDebugBlobCollection
    .p2align 5
NtDxgkDisableProcessDebugBlobCollection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1150, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDispMgrOperation
    .p2align 5
NtDxgkDispMgrOperation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1151, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDisplayMuxSwitchExecute
    .p2align 5
NtDxgkDisplayMuxSwitchExecute:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1152, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDisplayMuxSwitchFinish
    .p2align 5
NtDxgkDisplayMuxSwitchFinish:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1153, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDisplayMuxSwitchPrepare
    .p2align 5
NtDxgkDisplayMuxSwitchPrepare:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1154, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDisplayPortOperation
    .p2align 5
NtDxgkDisplayPortOperation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1155, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkDuplicateHandle
    .p2align 5
NtDxgkDuplicateHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1156, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkEnableProcessDebugBlobCollection
    .p2align 5
NtDxgkEnableProcessDebugBlobCollection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1157, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkEnumAdapters3
    .p2align 5
NtDxgkEnumAdapters3:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1158, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkEnumProcesses
    .p2align 5
NtDxgkEnumProcesses:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1159, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkGetAvailableTrackedWorkloadIndex
    .p2align 5
NtDxgkGetAvailableTrackedWorkloadIndex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkGetNativeFenceLogDetail
    .p2align 5
NtDxgkGetNativeFenceLogDetail:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkGetProcessList
    .p2align 5
NtDxgkGetProcessList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkGetProperties
    .p2align 5
NtDxgkGetProperties:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkGetTrackedWorkloadStatistics
    .p2align 5
NtDxgkGetTrackedWorkloadStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkIsFeatureEnabled
    .p2align 5
NtDxgkIsFeatureEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x115F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkMapProcessDebugBlob
    .p2align 5
NtDxgkMapProcessDebugBlob:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1160, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkNotifyWorkSubmission
    .p2align 5
NtDxgkNotifyWorkSubmission:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1161, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkOpenNativeFenceFromNtHandle
    .p2align 5
NtDxgkOpenNativeFenceFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1162, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkOutputDuplPresentToHwQueue
    .p2align 5
NtDxgkOutputDuplPresentToHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1163, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkPinResources
    .p2align 5
NtDxgkPinResources:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1164, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkRegisterVailProcess
    .p2align 5
NtDxgkRegisterVailProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1165, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkResetTrackedWorkloadStatistics
    .p2align 5
NtDxgkResetTrackedWorkloadStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1166, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkSetProperties
    .p2align 5
NtDxgkSetProperties:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1167, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkSubmitPresentBltToHwQueue
    .p2align 5
NtDxgkSubmitPresentBltToHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1168, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkSubmitPresentToHwQueue
    .p2align 5
NtDxgkSubmitPresentToHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1169, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkUnmapProcessDebugBlob
    .p2align 5
NtDxgkUnmapProcessDebugBlob:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkUnpinResources
    .p2align 5
NtDxgkUnpinResources:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkUpdateTrackedWorkload
    .p2align 5
NtDxgkUpdateTrackedWorkload:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkVailConnect
    .p2align 5
NtDxgkVailConnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkVailDisconnect
    .p2align 5
NtDxgkVailDisconnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtDxgkVailPromoteCompositionSurface
    .p2align 5
NtDxgkVailPromoteCompositionSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x116F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtEnableOneCoreTransformMode
    .p2align 5
NtEnableOneCoreTransformMode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1170, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectAddContent
    .p2align 5
NtFlipObjectAddContent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1171, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectAddPoolBuffer
    .p2align 5
NtFlipObjectAddPoolBuffer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1172, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectConsumerAcquirePresent
    .p2align 5
NtFlipObjectConsumerAcquirePresent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1173, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectConsumerAdjustUsageReference
    .p2align 5
NtFlipObjectConsumerAdjustUsageReference:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1174, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectConsumerBeginProcessPresent
    .p2align 5
NtFlipObjectConsumerBeginProcessPresent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1175, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectConsumerEndProcessPresent
    .p2align 5
NtFlipObjectConsumerEndProcessPresent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1176, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectConsumerPostMessage
    .p2align 5
NtFlipObjectConsumerPostMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1177, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectConsumerQueryBufferInfo
    .p2align 5
NtFlipObjectConsumerQueryBufferInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1178, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectCreate
    .p2align 5
NtFlipObjectCreate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1179, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectDisconnectEndpoint
    .p2align 5
NtFlipObjectDisconnectEndpoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectEnablePresentStatisticsType
    .p2align 5
NtFlipObjectEnablePresentStatisticsType:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectOpen
    .p2align 5
NtFlipObjectOpen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectPresentCancel
    .p2align 5
NtFlipObjectPresentCancel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectQueryBufferAvailableEvent
    .p2align 5
NtFlipObjectQueryBufferAvailableEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectQueryEndpointConnected
    .p2align 5
NtFlipObjectQueryEndpointConnected:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x117F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectQueryLostEvent
    .p2align 5
NtFlipObjectQueryLostEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1180, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectQueryNextMessageToProducer
    .p2align 5
NtFlipObjectQueryNextMessageToProducer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1181, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectReadNextMessageToProducer
    .p2align 5
NtFlipObjectReadNextMessageToProducer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1182, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectRemoveContent
    .p2align 5
NtFlipObjectRemoveContent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1183, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectRemovePoolBuffer
    .p2align 5
NtFlipObjectRemovePoolBuffer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1184, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtFlipObjectSetContent
    .p2align 5
NtFlipObjectSetContent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1185, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAbortDoc
    .p2align 5
NtGdiAbortDoc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1186, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAbortPath
    .p2align 5
NtGdiAbortPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1187, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAddEmbFontToDC
    .p2align 5
NtGdiAddEmbFontToDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1188, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAddFontResourceW
    .p2align 5
NtGdiAddFontResourceW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1189, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAddInitialFonts
    .p2align 5
NtGdiAddInitialFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAddRemoteFontToDC
    .p2align 5
NtGdiAddRemoteFontToDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAddRemoteMMInstanceToDC
    .p2align 5
NtGdiAddRemoteMMInstanceToDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAngleArc
    .p2align 5
NtGdiAngleArc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiAnyLinkedFonts
    .p2align 5
NtGdiAnyLinkedFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiArcInternal
    .p2align 5
NtGdiArcInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x118F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBRUSHOBJ_DeleteRbrush
    .p2align 5
NtGdiBRUSHOBJ_DeleteRbrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1190, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBRUSHOBJ_hGetColorTransform
    .p2align 5
NtGdiBRUSHOBJ_hGetColorTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1191, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBRUSHOBJ_pvAllocRbrush
    .p2align 5
NtGdiBRUSHOBJ_pvAllocRbrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1192, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBRUSHOBJ_pvGetRbrush
    .p2align 5
NtGdiBRUSHOBJ_pvGetRbrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1193, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBRUSHOBJ_ulGetBrushColor
    .p2align 5
NtGdiBRUSHOBJ_ulGetBrushColor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1194, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiBeginGdiRendering
    .p2align 5
NtGdiBeginGdiRendering:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1195, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCLIPOBJ_bEnum
    .p2align 5
NtGdiCLIPOBJ_bEnum:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1196, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCLIPOBJ_cEnumStart
    .p2align 5
NtGdiCLIPOBJ_cEnumStart:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1197, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCLIPOBJ_ppoGetPath
    .p2align 5
NtGdiCLIPOBJ_ppoGetPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1198, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCancelDC
    .p2align 5
NtGdiCancelDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1199, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiChangeGhostFont
    .p2align 5
NtGdiChangeGhostFont:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCheckBitmapBits
    .p2align 5
NtGdiCheckBitmapBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiClearBitmapAttributes
    .p2align 5
NtGdiClearBitmapAttributes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiClearBrushAttributes
    .p2align 5
NtGdiClearBrushAttributes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiColorCorrectPalette
    .p2align 5
NtGdiColorCorrectPalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiConfigureOPMProtectedOutput
    .p2align 5
NtGdiConfigureOPMProtectedOutput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x119F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiConvertMetafileRect
    .p2align 5
NtGdiConvertMetafileRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateBitmapFromDxSurface
    .p2align 5
NtGdiCreateBitmapFromDxSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateBitmapFromDxSurface2
    .p2align 5
NtGdiCreateBitmapFromDxSurface2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateColorTransform
    .p2align 5
NtGdiCreateColorTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateEllipticRgn
    .p2align 5
NtGdiCreateEllipticRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateHatchBrushInternal
    .p2align 5
NtGdiCreateHatchBrushInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateMetafileDC
    .p2align 5
NtGdiCreateMetafileDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateOPMProtectedOutput
    .p2align 5
NtGdiCreateOPMProtectedOutput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateOPMProtectedOutputs
    .p2align 5
NtGdiCreateOPMProtectedOutputs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateRoundRectRgn
    .p2align 5
NtGdiCreateRoundRectRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateServerMetaFile
    .p2align 5
NtGdiCreateServerMetaFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiCreateSessionMappedDIBSection
    .p2align 5
NtGdiCreateSessionMappedDIBSection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDDCCIGetCapabilitiesString
    .p2align 5
NtGdiDDCCIGetCapabilitiesString:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDDCCIGetCapabilitiesStringLength
    .p2align 5
NtGdiDDCCIGetCapabilitiesStringLength:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDDCCIGetTimingReport
    .p2align 5
NtGdiDDCCIGetTimingReport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDDCCIGetVCPFeature
    .p2align 5
NtGdiDDCCIGetVCPFeature:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDDCCISaveCurrentSettings
    .p2align 5
NtGdiDDCCISaveCurrentSettings:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDDCCISetVCPFeature
    .p2align 5
NtGdiDDCCISetVCPFeature:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdCreateFullscreenSprite
    .p2align 5
NtGdiDdCreateFullscreenSprite:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIAbandonSwapChain
    .p2align 5
NtGdiDdDDIAbandonSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIAcquireKeyedMutex
    .p2align 5
NtGdiDdDDIAcquireKeyedMutex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIAcquireKeyedMutex2
    .p2align 5
NtGdiDdDDIAcquireKeyedMutex2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIAcquireSwapChain
    .p2align 5
NtGdiDdDDIAcquireSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIAddSurfaceToSwapChain
    .p2align 5
NtGdiDdDDIAddSurfaceToSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIAdjustFullscreenGamma
    .p2align 5
NtGdiDdDDIAdjustFullscreenGamma:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICacheHybridQueryValue
    .p2align 5
NtGdiDdDDICacheHybridQueryValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIChangeVideoMemoryReservation
    .p2align 5
NtGdiDdDDIChangeVideoMemoryReservation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckExclusiveOwnership
    .p2align 5
NtGdiDdDDICheckExclusiveOwnership:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckMonitorPowerState
    .p2align 5
NtGdiDdDDICheckMonitorPowerState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckMultiPlaneOverlaySupport
    .p2align 5
NtGdiDdDDICheckMultiPlaneOverlaySupport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckMultiPlaneOverlaySupport2
    .p2align 5
NtGdiDdDDICheckMultiPlaneOverlaySupport2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckMultiPlaneOverlaySupport3
    .p2align 5
NtGdiDdDDICheckMultiPlaneOverlaySupport3:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckOcclusion
    .p2align 5
NtGdiDdDDICheckOcclusion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckSharedResourceAccess
    .p2align 5
NtGdiDdDDICheckSharedResourceAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICheckVidPnExclusiveOwnership
    .p2align 5
NtGdiDdDDICheckVidPnExclusiveOwnership:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICloseAdapter
    .p2align 5
NtGdiDdDDICloseAdapter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIConfigureSharedResource
    .p2align 5
NtGdiDdDDIConfigureSharedResource:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateAllocation
    .p2align 5
NtGdiDdDDICreateAllocation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateBundleObject
    .p2align 5
NtGdiDdDDICreateBundleObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateContext
    .p2align 5
NtGdiDdDDICreateContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateContextVirtual
    .p2align 5
NtGdiDdDDICreateContextVirtual:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateDCFromMemory
    .p2align 5
NtGdiDdDDICreateDCFromMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateDevice
    .p2align 5
NtGdiDdDDICreateDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateHwContext
    .p2align 5
NtGdiDdDDICreateHwContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateHwQueue
    .p2align 5
NtGdiDdDDICreateHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateKeyedMutex
    .p2align 5
NtGdiDdDDICreateKeyedMutex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateKeyedMutex2
    .p2align 5
NtGdiDdDDICreateKeyedMutex2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateOutputDupl
    .p2align 5
NtGdiDdDDICreateOutputDupl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateOverlay
    .p2align 5
NtGdiDdDDICreateOverlay:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreatePagingQueue
    .p2align 5
NtGdiDdDDICreatePagingQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateProtectedSession
    .p2align 5
NtGdiDdDDICreateProtectedSession:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateSwapChain
    .p2align 5
NtGdiDdDDICreateSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDICreateSynchronizationObject
    .p2align 5
NtGdiDdDDICreateSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDDisplayEnum
    .p2align 5
NtGdiDdDDIDDisplayEnum:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyAllocation
    .p2align 5
NtGdiDdDDIDestroyAllocation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyAllocation2
    .p2align 5
NtGdiDdDDIDestroyAllocation2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyContext
    .p2align 5
NtGdiDdDDIDestroyContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyDCFromMemory
    .p2align 5
NtGdiDdDDIDestroyDCFromMemory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyDevice
    .p2align 5
NtGdiDdDDIDestroyDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyHwContext
    .p2align 5
NtGdiDdDDIDestroyHwContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11DB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyHwQueue
    .p2align 5
NtGdiDdDDIDestroyHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11DC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyKeyedMutex
    .p2align 5
NtGdiDdDDIDestroyKeyedMutex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11DD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyOutputDupl
    .p2align 5
NtGdiDdDDIDestroyOutputDupl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11DE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyOverlay
    .p2align 5
NtGdiDdDDIDestroyOverlay:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11DF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyPagingQueue
    .p2align 5
NtGdiDdDDIDestroyPagingQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroyProtectedSession
    .p2align 5
NtGdiDdDDIDestroyProtectedSession:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDestroySynchronizationObject
    .p2align 5
NtGdiDdDDIDestroySynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDispMgrCreate
    .p2align 5
NtGdiDdDDIDispMgrCreate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDispMgrSourceOperation
    .p2align 5
NtGdiDdDDIDispMgrSourceOperation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIDispMgrTargetOperation
    .p2align 5
NtGdiDdDDIDispMgrTargetOperation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIEnumAdapters
    .p2align 5
NtGdiDdDDIEnumAdapters:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIEnumAdapters2
    .p2align 5
NtGdiDdDDIEnumAdapters2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIEscape
    .p2align 5
NtGdiDdDDIEscape:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIEvict
    .p2align 5
NtGdiDdDDIEvict:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11E9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIExtractBundleObject
    .p2align 5
NtGdiDdDDIExtractBundleObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11EA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIFlipOverlay
    .p2align 5
NtGdiDdDDIFlipOverlay:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11EB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIFlushHeapTransitions
    .p2align 5
NtGdiDdDDIFlushHeapTransitions:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11EC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIFreeGpuVirtualAddress
    .p2align 5
NtGdiDdDDIFreeGpuVirtualAddress:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11ED, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetAllocationPriority
    .p2align 5
NtGdiDdDDIGetAllocationPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11EE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetCachedHybridQueryValue
    .p2align 5
NtGdiDdDDIGetCachedHybridQueryValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11EF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetContextInProcessSchedulingPriority
    .p2align 5
NtGdiDdDDIGetContextInProcessSchedulingPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetContextSchedulingPriority
    .p2align 5
NtGdiDdDDIGetContextSchedulingPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetDWMVerticalBlankEvent
    .p2align 5
NtGdiDdDDIGetDWMVerticalBlankEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetDeviceState
    .p2align 5
NtGdiDdDDIGetDeviceState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetDisplayModeList
    .p2align 5
NtGdiDdDDIGetDisplayModeList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetMemoryBudgetTarget
    .p2align 5
NtGdiDdDDIGetMemoryBudgetTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetMultiPlaneOverlayCaps
    .p2align 5
NtGdiDdDDIGetMultiPlaneOverlayCaps:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetMultisampleMethodList
    .p2align 5
NtGdiDdDDIGetMultisampleMethodList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetOverlayState
    .p2align 5
NtGdiDdDDIGetOverlayState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetPostCompositionCaps
    .p2align 5
NtGdiDdDDIGetPostCompositionCaps:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11F9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetPresentHistory
    .p2align 5
NtGdiDdDDIGetPresentHistory:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11FA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetPresentQueueEvent
    .p2align 5
NtGdiDdDDIGetPresentQueueEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11FB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetProcessDeviceRemovalSupport
    .p2align 5
NtGdiDdDDIGetProcessDeviceRemovalSupport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11FC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetProcessSchedulingPriorityBand
    .p2align 5
NtGdiDdDDIGetProcessSchedulingPriorityBand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11FD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetProcessSchedulingPriorityClass
    .p2align 5
NtGdiDdDDIGetProcessSchedulingPriorityClass:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11FE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetResourcePresentPrivateDriverData
    .p2align 5
NtGdiDdDDIGetResourcePresentPrivateDriverData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x11FF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetRuntimeData
    .p2align 5
NtGdiDdDDIGetRuntimeData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1200, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetScanLine
    .p2align 5
NtGdiDdDDIGetScanLine:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1201, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetSetSwapChainMetadata
    .p2align 5
NtGdiDdDDIGetSetSwapChainMetadata:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1202, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetSharedPrimaryHandle
    .p2align 5
NtGdiDdDDIGetSharedPrimaryHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1203, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetSharedResourceAdapterLuid
    .p2align 5
NtGdiDdDDIGetSharedResourceAdapterLuid:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1204, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetSharedResourceAdapterLuidFlipManager
    .p2align 5
NtGdiDdDDIGetSharedResourceAdapterLuidFlipManager:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1205, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetSwapChainSurfacePhysicalAddress
    .p2align 5
NtGdiDdDDIGetSwapChainSurfacePhysicalAddress:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1206, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIGetYieldPercentage
    .p2align 5
NtGdiDdDDIGetYieldPercentage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1207, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIInvalidateActiveVidPn
    .p2align 5
NtGdiDdDDIInvalidateActiveVidPn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1208, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIInvalidateCache
    .p2align 5
NtGdiDdDDIInvalidateCache:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1209, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDILock
    .p2align 5
NtGdiDdDDILock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDILock2
    .p2align 5
NtGdiDdDDILock2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIMakeResident
    .p2align 5
NtGdiDdDDIMakeResident:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIMapGpuVirtualAddress
    .p2align 5
NtGdiDdDDIMapGpuVirtualAddress:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIMarkDeviceAsError
    .p2align 5
NtGdiDdDDIMarkDeviceAsError:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDINetDispGetNextChunkInfo
    .p2align 5
NtGdiDdDDINetDispGetNextChunkInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x120F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDINetDispQueryMiracastDisplayDeviceStatus
    .p2align 5
NtGdiDdDDINetDispQueryMiracastDisplayDeviceStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1210, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDINetDispQueryMiracastDisplayDeviceSupport
    .p2align 5
NtGdiDdDDINetDispQueryMiracastDisplayDeviceSupport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1211, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDINetDispStartMiracastDisplayDevice
    .p2align 5
NtGdiDdDDINetDispStartMiracastDisplayDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1212, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDINetDispStopMiracastDisplayDevice
    .p2align 5
NtGdiDdDDINetDispStopMiracastDisplayDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1213, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOfferAllocations
    .p2align 5
NtGdiDdDDIOfferAllocations:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1214, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenAdapterFromDeviceName
    .p2align 5
NtGdiDdDDIOpenAdapterFromDeviceName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1215, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenAdapterFromHdc
    .p2align 5
NtGdiDdDDIOpenAdapterFromHdc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1216, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenAdapterFromLuid
    .p2align 5
NtGdiDdDDIOpenAdapterFromLuid:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1217, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenBundleObjectNtHandleFromName
    .p2align 5
NtGdiDdDDIOpenBundleObjectNtHandleFromName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1218, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenKeyedMutex
    .p2align 5
NtGdiDdDDIOpenKeyedMutex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1219, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenKeyedMutex2
    .p2align 5
NtGdiDdDDIOpenKeyedMutex2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenKeyedMutexFromNtHandle
    .p2align 5
NtGdiDdDDIOpenKeyedMutexFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenNtHandleFromName
    .p2align 5
NtGdiDdDDIOpenNtHandleFromName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenProtectedSessionFromNtHandle
    .p2align 5
NtGdiDdDDIOpenProtectedSessionFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenResource
    .p2align 5
NtGdiDdDDIOpenResource:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenResourceFromNtHandle
    .p2align 5
NtGdiDdDDIOpenResourceFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x121F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenSwapChain
    .p2align 5
NtGdiDdDDIOpenSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1220, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenSyncObjectFromNtHandle
    .p2align 5
NtGdiDdDDIOpenSyncObjectFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1221, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenSyncObjectFromNtHandle2
    .p2align 5
NtGdiDdDDIOpenSyncObjectFromNtHandle2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1222, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenSyncObjectNtHandleFromName
    .p2align 5
NtGdiDdDDIOpenSyncObjectNtHandleFromName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1223, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOpenSynchronizationObject
    .p2align 5
NtGdiDdDDIOpenSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1224, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOutputDuplGetFrameInfo
    .p2align 5
NtGdiDdDDIOutputDuplGetFrameInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1225, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOutputDuplGetMetaData
    .p2align 5
NtGdiDdDDIOutputDuplGetMetaData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1226, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOutputDuplGetPointerShapeData
    .p2align 5
NtGdiDdDDIOutputDuplGetPointerShapeData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1227, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOutputDuplPresent
    .p2align 5
NtGdiDdDDIOutputDuplPresent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1228, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIOutputDuplReleaseFrame
    .p2align 5
NtGdiDdDDIOutputDuplReleaseFrame:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1229, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIPollDisplayChildren
    .p2align 5
NtGdiDdDDIPollDisplayChildren:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIPresent
    .p2align 5
NtGdiDdDDIPresent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIPresentMultiPlaneOverlay
    .p2align 5
NtGdiDdDDIPresentMultiPlaneOverlay:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIPresentMultiPlaneOverlay2
    .p2align 5
NtGdiDdDDIPresentMultiPlaneOverlay2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIPresentMultiPlaneOverlay3
    .p2align 5
NtGdiDdDDIPresentMultiPlaneOverlay3:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIPresentRedirected
    .p2align 5
NtGdiDdDDIPresentRedirected:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x122F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryAdapterInfo
    .p2align 5
NtGdiDdDDIQueryAdapterInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1230, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryAllocationResidency
    .p2align 5
NtGdiDdDDIQueryAllocationResidency:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1231, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryClockCalibration
    .p2align 5
NtGdiDdDDIQueryClockCalibration:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1232, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryFSEBlock
    .p2align 5
NtGdiDdDDIQueryFSEBlock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1233, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryProcessOfferInfo
    .p2align 5
NtGdiDdDDIQueryProcessOfferInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1234, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryProtectedSessionInfoFromNtHandle
    .p2align 5
NtGdiDdDDIQueryProtectedSessionInfoFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1235, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryProtectedSessionStatus
    .p2align 5
NtGdiDdDDIQueryProtectedSessionStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1236, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName
    .p2align 5
NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1237, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryResourceInfo
    .p2align 5
NtGdiDdDDIQueryResourceInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1238, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryResourceInfoFromNtHandle
    .p2align 5
NtGdiDdDDIQueryResourceInfoFromNtHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1239, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryStatistics
    .p2align 5
NtGdiDdDDIQueryStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryVidPnExclusiveOwnership
    .p2align 5
NtGdiDdDDIQueryVidPnExclusiveOwnership:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIQueryVideoMemoryInfo
    .p2align 5
NtGdiDdDDIQueryVideoMemoryInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReclaimAllocations
    .p2align 5
NtGdiDdDDIReclaimAllocations:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReclaimAllocations2
    .p2align 5
NtGdiDdDDIReclaimAllocations2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReleaseKeyedMutex
    .p2align 5
NtGdiDdDDIReleaseKeyedMutex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x123F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReleaseKeyedMutex2
    .p2align 5
NtGdiDdDDIReleaseKeyedMutex2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1240, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReleaseProcessVidPnSourceOwners
    .p2align 5
NtGdiDdDDIReleaseProcessVidPnSourceOwners:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1241, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReleaseSwapChain
    .p2align 5
NtGdiDdDDIReleaseSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1242, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIRemoveSurfaceFromSwapChain
    .p2align 5
NtGdiDdDDIRemoveSurfaceFromSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1243, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIRender
    .p2align 5
NtGdiDdDDIRender:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1244, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIReserveGpuVirtualAddress
    .p2align 5
NtGdiDdDDIReserveGpuVirtualAddress:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1245, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetAllocationPriority
    .p2align 5
NtGdiDdDDISetAllocationPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1246, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetContextInProcessSchedulingPriority
    .p2align 5
NtGdiDdDDISetContextInProcessSchedulingPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1247, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetContextSchedulingPriority
    .p2align 5
NtGdiDdDDISetContextSchedulingPriority:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1248, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetDisplayMode
    .p2align 5
NtGdiDdDDISetDisplayMode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1249, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetDodIndirectSwapchain
    .p2align 5
NtGdiDdDDISetDodIndirectSwapchain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetFSEBlock
    .p2align 5
NtGdiDdDDISetFSEBlock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetGammaRamp
    .p2align 5
NtGdiDdDDISetGammaRamp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetHwProtectionTeardownRecovery
    .p2align 5
NtGdiDdDDISetHwProtectionTeardownRecovery:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetMemoryBudgetTarget
    .p2align 5
NtGdiDdDDISetMemoryBudgetTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetMonitorColorSpaceTransform
    .p2align 5
NtGdiDdDDISetMonitorColorSpaceTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x124F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetProcessDeviceRemovalSupport
    .p2align 5
NtGdiDdDDISetProcessDeviceRemovalSupport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1250, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetProcessSchedulingPriorityBand
    .p2align 5
NtGdiDdDDISetProcessSchedulingPriorityBand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1251, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetProcessSchedulingPriorityClass
    .p2align 5
NtGdiDdDDISetProcessSchedulingPriorityClass:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1252, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetQueuedLimit
    .p2align 5
NtGdiDdDDISetQueuedLimit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1253, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetStablePowerState
    .p2align 5
NtGdiDdDDISetStablePowerState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1254, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetStereoEnabled
    .p2align 5
NtGdiDdDDISetStereoEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1255, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetSyncRefreshCountWaitTarget
    .p2align 5
NtGdiDdDDISetSyncRefreshCountWaitTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1256, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetVidPnSourceHwProtection
    .p2align 5
NtGdiDdDDISetVidPnSourceHwProtection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1257, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetVidPnSourceOwner
    .p2align 5
NtGdiDdDDISetVidPnSourceOwner:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1258, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISetYieldPercentage
    .p2align 5
NtGdiDdDDISetYieldPercentage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1259, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIShareObjects
    .p2align 5
NtGdiDdDDIShareObjects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISharedPrimaryLockNotification
    .p2align 5
NtGdiDdDDISharedPrimaryLockNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISharedPrimaryUnLockNotification
    .p2align 5
NtGdiDdDDISharedPrimaryUnLockNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISignalSynchronizationObject
    .p2align 5
NtGdiDdDDISignalSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISignalSynchronizationObjectFromCpu
    .p2align 5
NtGdiDdDDISignalSynchronizationObjectFromCpu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISignalSynchronizationObjectFromGpu
    .p2align 5
NtGdiDdDDISignalSynchronizationObjectFromGpu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x125F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISignalSynchronizationObjectFromGpu2
    .p2align 5
NtGdiDdDDISignalSynchronizationObjectFromGpu2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1260, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISubmitCommand
    .p2align 5
NtGdiDdDDISubmitCommand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1261, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISubmitCommandToHwQueue
    .p2align 5
NtGdiDdDDISubmitCommandToHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1262, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISubmitSignalSyncObjectsToHwQueue
    .p2align 5
NtGdiDdDDISubmitSignalSyncObjectsToHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1263, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue
    .p2align 5
NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1264, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDITrimProcessCommitment
    .p2align 5
NtGdiDdDDITrimProcessCommitment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1265, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIUnOrderedPresentSwapChain
    .p2align 5
NtGdiDdDDIUnOrderedPresentSwapChain:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1266, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIUnlock
    .p2align 5
NtGdiDdDDIUnlock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1267, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIUnlock2
    .p2align 5
NtGdiDdDDIUnlock2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1268, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIUpdateAllocationProperty
    .p2align 5
NtGdiDdDDIUpdateAllocationProperty:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1269, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIUpdateGpuVirtualAddress
    .p2align 5
NtGdiDdDDIUpdateGpuVirtualAddress:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIUpdateOverlay
    .p2align 5
NtGdiDdDDIUpdateOverlay:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIWaitForIdle
    .p2align 5
NtGdiDdDDIWaitForIdle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIWaitForSynchronizationObject
    .p2align 5
NtGdiDdDDIWaitForSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIWaitForSynchronizationObjectFromCpu
    .p2align 5
NtGdiDdDDIWaitForSynchronizationObjectFromCpu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIWaitForSynchronizationObjectFromGpu
    .p2align 5
NtGdiDdDDIWaitForSynchronizationObjectFromGpu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x126F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIWaitForVerticalBlankEvent
    .p2align 5
NtGdiDdDDIWaitForVerticalBlankEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1270, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDDIWaitForVerticalBlankEvent2
    .p2align 5
NtGdiDdDDIWaitForVerticalBlankEvent2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1271, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdDestroyFullscreenSprite
    .p2align 5
NtGdiDdDestroyFullscreenSprite:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1272, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdNotifyFullscreenSpriteUpdate
    .p2align 5
NtGdiDdNotifyFullscreenSpriteUpdate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1273, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDdQueryVisRgnUniqueness
    .p2align 5
NtGdiDdQueryVisRgnUniqueness:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1274, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDeleteColorTransform
    .p2align 5
NtGdiDeleteColorTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1275, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDescribePixelFormat
    .p2align 5
NtGdiDescribePixelFormat:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1276, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDestroyOPMProtectedOutput
    .p2align 5
NtGdiDestroyOPMProtectedOutput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1277, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDestroyPhysicalMonitor
    .p2align 5
NtGdiDestroyPhysicalMonitor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1278, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDisableUMPDSandboxing
    .p2align 5
NtGdiDisableUMPDSandboxing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1279, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDoBanding
    .p2align 5
NtGdiDoBanding:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDrawEscape
    .p2align 5
NtGdiDrawEscape:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiDwmCreatedBitmapRemotingOutput
    .p2align 5
NtGdiDwmCreatedBitmapRemotingOutput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEllipse
    .p2align 5
NtGdiEllipse:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEnableEudc
    .p2align 5
NtGdiEnableEudc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEndDoc
    .p2align 5
NtGdiEndDoc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x127F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEndGdiRendering
    .p2align 5
NtGdiEndGdiRendering:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1280, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEndPage
    .p2align 5
NtGdiEndPage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1281, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngAlphaBlend
    .p2align 5
NtGdiEngAlphaBlend:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1282, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngAssociateSurface
    .p2align 5
NtGdiEngAssociateSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1283, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngBitBlt
    .p2align 5
NtGdiEngBitBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1284, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCheckAbort
    .p2align 5
NtGdiEngCheckAbort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1285, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngComputeGlyphSet
    .p2align 5
NtGdiEngComputeGlyphSet:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1286, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCopyBits
    .p2align 5
NtGdiEngCopyBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1287, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCreateBitmap
    .p2align 5
NtGdiEngCreateBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1288, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCreateClip
    .p2align 5
NtGdiEngCreateClip:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1289, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCreateDeviceBitmap
    .p2align 5
NtGdiEngCreateDeviceBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCreateDeviceSurface
    .p2align 5
NtGdiEngCreateDeviceSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngCreatePalette
    .p2align 5
NtGdiEngCreatePalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngDeleteClip
    .p2align 5
NtGdiEngDeleteClip:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngDeletePalette
    .p2align 5
NtGdiEngDeletePalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngDeletePath
    .p2align 5
NtGdiEngDeletePath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x128F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngDeleteSurface
    .p2align 5
NtGdiEngDeleteSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1290, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngEraseSurface
    .p2align 5
NtGdiEngEraseSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1291, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngFillPath
    .p2align 5
NtGdiEngFillPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1292, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngGradientFill
    .p2align 5
NtGdiEngGradientFill:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1293, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngLineTo
    .p2align 5
NtGdiEngLineTo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1294, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngLockSurface
    .p2align 5
NtGdiEngLockSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1295, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngMarkBandingSurface
    .p2align 5
NtGdiEngMarkBandingSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1296, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngPaint
    .p2align 5
NtGdiEngPaint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1297, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngPlgBlt
    .p2align 5
NtGdiEngPlgBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1298, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngStretchBlt
    .p2align 5
NtGdiEngStretchBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1299, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngStretchBltROP
    .p2align 5
NtGdiEngStretchBltROP:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngStrokeAndFillPath
    .p2align 5
NtGdiEngStrokeAndFillPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngStrokePath
    .p2align 5
NtGdiEngStrokePath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngTextOut
    .p2align 5
NtGdiEngTextOut:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngTransparentBlt
    .p2align 5
NtGdiEngTransparentBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEngUnlockSurface
    .p2align 5
NtGdiEngUnlockSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x129F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEnsureDpiDepDefaultGuiFontForPlateau
    .p2align 5
NtGdiEnsureDpiDepDefaultGuiFontForPlateau:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEnumFonts
    .p2align 5
NtGdiEnumFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEnumObjects
    .p2align 5
NtGdiEnumObjects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiEudcLoadUnloadLink
    .p2align 5
NtGdiEudcLoadUnloadLink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiExtFloodFill
    .p2align 5
NtGdiExtFloodFill:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_cGetAllGlyphHandles
    .p2align 5
NtGdiFONTOBJ_cGetAllGlyphHandles:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_cGetGlyphs
    .p2align 5
NtGdiFONTOBJ_cGetGlyphs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_pQueryGlyphAttrs
    .p2align 5
NtGdiFONTOBJ_pQueryGlyphAttrs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_pfdg
    .p2align 5
NtGdiFONTOBJ_pfdg:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_pifi
    .p2align 5
NtGdiFONTOBJ_pifi:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_pvTrueTypeFontFile
    .p2align 5
NtGdiFONTOBJ_pvTrueTypeFontFile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_pxoGetXform
    .p2align 5
NtGdiFONTOBJ_pxoGetXform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFONTOBJ_vGetInfo
    .p2align 5
NtGdiFONTOBJ_vGetInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFlattenPath
    .p2align 5
NtGdiFlattenPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFontIsLinked
    .p2align 5
NtGdiFontIsLinked:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiForceUFIMapping
    .p2align 5
NtGdiForceUFIMapping:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFrameRgn
    .p2align 5
NtGdiFrameRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiFullscreenControl
    .p2align 5
NtGdiFullscreenControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetBitmapDpiScaleValue
    .p2align 5
NtGdiGetBitmapDpiScaleValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetBoundsRect
    .p2align 5
NtGdiGetBoundsRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCOPPCompatibleOPMInformation
    .p2align 5
NtGdiGetCOPPCompatibleOPMInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCertificate
    .p2align 5
NtGdiGetCertificate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCertificateByHandle
    .p2align 5
NtGdiGetCertificateByHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCertificateSize
    .p2align 5
NtGdiGetCertificateSize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCertificateSizeByHandle
    .p2align 5
NtGdiGetCertificateSizeByHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCharABCWidthsW
    .p2align 5
NtGdiGetCharABCWidthsW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCharacterPlacementW
    .p2align 5
NtGdiGetCharacterPlacementW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetColorAdjustment
    .p2align 5
NtGdiGetColorAdjustment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetColorSpaceforBitmap
    .p2align 5
NtGdiGetColorSpaceforBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetCurrentDpiInfo
    .p2align 5
NtGdiGetCurrentDpiInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDCDpiScaleValue
    .p2align 5
NtGdiGetDCDpiScaleValue:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDeviceCaps
    .p2align 5
NtGdiGetDeviceCaps:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDeviceCapsAll
    .p2align 5
NtGdiGetDeviceCapsAll:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDeviceWidth
    .p2align 5
NtGdiGetDeviceWidth:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetDhpdev
    .p2align 5
NtGdiGetDhpdev:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetETM
    .p2align 5
NtGdiGetETM:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetEmbUFI
    .p2align 5
NtGdiGetEmbUFI:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetEmbedFonts
    .p2align 5
NtGdiGetEmbedFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetEntry
    .p2align 5
NtGdiGetEntry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetEudcTimeStampEx
    .p2align 5
NtGdiGetEudcTimeStampEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetFontFileData
    .p2align 5
NtGdiGetFontFileData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetFontFileInfo
    .p2align 5
NtGdiGetFontFileInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetFontResourceInfoInternalW
    .p2align 5
NtGdiGetFontResourceInfoInternalW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetFontUnicodeRanges
    .p2align 5
NtGdiGetFontUnicodeRanges:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetGlyphIndicesW
    .p2align 5
NtGdiGetGlyphIndicesW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetGlyphIndicesWInternal
    .p2align 5
NtGdiGetGlyphIndicesWInternal:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetGlyphOutline
    .p2align 5
NtGdiGetGlyphOutline:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetKerningPairs
    .p2align 5
NtGdiGetKerningPairs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetLinkedUFIs
    .p2align 5
NtGdiGetLinkedUFIs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetMiterLimit
    .p2align 5
NtGdiGetMiterLimit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetMonitorID
    .p2align 5
NtGdiGetMonitorID:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetNumberOfPhysicalMonitors
    .p2align 5
NtGdiGetNumberOfPhysicalMonitors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetOPMInformation
    .p2align 5
NtGdiGetOPMInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetOPMRandomNumber
    .p2align 5
NtGdiGetOPMRandomNumber:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetObjectBitmapHandle
    .p2align 5
NtGdiGetObjectBitmapHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPath
    .p2align 5
NtGdiGetPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPerBandInfo
    .p2align 5
NtGdiGetPerBandInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPhysicalMonitorDescription
    .p2align 5
NtGdiGetPhysicalMonitorDescription:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPhysicalMonitorFromTarget
    .p2align 5
NtGdiGetPhysicalMonitorFromTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPhysicalMonitors
    .p2align 5
NtGdiGetPhysicalMonitors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12DB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetProcessSessionFonts
    .p2align 5
NtGdiGetProcessSessionFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12DC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetPublicFontTableChangeCookie
    .p2align 5
NtGdiGetPublicFontTableChangeCookie:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12DD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetRealizationInfo
    .p2align 5
NtGdiGetRealizationInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12DE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetServerMetaFileBits
    .p2align 5
NtGdiGetServerMetaFileBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12DF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetSpoolMessage
    .p2align 5
NtGdiGetSpoolMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetStats
    .p2align 5
NtGdiGetStats:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetStringBitmapW
    .p2align 5
NtGdiGetStringBitmapW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetSuggestedOPMProtectedOutputArraySize
    .p2align 5
NtGdiGetSuggestedOPMProtectedOutputArraySize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetTextExtentExW
    .p2align 5
NtGdiGetTextExtentExW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetUFI
    .p2align 5
NtGdiGetUFI:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetUFIPathname
    .p2align 5
NtGdiGetUFIPathname:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGradientFill
    .p2align 5
NtGdiGradientFill:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiHLSurfGetInformation
    .p2align 5
NtGdiHLSurfGetInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiHLSurfSetInformation
    .p2align 5
NtGdiHLSurfSetInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12E9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiHT_Get8BPPFormatPalette
    .p2align 5
NtGdiHT_Get8BPPFormatPalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12EA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiHT_Get8BPPMaskPalette
    .p2align 5
NtGdiHT_Get8BPPMaskPalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12EB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiIcmBrushInfo
    .p2align 5
NtGdiIcmBrushInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12EC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiInit
    .p2align 5
NtGdiInit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12ED, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiInit2
    .p2align 5
NtGdiInit2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12EE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiInitSpool
    .p2align 5
NtGdiInitSpool:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12EF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMakeFontDir
    .p2align 5
NtGdiMakeFontDir:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMakeInfoDC
    .p2align 5
NtGdiMakeInfoDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMakeObjectUnXferable
    .p2align 5
NtGdiMakeObjectUnXferable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMakeObjectXferable
    .p2align 5
NtGdiMakeObjectXferable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMirrorWindowOrg
    .p2align 5
NtGdiMirrorWindowOrg:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMonoBitmap
    .p2align 5
NtGdiMonoBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMoveTo
    .p2align 5
NtGdiMoveTo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiOffsetClipRgn
    .p2align 5
NtGdiOffsetClipRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPATHOBJ_bEnum
    .p2align 5
NtGdiPATHOBJ_bEnum:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPATHOBJ_bEnumClipLines
    .p2align 5
NtGdiPATHOBJ_bEnumClipLines:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12F9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPATHOBJ_vEnumStart
    .p2align 5
NtGdiPATHOBJ_vEnumStart:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12FA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPATHOBJ_vEnumStartClipLines
    .p2align 5
NtGdiPATHOBJ_vEnumStartClipLines:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12FB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPATHOBJ_vGetBounds
    .p2align 5
NtGdiPATHOBJ_vGetBounds:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12FC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPathToRegion
    .p2align 5
NtGdiPathToRegion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12FD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPlgBlt
    .p2align 5
NtGdiPlgBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12FE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPolyDraw
    .p2align 5
NtGdiPolyDraw:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x12FF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPolyTextOutW
    .p2align 5
NtGdiPolyTextOutW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1300, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPtInRegion
    .p2align 5
NtGdiPtInRegion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1301, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiPtVisible
    .p2align 5
NtGdiPtVisible:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1302, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiQueryFonts
    .p2align 5
NtGdiQueryFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1303, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRemoveFontResourceW
    .p2align 5
NtGdiRemoveFontResourceW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1304, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRemoveMergeFont
    .p2align 5
NtGdiRemoveMergeFont:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1305, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiResetDC
    .p2align 5
NtGdiResetDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1306, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiResizePalette
    .p2align 5
NtGdiResizePalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1307, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiRoundRect
    .p2align 5
NtGdiRoundRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1308, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSTROBJ_bEnum
    .p2align 5
NtGdiSTROBJ_bEnum:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1309, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSTROBJ_bEnumPositionsOnly
    .p2align 5
NtGdiSTROBJ_bEnumPositionsOnly:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSTROBJ_bGetAdvanceWidths
    .p2align 5
NtGdiSTROBJ_bGetAdvanceWidths:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSTROBJ_dwGetCodePage
    .p2align 5
NtGdiSTROBJ_dwGetCodePage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSTROBJ_vEnumStart
    .p2align 5
NtGdiSTROBJ_vEnumStart:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiScaleRgn
    .p2align 5
NtGdiScaleRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiScaleValues
    .p2align 5
NtGdiScaleValues:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x130F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiScaleViewportExtEx
    .p2align 5
NtGdiScaleViewportExtEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1310, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiScaleWindowExtEx
    .p2align 5
NtGdiScaleWindowExtEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1311, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSelectBrush
    .p2align 5
NtGdiSelectBrush:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1312, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSelectClipPath
    .p2align 5
NtGdiSelectClipPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1313, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSelectPen
    .p2align 5
NtGdiSelectPen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1314, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetBitmapAttributes
    .p2align 5
NtGdiSetBitmapAttributes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1315, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetBrushAttributes
    .p2align 5
NtGdiSetBrushAttributes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1316, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetColorAdjustment
    .p2align 5
NtGdiSetColorAdjustment:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1317, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetColorSpace
    .p2align 5
NtGdiSetColorSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1318, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetFontXform
    .p2align 5
NtGdiSetFontXform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1319, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetIcmMode
    .p2align 5
NtGdiSetIcmMode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetLinkedUFIs
    .p2align 5
NtGdiSetLinkedUFIs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetMagicColors
    .p2align 5
NtGdiSetMagicColors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetOPMSigningKeyAndSequenceNumbers
    .p2align 5
NtGdiSetOPMSigningKeyAndSequenceNumbers:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetPUMPDOBJ
    .p2align 5
NtGdiSetPUMPDOBJ:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetPixelFormat
    .p2align 5
NtGdiSetPixelFormat:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x131F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetRectRgn
    .p2align 5
NtGdiSetRectRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1320, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetSizeDevice
    .p2align 5
NtGdiSetSizeDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1321, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetSystemPaletteUse
    .p2align 5
NtGdiSetSystemPaletteUse:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1322, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetTextJustification
    .p2align 5
NtGdiSetTextJustification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1323, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetUMPDSandboxState
    .p2align 5
NtGdiSetUMPDSandboxState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1324, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiStartDoc
    .p2align 5
NtGdiStartDoc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1325, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiStartPage
    .p2align 5
NtGdiStartPage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1326, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiStrokeAndFillPath
    .p2align 5
NtGdiStrokeAndFillPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1327, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiStrokePath
    .p2align 5
NtGdiStrokePath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1328, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSwapBuffers
    .p2align 5
NtGdiSwapBuffers:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1329, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiTransparentBlt
    .p2align 5
NtGdiTransparentBlt:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiUMPDEngFreeUserMem
    .p2align 5
NtGdiUMPDEngFreeUserMem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiUnloadPrinterDriver
    .p2align 5
NtGdiUnloadPrinterDriver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiUnmapMemFont
    .p2align 5
NtGdiUnmapMemFont:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiUpdateColors
    .p2align 5
NtGdiUpdateColors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiUpdateTransform
    .p2align 5
NtGdiUpdateTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x132F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiWaitForTextReady
    .p2align 5
NtGdiWaitForTextReady:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1330, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiWidenPath
    .p2align 5
NtGdiWidenPath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1331, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiXFORMOBJ_bApplyXform
    .p2align 5
NtGdiXFORMOBJ_bApplyXform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1332, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiXFORMOBJ_iGetXform
    .p2align 5
NtGdiXFORMOBJ_iGetXform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1333, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiXLATEOBJ_cGetPalette
    .p2align 5
NtGdiXLATEOBJ_cGetPalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1334, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiXLATEOBJ_hGetColorTransform
    .p2align 5
NtGdiXLATEOBJ_hGetColorTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1335, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetOwnerTransformedMonitorRect
    .p2align 5
NtUserGetOwnerTransformedMonitorRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1336, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiXLATEOBJ_iXlate
    .p2align 5
NtGdiXLATEOBJ_iXlate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1337, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtHWCursorUpdatePointer
    .p2align 5
NtHWCursorUpdatePointer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1338, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtInputSpaceRegionFromPoint
    .p2align 5
NtInputSpaceRegionFromPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1339, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtIsOneCoreTransformMode
    .p2align 5
NtIsOneCoreTransformMode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtKSTInitialize
    .p2align 5
NtKSTInitialize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtKSTWait
    .p2align 5
NtKSTWait:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITAccessibilityTimerNotification
    .p2align 5
NtMITAccessibilityTimerNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITActivateInputProcessing
    .p2align 5
NtMITActivateInputProcessing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITConfigureVirtualTouchpad
    .p2align 5
NtMITConfigureVirtualTouchpad:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x133F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITCoreMsgKOpenConnectionTo
    .p2align 5
NtMITCoreMsgKOpenConnectionTo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1340, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITDeactivateInputProcessing
    .p2align 5
NtMITDeactivateInputProcessing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1341, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITDisableMouseIntercept
    .p2align 5
NtMITDisableMouseIntercept:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1342, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITDispatchCompletion
    .p2align 5
NtMITDispatchCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1343, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITEnableMouseIntercept
    .p2align 5
NtMITEnableMouseIntercept:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1344, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITGetCursorUpdateHandle
    .p2align 5
NtMITGetCursorUpdateHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1345, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITInitMinuserThread
    .p2align 5
NtMITInitMinuserThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1346, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITMinuserSetInputTransformOffset
    .p2align 5
NtMITMinuserSetInputTransformOffset:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1347, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITMinuserWindowDestroyed
    .p2align 5
NtMITMinuserWindowDestroyed:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1348, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITPostMouseInputMessage
    .p2align 5
NtMITPostMouseInputMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1349, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITPostThreadEventMessage
    .p2align 5
NtMITPostThreadEventMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITPostWindowEventMessage
    .p2align 5
NtMITPostWindowEventMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITPrepareReceiveInputMessage
    .p2align 5
NtMITPrepareReceiveInputMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITPrepareSendInputMessage
    .p2align 5
NtMITPrepareSendInputMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITProcessDelegateCapturedPointers
    .p2align 5
NtMITProcessDelegateCapturedPointers:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSetInputCallbacks
    .p2align 5
NtMITSetInputCallbacks:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x134F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSetInputDelegationMode
    .p2align 5
NtMITSetInputDelegationMode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1350, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSetInputObservationState
    .p2align 5
NtMITSetInputObservationState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1351, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSetKeyboardInputRoutingPolicy
    .p2align 5
NtMITSetKeyboardInputRoutingPolicy:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1352, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSetKeyboardOverriderState
    .p2align 5
NtMITSetKeyboardOverriderState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1353, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSetLastInputRecipient
    .p2align 5
NtMITSetLastInputRecipient:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1354, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSynthesizeKeyboardInput
    .p2align 5
NtMITSynthesizeKeyboardInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1355, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSynthesizeMouseInput
    .p2align 5
NtMITSynthesizeMouseInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1356, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITSynthesizeTouchInput
    .p2align 5
NtMITSynthesizeTouchInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1357, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITUninitMinuserThread
    .p2align 5
NtMITUninitMinuserThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1358, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMITUpdateInputGlobals
    .p2align 5
NtMITUpdateInputGlobals:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1359, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMapVisualRelativePoints
    .p2align 5
NtMapVisualRelativePoints:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMinGetInputTransform
    .p2align 5
NtMinGetInputTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMinInteropCoreMessagingWithInput
    .p2align 5
NtMinInteropCoreMessagingWithInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMinQPeekForInput
    .p2align 5
NtMinQPeekForInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMinQSuspendInputProcessing
    .p2align 5
NtMinQSuspendInputProcessing:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtMinQUpdateWakeMask
    .p2align 5
NtMinQUpdateWakeMask:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x135F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreBeginLayoutUpdate
    .p2align 5
NtModerncoreBeginLayoutUpdate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1360, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreCreateDCompositionHwndTarget
    .p2align 5
NtModerncoreCreateDCompositionHwndTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1361, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreCreateGDIHwndTarget
    .p2align 5
NtModerncoreCreateGDIHwndTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1362, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreDestroyDCompositionHwndTarget
    .p2align 5
NtModerncoreDestroyDCompositionHwndTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1363, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreDestroyGDIHwndTarget
    .p2align 5
NtModerncoreDestroyGDIHwndTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1364, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreEnableResizeLayoutSynchronization
    .p2align 5
NtModerncoreEnableResizeLayoutSynchronization:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1365, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreGetNavigationWindowVisual
    .p2align 5
NtModerncoreGetNavigationWindowVisual:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1366, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreGetResizeDCompositionSynchronizationObject
    .p2align 5
NtModerncoreGetResizeDCompositionSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1367, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreGetWindowContentVisual
    .p2align 5
NtModerncoreGetWindowContentVisual:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1368, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreIdleTimerThread
    .p2align 5
NtModerncoreIdleTimerThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1369, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreIsResizeLayoutSynchronizationEnabled
    .p2align 5
NtModerncoreIsResizeLayoutSynchronizationEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreProcessConnect
    .p2align 5
NtModerncoreProcessConnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreRegisterEnhancedNavigationWindowHandle
    .p2align 5
NtModerncoreRegisterEnhancedNavigationWindowHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreRegisterNavigationWindowHandle
    .p2align 5
NtModerncoreRegisterNavigationWindowHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreSetNavigationServiceSid
    .p2align 5
NtModerncoreSetNavigationServiceSid:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtModerncoreUnregisterNavigationWindowHandle
    .p2align 5
NtModerncoreUnregisterNavigationWindowHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x136F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtNotifyPresentToCompositionSurface
    .p2align 5
NtNotifyPresentToCompositionSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1370, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenCompositionSurfaceDirtyRegion
    .p2align 5
NtOpenCompositionSurfaceDirtyRegion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1371, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenCompositionSurfaceRealizationInfo
    .p2align 5
NtOpenCompositionSurfaceRealizationInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1372, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtOpenCompositionSurfaceSectionInfo
    .p2align 5
NtOpenCompositionSurfaceSectionInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1373, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionInputIsImplicit
    .p2align 5
NtQueryCompositionInputIsImplicit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1374, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionInputQueueAndTransform
    .p2align 5
NtQueryCompositionInputQueueAndTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1375, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionInputSink
    .p2align 5
NtQueryCompositionInputSink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1376, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionInputSinkLuid
    .p2align 5
NtQueryCompositionInputSinkLuid:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1377, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionInputSinkViewId
    .p2align 5
NtQueryCompositionInputSinkViewId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1378, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionSurfaceBinding
    .p2align 5
NtQueryCompositionSurfaceBinding:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1379, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionSurfaceFrameRate
    .p2align 5
NtQueryCompositionSurfaceFrameRate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionSurfaceHDRMetaData
    .p2align 5
NtQueryCompositionSurfaceHDRMetaData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionSurfaceRenderingRealization
    .p2align 5
NtQueryCompositionSurfaceRenderingRealization:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtQueryCompositionSurfaceStatistics
    .p2align 5
NtQueryCompositionSurfaceStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMAddInputObserver
    .p2align 5
NtRIMAddInputObserver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMAreSiblingDevices
    .p2align 5
NtRIMAreSiblingDevices:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x137F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMDeviceIoControl
    .p2align 5
NtRIMDeviceIoControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1380, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMEnableMonitorMappingForDevice
    .p2align 5
NtRIMEnableMonitorMappingForDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1381, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMFreeInputBuffer
    .p2align 5
NtRIMFreeInputBuffer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1382, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMGetDevicePreparsedData
    .p2align 5
NtRIMGetDevicePreparsedData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1383, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMGetDevicePreparsedDataLockfree
    .p2align 5
NtRIMGetDevicePreparsedDataLockfree:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1384, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMGetDeviceProperties
    .p2align 5
NtRIMGetDeviceProperties:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1385, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMGetDevicePropertiesLockfree
    .p2align 5
NtRIMGetDevicePropertiesLockfree:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1386, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMGetPhysicalDeviceRect
    .p2align 5
NtRIMGetPhysicalDeviceRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1387, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMGetSourceProcessId
    .p2align 5
NtRIMGetSourceProcessId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1388, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMObserveNextInput
    .p2align 5
NtRIMObserveNextInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1389, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMOnAsyncPnpWorkNotification
    .p2align 5
NtRIMOnAsyncPnpWorkNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMOnPnpNotification
    .p2align 5
NtRIMOnPnpNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMOnTimerNotification
    .p2align 5
NtRIMOnTimerNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMQueryDevicePath
    .p2align 5
NtRIMQueryDevicePath:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMReadInput
    .p2align 5
NtRIMReadInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMRegisterForInputEx
    .p2align 5
NtRIMRegisterForInputEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x138F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMRemoveInputObserver
    .p2align 5
NtRIMRemoveInputObserver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1390, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMSetDeadzoneRotation
    .p2align 5
NtRIMSetDeadzoneRotation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1391, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMSetExtendedDeviceProperty
    .p2align 5
NtRIMSetExtendedDeviceProperty:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1392, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMSetTestModeStatus
    .p2align 5
NtRIMSetTestModeStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1393, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMUnregisterForInput
    .p2align 5
NtRIMUnregisterForInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1394, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtRIMUpdateInputObserverRegistration
    .p2align 5
NtRIMUpdateInputObserverRegistration:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1395, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCompositionSurfaceAnalogExclusive
    .p2align 5
NtSetCompositionSurfaceAnalogExclusive:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1396, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCompositionSurfaceBufferUsage
    .p2align 5
NtSetCompositionSurfaceBufferUsage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1397, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCompositionSurfaceDirectFlipState
    .p2align 5
NtSetCompositionSurfaceDirectFlipState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1398, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCompositionSurfaceIndependentFlipInfo
    .p2align 5
NtSetCompositionSurfaceIndependentFlipInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1399, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCompositionSurfaceStatistics
    .p2align 5
NtSetCompositionSurfaceStatistics:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetCursorInputSpace
    .p2align 5
NtSetCursorInputSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetPointerDeviceInputSpace
    .p2align 5
NtSetPointerDeviceInputSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtSetShellCursorState
    .p2align 5
NtSetShellCursorState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerConfirmOutstandingAnalogToken
    .p2align 5
NtTokenManagerConfirmOutstandingAnalogToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerCreateCompositionTokenHandle
    .p2align 5
NtTokenManagerCreateCompositionTokenHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x139F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerCreateFlipObjectReturnTokenHandle
    .p2align 5
NtTokenManagerCreateFlipObjectReturnTokenHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerCreateFlipObjectTokenHandle
    .p2align 5
NtTokenManagerCreateFlipObjectTokenHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerGetAnalogExclusiveSurfaceUpdates
    .p2align 5
NtTokenManagerGetAnalogExclusiveSurfaceUpdates:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerGetAnalogExclusiveTokenEvent
    .p2align 5
NtTokenManagerGetAnalogExclusiveTokenEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerOpenSectionAndEvents
    .p2align 5
NtTokenManagerOpenSectionAndEvents:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtTokenManagerThread
    .p2align 5
NtTokenManagerThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUnBindCompositionSurface
    .p2align 5
NtUnBindCompositionSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUpdateInputSinkTransforms
    .p2align 5
NtUpdateInputSinkTransforms:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAcquireIAMKey
    .p2align 5
NtUserAcquireIAMKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAcquireInteractiveControlBackgroundAccess
    .p2align 5
NtUserAcquireInteractiveControlBackgroundAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAddClipboardFormatListener
    .p2align 5
NtUserAddClipboardFormatListener:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAddVisualIdentifier
    .p2align 5
NtUserAddVisualIdentifier:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAllowSetForegroundWindow
    .p2align 5
NtUserAllowSetForegroundWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserApplyWindowAction
    .p2align 5
NtUserApplyWindowAction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserArrangeIconicWindows
    .p2align 5
NtUserArrangeIconicWindows:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAssociateInputContext
    .p2align 5
NtUserAssociateInputContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAutoPromoteMouseInPointer
    .p2align 5
NtUserAutoPromoteMouseInPointer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserAutoRotateScreen
    .p2align 5
NtUserAutoRotateScreen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBeginDeferWindowPos
    .p2align 5
NtUserBeginDeferWindowPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBeginLayoutUpdate
    .p2align 5
NtUserBeginLayoutUpdate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBlockInput
    .p2align 5
NtUserBlockInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBroadcastImeShowStatusChange
    .p2align 5
NtUserBroadcastImeShowStatusChange:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBroadcastThemeChangeEvent
    .p2align 5
NtUserBroadcastThemeChangeEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBuildHimcList
    .p2align 5
NtUserBuildHimcList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserBuildPropList
    .p2align 5
NtUserBuildPropList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCalculatePopupWindowPosition
    .p2align 5
NtUserCalculatePopupWindowPosition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCanCurrentThreadChangeForeground
    .p2align 5
NtUserCanCurrentThreadChangeForeground:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCancelQueueEventCompletionPacket
    .p2align 5
NtUserCancelQueueEventCompletionPacket:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserChangeDisplaySettings
    .p2align 5
NtUserChangeDisplaySettings:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserChangeWindowMessageFilter
    .p2align 5
NtUserChangeWindowMessageFilter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserChangeWindowMessageFilterEx
    .p2align 5
NtUserChangeWindowMessageFilterEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCheckImeShowStatusInThread
    .p2align 5
NtUserCheckImeShowStatusInThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCheckProcessForClipboardAccess
    .p2align 5
NtUserCheckProcessForClipboardAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCheckProcessSession
    .p2align 5
NtUserCheckProcessSession:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCheckWindowThreadDesktop
    .p2align 5
NtUserCheckWindowThreadDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserChildWindowFromPointEx
    .p2align 5
NtUserChildWindowFromPointEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCitSetInfo
    .p2align 5
NtUserCitSetInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserClearForeground
    .p2align 5
NtUserClearForeground:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserClearWakeMask
    .p2align 5
NtUserClearWakeMask:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserClearWindowState
    .p2align 5
NtUserClearWindowState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserClipCursor
    .p2align 5
NtUserClipCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCompositionInputSinkLuidFromPoint
    .p2align 5
NtUserCompositionInputSinkLuidFromPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCompositionInputSinkViewInstanceIdFromPoint
    .p2align 5
NtUserCompositionInputSinkViewInstanceIdFromPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserConfigureActivationObject
    .p2align 5
NtUserConfigureActivationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserConfirmResizeCommit
    .p2align 5
NtUserConfirmResizeCommit:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserConvertPrimaryPointerToMouseDrag
    .p2align 5
NtUserConvertPrimaryPointerToMouseDrag:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserConvertToInterceptWindow
    .p2align 5
NtUserConvertToInterceptWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateActivationObject
    .p2align 5
NtUserCreateActivationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateBaseWindow
    .p2align 5
NtUserCreateBaseWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateDCompositionHwndTarget
    .p2align 5
NtUserCreateDCompositionHwndTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateDesktopEx
    .p2align 5
NtUserCreateDesktopEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateEmptyCursorObject
    .p2align 5
NtUserCreateEmptyCursorObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateInputContext
    .p2align 5
NtUserCreateInputContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateLayoutSyncForHwnd
    .p2align 5
NtUserCreateLayoutSyncForHwnd:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateMenu
    .p2align 5
NtUserCreateMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreatePalmRejectionDelayZone
    .p2align 5
NtUserCreatePalmRejectionDelayZone:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreatePopupMenu
    .p2align 5
NtUserCreatePopupMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateSyntheticPointerDevice2
    .p2align 5
NtUserCreateSyntheticPointerDevice2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateSystemThreads
    .p2align 5
NtUserCreateSystemThreads:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateWindowStation
    .p2align 5
NtUserCreateWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13DB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateWindowStationEx
    .p2align 5
NtUserCreateWindowStationEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13DC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCsDdeUninitialize
    .p2align 5
NtUserCsDdeUninitialize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13DD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCtxDisplayIOCtl
    .p2align 5
NtUserCtxDisplayIOCtl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13DE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDWP_GetEnabledPopupOffset
    .p2align 5
NtUserDWP_GetEnabledPopupOffset:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13DF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDeferWindowDpiChanges
    .p2align 5
NtUserDeferWindowDpiChanges:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDeferWindowPosAndBand
    .p2align 5
NtUserDeferWindowPosAndBand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDeferredDesktopRotation
    .p2align 5
NtUserDeferredDesktopRotation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDelegateCapturePointers
    .p2align 5
NtUserDelegateCapturePointers:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDelegateInput
    .p2align 5
NtUserDelegateInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDeregisterShellHookWindow
    .p2align 5
NtUserDeregisterShellHookWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyActivationObject
    .p2align 5
NtUserDestroyActivationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyCaret
    .p2align 5
NtUserDestroyCaret:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyDCompositionHwndTarget
    .p2align 5
NtUserDestroyDCompositionHwndTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyInputContext
    .p2align 5
NtUserDestroyInputContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13E9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDestroyPalmRejectionDelayZone
    .p2align 5
NtUserDestroyPalmRejectionDelayZone:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13EA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDisableImmersiveOwner
    .p2align 5
NtUserDisableImmersiveOwner:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13EB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDisableProcessWindowFiltering
    .p2align 5
NtUserDisableProcessWindowFiltering:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13EC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDisableProcessWindowsGhosting
    .p2align 5
NtUserDisableProcessWindowsGhosting:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13ED, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDisableThreadIme
    .p2align 5
NtUserDisableThreadIme:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13EE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDiscardPointerFrameMessages
    .p2align 5
NtUserDiscardPointerFrameMessages:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13EF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDisplayConfigGetDeviceInfo
    .p2align 5
NtUserDisplayConfigGetDeviceInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDisplayConfigSetDeviceInfo
    .p2align 5
NtUserDisplayConfigSetDeviceInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDoInitMessagePumpHook
    .p2align 5
NtUserDoInitMessagePumpHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDoMenuOperation
    .p2align 5
NtUserDoMenuOperation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDoSoundConnect
    .p2align 5
NtUserDoSoundConnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDoSoundDisconnect
    .p2align 5
NtUserDoSoundDisconnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDoUninitMessagePumpHook
    .p2align 5
NtUserDoUninitMessagePumpHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDownlevelTouchpad
    .p2align 5
NtUserDownlevelTouchpad:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDragDetect
    .p2align 5
NtUserDragDetect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDragObject
    .p2align 5
NtUserDragObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13F9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrainThreadCoreMessagingCompletions
    .p2align 5
NtUserDrainThreadCoreMessagingCompletions:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13FA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrainThreadCoreMessagingCompletions2
    .p2align 5
NtUserDrainThreadCoreMessagingCompletions2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13FB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrawAnimatedRects
    .p2align 5
NtUserDrawAnimatedRects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13FC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrawCaption
    .p2align 5
NtUserDrawCaption:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13FD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrawCaptionTemp
    .p2align 5
NtUserDrawCaptionTemp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13FE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrawMenuBar
    .p2align 5
NtUserDrawMenuBar:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x13FF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDrawMenuBarTemp
    .p2align 5
NtUserDrawMenuBarTemp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1400, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmGetRemoteSessionOcclusionEvent
    .p2align 5
NtUserDwmGetRemoteSessionOcclusionEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1401, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmGetRemoteSessionOcclusionState
    .p2align 5
NtUserDwmGetRemoteSessionOcclusionState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1402, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmKernelShutdown
    .p2align 5
NtUserDwmKernelShutdown:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1403, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmKernelStartup
    .p2align 5
NtUserDwmKernelStartup:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1404, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmLockScreenUpdates
    .p2align 5
NtUserDwmLockScreenUpdates:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1405, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmValidateWindow
    .p2align 5
NtUserDwmValidateWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1406, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserDwmWindowNotificationsEnabled
    .p2align 5
NtUserDwmWindowNotificationsEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1407, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableChildWindowDpiMessage
    .p2align 5
NtUserEnableChildWindowDpiMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1408, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableIAMAccess
    .p2align 5
NtUserEnableIAMAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1409, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableModernAppWindowKeyboardIntercept
    .p2align 5
NtUserEnableModernAppWindowKeyboardIntercept:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableMouseInPointer
    .p2align 5
NtUserEnableMouseInPointer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableMouseInPointerForThread
    .p2align 5
NtUserEnableMouseInPointerForThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableMouseInPointerForWindow
    .p2align 5
NtUserEnableMouseInPointerForWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableMouseInputForCursorSuppression
    .p2align 5
NtUserEnableMouseInputForCursorSuppression:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableNonClientDpiScaling
    .p2align 5
NtUserEnableNonClientDpiScaling:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x140F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableResizeLayoutSynchronization
    .p2align 5
NtUserEnableResizeLayoutSynchronization:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1410, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableSessionForMMCSS
    .p2align 5
NtUserEnableSessionForMMCSS:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1411, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableShellWindowManagementBehavior
    .p2align 5
NtUserEnableShellWindowManagementBehavior:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1412, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableSoftwareCursorForScreenCapture
    .p2align 5
NtUserEnableSoftwareCursorForScreenCapture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1413, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableSynchronizedLayout
    .p2align 5
NtUserEnableSynchronizedLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1414, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableTouchPad
    .p2align 5
NtUserEnableTouchPad:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1415, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableWindow
    .p2align 5
NtUserEnableWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1416, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableWindowGDIScaledDpiMessage
    .p2align 5
NtUserEnableWindowGDIScaledDpiMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1417, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableWindowResizeOptimization
    .p2align 5
NtUserEnableWindowResizeOptimization:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1418, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnableWindowShellWindowManagementBehavior
    .p2align 5
NtUserEnableWindowShellWindowManagementBehavior:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1419, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSensorPresence
    .p2align 5
NtUserSetSensorPresence:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEndMenu
    .p2align 5
NtUserEndMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnsureDpiDepSysMetCacheForPlateau
    .p2align 5
NtUserEnsureDpiDepSysMetCacheForPlateau:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnsureDpiMetricsForDpi
    .p2align 5
NtUserEnsureDpiMetricsForDpi:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnsureDpiServerInfoForDpi
    .p2align 5
NtUserEnsureDpiServerInfoForDpi:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnsureOemBitmapInfoForDpi
    .p2align 5
NtUserEnsureOemBitmapInfoForDpi:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x141F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnterMoveSizeLoop
    .p2align 5
NtUserEnterMoveSizeLoop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1420, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEnumClipboardFormats
    .p2align 5
NtUserEnumClipboardFormats:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1421, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserEvent
    .p2align 5
NtUserEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1422, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserFlashWindowEx
    .p2align 5
NtUserFlashWindowEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1423, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserForceEnableNumpadTranslation
    .p2align 5
NtUserForceEnableNumpadTranslation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1424, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserForceWindowToDpiForTest
    .p2align 5
NtUserForceWindowToDpiForTest:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1425, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserFrostCrashedWindow
    .p2align 5
NtUserFrostCrashedWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1426, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserFunctionalizeDisplayConfig
    .p2align 5
NtUserFunctionalizeDisplayConfig:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1427, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetActiveProcessesDpis
    .p2align 5
NtUserGetActiveProcessesDpis:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1428, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetAppImeLevel
    .p2align 5
NtUserGetAppImeLevel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1429, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetAutoRotationState
    .p2align 5
NtUserGetAutoRotationState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCIMSSM
    .p2align 5
NtUserGetCIMSSM:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCaretPos
    .p2align 5
NtUserGetCaretPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClassIcoCur
    .p2align 5
NtUserGetClassIcoCur:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipCursor
    .p2align 5
NtUserGetClipCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardAccessToken
    .p2align 5
NtUserGetClipboardAccessToken:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x142F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardMetadata
    .p2align 5
NtUserGetClipboardMetadata:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1430, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetClipboardViewer
    .p2align 5
NtUserGetClipboardViewer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1431, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetComboBoxInfo
    .p2align 5
NtUserGetComboBoxInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1432, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCurrentDpiInfoForWindow
    .p2align 5
NtUserGetCurrentDpiInfoForWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1433, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCurrentInputMessageSource
    .p2align 5
NtUserGetCurrentInputMessageSource:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1434, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCursor
    .p2align 5
NtUserGetCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1435, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCursorInfo
    .p2align 5
NtUserGetCursorInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1436, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetCursorPos
    .p2align 5
NtUserGetCursorPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1437, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDCompositionHwndBitmap
    .p2align 5
NtUserGetDCompositionHwndBitmap:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1438, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDManipHookInitFunction
    .p2align 5
NtUserGetDManipHookInitFunction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1439, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDesktopID
    .p2align 5
NtUserGetDesktopID:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDesktopVisualTransform
    .p2align 5
NtUserGetDesktopVisualTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDeviceChangeInfo
    .p2align 5
NtUserGetDeviceChangeInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDisplayAutoRotationPreferences
    .p2align 5
NtUserGetDisplayAutoRotationPreferences:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDisplayAutoRotationPreferencesByProcessId
    .p2align 5
NtUserGetDisplayAutoRotationPreferencesByProcessId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDisplayConfigBufferSizes
    .p2align 5
NtUserGetDisplayConfigBufferSizes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x143F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDpiForCurrentProcess
    .p2align 5
NtUserGetDpiForCurrentProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1440, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDpiForMonitor
    .p2align 5
NtUserGetDpiForMonitor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1441, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetDwmCursorShape
    .p2align 5
NtUserGetDwmCursorShape:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1442, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetExtendedPointerDeviceProperty
    .p2align 5
NtUserGetExtendedPointerDeviceProperty:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1443, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetGestureConfig
    .p2align 5
NtUserGetGestureConfig:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1444, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetGestureExtArgs
    .p2align 5
NtUserGetGestureExtArgs:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1445, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetGestureInfo
    .p2align 5
NtUserGetGestureInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1446, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetGuiResources
    .p2align 5
NtUserGetGuiResources:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1447, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetHDevName
    .p2align 5
NtUserGetHDevName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1448, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetHimetricScaleFactorFromPixelLocation
    .p2align 5
NtUserGetHimetricScaleFactorFromPixelLocation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1449, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetIMEShowStatus
    .p2align 5
NtUserGetIMEShowStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetImeHotKey
    .p2align 5
NtUserGetImeHotKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetImeInfoEx
    .p2align 5
NtUserGetImeInfoEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInputContainerId
    .p2align 5
NtUserGetInputContainerId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInputDesktop
    .p2align 5
NtUserGetInputDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInputEvent
    .p2align 5
NtUserGetInputEvent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x144F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInputLocaleInfo
    .p2align 5
NtUserGetInputLocaleInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1450, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInteractiveControlDeviceInfo
    .p2align 5
NtUserGetInteractiveControlDeviceInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1451, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInteractiveControlInfo
    .p2align 5
NtUserGetInteractiveControlInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1452, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetInteractiveCtrlSupportedWaveforms
    .p2align 5
NtUserGetInteractiveCtrlSupportedWaveforms:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1453, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyNameText
    .p2align 5
NtUserGetKeyNameText:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1454, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyboardInputThreadId
    .p2align 5
NtUserGetKeyboardInputThreadId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1455, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyboardLayout
    .p2align 5
NtUserGetKeyboardLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1456, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyboardLayoutName
    .p2align 5
NtUserGetKeyboardLayoutName:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1457, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetKeyboardType
    .p2align 5
NtUserGetKeyboardType:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1458, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetLatestInputMessageData
    .p2align 5
NtUserGetLatestInputMessageData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1459, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetLayeredWindowAttributes
    .p2align 5
NtUserGetLayeredWindowAttributes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetListBoxInfo
    .p2align 5
NtUserGetListBoxInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMenuIndex
    .p2align 5
NtUserGetMenuIndex:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMenuItemRect
    .p2align 5
NtUserGetMenuItemRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMessagePos
    .p2align 5
NtUserGetMessagePos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMinuserIdForBaseWindow
    .p2align 5
NtUserGetMinuserIdForBaseWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x145F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetModernAppWindow
    .p2align 5
NtUserGetModernAppWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1460, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetMouseMovePointsEx
    .p2align 5
NtUserGetMouseMovePointsEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1461, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetOemBitmapSize
    .p2align 5
NtUserGetOemBitmapSize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1462, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPhysicalDeviceRect
    .p2align 5
NtUserGetPhysicalDeviceRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1463, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerCursorId
    .p2align 5
NtUserGetPointerCursorId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1464, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDevice
    .p2align 5
NtUserGetPointerDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1465, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDeviceCursors
    .p2align 5
NtUserGetPointerDeviceCursors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1466, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDeviceInputSpace
    .p2align 5
NtUserGetPointerDeviceInputSpace:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1467, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDeviceOrientation
    .p2align 5
NtUserGetPointerDeviceOrientation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1468, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDeviceProperties
    .p2align 5
NtUserGetPointerDeviceProperties:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1469, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDeviceRects
    .p2align 5
NtUserGetPointerDeviceRects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerDevices
    .p2align 5
NtUserGetPointerDevices:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerFrameTimes
    .p2align 5
NtUserGetPointerFrameTimes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerIdForPromotion
    .p2align 5
NtUserGetPointerIdForPromotion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerInfoList
    .p2align 5
NtUserGetPointerInfoList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerInputTransform
    .p2align 5
NtUserGetPointerInputTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x146F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerProprietaryId
    .p2align 5
NtUserGetPointerProprietaryId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1470, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPointerType
    .p2align 5
NtUserGetPointerType:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1471, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetPriorityClipboardFormat
    .p2align 5
NtUserGetPriorityClipboardFormat:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1472, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetProcessDefaultLayout
    .p2align 5
NtUserGetProcessDefaultLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1473, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetProcessDpiAwarenessContext
    .p2align 5
NtUserGetProcessDpiAwarenessContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1474, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetProcessUIContextInformation
    .p2align 5
NtUserGetProcessUIContextInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1475, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetProp2
    .p2align 5
NtUserGetProp2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1476, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetQueueIocp
    .p2align 5
NtUserGetQueueIocp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1477, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetQueueStatus
    .p2align 5
NtUserGetQueueStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1478, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetQueueStatusReadonly
    .p2align 5
NtUserGetQueueStatusReadonly:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1479, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRawInputBuffer
    .p2align 5
NtUserGetRawInputBuffer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRawInputData
    .p2align 5
NtUserGetRawInputData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRawInputDeviceInfo
    .p2align 5
NtUserGetRawInputDeviceInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRawInputDeviceList
    .p2align 5
NtUserGetRawInputDeviceList:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRawPointerDeviceData
    .p2align 5
NtUserGetRawPointerDeviceData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRegisteredRawInputDevices
    .p2align 5
NtUserGetRegisteredRawInputDevices:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x147F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetRequiredCursorSizes
    .p2align 5
NtUserGetRequiredCursorSizes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1480, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetResizeDCompositionSynchronizationObject
    .p2align 5
NtUserGetResizeDCompositionSynchronizationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1481, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetSendMessageReceiver
    .p2align 5
NtUserGetSendMessageReceiver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1482, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetSharedWindowData
    .p2align 5
NtUserGetSharedWindowData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1483, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetSysMenuOffset
    .p2align 5
NtUserGetSysMenuOffset:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1484, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetSystemContentRects
    .p2align 5
NtUserGetSystemContentRects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1485, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetSystemDpiForProcess
    .p2align 5
NtUserGetSystemDpiForProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1486, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetTopLevelWindow
    .p2align 5
NtUserGetTopLevelWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1487, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetTouchInputInfo
    .p2align 5
NtUserGetTouchInputInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1488, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetTouchValidationStatus
    .p2align 5
NtUserGetTouchValidationStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1489, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetUniformSpaceMapping
    .p2align 5
NtUserGetUniformSpaceMapping:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetUnpredictedMessagePos
    .p2align 5
NtUserGetUnpredictedMessagePos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetUpdatedClipboardFormats
    .p2align 5
NtUserGetUpdatedClipboardFormats:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWOWClass
    .p2align 5
NtUserGetWOWClass:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWinStationInfo
    .p2align 5
NtUserGetWinStationInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowBand
    .p2align 5
NtUserGetWindowBand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x148F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowCompositionAttribute
    .p2align 5
NtUserGetWindowCompositionAttribute:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1490, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowCompositionInfo
    .p2align 5
NtUserGetWindowCompositionInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1491, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowContextHelpId
    .p2align 5
NtUserGetWindowContextHelpId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1492, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowDisplayAffinity
    .p2align 5
NtUserGetWindowDisplayAffinity:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1493, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowFeedbackSetting
    .p2align 5
NtUserGetWindowFeedbackSetting:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1494, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowMinimizeRect
    .p2align 5
NtUserGetWindowMinimizeRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1495, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowProcessHandle
    .p2align 5
NtUserGetWindowProcessHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1496, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowRgnEx
    .p2align 5
NtUserGetWindowRgnEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1497, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowThreadProcessId
    .p2align 5
NtUserGetWindowThreadProcessId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1498, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGetWindowTrackInfoAsync
    .p2align 5
NtUserGetWindowTrackInfoAsync:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1499, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserGhostWindowFromHungWindow
    .p2align 5
NtUserGhostWindowFromHungWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHandleDelegatedInput
    .p2align 5
NtUserHandleDelegatedInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHandleSystemThreadCreationFailure
    .p2align 5
NtUserHandleSystemThreadCreationFailure:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHardErrorControl
    .p2align 5
NtUserHardErrorControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHideCursorNoCapture
    .p2align 5
NtUserHideCursorNoCapture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHidePointerContactVisualization
    .p2align 5
NtUserHidePointerContactVisualization:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x149F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHiliteMenuItem
    .p2align 5
NtUserHiliteMenuItem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHungWindowFromGhostWindow
    .p2align 5
NtUserHungWindowFromGhostWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHwndQueryRedirectionInfo
    .p2align 5
NtUserHwndQueryRedirectionInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserHwndSetRedirectionInfo
    .p2align 5
NtUserHwndSetRedirectionInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserImpersonateDdeClientWindow
    .p2align 5
NtUserImpersonateDdeClientWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInheritWindowMonitor
    .p2align 5
NtUserInheritWindowMonitor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitAnsiOem
    .p2align 5
NtUserInitAnsiOem:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitThreadCoreMessagingIocp
    .p2align 5
NtUserInitThreadCoreMessagingIocp:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitThreadCoreMessagingIocp2
    .p2align 5
NtUserInitThreadCoreMessagingIocp2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitialize
    .p2align 5
NtUserInitialize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitializeClientPfnArrays
    .p2align 5
NtUserInitializeClientPfnArrays:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitializeGenericHidInjection
    .p2align 5
NtUserInitializeGenericHidInjection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitializeInputDeviceInjection
    .p2align 5
NtUserInitializeInputDeviceInjection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitializeTouchInjection
    .p2align 5
NtUserInitializeTouchInjection:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInitializeUserModeMenus
    .p2align 5
NtUserInitializeUserModeMenus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectDeviceInput
    .p2align 5
NtUserInjectDeviceInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectGenericHidInput
    .p2align 5
NtUserInjectGenericHidInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectGesture
    .p2align 5
NtUserInjectGesture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectKeyboardInput
    .p2align 5
NtUserInjectKeyboardInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectMouseInput
    .p2align 5
NtUserInjectMouseInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectPointerInput
    .p2align 5
NtUserInjectPointerInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectTouchInput
    .p2align 5
NtUserInjectTouchInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInjectTouchpadAction
    .p2align 5
NtUserInjectTouchpadAction:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInteractiveControlQueryUsage
    .p2align 5
NtUserInteractiveControlQueryUsage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInternalGetWindowIcon
    .p2align 5
NtUserInternalGetWindowIcon:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserInternalToUnicode
    .p2align 5
NtUserInternalToUnicode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsChildWindowDpiMessageEnabled
    .p2align 5
NtUserIsChildWindowDpiMessageEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsInterceptWindow
    .p2align 5
NtUserIsInterceptWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsMouseInPointerEnabled
    .p2align 5
NtUserIsMouseInPointerEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsMouseInputEnabled
    .p2align 5
NtUserIsMouseInputEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsNonClientDpiScalingEnabled
    .p2align 5
NtUserIsNonClientDpiScalingEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsResizeLayoutSynchronizationEnabled
    .p2align 5
NtUserIsResizeLayoutSynchronizationEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsTopLevelWindow
    .p2align 5
NtUserIsTopLevelWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsTouchWindow
    .p2align 5
NtUserIsTouchWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsUtilitySession
    .p2align 5
NtUserIsUtilitySession:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsWindowBroadcastingDpiToChildren
    .p2align 5
NtUserIsWindowBroadcastingDpiToChildren:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsWindowDisplayChangeSuppressed
    .p2align 5
NtUserIsWindowDisplayChangeSuppressed:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserIsWindowGDIScaledDpiMessageEnabled
    .p2align 5
NtUserIsWindowGDIScaledDpiMessageEnabled:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserKillSystemTimer
    .p2align 5
NtUserKillSystemTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLW_LoadFonts
    .p2align 5
NtUserLW_LoadFonts:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLayoutCompleted
    .p2align 5
NtUserLayoutCompleted:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLinkDpiCursor
    .p2align 5
NtUserLinkDpiCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLoadCursorsAndIcons
    .p2align 5
NtUserLoadCursorsAndIcons:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLoadKeyboardLayoutEx
    .p2align 5
NtUserLoadKeyboardLayoutEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLoadUserApiHook
    .p2align 5
NtUserLoadUserApiHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLockCursor
    .p2align 5
NtUserLockCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLockSetForegroundWindow
    .p2align 5
NtUserLockSetForegroundWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLockWindowStation
    .p2align 5
NtUserLockWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLockWorkStation
    .p2align 5
NtUserLockWorkStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLogicalToPerMonitorDPIPhysicalPoint
    .p2align 5
NtUserLogicalToPerMonitorDPIPhysicalPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLogicalToPhysicalDpiPointForWindow
    .p2align 5
NtUserLogicalToPhysicalDpiPointForWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserLogicalToPhysicalPoint
    .p2align 5
NtUserLogicalToPhysicalPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMNDragLeave
    .p2align 5
NtUserMNDragLeave:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMNDragOver
    .p2align 5
NtUserMNDragOver:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMagControl
    .p2align 5
NtUserMagControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMagGetContextInformation
    .p2align 5
NtUserMagGetContextInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMagSetContextInformation
    .p2align 5
NtUserMagSetContextInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMapDesktopObject
    .p2align 5
NtUserMapDesktopObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMapPointsByVisualIdentifier
    .p2align 5
NtUserMapPointsByVisualIdentifier:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMarkWindowForRawMouse
    .p2align 5
NtUserMarkWindowForRawMouse:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14DB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMenuItemFromPoint
    .p2align 5
NtUserMenuItemFromPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14DC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMessageBeep
    .p2align 5
NtUserMessageBeep:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14DD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMinInitialize
    .p2align 5
NtUserMinInitialize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14DE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMinMaximize
    .p2align 5
NtUserMinMaximize:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14DF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserModifyWindowTouchCapability
    .p2align 5
NtUserModifyWindowTouchCapability:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMsgWaitForMultipleObjectsEx
    .p2align 5
NtUserMsgWaitForMultipleObjectsEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserNavigateFocus
    .p2align 5
NtUserNavigateFocus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserNlsKbdSendIMENotification
    .p2align 5
NtUserNlsKbdSendIMENotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserNotifyIMEStatus
    .p2align 5
NtUserNotifyIMEStatus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserNotifyOverlayWindow
    .p2align 5
NtUserNotifyOverlayWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserOpenInputDesktop
    .p2align 5
NtUserOpenInputDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserOpenThreadDesktop
    .p2align 5
NtUserOpenThreadDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPaintMonitor
    .p2align 5
NtUserPaintMonitor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPerMonitorDPIPhysicalToLogicalPoint
    .p2align 5
NtUserPerMonitorDPIPhysicalToLogicalPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14E9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPhysicalToLogicalDpiPointForWindow
    .p2align 5
NtUserPhysicalToLogicalDpiPointForWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14EA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPhysicalToLogicalPoint
    .p2align 5
NtUserPhysicalToLogicalPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14EB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPlayEventSound
    .p2align 5
NtUserPlayEventSound:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14EC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPostKeyboardInputMessage
    .p2align 5
NtUserPostKeyboardInputMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14ED, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPostQuitMessage
    .p2align 5
NtUserPostQuitMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14EE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPrepareForLogoff
    .p2align 5
NtUserPrepareForLogoff:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14EF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPrintWindow
    .p2align 5
NtUserPrintWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserProcessInkFeedbackCommand
    .p2align 5
NtUserProcessInkFeedbackCommand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPromoteMouseInPointer
    .p2align 5
NtUserPromoteMouseInPointer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserPromotePointer
    .p2align 5
NtUserPromotePointer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserQueryBSDRWindow
    .p2align 5
NtUserQueryBSDRWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserQueryDisplayConfig
    .p2align 5
NtUserQueryDisplayConfig:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserQueryInformationThread
    .p2align 5
NtUserQueryInformationThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserQueryInputContext
    .p2align 5
NtUserQueryInputContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRaiseLowerShellWindow
    .p2align 5
NtUserRaiseLowerShellWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRealChildWindowFromPoint
    .p2align 5
NtUserRealChildWindowFromPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14F9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRealWaitMessageEx
    .p2align 5
NtUserRealWaitMessageEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14FA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRealizePalette
    .p2align 5
NtUserRealizePalette:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14FB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserReassociateQueueEventCompletionPacket
    .p2align 5
NtUserReassociateQueueEventCompletionPacket:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14FC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRedrawFrame
    .p2align 5
NtUserRedrawFrame:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14FD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRedrawFrameAndHook
    .p2align 5
NtUserRedrawFrameAndHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14FE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRedrawTitle
    .p2align 5
NtUserRedrawTitle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x14FF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterBSDRWindow
    .p2align 5
NtUserRegisterBSDRWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1500, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterCloakedNotification
    .p2align 5
NtUserRegisterCloakedNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1501, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterCoreMessagingEndPoint
    .p2align 5
NtUserRegisterCoreMessagingEndPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1502, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterDManipHook
    .p2align 5
NtUserRegisterDManipHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1503, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterEdgy
    .p2align 5
NtUserRegisterEdgy:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1504, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterErrorReportingDialog
    .p2align 5
NtUserRegisterErrorReportingDialog:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1505, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterForCustomDockTargets
    .p2align 5
NtUserRegisterForCustomDockTargets:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1506, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterForTooltipDismissNotification
    .p2align 5
NtUserRegisterForTooltipDismissNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1507, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterGhostWindow
    .p2align 5
NtUserRegisterGhostWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1508, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterHotKey
    .p2align 5
NtUserRegisterHotKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1509, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterLPK
    .p2align 5
NtUserRegisterLPK:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterLogonProcess
    .p2align 5
NtUserRegisterLogonProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterManipulationThread
    .p2align 5
NtUserRegisterManipulationThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterPointerDeviceNotifications
    .p2align 5
NtUserRegisterPointerDeviceNotifications:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterPointerInputTarget
    .p2align 5
NtUserRegisterPointerInputTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterRawInputDevices
    .p2align 5
NtUserRegisterRawInputDevices:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x150F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterServicesProcess
    .p2align 5
NtUserRegisterServicesProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1510, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterSessionPort
    .p2align 5
NtUserRegisterSessionPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1511, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterShellHookWindow
    .p2align 5
NtUserRegisterShellHookWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1512, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterSiblingFrostWindow
    .p2align 5
NtUserRegisterSiblingFrostWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1513, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterSystemThread
    .p2align 5
NtUserRegisterSystemThread:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1514, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterTasklist
    .p2align 5
NtUserRegisterTasklist:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1515, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterTouchHitTestingWindow
    .p2align 5
NtUserRegisterTouchHitTestingWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1516, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterTouchPadCapable
    .p2align 5
NtUserRegisterTouchPadCapable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1517, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterTouchpadCapableWindow
    .p2align 5
NtUserRegisterTouchpadCapableWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1518, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterUserApiHook
    .p2align 5
NtUserRegisterUserApiHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1519, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRegisterWindowArrangementCallout
    .p2align 5
NtUserRegisterWindowArrangementCallout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserReleaseCapture
    .p2align 5
NtUserReleaseCapture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserReleaseDC
    .p2align 5
NtUserReleaseDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserReleaseDwmHitTestWaiters
    .p2align 5
NtUserReleaseDwmHitTestWaiters:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteConnect
    .p2align 5
NtUserRemoteConnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteConnectState
    .p2align 5
NtUserRemoteConnectState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x151F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteConsoleShadowStop
    .p2align 5
NtUserRemoteConsoleShadowStop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1520, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteDisconnect
    .p2align 5
NtUserRemoteDisconnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1521, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteNotify
    .p2align 5
NtUserRemoteNotify:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1522, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemotePassthruDisable
    .p2align 5
NtUserRemotePassthruDisable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1523, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemotePassthruEnable
    .p2align 5
NtUserRemotePassthruEnable:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1524, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteReconnect
    .p2align 5
NtUserRemoteReconnect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1525, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteRedrawRectangle
    .p2align 5
NtUserRemoteRedrawRectangle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1526, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteRedrawScreen
    .p2align 5
NtUserRemoteRedrawScreen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1527, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteShadowCleanup
    .p2align 5
NtUserRemoteShadowCleanup:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1528, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteShadowSetup
    .p2align 5
NtUserRemoteShadowSetup:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1529, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteShadowStart
    .p2align 5
NtUserRemoteShadowStart:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteShadowStop
    .p2align 5
NtUserRemoteShadowStop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteStopScreenUpdates
    .p2align 5
NtUserRemoteStopScreenUpdates:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoteThinwireStats
    .p2align 5
NtUserRemoteThinwireStats:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoveClipboardFormatListener
    .p2align 5
NtUserRemoveClipboardFormatListener:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoveInjectionDevice
    .p2align 5
NtUserRemoveInjectionDevice:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x152F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoveQueueCompletion
    .p2align 5
NtUserRemoveQueueCompletion:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1530, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRemoveVisualIdentifier
    .p2align 5
NtUserRemoveVisualIdentifier:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1531, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserReplyMessage
    .p2align 5
NtUserReplyMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1532, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserReportInertia
    .p2align 5
NtUserReportInertia:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1533, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRequestLowLatencyProfile
    .p2align 5
NtUserRequestLowLatencyProfile:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1534, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserResetDblClk
    .p2align 5
NtUserResetDblClk:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1535, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserResolveDesktopForWOW
    .p2align 5
NtUserResolveDesktopForWOW:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1536, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserRestoreWindowDpiChanges
    .p2align 5
NtUserRestoreWindowDpiChanges:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1537, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserScaleSystemMetricForDPIWithoutCache
    .p2align 5
NtUserScaleSystemMetricForDPIWithoutCache:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1538, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserScheduleDispatchNotification
    .p2align 5
NtUserScheduleDispatchNotification:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1539, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSendEventMessage
    .p2align 5
NtUserSendEventMessage:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSendInteractiveControlHapticsReport
    .p2align 5
NtUserSendInteractiveControlHapticsReport:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetActivationFilter
    .p2align 5
NtUserSetActivationFilter:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetActiveProcessForMonitor
    .p2align 5
NtUserSetActiveProcessForMonitor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetAdditionalForegroundBoostProcesses
    .p2align 5
NtUserSetAdditionalForegroundBoostProcesses:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetAdditionalPowerThrottlingProcess
    .p2align 5
NtUserSetAdditionalPowerThrottlingProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x153F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetAppImeLevel
    .p2align 5
NtUserSetAppImeLevel:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1540, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetAutoRotation
    .p2align 5
NtUserSetAutoRotation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1541, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetBridgeWindowChild
    .p2align 5
NtUserSetBridgeWindowChild:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1542, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetBrokeredForeground
    .p2align 5
NtUserSetBrokeredForeground:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1543, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCalibrationData
    .p2align 5
NtUserSetCalibrationData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1544, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCancelRotationDelayHintWindow
    .p2align 5
NtUserSetCancelRotationDelayHintWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1545, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCaretBlinkTime
    .p2align 5
NtUserSetCaretBlinkTime:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1546, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCaretPos
    .p2align 5
NtUserSetCaretPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1547, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetChildWindowNoActivate
    .p2align 5
NtUserSetChildWindowNoActivate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1548, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetClassWord
    .p2align 5
NtUserSetClassWord:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1549, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCoreWindow
    .p2align 5
NtUserSetCoreWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCoreWindowPartner
    .p2align 5
NtUserSetCoreWindowPartner:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCoveredWindowStates
    .p2align 5
NtUserSetCoveredWindowStates:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCursorIconDataEx
    .p2align 5
NtUserSetCursorIconDataEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetCursorPos
    .p2align 5
NtUserSetCursorPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDesktopColorTransform
    .p2align 5
NtUserSetDesktopColorTransform:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x154F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDesktopVisualInputSink
    .p2align 5
NtUserSetDesktopVisualInputSink:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1550, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDialogControlDpiChangeBehavior
    .p2align 5
NtUserSetDialogControlDpiChangeBehavior:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1551, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDialogPointer
    .p2align 5
NtUserSetDialogPointer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1552, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDialogSystemMenu
    .p2align 5
NtUserSetDialogSystemMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1553, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDisplayAutoRotationPreferences
    .p2align 5
NtUserSetDisplayAutoRotationPreferences:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1554, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDisplayConfig
    .p2align 5
NtUserSetDisplayConfig:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1555, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDisplayMapping
    .p2align 5
NtUserSetDisplayMapping:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1556, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDoubleClickTime
    .p2align 5
NtUserSetDoubleClickTime:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1557, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetDpiForWindow
    .p2align 5
NtUserSetDpiForWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1558, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetFeatureReportResponse
    .p2align 5
NtUserSetFeatureReportResponse:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1559, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetForegroundRedirectionForActivationObject
    .p2align 5
NtUserSetForegroundRedirectionForActivationObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetForegroundWindow
    .p2align 5
NtUserSetForegroundWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetForegroundWindowForApplication
    .p2align 5
NtUserSetForegroundWindowForApplication:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetFullscreenMagnifierOffsetsDWMUpdated
    .p2align 5
NtUserSetFullscreenMagnifierOffsetsDWMUpdated:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetGUISystemInformation
    .p2align 5
NtUserSetGUISystemInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetGestureConfig
    .p2align 5
NtUserSetGestureConfig:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x155F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetImeHotKey
    .p2align 5
NtUserSetImeHotKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1560, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetImeInfoEx
    .p2align 5
NtUserSetImeInfoEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1561, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetImeOwnerWindow
    .p2align 5
NtUserSetImeOwnerWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1562, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetInputServiceState
    .p2align 5
NtUserSetInputServiceState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1563, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetInteractiveControlFocus
    .p2align 5
NtUserSetInteractiveControlFocus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1564, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetInteractiveCtrlRotationAngle
    .p2align 5
NtUserSetInteractiveCtrlRotationAngle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1565, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetInternalWindowPos
    .p2align 5
NtUserSetInternalWindowPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1566, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetJobUILimits
    .p2align 5
NtUserSetJobUILimits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1567, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetLayeredWindowAttributes
    .p2align 5
NtUserSetLayeredWindowAttributes:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1568, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMagnificationDesktopMagnifierOffsetsDWMUpdated
    .p2align 5
NtUserSetMagnificationDesktopMagnifierOffsetsDWMUpdated:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1569, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetManipulationInputTarget
    .p2align 5
NtUserSetManipulationInputTarget:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMaxTouchpadSensitivity
    .p2align 5
NtUserSetMaxTouchpadSensitivity:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMenu
    .p2align 5
NtUserSetMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMenuContextHelpId
    .p2align 5
NtUserSetMenuContextHelpId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMenuFlagRtoL
    .p2align 5
NtUserSetMenuFlagRtoL:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMessageExtraInfo
    .p2align 5
NtUserSetMessageExtraInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x156F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMirrorRendering
    .p2align 5
NtUserSetMirrorRendering:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1570, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetModernAppWindow
    .p2align 5
NtUserSetModernAppWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1571, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMonitorWorkArea
    .p2align 5
NtUserSetMonitorWorkArea:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1572, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetMsgBox
    .p2align 5
NtUserSetMsgBox:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1573, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetObjectInformation
    .p2align 5
NtUserSetObjectInformation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1574, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessDefaultLayout
    .p2align 5
NtUserSetProcessDefaultLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1575, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessDpiAwarenessContext
    .p2align 5
NtUserSetProcessDpiAwarenessContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1576, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessInteractionFlags
    .p2align 5
NtUserSetProcessInteractionFlags:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1577, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessLaunchForegroundPolicy
    .p2align 5
NtUserSetProcessLaunchForegroundPolicy:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1578, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessMousewheelRoutingMode
    .p2align 5
NtUserSetProcessMousewheelRoutingMode:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1579, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessRestrictionExemption
    .p2align 5
NtUserSetProcessRestrictionExemption:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessUIAccessZorder
    .p2align 5
NtUserSetProcessUIAccessZorder:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProcessWin32Capabilities
    .p2align 5
NtUserSetProcessWin32Capabilities:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProgmanWindow
    .p2align 5
NtUserSetProgmanWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetProp2
    .p2align 5
NtUserSetProp2:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSharedWindowData
    .p2align 5
NtUserSetSharedWindowData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x157F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetShellChangeNotifyHWND
    .p2align 5
NtUserSetShellChangeNotifyHWND:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1580, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetShellSpecialWindow
    .p2align 5
NtUserSetShellSpecialWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1581, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetShellWindowEx
    .p2align 5
NtUserSetShellWindowEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1582, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSysColors
    .p2align 5
NtUserSetSysColors:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1583, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSysMenu
    .p2align 5
NtUserSetSysMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1584, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSystemContentRects
    .p2align 5
NtUserSetSystemContentRects:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1585, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSystemCursor
    .p2align 5
NtUserSetSystemCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1586, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetSystemTimer
    .p2align 5
NtUserSetSystemTimer:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1587, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetTSFEventState
    .p2align 5
NtUserSetTSFEventState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1588, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetTargetForResourceBrokering
    .p2align 5
NtUserSetTargetForResourceBrokering:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1589, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetTaskmanWindow
    .p2align 5
NtUserSetTaskmanWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetThreadInputBlocked
    .p2align 5
NtUserSetThreadInputBlocked:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetThreadLayoutHandles
    .p2align 5
NtUserSetThreadLayoutHandles:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetThreadQueueMergeSetting
    .p2align 5
NtUserSetThreadQueueMergeSetting:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetUserObjectCapability
    .p2align 5
NtUserSetUserObjectCapability:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetVisible
    .p2align 5
NtUserSetVisible:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x158F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWaitForQueueAttach
    .p2align 5
NtUserSetWaitForQueueAttach:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1590, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWatermarkStrings
    .p2align 5
NtUserSetWatermarkStrings:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1591, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowBand
    .p2align 5
NtUserSetWindowBand:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1592, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowCompositionAttribute
    .p2align 5
NtUserSetWindowCompositionAttribute:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1593, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowCompositionTransition
    .p2align 5
NtUserSetWindowCompositionTransition:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1594, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowContextHelpId
    .p2align 5
NtUserSetWindowContextHelpId:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1595, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowDisplayAffinity
    .p2align 5
NtUserSetWindowDisplayAffinity:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1596, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowFeedbackSetting
    .p2align 5
NtUserSetWindowFeedbackSetting:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1597, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowMessageCapability
    .p2align 5
NtUserSetWindowMessageCapability:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1598, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowRgnEx
    .p2align 5
NtUserSetWindowRgnEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1599, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowShowState
    .p2align 5
NtUserSetWindowShowState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowState
    .p2align 5
NtUserSetWindowState:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowStationUser
    .p2align 5
NtUserSetWindowStationUser:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellForegroundBoostProcess
    .p2align 5
NtUserShellForegroundBoostProcess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellHandwritingDelegateInput
    .p2align 5
NtUserShellHandwritingDelegateInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellHandwritingHandleDelegatedInput
    .p2align 5
NtUserShellHandwritingHandleDelegatedInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x159F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellHandwritingUndelegateInput
    .p2align 5
NtUserShellHandwritingUndelegateInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellMigrateWindow
    .p2align 5
NtUserShellMigrateWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellRegisterHotKey
    .p2align 5
NtUserShellRegisterHotKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShellSetWindowPos
    .p2align 5
NtUserShellSetWindowPos:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowCursor
    .p2align 5
NtUserShowCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowOwnedPopups
    .p2align 5
NtUserShowOwnedPopups:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowStartGlass
    .p2align 5
NtUserShowStartGlass:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShowSystemCursor
    .p2align 5
NtUserShowSystemCursor:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShutdownBlockReasonCreate
    .p2align 5
NtUserShutdownBlockReasonCreate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShutdownBlockReasonQuery
    .p2align 5
NtUserShutdownBlockReasonQuery:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15A9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserShutdownReasonDestroy
    .p2align 5
NtUserShutdownReasonDestroy:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15AA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSignalRedirectionStartComplete
    .p2align 5
NtUserSignalRedirectionStartComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15AB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSlicerControl
    .p2align 5
NtUserSlicerControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15AC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSoundSentry
    .p2align 5
NtUserSoundSentry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15AD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserStopAndEndInertia
    .p2align 5
NtUserStopAndEndInertia:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15AE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSuppressWindowDisplayChange
    .p2align 5
NtUserSuppressWindowDisplayChange:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15AF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSwapMouseButton
    .p2align 5
NtUserSwapMouseButton:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSwitchDesktop
    .p2align 5
NtUserSwitchDesktop:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSwitchToThisWindow
    .p2align 5
NtUserSwitchToThisWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSystemParametersInfoForDpi
    .p2align 5
NtUserSystemParametersInfoForDpi:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTestForInteractiveUser
    .p2align 5
NtUserTestForInteractiveUser:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserThreadMessageQueueAttached
    .p2align 5
NtUserThreadMessageQueueAttached:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTraceLoggingSendMixedModeTelemetry
    .p2align 5
NtUserTraceLoggingSendMixedModeTelemetry:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTrackPopupMenuEx
    .p2align 5
NtUserTrackPopupMenuEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTransformPoint
    .p2align 5
NtUserTransformPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserTransformRect
    .p2align 5
NtUserTransformRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15B9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUndelegateInput
    .p2align 5
NtUserUndelegateInput:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15BA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnhookWindowsHook
    .p2align 5
NtUserUnhookWindowsHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15BB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnloadKeyboardLayout
    .p2align 5
NtUserUnloadKeyboardLayout:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15BC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnlockWindowStation
    .p2align 5
NtUserUnlockWindowStation:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15BD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnregisterHotKey
    .p2align 5
NtUserUnregisterHotKey:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15BE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnregisterSessionPort
    .p2align 5
NtUserUnregisterSessionPort:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15BF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUnregisterUserApiHook
    .p2align 5
NtUserUnregisterUserApiHook:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateClientRect
    .p2align 5
NtUserUpdateClientRect:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateDefaultDesktopThumbnail
    .p2align 5
NtUserUpdateDefaultDesktopThumbnail:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateInputContext
    .p2align 5
NtUserUpdateInputContext:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateInstance
    .p2align 5
NtUserUpdateInstance:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateLayeredWindow
    .p2align 5
NtUserUpdateLayeredWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdatePerUserImmEnabling
    .p2align 5
NtUserUpdatePerUserImmEnabling:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdatePerUserSystemParameters
    .p2align 5
NtUserUpdatePerUserSystemParameters:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateWindow
    .p2align 5
NtUserUpdateWindow:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateWindowInputSinkHints
    .p2align 5
NtUserUpdateWindowInputSinkHints:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15C9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateWindowTrackingInfo
    .p2align 5
NtUserUpdateWindowTrackingInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15CA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUpdateWindows
    .p2align 5
NtUserUpdateWindows:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15CB, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUserHandleGrantAccess
    .p2align 5
NtUserUserHandleGrantAccess:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15CC, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserUserPowerCalloutWorker
    .p2align 5
NtUserUserPowerCalloutWorker:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15CD, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserValidateHandleSecure
    .p2align 5
NtUserValidateHandleSecure:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15CE, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserValidateRgn
    .p2align 5
NtUserValidateRgn:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15CF, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWaitAvailableMessageEx
    .p2align 5
NtUserWaitAvailableMessageEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D0, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWaitForInputIdle
    .p2align 5
NtUserWaitForInputIdle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D1, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWaitForRedirectionStartComplete
    .p2align 5
NtUserWaitForRedirectionStartComplete:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D2, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWakeRITForShutdown
    .p2align 5
NtUserWakeRITForShutdown:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D3, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWindowFromDC
    .p2align 5
NtUserWindowFromDC:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D4, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserWindowFromPhysicalPoint
    .p2align 5
NtUserWindowFromPhysicalPoint:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D5, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserZapActiveAndFocus
    .p2align 5
NtUserZapActiveAndFocus:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D6, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtValidateCompositionSurfaceHandle
    .p2align 5
NtValidateCompositionSurfaceHandle:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D7, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtVisualCaptureBits
    .p2align 5
NtVisualCaptureBits:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D8, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetClassLongPtr
    .p2align 5
NtUserSetClassLongPtr:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15D9, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserSetWindowLongPtr
    .p2align 5
NtUserSetWindowLongPtr:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x15DA, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallNoParam
    .p2align 5
NtUserCallNoParam:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1700, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallOneParam
    .p2align 5
NtUserCallOneParam:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1701, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallTwoParam
    .p2align 5
NtUserCallTwoParam:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1702, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallHwnd
    .p2align 5
NtUserCallHwnd:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1703, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCallHwndParam
    .p2align 5
NtUserCallHwndParam:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1704, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserCreateEmptyCurObject
    .p2align 5
NtUserCreateEmptyCurObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1705, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiGetStockObject
    .p2align 5
NtGdiGetStockObject:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1706, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetViewportOrgEx
    .p2align 5
NtGdiSetViewportOrgEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1707, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiSetWindowOrgEx
    .p2align 5
NtGdiSetWindowOrgEx:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1708, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeGetMenuItemInfo
    .p2align 5
NtUserMimeGetMenuItemInfo:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1709, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeCallMenu
    .p2align 5
NtUserMimeCallMenu:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGpuOpen
    .p2align 5
NtGdiMimeGpuOpen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGpuAddBlock
    .p2align 5
NtGdiMimeGpuAddBlock:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGpuKick
    .p2align 5
NtGdiMimeGpuKick:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGpuWait
    .p2align 5
NtGdiMimeGpuWait:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGpuGetSurface
    .p2align 5
NtGdiMimeGpuGetSurface:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x170F, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeAudioCreate
    .p2align 5
NtGdiMimeAudioCreate:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1710, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeAudioControl
    .p2align 5
NtGdiMimeAudioControl:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1711, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeVkOpen
    .p2align 5
NtGdiMimeVkOpen:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1712, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeVkAlloc
    .p2align 5
NtGdiMimeVkAlloc:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1713, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeVkFree
    .p2align 5
NtGdiMimeVkFree:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1714, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeVkKick
    .p2align 5
NtGdiMimeVkKick:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1715, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeVkWait
    .p2align 5
NtGdiMimeVkWait:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1716, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGamma
    .p2align 5
NtGdiMimeGamma:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1717, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeSetExtent
    .p2align 5
NtGdiMimeSetExtent:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1718, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeDnd
    .p2align 5
NtUserMimeDnd:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1719, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeDndData
    .p2align 5
NtUserMimeDndData:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x171A, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeDndProvide
    .p2align 5
NtUserMimeDndProvide:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x171B, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeIme
    .p2align 5
NtUserMimeIme:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x171C, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeTray
    .p2align 5
NtUserMimeTray:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x171D, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeExitWindows
    .p2align 5
NtUserMimeExitWindows:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x171E, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeCapture
    .p2align 5
NtUserMimeCapture:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1750, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtGdiMimeGfxShare
    .p2align 5
NtGdiMimeGfxShare:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1760, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
    .globl NtUserMimeShell
    .p2align 5
NtUserMimeShell:
    .byte 0x4C, 0x8B, 0xD1
    movl $0x1770, %eax
    testb $1, 0x7FFE0308
    jne 1f
    syscall
    ret
1:  int $0x2E
    ret
