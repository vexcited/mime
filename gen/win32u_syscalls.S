    .text
    .globl NtUserGetThreadState
    .p2align 4
NtUserGetThreadState:
    svc #0x1000
    ret
    .globl NtUserPeekMessage
    .p2align 4
NtUserPeekMessage:
    svc #0x1001
    ret
    .globl NtUserGetKeyState
    .p2align 4
NtUserGetKeyState:
    svc #0x1002
    ret
    .globl NtUserInvalidateRect
    .p2align 4
NtUserInvalidateRect:
    svc #0x1003
    ret
    .globl NtUserGetMessage
    .p2align 4
NtUserGetMessage:
    svc #0x1004
    ret
    .globl NtUserMessageCall
    .p2align 4
NtUserMessageCall:
    svc #0x1005
    ret
    .globl NtGdiBitBlt
    .p2align 4
NtGdiBitBlt:
    svc #0x1006
    ret
    .globl NtGdiGetCharSet
    .p2align 4
NtGdiGetCharSet:
    svc #0x1007
    ret
    .globl NtUserGetDC
    .p2align 4
NtUserGetDC:
    svc #0x1008
    ret
    .globl NtGdiSelectBitmap
    .p2align 4
NtGdiSelectBitmap:
    svc #0x1009
    ret
    .globl NtUserWaitMessage
    .p2align 4
NtUserWaitMessage:
    svc #0x100A
    ret
    .globl NtUserTranslateMessage
    .p2align 4
NtUserTranslateMessage:
    svc #0x100B
    ret
    .globl NtUserGetProp
    .p2align 4
NtUserGetProp:
    svc #0x100C
    ret
    .globl NtUserPostMessage
    .p2align 4
NtUserPostMessage:
    svc #0x100D
    ret
    .globl NtUserQueryWindow
    .p2align 4
NtUserQueryWindow:
    svc #0x100E
    ret
    .globl NtUserTranslateAccelerator
    .p2align 4
NtUserTranslateAccelerator:
    svc #0x100F
    ret
    .globl NtGdiFlush
    .p2align 4
NtGdiFlush:
    svc #0x1010
    ret
    .globl NtUserRedrawWindow
    .p2align 4
NtUserRedrawWindow:
    svc #0x1011
    ret
    .globl NtUserWindowFromPoint
    .p2align 4
NtUserWindowFromPoint:
    svc #0x1012
    ret
    .globl NtUserCallMsgFilter
    .p2align 4
NtUserCallMsgFilter:
    svc #0x1013
    ret
    .globl NtUserValidateTimerCallback
    .p2align 4
NtUserValidateTimerCallback:
    svc #0x1014
    ret
    .globl NtUserBeginPaint
    .p2align 4
NtUserBeginPaint:
    svc #0x1015
    ret
    .globl NtUserSetTimer
    .p2align 4
NtUserSetTimer:
    svc #0x1016
    ret
    .globl NtUserEndPaint
    .p2align 4
NtUserEndPaint:
    svc #0x1017
    ret
    .globl NtUserSetCursor
    .p2align 4
NtUserSetCursor:
    svc #0x1018
    ret
    .globl NtUserKillTimer
    .p2align 4
NtUserKillTimer:
    svc #0x1019
    ret
    .globl NtUserBuildHwndList
    .p2align 4
NtUserBuildHwndList:
    svc #0x101A
    ret
    .globl NtUserSelectPalette
    .p2align 4
NtUserSelectPalette:
    svc #0x101B
    ret
    .globl NtUserCallNextHookEx
    .p2align 4
NtUserCallNextHookEx:
    svc #0x101C
    ret
    .globl NtUserHideCaret
    .p2align 4
NtUserHideCaret:
    svc #0x101D
    ret
    .globl NtGdiIntersectClipRect
    .p2align 4
NtGdiIntersectClipRect:
    svc #0x101E
    ret
    .globl NtUserGetProcessWindowStation
    .p2align 4
NtUserGetProcessWindowStation:
    svc #0x101F
    ret
    .globl NtGdiDeleteObjectApp
    .p2align 4
NtGdiDeleteObjectApp:
    svc #0x1020
    ret
    .globl NtUserSetWindowPos
    .p2align 4
NtUserSetWindowPos:
    svc #0x1021
    ret
    .globl NtUserShowCaret
    .p2align 4
NtUserShowCaret:
    svc #0x1022
    ret
    .globl NtUserEndDeferWindowPosEx
    .p2align 4
NtUserEndDeferWindowPosEx:
    svc #0x1023
    ret
    .globl NtUserVkKeyScanEx
    .p2align 4
NtUserVkKeyScanEx:
    svc #0x1024
    ret
    .globl NtGdiSetDIBitsToDeviceInternal
    .p2align 4
NtGdiSetDIBitsToDeviceInternal:
    svc #0x1025
    ret
    .globl NtGdiGetRandomRgn
    .p2align 4
NtGdiGetRandomRgn:
    svc #0x1026
    ret
    .globl NtUserCopyAcceleratorTable
    .p2align 4
NtUserCopyAcceleratorTable:
    svc #0x1027
    ret
    .globl NtUserNotifyWinEvent
    .p2align 4
NtUserNotifyWinEvent:
    svc #0x1028
    ret
    .globl NtGdiExtSelectClipRgn
    .p2align 4
NtGdiExtSelectClipRgn:
    svc #0x1029
    ret
    .globl NtUserIsClipboardFormatAvailable
    .p2align 4
NtUserIsClipboardFormatAvailable:
    svc #0x102A
    ret
    .globl NtUserSetScrollInfo
    .p2align 4
NtUserSetScrollInfo:
    svc #0x102B
    ret
    .globl NtGdiStretchBlt
    .p2align 4
NtGdiStretchBlt:
    svc #0x102C
    ret
    .globl NtUserCreateCaret
    .p2align 4
NtUserCreateCaret:
    svc #0x102D
    ret
    .globl NtGdiRectVisible
    .p2align 4
NtGdiRectVisible:
    svc #0x102E
    ret
    .globl NtGdiCombineRgn
    .p2align 4
NtGdiCombineRgn:
    svc #0x102F
    ret
    .globl NtGdiGetDCObject
    .p2align 4
NtGdiGetDCObject:
    svc #0x1030
    ret
    .globl NtUserDispatchMessage
    .p2align 4
NtUserDispatchMessage:
    svc #0x1031
    ret
    .globl NtUserRegisterWindowMessage
    .p2align 4
NtUserRegisterWindowMessage:
    svc #0x1032
    ret
    .globl NtGdiExtTextOutW
    .p2align 4
NtGdiExtTextOutW:
    svc #0x1033
    ret
    .globl NtGdiSelectFont
    .p2align 4
NtGdiSelectFont:
    svc #0x1034
    ret
    .globl NtGdiRestoreDC
    .p2align 4
NtGdiRestoreDC:
    svc #0x1035
    ret
    .globl NtGdiSaveDC
    .p2align 4
NtGdiSaveDC:
    svc #0x1036
    ret
    .globl NtUserGetForegroundWindow
    .p2align 4
NtUserGetForegroundWindow:
    svc #0x1037
    ret
    .globl NtUserShowScrollBar
    .p2align 4
NtUserShowScrollBar:
    svc #0x1038
    ret
    .globl NtUserFindExistingCursorIcon
    .p2align 4
NtUserFindExistingCursorIcon:
    svc #0x1039
    ret
    .globl NtGdiGetDCDword
    .p2align 4
NtGdiGetDCDword:
    svc #0x103A
    ret
    .globl NtGdiGetRegionData
    .p2align 4
NtGdiGetRegionData:
    svc #0x103B
    ret
    .globl NtGdiLineTo
    .p2align 4
NtGdiLineTo:
    svc #0x103C
    ret
    .globl NtUserSystemParametersInfo
    .p2align 4
NtUserSystemParametersInfo:
    svc #0x103D
    ret
    .globl NtGdiGetAppClipBox
    .p2align 4
NtGdiGetAppClipBox:
    svc #0x103E
    ret
    .globl NtUserGetAsyncKeyState
    .p2align 4
NtUserGetAsyncKeyState:
    svc #0x103F
    ret
    .globl NtUserGetCPD
    .p2align 4
NtUserGetCPD:
    svc #0x1040
    ret
    .globl NtUserRemoveProp
    .p2align 4
NtUserRemoveProp:
    svc #0x1041
    ret
    .globl NtGdiDoPalette
    .p2align 4
NtGdiDoPalette:
    svc #0x1042
    ret
    .globl NtGdiPolyPolyDraw
    .p2align 4
NtGdiPolyPolyDraw:
    svc #0x1043
    ret
    .globl NtUserSetCapture
    .p2align 4
NtUserSetCapture:
    svc #0x1044
    ret
    .globl NtUserEnumDisplayMonitors
    .p2align 4
NtUserEnumDisplayMonitors:
    svc #0x1045
    ret
    .globl NtGdiCreateCompatibleBitmap
    .p2align 4
NtGdiCreateCompatibleBitmap:
    svc #0x1046
    ret
    .globl NtUserSetProp
    .p2align 4
NtUserSetProp:
    svc #0x1047
    ret
    .globl NtGdiGetTextCharsetInfo
    .p2align 4
NtGdiGetTextCharsetInfo:
    svc #0x1048
    ret
    .globl NtUserSBGetParms
    .p2align 4
NtUserSBGetParms:
    svc #0x1049
    ret
    .globl NtUserGetIconInfo
    .p2align 4
NtUserGetIconInfo:
    svc #0x104A
    ret
    .globl NtUserExcludeUpdateRgn
    .p2align 4
NtUserExcludeUpdateRgn:
    svc #0x104B
    ret
    .globl NtUserSetFocus
    .p2align 4
NtUserSetFocus:
    svc #0x104C
    ret
    .globl NtGdiExtGetObjectW
    .p2align 4
NtGdiExtGetObjectW:
    svc #0x104D
    ret
    .globl NtUserGetUpdateRect
    .p2align 4
NtUserGetUpdateRect:
    svc #0x104E
    ret
    .globl NtGdiCreateCompatibleDC
    .p2align 4
NtGdiCreateCompatibleDC:
    svc #0x104F
    ret
    .globl NtUserGetClipboardSequenceNumber
    .p2align 4
NtUserGetClipboardSequenceNumber:
    svc #0x1050
    ret
    .globl NtGdiCreatePen
    .p2align 4
NtGdiCreatePen:
    svc #0x1051
    ret
    .globl NtUserShowWindow
    .p2align 4
NtUserShowWindow:
    svc #0x1052
    ret
    .globl NtUserGetKeyboardLayoutList
    .p2align 4
NtUserGetKeyboardLayoutList:
    svc #0x1053
    ret
    .globl NtGdiPatBlt
    .p2align 4
NtGdiPatBlt:
    svc #0x1054
    ret
    .globl NtUserMapVirtualKeyEx
    .p2align 4
NtUserMapVirtualKeyEx:
    svc #0x1055
    ret
    .globl NtUserSetWindowLong
    .p2align 4
NtUserSetWindowLong:
    svc #0x1056
    ret
    .globl NtGdiHfontCreate
    .p2align 4
NtGdiHfontCreate:
    svc #0x1057
    ret
    .globl NtUserMoveWindow
    .p2align 4
NtUserMoveWindow:
    svc #0x1058
    ret
    .globl NtUserPostThreadMessage
    .p2align 4
NtUserPostThreadMessage:
    svc #0x1059
    ret
    .globl NtUserDrawIconEx
    .p2align 4
NtUserDrawIconEx:
    svc #0x105A
    ret
    .globl NtUserGetSystemMenu
    .p2align 4
NtUserGetSystemMenu:
    svc #0x105B
    ret
    .globl NtGdiDrawStream
    .p2align 4
NtGdiDrawStream:
    svc #0x105C
    ret
    .globl NtUserInternalGetWindowText
    .p2align 4
NtUserInternalGetWindowText:
    svc #0x105D
    ret
    .globl NtUserGetWindowDC
    .p2align 4
NtUserGetWindowDC:
    svc #0x105E
    ret
    .globl NtGdiInvertRgn
    .p2align 4
NtGdiInvertRgn:
    svc #0x105F
    ret
    .globl NtGdiGetRgnBox
    .p2align 4
NtGdiGetRgnBox:
    svc #0x1060
    ret
    .globl NtGdiGetAndSetDCDword
    .p2align 4
NtGdiGetAndSetDCDword:
    svc #0x1061
    ret
    .globl NtGdiMaskBlt
    .p2align 4
NtGdiMaskBlt:
    svc #0x1062
    ret
    .globl NtGdiGetWidthTable
    .p2align 4
NtGdiGetWidthTable:
    svc #0x1063
    ret
    .globl NtUserScrollDC
    .p2align 4
NtUserScrollDC:
    svc #0x1064
    ret
    .globl NtUserGetObjectInformation
    .p2align 4
NtUserGetObjectInformation:
    svc #0x1065
    ret
    .globl NtGdiCreateBitmap
    .p2align 4
NtGdiCreateBitmap:
    svc #0x1066
    ret
    .globl NtUserFindWindowEx
    .p2align 4
NtUserFindWindowEx:
    svc #0x1067
    ret
    .globl NtGdiPolyPatBlt
    .p2align 4
NtGdiPolyPatBlt:
    svc #0x1068
    ret
    .globl NtUserUnhookWindowsHookEx
    .p2align 4
NtUserUnhookWindowsHookEx:
    svc #0x1069
    ret
    .globl NtGdiGetNearestColor
    .p2align 4
NtGdiGetNearestColor:
    svc #0x106A
    ret
    .globl NtGdiTransformPoints
    .p2align 4
NtGdiTransformPoints:
    svc #0x106B
    ret
    .globl NtGdiGetDCPoint
    .p2align 4
NtGdiGetDCPoint:
    svc #0x106C
    ret
    .globl NtGdiCreateDIBBrush
    .p2align 4
NtGdiCreateDIBBrush:
    svc #0x106D
    ret
    .globl NtGdiGetTextMetricsW
    .p2align 4
NtGdiGetTextMetricsW:
    svc #0x106E
    ret
    .globl NtUserCreateWindowEx
    .p2align 4
NtUserCreateWindowEx:
    svc #0x106F
    ret
    .globl NtUserSetParent
    .p2align 4
NtUserSetParent:
    svc #0x1070
    ret
    .globl NtUserGetKeyboardState
    .p2align 4
NtUserGetKeyboardState:
    svc #0x1071
    ret
    .globl NtUserToUnicodeEx
    .p2align 4
NtUserToUnicodeEx:
    svc #0x1072
    ret
    .globl NtUserGetControlBrush
    .p2align 4
NtUserGetControlBrush:
    svc #0x1073
    ret
    .globl NtUserGetClassName
    .p2align 4
NtUserGetClassName:
    svc #0x1074
    ret
    .globl NtGdiAlphaBlend
    .p2align 4
NtGdiAlphaBlend:
    svc #0x1075
    ret
    .globl NtGdiOffsetRgn
    .p2align 4
NtGdiOffsetRgn:
    svc #0x1076
    ret
    .globl NtUserDefSetText
    .p2align 4
NtUserDefSetText:
    svc #0x1077
    ret
    .globl NtGdiGetTextFaceW
    .p2align 4
NtGdiGetTextFaceW:
    svc #0x1078
    ret
    .globl NtGdiStretchDIBitsInternal
    .p2align 4
NtGdiStretchDIBitsInternal:
    svc #0x1079
    ret
    .globl NtUserSendInput
    .p2align 4
NtUserSendInput:
    svc #0x107A
    ret
    .globl NtUserGetThreadDesktop
    .p2align 4
NtUserGetThreadDesktop:
    svc #0x107B
    ret
    .globl NtGdiCreateRectRgn
    .p2align 4
NtGdiCreateRectRgn:
    svc #0x107C
    ret
    .globl NtGdiGetDIBitsInternal
    .p2align 4
NtGdiGetDIBitsInternal:
    svc #0x107D
    ret
    .globl NtUserGetUpdateRgn
    .p2align 4
NtUserGetUpdateRgn:
    svc #0x107E
    ret
    .globl NtGdiDeleteClientObj
    .p2align 4
NtGdiDeleteClientObj:
    svc #0x107F
    ret
    .globl NtUserGetIconSize
    .p2align 4
NtUserGetIconSize:
    svc #0x1080
    ret
    .globl NtUserFillWindow
    .p2align 4
NtUserFillWindow:
    svc #0x1081
    ret
    .globl NtGdiExtCreateRegion
    .p2align 4
NtGdiExtCreateRegion:
    svc #0x1082
    ret
    .globl NtGdiComputeXformCoefficients
    .p2align 4
NtGdiComputeXformCoefficients:
    svc #0x1083
    ret
    .globl NtUserSetWindowsHookEx
    .p2align 4
NtUserSetWindowsHookEx:
    svc #0x1084
    ret
    .globl NtUserNotifyProcessCreate
    .p2align 4
NtUserNotifyProcessCreate:
    svc #0x1085
    ret
    .globl NtGdiUnrealizeObject
    .p2align 4
NtGdiUnrealizeObject:
    svc #0x1086
    ret
    .globl NtUserGetTitleBarInfo
    .p2align 4
NtUserGetTitleBarInfo:
    svc #0x1087
    ret
    .globl NtGdiRectangle
    .p2align 4
NtGdiRectangle:
    svc #0x1088
    ret
    .globl NtUserSetThreadDesktop
    .p2align 4
NtUserSetThreadDesktop:
    svc #0x1089
    ret
    .globl NtUserGetDCEx
    .p2align 4
NtUserGetDCEx:
    svc #0x108A
    ret
    .globl NtUserGetScrollBarInfo
    .p2align 4
NtUserGetScrollBarInfo:
    svc #0x108B
    ret
    .globl NtGdiGetTextExtent
    .p2align 4
NtGdiGetTextExtent:
    svc #0x108C
    ret
    .globl NtUserSetWindowFNID
    .p2align 4
NtUserSetWindowFNID:
    svc #0x108D
    ret
    .globl NtGdiSetLayout
    .p2align 4
NtGdiSetLayout:
    svc #0x108E
    ret
    .globl NtUserCalcMenuBar
    .p2align 4
NtUserCalcMenuBar:
    svc #0x108F
    ret
    .globl NtUserThunkedMenuItemInfo
    .p2align 4
NtUserThunkedMenuItemInfo:
    svc #0x1090
    ret
    .globl NtGdiExcludeClipRect
    .p2align 4
NtGdiExcludeClipRect:
    svc #0x1091
    ret
    .globl NtGdiCreateDIBSection
    .p2align 4
NtGdiCreateDIBSection:
    svc #0x1092
    ret
    .globl NtGdiGetDCforBitmap
    .p2align 4
NtGdiGetDCforBitmap:
    svc #0x1093
    ret
    .globl NtUserDestroyCursor
    .p2align 4
NtUserDestroyCursor:
    svc #0x1094
    ret
    .globl NtUserDestroyWindow
    .p2align 4
NtUserDestroyWindow:
    svc #0x1095
    ret
    .globl NtGdiCreateDIBitmapInternal
    .p2align 4
NtGdiCreateDIBitmapInternal:
    svc #0x1096
    ret
    .globl NtUserOpenWindowStation
    .p2align 4
NtUserOpenWindowStation:
    svc #0x1097
    ret
    .globl NtUserSetCursorIconData
    .p2align 4
NtUserSetCursorIconData:
    svc #0x1098
    ret
    .globl NtUserCloseDesktop
    .p2align 4
NtUserCloseDesktop:
    svc #0x1099
    ret
    .globl NtUserOpenDesktop
    .p2align 4
NtUserOpenDesktop:
    svc #0x109A
    ret
    .globl NtUserSetProcessWindowStation
    .p2align 4
NtUserSetProcessWindowStation:
    svc #0x109B
    ret
    .globl NtUserGetAtomName
    .p2align 4
NtUserGetAtomName:
    svc #0x109C
    ret
    .globl NtGdiExtCreatePen
    .p2align 4
NtGdiExtCreatePen:
    svc #0x109D
    ret
    .globl NtGdiCreatePaletteInternal
    .p2align 4
NtGdiCreatePaletteInternal:
    svc #0x109E
    ret
    .globl NtGdiSetBrushOrg
    .p2align 4
NtGdiSetBrushOrg:
    svc #0x109F
    ret
    .globl NtUserBuildNameList
    .p2align 4
NtUserBuildNameList:
    svc #0x10A0
    ret
    .globl NtGdiSetPixel
    .p2align 4
NtGdiSetPixel:
    svc #0x10A1
    ret
    .globl NtUserRegisterClassExWOW
    .p2align 4
NtUserRegisterClassExWOW:
    svc #0x10A2
    ret
    .globl NtGdiCreatePatternBrushInternal
    .p2align 4
NtGdiCreatePatternBrushInternal:
    svc #0x10A3
    ret
    .globl NtUserGetAncestor
    .p2align 4
NtUserGetAncestor:
    svc #0x10A4
    ret
    .globl NtGdiGetOutlineTextMetricsInternalW
    .p2align 4
NtGdiGetOutlineTextMetricsInternalW:
    svc #0x10A5
    ret
    .globl NtGdiSetBitmapBits
    .p2align 4
NtGdiSetBitmapBits:
    svc #0x10A6
    ret
    .globl NtUserCloseWindowStation
    .p2align 4
NtUserCloseWindowStation:
    svc #0x10A7
    ret
    .globl NtUserGetDoubleClickTime
    .p2align 4
NtUserGetDoubleClickTime:
    svc #0x10A8
    ret
    .globl NtUserEnableScrollBar
    .p2align 4
NtUserEnableScrollBar:
    svc #0x10A9
    ret
    .globl NtGdiCreateSolidBrush
    .p2align 4
NtGdiCreateSolidBrush:
    svc #0x10AA
    ret
    .globl NtUserGetClassInfoEx
    .p2align 4
NtUserGetClassInfoEx:
    svc #0x10AB
    ret
    .globl NtGdiCreateClientObj
    .p2align 4
NtGdiCreateClientObj:
    svc #0x10AC
    ret
    .globl NtUserUnregisterClass
    .p2align 4
NtUserUnregisterClass:
    svc #0x10AD
    ret
    .globl NtUserDeleteMenu
    .p2align 4
NtUserDeleteMenu:
    svc #0x10AE
    ret
    .globl NtGdiRectInRegion
    .p2align 4
NtGdiRectInRegion:
    svc #0x10AF
    ret
    .globl NtUserScrollWindowEx
    .p2align 4
NtUserScrollWindowEx:
    svc #0x10B0
    ret
    .globl NtGdiGetPixel
    .p2align 4
NtGdiGetPixel:
    svc #0x10B1
    ret
    .globl NtUserSetClassLong
    .p2align 4
NtUserSetClassLong:
    svc #0x10B2
    ret
    .globl NtUserGetMenuBarInfo
    .p2align 4
NtUserGetMenuBarInfo:
    svc #0x10B3
    ret
    .globl NtGdiGetNearestPaletteIndex
    .p2align 4
NtGdiGetNearestPaletteIndex:
    svc #0x10B4
    ret
    .globl NtGdiGetCharWidthW
    .p2align 4
NtGdiGetCharWidthW:
    svc #0x10B5
    ret
    .globl NtUserInvalidateRgn
    .p2align 4
NtUserInvalidateRgn:
    svc #0x10B6
    ret
    .globl NtUserGetClipboardOwner
    .p2align 4
NtUserGetClipboardOwner:
    svc #0x10B7
    ret
    .globl NtUserSetWindowRgn
    .p2align 4
NtUserSetWindowRgn:
    svc #0x10B8
    ret
    .globl NtUserBitBltSysBmp
    .p2align 4
NtUserBitBltSysBmp:
    svc #0x10B9
    ret
    .globl NtGdiGetCharWidthInfo
    .p2align 4
NtGdiGetCharWidthInfo:
    svc #0x10BA
    ret
    .globl NtUserValidateRect
    .p2align 4
NtUserValidateRect:
    svc #0x10BB
    ret
    .globl NtUserCloseClipboard
    .p2align 4
NtUserCloseClipboard:
    svc #0x10BC
    ret
    .globl NtUserOpenClipboard
    .p2align 4
NtUserOpenClipboard:
    svc #0x10BD
    ret
    .globl NtUserSetClipboardData
    .p2align 4
NtUserSetClipboardData:
    svc #0x10BE
    ret
    .globl NtUserEnableMenuItem
    .p2align 4
NtUserEnableMenuItem:
    svc #0x10BF
    ret
    .globl NtUserAlterWindowStyle
    .p2align 4
NtUserAlterWindowStyle:
    svc #0x10C0
    ret
    .globl NtGdiFillRgn
    .p2align 4
NtGdiFillRgn:
    svc #0x10C1
    ret
    .globl NtUserGetWindowPlacement
    .p2align 4
NtUserGetWindowPlacement:
    svc #0x10C2
    ret
    .globl NtGdiModifyWorldTransform
    .p2align 4
NtGdiModifyWorldTransform:
    svc #0x10C3
    ret
    .globl NtGdiGetFontData
    .p2align 4
NtGdiGetFontData:
    svc #0x10C4
    ret
    .globl NtUserGetOpenClipboardWindow
    .p2align 4
NtUserGetOpenClipboardWindow:
    svc #0x10C5
    ret
    .globl NtUserSetThreadState
    .p2align 4
NtUserSetThreadState:
    svc #0x10C6
    ret
    .globl NtGdiOpenDCW
    .p2align 4
NtGdiOpenDCW:
    svc #0x10C7
    ret
    .globl NtUserTrackMouseEvent
    .p2align 4
NtUserTrackMouseEvent:
    svc #0x10C8
    ret
    .globl NtGdiGetTransform
    .p2align 4
NtGdiGetTransform:
    svc #0x10C9
    ret
    .globl NtUserDestroyMenu
    .p2align 4
NtUserDestroyMenu:
    svc #0x10CA
    ret
    .globl NtGdiGetBitmapBits
    .p2align 4
NtGdiGetBitmapBits:
    svc #0x10CB
    ret
    .globl NtUserConsoleControl
    .p2align 4
NtUserConsoleControl:
    svc #0x10CC
    ret
    .globl NtUserSetActiveWindow
    .p2align 4
NtUserSetActiveWindow:
    svc #0x10CD
    ret
    .globl NtUserSetInformationThread
    .p2align 4
NtUserSetInformationThread:
    svc #0x10CE
    ret
    .globl NtUserSetWindowPlacement
    .p2align 4
NtUserSetWindowPlacement:
    svc #0x10CF
    ret
    .globl NtUserGetControlColor
    .p2align 4
NtUserGetControlColor:
    svc #0x10D0
    ret
    .globl NtGdiSetMetaRgn
    .p2align 4
NtGdiSetMetaRgn:
    svc #0x10D1
    ret
    .globl NtGdiSetMiterLimit
    .p2align 4
NtGdiSetMiterLimit:
    svc #0x10D2
    ret
    .globl NtGdiSetVirtualResolution
    .p2align 4
NtGdiSetVirtualResolution:
    svc #0x10D3
    ret
    .globl NtGdiGetRasterizerCaps
    .p2align 4
NtGdiGetRasterizerCaps:
    svc #0x10D4
    ret
    .globl NtUserSetWindowWord
    .p2align 4
NtUserSetWindowWord:
    svc #0x10D5
    ret
    .globl NtUserGetClipboardFormatName
    .p2align 4
NtUserGetClipboardFormatName:
    svc #0x10D6
    ret
    .globl NtUserRealInternalGetMessage
    .p2align 4
NtUserRealInternalGetMessage:
    svc #0x10D7
    ret
    .globl NtUserCreateLocalMemHandle
    .p2align 4
NtUserCreateLocalMemHandle:
    svc #0x10D8
    ret
    .globl NtUserAttachThreadInput
    .p2align 4
NtUserAttachThreadInput:
    svc #0x10D9
    ret
    .globl NtGdiCreateHalftonePalette
    .p2align 4
NtGdiCreateHalftonePalette:
    svc #0x10DA
    ret
    .globl NtUserPaintMenuBar
    .p2align 4
NtUserPaintMenuBar:
    svc #0x10DB
    ret
    .globl NtUserSetKeyboardState
    .p2align 4
NtUserSetKeyboardState:
    svc #0x10DC
    ret
    .globl NtGdiCombineTransform
    .p2align 4
NtGdiCombineTransform:
    svc #0x10DD
    ret
    .globl NtUserCreateAcceleratorTable
    .p2align 4
NtUserCreateAcceleratorTable:
    svc #0x10DE
    ret
    .globl NtUserGetCursorFrameInfo
    .p2align 4
NtUserGetCursorFrameInfo:
    svc #0x10DF
    ret
    .globl NtUserGetCaretBlinkTime
    .p2align 4
NtUserGetCaretBlinkTime:
    svc #0x10E0
    ret
    .globl NtGdiQueryFontAssocInfo
    .p2align 4
NtGdiQueryFontAssocInfo:
    svc #0x10E1
    ret
    .globl NtUserProcessConnect
    .p2align 4
NtUserProcessConnect:
    svc #0x10E2
    ret
    .globl NtUserEnumDisplayDevices
    .p2align 4
NtUserEnumDisplayDevices:
    svc #0x10E3
    ret
    .globl NtUserEmptyClipboard
    .p2align 4
NtUserEmptyClipboard:
    svc #0x10E4
    ret
    .globl NtUserGetClipboardData
    .p2align 4
NtUserGetClipboardData:
    svc #0x10E5
    ret
    .globl NtUserRemoveMenu
    .p2align 4
NtUserRemoveMenu:
    svc #0x10E6
    ret
    .globl NtGdiSetBoundsRect
    .p2align 4
NtGdiSetBoundsRect:
    svc #0x10E7
    ret
    .globl NtGdiGetBitmapDimension
    .p2align 4
NtGdiGetBitmapDimension:
    svc #0x10E8
    ret
    .globl NtUserConvertMemHandle
    .p2align 4
NtUserConvertMemHandle:
    svc #0x10E9
    ret
    .globl NtUserDestroyAcceleratorTable
    .p2align 4
NtUserDestroyAcceleratorTable:
    svc #0x10EA
    ret
    .globl NtUserGetGUIThreadInfo
    .p2align 4
NtUserGetGUIThreadInfo:
    svc #0x10EB
    ret
    .globl NtGdiCloseFigure
    .p2align 4
NtGdiCloseFigure:
    svc #0x10EC
    ret
    .globl NtUserSetWindowsHookAW
    .p2align 4
NtUserSetWindowsHookAW:
    svc #0x10ED
    ret
    .globl NtUserSetMenuDefaultItem
    .p2align 4
NtUserSetMenuDefaultItem:
    svc #0x10EE
    ret
    .globl NtUserCheckMenuItem
    .p2align 4
NtUserCheckMenuItem:
    svc #0x10EF
    ret
    .globl NtUserSetWinEventHook
    .p2align 4
NtUserSetWinEventHook:
    svc #0x10F0
    ret
    .globl NtUserUnhookWinEvent
    .p2align 4
NtUserUnhookWinEvent:
    svc #0x10F1
    ret
    .globl NtUserLockWindowUpdate
    .p2align 4
NtUserLockWindowUpdate:
    svc #0x10F2
    ret
    .globl NtUserSetSystemMenu
    .p2align 4
NtUserSetSystemMenu:
    svc #0x10F3
    ret
    .globl NtUserThunkedMenuInfo
    .p2align 4
NtUserThunkedMenuInfo:
    svc #0x10F4
    ret
    .globl NtGdiBeginPath
    .p2align 4
NtGdiBeginPath:
    svc #0x10F5
    ret
    .globl NtGdiEndPath
    .p2align 4
NtGdiEndPath:
    svc #0x10F6
    ret
    .globl NtGdiFillPath
    .p2align 4
NtGdiFillPath:
    svc #0x10F7
    ret
    .globl NtUserDdeInitialize
    .p2align 4
NtUserDdeInitialize:
    svc #0x10F8
    ret
    .globl NtUserModifyUserStartupInfoFlags
    .p2align 4
NtUserModifyUserStartupInfoFlags:
    svc #0x10F9
    ret
    .globl NtUserCountClipboardFormats
    .p2align 4
NtUserCountClipboardFormats:
    svc #0x10FA
    ret
    .globl NtGdiAddFontMemResourceEx
    .p2align 4
NtGdiAddFontMemResourceEx:
    svc #0x10FB
    ret
    .globl NtGdiEqualRgn
    .p2align 4
NtGdiEqualRgn:
    svc #0x10FC
    ret
    .globl NtGdiGetSystemPaletteUse
    .p2align 4
NtGdiGetSystemPaletteUse:
    svc #0x10FD
    ret
    .globl NtGdiRemoveFontMemResourceEx
    .p2align 4
NtGdiRemoveFontMemResourceEx:
    svc #0x10FE
    ret
    .globl NtUserEnumDisplaySettings
    .p2align 4
NtUserEnumDisplaySettings:
    svc #0x10FF
    ret
    .globl NtUserPaintDesktop
    .p2align 4
NtUserPaintDesktop:
    svc #0x1100
    ret
    .globl NtGdiExtEscape
    .p2align 4
NtGdiExtEscape:
    svc #0x1101
    ret
    .globl NtGdiSetBitmapDimension
    .p2align 4
NtGdiSetBitmapDimension:
    svc #0x1102
    ret
    .globl NtGdiSetFontEnumeration
    .p2align 4
NtGdiSetFontEnumeration:
    svc #0x1103
    ret
    .globl NtUserChangeClipboardChain
    .p2align 4
NtUserChangeClipboardChain:
    svc #0x1104
    ret
    .globl NtUserSetClipboardViewer
    .p2align 4
NtUserSetClipboardViewer:
    svc #0x1105
    ret
    .globl NtUserShowWindowAsync
    .p2align 4
NtUserShowWindowAsync:
    svc #0x1106
    ret
    .globl NtGdiCreateColorSpace
    .p2align 4
NtGdiCreateColorSpace:
    svc #0x1107
    ret
    .globl NtGdiDeleteColorSpace
    .p2align 4
NtGdiDeleteColorSpace:
    svc #0x1108
    ret
    .globl NtUserActivateKeyboardLayout
    .p2align 4
NtUserActivateKeyboardLayout:
    svc #0x1109
    ret
    .globl NtBindCompositionSurface
    .p2align 4
NtBindCompositionSurface:
    svc #0x110A
    ret
    .globl NtCloseCompositionInputSink
    .p2align 4
NtCloseCompositionInputSink:
    svc #0x110B
    ret
    .globl NtCompositionSetDropTarget
    .p2align 4
NtCompositionSetDropTarget:
    svc #0x110C
    ret
    .globl NtCompositorNotifyExitWindows
    .p2align 4
NtCompositorNotifyExitWindows:
    svc #0x110D
    ret
    .globl NtConfigureInputSpace
    .p2align 4
NtConfigureInputSpace:
    svc #0x110E
    ret
    .globl NtConfirmCompositionSurfaceIndependentFlipEntry
    .p2align 4
NtConfirmCompositionSurfaceIndependentFlipEntry:
    svc #0x110F
    ret
    .globl NtCreateCompositionInputSink
    .p2align 4
NtCreateCompositionInputSink:
    svc #0x1110
    ret
    .globl NtCreateCompositionSurfaceHandle
    .p2align 4
NtCreateCompositionSurfaceHandle:
    svc #0x1111
    ret
    .globl NtCreateImplicitCompositionInputSink
    .p2align 4
NtCreateImplicitCompositionInputSink:
    svc #0x1112
    ret
    .globl NtDCompositionAddCrossDeviceVisualChild
    .p2align 4
NtDCompositionAddCrossDeviceVisualChild:
    svc #0x1113
    ret
    .globl NtDCompositionBeginFrame
    .p2align 4
NtDCompositionBeginFrame:
    svc #0x1114
    ret
    .globl NtDCompositionBoostCompositorClock
    .p2align 4
NtDCompositionBoostCompositorClock:
    svc #0x1115
    ret
    .globl NtDCompositionCommitChannel
    .p2align 4
NtDCompositionCommitChannel:
    svc #0x1116
    ret
    .globl NtDCompositionCommitSynchronizationObject
    .p2align 4
NtDCompositionCommitSynchronizationObject:
    svc #0x1117
    ret
    .globl NtDCompositionConfirmFrame
    .p2align 4
NtDCompositionConfirmFrame:
    svc #0x1118
    ret
    .globl NtDCompositionConnectPipe
    .p2align 4
NtDCompositionConnectPipe:
    svc #0x1119
    ret
    .globl NtDCompositionCreateAndBindSharedSection
    .p2align 4
NtDCompositionCreateAndBindSharedSection:
    svc #0x111A
    ret
    .globl NtDCompositionCreateBufferCollection
    .p2align 4
NtDCompositionCreateBufferCollection:
    svc #0x111B
    ret
    .globl NtDCompositionCreateChannel
    .p2align 4
NtDCompositionCreateChannel:
    svc #0x111C
    ret
    .globl NtDCompositionCreateConnection
    .p2align 4
NtDCompositionCreateConnection:
    svc #0x111D
    ret
    .globl NtDCompositionCreateSharedResourceHandle
    .p2align 4
NtDCompositionCreateSharedResourceHandle:
    svc #0x111E
    ret
    .globl NtDCompositionCreateSynchronizationObject
    .p2align 4
NtDCompositionCreateSynchronizationObject:
    svc #0x111F
    ret
    .globl NtDCompositionDestroyChannel
    .p2align 4
NtDCompositionDestroyChannel:
    svc #0x1120
    ret
    .globl NtDCompositionDestroyConnection
    .p2align 4
NtDCompositionDestroyConnection:
    svc #0x1121
    ret
    .globl NtDCompositionDuplicateHandleToProcess
    .p2align 4
NtDCompositionDuplicateHandleToProcess:
    svc #0x1122
    ret
    .globl NtDCompositionDuplicateSwapchainHandleToDwm
    .p2align 4
NtDCompositionDuplicateSwapchainHandleToDwm:
    svc #0x1123
    ret
    .globl NtDCompositionEnableMMCSS
    .p2align 4
NtDCompositionEnableMMCSS:
    svc #0x1124
    ret
    .globl NtDCompositionGetBatchId
    .p2align 4
NtDCompositionGetBatchId:
    svc #0x1125
    ret
    .globl NtDCompositionGetChannels
    .p2align 4
NtDCompositionGetChannels:
    svc #0x1126
    ret
    .globl NtDCompositionGetConnectionBatch
    .p2align 4
NtDCompositionGetConnectionBatch:
    svc #0x1127
    ret
    .globl NtDCompositionGetDeletedResources
    .p2align 4
NtDCompositionGetDeletedResources:
    svc #0x1128
    ret
    .globl NtDCompositionGetFrameId
    .p2align 4
NtDCompositionGetFrameId:
    svc #0x1129
    ret
    .globl NtDCompositionGetFrameIdFromBatchId
    .p2align 4
NtDCompositionGetFrameIdFromBatchId:
    svc #0x112A
    ret
    .globl NtDCompositionGetFrameLegacyTokens
    .p2align 4
NtDCompositionGetFrameLegacyTokens:
    svc #0x112B
    ret
    .globl NtDCompositionGetFrameStatistics
    .p2align 4
NtDCompositionGetFrameStatistics:
    svc #0x112C
    ret
    .globl NtDCompositionGetFrameSurfaceUpdates
    .p2align 4
NtDCompositionGetFrameSurfaceUpdates:
    svc #0x112D
    ret
    .globl NtDCompositionGetStatistics
    .p2align 4
NtDCompositionGetStatistics:
    svc #0x112E
    ret
    .globl NtDCompositionGetTargetStatistics
    .p2align 4
NtDCompositionGetTargetStatistics:
    svc #0x112F
    ret
    .globl NtDCompositionNotifySuperWetInkWork
    .p2align 4
NtDCompositionNotifySuperWetInkWork:
    svc #0x1130
    ret
    .globl NtDCompositionProcessChannelBatchBuffer
    .p2align 4
NtDCompositionProcessChannelBatchBuffer:
    svc #0x1131
    ret
    .globl NtDCompositionRegisterThumbnailVisual
    .p2align 4
NtDCompositionRegisterThumbnailVisual:
    svc #0x1132
    ret
    .globl NtDCompositionRegisterVirtualDesktopVisual
    .p2align 4
NtDCompositionRegisterVirtualDesktopVisual:
    svc #0x1133
    ret
    .globl NtDCompositionReleaseAllResources
    .p2align 4
NtDCompositionReleaseAllResources:
    svc #0x1134
    ret
    .globl NtDCompositionRemoveCrossDeviceVisualChild
    .p2align 4
NtDCompositionRemoveCrossDeviceVisualChild:
    svc #0x1135
    ret
    .globl NtDCompositionSendDwmLpcMessage
    .p2align 4
NtDCompositionSendDwmLpcMessage:
    svc #0x1136
    ret
    .globl NtDCompositionSetBlurredWallpaperSurface
    .p2align 4
NtDCompositionSetBlurredWallpaperSurface:
    svc #0x1137
    ret
    .globl NtDCompositionSetChannelCommitCompletionEvent
    .p2align 4
NtDCompositionSetChannelCommitCompletionEvent:
    svc #0x1138
    ret
    .globl NtDCompositionSetChannelConnectionId
    .p2align 4
NtDCompositionSetChannelConnectionId:
    svc #0x1139
    ret
    .globl NtDCompositionSetChildRootVisual
    .p2align 4
NtDCompositionSetChildRootVisual:
    svc #0x113A
    ret
    .globl NtDCompositionSetDebugCounter
    .p2align 4
NtDCompositionSetDebugCounter:
    svc #0x113B
    ret
    .globl NtDCompositionSuspendAnimations
    .p2align 4
NtDCompositionSuspendAnimations:
    svc #0x113C
    ret
    .globl NtDCompositionSyncWait
    .p2align 4
NtDCompositionSyncWait:
    svc #0x113D
    ret
    .globl NtDCompositionSynchronize
    .p2align 4
NtDCompositionSynchronize:
    svc #0x113E
    ret
    .globl NtDCompositionTelemetrySetApplicationId
    .p2align 4
NtDCompositionTelemetrySetApplicationId:
    svc #0x113F
    ret
    .globl NtDCompositionUpdatePointerCapture
    .p2align 4
NtDCompositionUpdatePointerCapture:
    svc #0x1140
    ret
    .globl NtDCompositionWaitForChannel
    .p2align 4
NtDCompositionWaitForChannel:
    svc #0x1141
    ret
    .globl NtDCompositionWaitForCompositorClock
    .p2align 4
NtDCompositionWaitForCompositorClock:
    svc #0x1142
    ret
    .globl NtDesktopCaptureBits
    .p2align 4
NtDesktopCaptureBits:
    svc #0x1143
    ret
    .globl NtDuplicateCompositionInputSink
    .p2align 4
NtDuplicateCompositionInputSink:
    svc #0x1144
    ret
    .globl NtDxgkAcquireExclusiveEngineAccess
    .p2align 4
NtDxgkAcquireExclusiveEngineAccess:
    svc #0x1145
    ret
    .globl NtDxgkCancelPresents
    .p2align 4
NtDxgkCancelPresents:
    svc #0x1146
    ret
    .globl NtDxgkCheckEngineAccess
    .p2align 4
NtDxgkCheckEngineAccess:
    svc #0x1147
    ret
    .globl NtDxgkCheckSinglePlaneForMultiPlaneOverlaySupport
    .p2align 4
NtDxgkCheckSinglePlaneForMultiPlaneOverlaySupport:
    svc #0x1148
    ret
    .globl NtDxgkConnectDoorbell
    .p2align 4
NtDxgkConnectDoorbell:
    svc #0x1149
    ret
    .globl NtDxgkCreateDoorbell
    .p2align 4
NtDxgkCreateDoorbell:
    svc #0x114A
    ret
    .globl NtDxgkCreateNativeFence
    .p2align 4
NtDxgkCreateNativeFence:
    svc #0x114B
    ret
    .globl NtDxgkCreateTrackedWorkload
    .p2align 4
NtDxgkCreateTrackedWorkload:
    svc #0x114C
    ret
    .globl NtDxgkDDisplayEnum2
    .p2align 4
NtDxgkDDisplayEnum2:
    svc #0x114D
    ret
    .globl NtDxgkDestroyDoorbell
    .p2align 4
NtDxgkDestroyDoorbell:
    svc #0x114E
    ret
    .globl NtDxgkDestroyTrackedWorkload
    .p2align 4
NtDxgkDestroyTrackedWorkload:
    svc #0x114F
    ret
    .globl NtDxgkDisableProcessDebugBlobCollection
    .p2align 4
NtDxgkDisableProcessDebugBlobCollection:
    svc #0x1150
    ret
    .globl NtDxgkDispMgrOperation
    .p2align 4
NtDxgkDispMgrOperation:
    svc #0x1151
    ret
    .globl NtDxgkDisplayMuxSwitchExecute
    .p2align 4
NtDxgkDisplayMuxSwitchExecute:
    svc #0x1152
    ret
    .globl NtDxgkDisplayMuxSwitchFinish
    .p2align 4
NtDxgkDisplayMuxSwitchFinish:
    svc #0x1153
    ret
    .globl NtDxgkDisplayMuxSwitchPrepare
    .p2align 4
NtDxgkDisplayMuxSwitchPrepare:
    svc #0x1154
    ret
    .globl NtDxgkDisplayPortOperation
    .p2align 4
NtDxgkDisplayPortOperation:
    svc #0x1155
    ret
    .globl NtDxgkDuplicateHandle
    .p2align 4
NtDxgkDuplicateHandle:
    svc #0x1156
    ret
    .globl NtDxgkEnableProcessDebugBlobCollection
    .p2align 4
NtDxgkEnableProcessDebugBlobCollection:
    svc #0x1157
    ret
    .globl NtDxgkEnumAdapters3
    .p2align 4
NtDxgkEnumAdapters3:
    svc #0x1158
    ret
    .globl NtDxgkEnumProcesses
    .p2align 4
NtDxgkEnumProcesses:
    svc #0x1159
    ret
    .globl NtDxgkGetAvailableTrackedWorkloadIndex
    .p2align 4
NtDxgkGetAvailableTrackedWorkloadIndex:
    svc #0x115A
    ret
    .globl NtDxgkGetNativeFenceLogDetail
    .p2align 4
NtDxgkGetNativeFenceLogDetail:
    svc #0x115B
    ret
    .globl NtDxgkGetProcessList
    .p2align 4
NtDxgkGetProcessList:
    svc #0x115C
    ret
    .globl NtDxgkGetProperties
    .p2align 4
NtDxgkGetProperties:
    svc #0x115D
    ret
    .globl NtDxgkGetTrackedWorkloadStatistics
    .p2align 4
NtDxgkGetTrackedWorkloadStatistics:
    svc #0x115E
    ret
    .globl NtDxgkIsFeatureEnabled
    .p2align 4
NtDxgkIsFeatureEnabled:
    svc #0x115F
    ret
    .globl NtDxgkMapProcessDebugBlob
    .p2align 4
NtDxgkMapProcessDebugBlob:
    svc #0x1160
    ret
    .globl NtDxgkNotifyWorkSubmission
    .p2align 4
NtDxgkNotifyWorkSubmission:
    svc #0x1161
    ret
    .globl NtDxgkOpenNativeFenceFromNtHandle
    .p2align 4
NtDxgkOpenNativeFenceFromNtHandle:
    svc #0x1162
    ret
    .globl NtDxgkOutputDuplPresentToHwQueue
    .p2align 4
NtDxgkOutputDuplPresentToHwQueue:
    svc #0x1163
    ret
    .globl NtDxgkPinResources
    .p2align 4
NtDxgkPinResources:
    svc #0x1164
    ret
    .globl NtDxgkRegisterVailProcess
    .p2align 4
NtDxgkRegisterVailProcess:
    svc #0x1165
    ret
    .globl NtDxgkResetTrackedWorkloadStatistics
    .p2align 4
NtDxgkResetTrackedWorkloadStatistics:
    svc #0x1166
    ret
    .globl NtDxgkSetProperties
    .p2align 4
NtDxgkSetProperties:
    svc #0x1167
    ret
    .globl NtDxgkSubmitPresentBltToHwQueue
    .p2align 4
NtDxgkSubmitPresentBltToHwQueue:
    svc #0x1168
    ret
    .globl NtDxgkSubmitPresentToHwQueue
    .p2align 4
NtDxgkSubmitPresentToHwQueue:
    svc #0x1169
    ret
    .globl NtDxgkUnmapProcessDebugBlob
    .p2align 4
NtDxgkUnmapProcessDebugBlob:
    svc #0x116A
    ret
    .globl NtDxgkUnpinResources
    .p2align 4
NtDxgkUnpinResources:
    svc #0x116B
    ret
    .globl NtDxgkUpdateTrackedWorkload
    .p2align 4
NtDxgkUpdateTrackedWorkload:
    svc #0x116C
    ret
    .globl NtDxgkVailConnect
    .p2align 4
NtDxgkVailConnect:
    svc #0x116D
    ret
    .globl NtDxgkVailDisconnect
    .p2align 4
NtDxgkVailDisconnect:
    svc #0x116E
    ret
    .globl NtDxgkVailPromoteCompositionSurface
    .p2align 4
NtDxgkVailPromoteCompositionSurface:
    svc #0x116F
    ret
    .globl NtEnableOneCoreTransformMode
    .p2align 4
NtEnableOneCoreTransformMode:
    svc #0x1170
    ret
    .globl NtFlipObjectAddContent
    .p2align 4
NtFlipObjectAddContent:
    svc #0x1171
    ret
    .globl NtFlipObjectAddPoolBuffer
    .p2align 4
NtFlipObjectAddPoolBuffer:
    svc #0x1172
    ret
    .globl NtFlipObjectConsumerAcquirePresent
    .p2align 4
NtFlipObjectConsumerAcquirePresent:
    svc #0x1173
    ret
    .globl NtFlipObjectConsumerAdjustUsageReference
    .p2align 4
NtFlipObjectConsumerAdjustUsageReference:
    svc #0x1174
    ret
    .globl NtFlipObjectConsumerBeginProcessPresent
    .p2align 4
NtFlipObjectConsumerBeginProcessPresent:
    svc #0x1175
    ret
    .globl NtFlipObjectConsumerEndProcessPresent
    .p2align 4
NtFlipObjectConsumerEndProcessPresent:
    svc #0x1176
    ret
    .globl NtFlipObjectConsumerPostMessage
    .p2align 4
NtFlipObjectConsumerPostMessage:
    svc #0x1177
    ret
    .globl NtFlipObjectConsumerQueryBufferInfo
    .p2align 4
NtFlipObjectConsumerQueryBufferInfo:
    svc #0x1178
    ret
    .globl NtFlipObjectCreate
    .p2align 4
NtFlipObjectCreate:
    svc #0x1179
    ret
    .globl NtFlipObjectDisconnectEndpoint
    .p2align 4
NtFlipObjectDisconnectEndpoint:
    svc #0x117A
    ret
    .globl NtFlipObjectEnablePresentStatisticsType
    .p2align 4
NtFlipObjectEnablePresentStatisticsType:
    svc #0x117B
    ret
    .globl NtFlipObjectOpen
    .p2align 4
NtFlipObjectOpen:
    svc #0x117C
    ret
    .globl NtFlipObjectPresentCancel
    .p2align 4
NtFlipObjectPresentCancel:
    svc #0x117D
    ret
    .globl NtFlipObjectQueryBufferAvailableEvent
    .p2align 4
NtFlipObjectQueryBufferAvailableEvent:
    svc #0x117E
    ret
    .globl NtFlipObjectQueryEndpointConnected
    .p2align 4
NtFlipObjectQueryEndpointConnected:
    svc #0x117F
    ret
    .globl NtFlipObjectQueryLostEvent
    .p2align 4
NtFlipObjectQueryLostEvent:
    svc #0x1180
    ret
    .globl NtFlipObjectQueryNextMessageToProducer
    .p2align 4
NtFlipObjectQueryNextMessageToProducer:
    svc #0x1181
    ret
    .globl NtFlipObjectReadNextMessageToProducer
    .p2align 4
NtFlipObjectReadNextMessageToProducer:
    svc #0x1182
    ret
    .globl NtFlipObjectRemoveContent
    .p2align 4
NtFlipObjectRemoveContent:
    svc #0x1183
    ret
    .globl NtFlipObjectRemovePoolBuffer
    .p2align 4
NtFlipObjectRemovePoolBuffer:
    svc #0x1184
    ret
    .globl NtFlipObjectSetContent
    .p2align 4
NtFlipObjectSetContent:
    svc #0x1185
    ret
    .globl NtGdiAbortDoc
    .p2align 4
NtGdiAbortDoc:
    svc #0x1186
    ret
    .globl NtGdiAbortPath
    .p2align 4
NtGdiAbortPath:
    svc #0x1187
    ret
    .globl NtGdiAddEmbFontToDC
    .p2align 4
NtGdiAddEmbFontToDC:
    svc #0x1188
    ret
    .globl NtGdiAddFontResourceW
    .p2align 4
NtGdiAddFontResourceW:
    svc #0x1189
    ret
    .globl NtGdiAddInitialFonts
    .p2align 4
NtGdiAddInitialFonts:
    svc #0x118A
    ret
    .globl NtGdiAddRemoteFontToDC
    .p2align 4
NtGdiAddRemoteFontToDC:
    svc #0x118B
    ret
    .globl NtGdiAddRemoteMMInstanceToDC
    .p2align 4
NtGdiAddRemoteMMInstanceToDC:
    svc #0x118C
    ret
    .globl NtGdiAngleArc
    .p2align 4
NtGdiAngleArc:
    svc #0x118D
    ret
    .globl NtGdiAnyLinkedFonts
    .p2align 4
NtGdiAnyLinkedFonts:
    svc #0x118E
    ret
    .globl NtGdiArcInternal
    .p2align 4
NtGdiArcInternal:
    svc #0x118F
    ret
    .globl NtGdiBRUSHOBJ_DeleteRbrush
    .p2align 4
NtGdiBRUSHOBJ_DeleteRbrush:
    svc #0x1190
    ret
    .globl NtGdiBRUSHOBJ_hGetColorTransform
    .p2align 4
NtGdiBRUSHOBJ_hGetColorTransform:
    svc #0x1191
    ret
    .globl NtGdiBRUSHOBJ_pvAllocRbrush
    .p2align 4
NtGdiBRUSHOBJ_pvAllocRbrush:
    svc #0x1192
    ret
    .globl NtGdiBRUSHOBJ_pvGetRbrush
    .p2align 4
NtGdiBRUSHOBJ_pvGetRbrush:
    svc #0x1193
    ret
    .globl NtGdiBRUSHOBJ_ulGetBrushColor
    .p2align 4
NtGdiBRUSHOBJ_ulGetBrushColor:
    svc #0x1194
    ret
    .globl NtGdiBeginGdiRendering
    .p2align 4
NtGdiBeginGdiRendering:
    svc #0x1195
    ret
    .globl NtGdiCLIPOBJ_bEnum
    .p2align 4
NtGdiCLIPOBJ_bEnum:
    svc #0x1196
    ret
    .globl NtGdiCLIPOBJ_cEnumStart
    .p2align 4
NtGdiCLIPOBJ_cEnumStart:
    svc #0x1197
    ret
    .globl NtGdiCLIPOBJ_ppoGetPath
    .p2align 4
NtGdiCLIPOBJ_ppoGetPath:
    svc #0x1198
    ret
    .globl NtGdiCancelDC
    .p2align 4
NtGdiCancelDC:
    svc #0x1199
    ret
    .globl NtGdiChangeGhostFont
    .p2align 4
NtGdiChangeGhostFont:
    svc #0x119A
    ret
    .globl NtGdiCheckBitmapBits
    .p2align 4
NtGdiCheckBitmapBits:
    svc #0x119B
    ret
    .globl NtGdiClearBitmapAttributes
    .p2align 4
NtGdiClearBitmapAttributes:
    svc #0x119C
    ret
    .globl NtGdiClearBrushAttributes
    .p2align 4
NtGdiClearBrushAttributes:
    svc #0x119D
    ret
    .globl NtGdiColorCorrectPalette
    .p2align 4
NtGdiColorCorrectPalette:
    svc #0x119E
    ret
    .globl NtGdiConfigureOPMProtectedOutput
    .p2align 4
NtGdiConfigureOPMProtectedOutput:
    svc #0x119F
    ret
    .globl NtGdiConvertMetafileRect
    .p2align 4
NtGdiConvertMetafileRect:
    svc #0x11A0
    ret
    .globl NtGdiCreateBitmapFromDxSurface
    .p2align 4
NtGdiCreateBitmapFromDxSurface:
    svc #0x11A1
    ret
    .globl NtGdiCreateBitmapFromDxSurface2
    .p2align 4
NtGdiCreateBitmapFromDxSurface2:
    svc #0x11A2
    ret
    .globl NtGdiCreateColorTransform
    .p2align 4
NtGdiCreateColorTransform:
    svc #0x11A3
    ret
    .globl NtGdiCreateEllipticRgn
    .p2align 4
NtGdiCreateEllipticRgn:
    svc #0x11A4
    ret
    .globl NtGdiCreateHatchBrushInternal
    .p2align 4
NtGdiCreateHatchBrushInternal:
    svc #0x11A5
    ret
    .globl NtGdiCreateMetafileDC
    .p2align 4
NtGdiCreateMetafileDC:
    svc #0x11A6
    ret
    .globl NtGdiCreateOPMProtectedOutput
    .p2align 4
NtGdiCreateOPMProtectedOutput:
    svc #0x11A7
    ret
    .globl NtGdiCreateOPMProtectedOutputs
    .p2align 4
NtGdiCreateOPMProtectedOutputs:
    svc #0x11A8
    ret
    .globl NtGdiCreateRoundRectRgn
    .p2align 4
NtGdiCreateRoundRectRgn:
    svc #0x11A9
    ret
    .globl NtGdiCreateServerMetaFile
    .p2align 4
NtGdiCreateServerMetaFile:
    svc #0x11AA
    ret
    .globl NtGdiCreateSessionMappedDIBSection
    .p2align 4
NtGdiCreateSessionMappedDIBSection:
    svc #0x11AB
    ret
    .globl NtGdiDDCCIGetCapabilitiesString
    .p2align 4
NtGdiDDCCIGetCapabilitiesString:
    svc #0x11AC
    ret
    .globl NtGdiDDCCIGetCapabilitiesStringLength
    .p2align 4
NtGdiDDCCIGetCapabilitiesStringLength:
    svc #0x11AD
    ret
    .globl NtGdiDDCCIGetTimingReport
    .p2align 4
NtGdiDDCCIGetTimingReport:
    svc #0x11AE
    ret
    .globl NtGdiDDCCIGetVCPFeature
    .p2align 4
NtGdiDDCCIGetVCPFeature:
    svc #0x11AF
    ret
    .globl NtGdiDDCCISaveCurrentSettings
    .p2align 4
NtGdiDDCCISaveCurrentSettings:
    svc #0x11B0
    ret
    .globl NtGdiDDCCISetVCPFeature
    .p2align 4
NtGdiDDCCISetVCPFeature:
    svc #0x11B1
    ret
    .globl NtGdiDdCreateFullscreenSprite
    .p2align 4
NtGdiDdCreateFullscreenSprite:
    svc #0x11B2
    ret
    .globl NtGdiDdDDIAbandonSwapChain
    .p2align 4
NtGdiDdDDIAbandonSwapChain:
    svc #0x11B3
    ret
    .globl NtGdiDdDDIAcquireKeyedMutex
    .p2align 4
NtGdiDdDDIAcquireKeyedMutex:
    svc #0x11B4
    ret
    .globl NtGdiDdDDIAcquireKeyedMutex2
    .p2align 4
NtGdiDdDDIAcquireKeyedMutex2:
    svc #0x11B5
    ret
    .globl NtGdiDdDDIAcquireSwapChain
    .p2align 4
NtGdiDdDDIAcquireSwapChain:
    svc #0x11B6
    ret
    .globl NtGdiDdDDIAddSurfaceToSwapChain
    .p2align 4
NtGdiDdDDIAddSurfaceToSwapChain:
    svc #0x11B7
    ret
    .globl NtGdiDdDDIAdjustFullscreenGamma
    .p2align 4
NtGdiDdDDIAdjustFullscreenGamma:
    svc #0x11B8
    ret
    .globl NtGdiDdDDICacheHybridQueryValue
    .p2align 4
NtGdiDdDDICacheHybridQueryValue:
    svc #0x11B9
    ret
    .globl NtGdiDdDDIChangeVideoMemoryReservation
    .p2align 4
NtGdiDdDDIChangeVideoMemoryReservation:
    svc #0x11BA
    ret
    .globl NtGdiDdDDICheckExclusiveOwnership
    .p2align 4
NtGdiDdDDICheckExclusiveOwnership:
    svc #0x11BB
    ret
    .globl NtGdiDdDDICheckMonitorPowerState
    .p2align 4
NtGdiDdDDICheckMonitorPowerState:
    svc #0x11BC
    ret
    .globl NtGdiDdDDICheckMultiPlaneOverlaySupport
    .p2align 4
NtGdiDdDDICheckMultiPlaneOverlaySupport:
    svc #0x11BD
    ret
    .globl NtGdiDdDDICheckMultiPlaneOverlaySupport2
    .p2align 4
NtGdiDdDDICheckMultiPlaneOverlaySupport2:
    svc #0x11BE
    ret
    .globl NtGdiDdDDICheckMultiPlaneOverlaySupport3
    .p2align 4
NtGdiDdDDICheckMultiPlaneOverlaySupport3:
    svc #0x11BF
    ret
    .globl NtGdiDdDDICheckOcclusion
    .p2align 4
NtGdiDdDDICheckOcclusion:
    svc #0x11C0
    ret
    .globl NtGdiDdDDICheckSharedResourceAccess
    .p2align 4
NtGdiDdDDICheckSharedResourceAccess:
    svc #0x11C1
    ret
    .globl NtGdiDdDDICheckVidPnExclusiveOwnership
    .p2align 4
NtGdiDdDDICheckVidPnExclusiveOwnership:
    svc #0x11C2
    ret
    .globl NtGdiDdDDICloseAdapter
    .p2align 4
NtGdiDdDDICloseAdapter:
    svc #0x11C3
    ret
    .globl NtGdiDdDDIConfigureSharedResource
    .p2align 4
NtGdiDdDDIConfigureSharedResource:
    svc #0x11C4
    ret
    .globl NtGdiDdDDICreateAllocation
    .p2align 4
NtGdiDdDDICreateAllocation:
    svc #0x11C5
    ret
    .globl NtGdiDdDDICreateBundleObject
    .p2align 4
NtGdiDdDDICreateBundleObject:
    svc #0x11C6
    ret
    .globl NtGdiDdDDICreateContext
    .p2align 4
NtGdiDdDDICreateContext:
    svc #0x11C7
    ret
    .globl NtGdiDdDDICreateContextVirtual
    .p2align 4
NtGdiDdDDICreateContextVirtual:
    svc #0x11C8
    ret
    .globl NtGdiDdDDICreateDCFromMemory
    .p2align 4
NtGdiDdDDICreateDCFromMemory:
    svc #0x11C9
    ret
    .globl NtGdiDdDDICreateDevice
    .p2align 4
NtGdiDdDDICreateDevice:
    svc #0x11CA
    ret
    .globl NtGdiDdDDICreateHwContext
    .p2align 4
NtGdiDdDDICreateHwContext:
    svc #0x11CB
    ret
    .globl NtGdiDdDDICreateHwQueue
    .p2align 4
NtGdiDdDDICreateHwQueue:
    svc #0x11CC
    ret
    .globl NtGdiDdDDICreateKeyedMutex
    .p2align 4
NtGdiDdDDICreateKeyedMutex:
    svc #0x11CD
    ret
    .globl NtGdiDdDDICreateKeyedMutex2
    .p2align 4
NtGdiDdDDICreateKeyedMutex2:
    svc #0x11CE
    ret
    .globl NtGdiDdDDICreateOutputDupl
    .p2align 4
NtGdiDdDDICreateOutputDupl:
    svc #0x11CF
    ret
    .globl NtGdiDdDDICreateOverlay
    .p2align 4
NtGdiDdDDICreateOverlay:
    svc #0x11D0
    ret
    .globl NtGdiDdDDICreatePagingQueue
    .p2align 4
NtGdiDdDDICreatePagingQueue:
    svc #0x11D1
    ret
    .globl NtGdiDdDDICreateProtectedSession
    .p2align 4
NtGdiDdDDICreateProtectedSession:
    svc #0x11D2
    ret
    .globl NtGdiDdDDICreateSwapChain
    .p2align 4
NtGdiDdDDICreateSwapChain:
    svc #0x11D3
    ret
    .globl NtGdiDdDDICreateSynchronizationObject
    .p2align 4
NtGdiDdDDICreateSynchronizationObject:
    svc #0x11D4
    ret
    .globl NtGdiDdDDIDDisplayEnum
    .p2align 4
NtGdiDdDDIDDisplayEnum:
    svc #0x11D5
    ret
    .globl NtGdiDdDDIDestroyAllocation
    .p2align 4
NtGdiDdDDIDestroyAllocation:
    svc #0x11D6
    ret
    .globl NtGdiDdDDIDestroyAllocation2
    .p2align 4
NtGdiDdDDIDestroyAllocation2:
    svc #0x11D7
    ret
    .globl NtGdiDdDDIDestroyContext
    .p2align 4
NtGdiDdDDIDestroyContext:
    svc #0x11D8
    ret
    .globl NtGdiDdDDIDestroyDCFromMemory
    .p2align 4
NtGdiDdDDIDestroyDCFromMemory:
    svc #0x11D9
    ret
    .globl NtGdiDdDDIDestroyDevice
    .p2align 4
NtGdiDdDDIDestroyDevice:
    svc #0x11DA
    ret
    .globl NtGdiDdDDIDestroyHwContext
    .p2align 4
NtGdiDdDDIDestroyHwContext:
    svc #0x11DB
    ret
    .globl NtGdiDdDDIDestroyHwQueue
    .p2align 4
NtGdiDdDDIDestroyHwQueue:
    svc #0x11DC
    ret
    .globl NtGdiDdDDIDestroyKeyedMutex
    .p2align 4
NtGdiDdDDIDestroyKeyedMutex:
    svc #0x11DD
    ret
    .globl NtGdiDdDDIDestroyOutputDupl
    .p2align 4
NtGdiDdDDIDestroyOutputDupl:
    svc #0x11DE
    ret
    .globl NtGdiDdDDIDestroyOverlay
    .p2align 4
NtGdiDdDDIDestroyOverlay:
    svc #0x11DF
    ret
    .globl NtGdiDdDDIDestroyPagingQueue
    .p2align 4
NtGdiDdDDIDestroyPagingQueue:
    svc #0x11E0
    ret
    .globl NtGdiDdDDIDestroyProtectedSession
    .p2align 4
NtGdiDdDDIDestroyProtectedSession:
    svc #0x11E1
    ret
    .globl NtGdiDdDDIDestroySynchronizationObject
    .p2align 4
NtGdiDdDDIDestroySynchronizationObject:
    svc #0x11E2
    ret
    .globl NtGdiDdDDIDispMgrCreate
    .p2align 4
NtGdiDdDDIDispMgrCreate:
    svc #0x11E3
    ret
    .globl NtGdiDdDDIDispMgrSourceOperation
    .p2align 4
NtGdiDdDDIDispMgrSourceOperation:
    svc #0x11E4
    ret
    .globl NtGdiDdDDIDispMgrTargetOperation
    .p2align 4
NtGdiDdDDIDispMgrTargetOperation:
    svc #0x11E5
    ret
    .globl NtGdiDdDDIEnumAdapters
    .p2align 4
NtGdiDdDDIEnumAdapters:
    svc #0x11E6
    ret
    .globl NtGdiDdDDIEnumAdapters2
    .p2align 4
NtGdiDdDDIEnumAdapters2:
    svc #0x11E7
    ret
    .globl NtGdiDdDDIEscape
    .p2align 4
NtGdiDdDDIEscape:
    svc #0x11E8
    ret
    .globl NtGdiDdDDIEvict
    .p2align 4
NtGdiDdDDIEvict:
    svc #0x11E9
    ret
    .globl NtGdiDdDDIExtractBundleObject
    .p2align 4
NtGdiDdDDIExtractBundleObject:
    svc #0x11EA
    ret
    .globl NtGdiDdDDIFlipOverlay
    .p2align 4
NtGdiDdDDIFlipOverlay:
    svc #0x11EB
    ret
    .globl NtGdiDdDDIFlushHeapTransitions
    .p2align 4
NtGdiDdDDIFlushHeapTransitions:
    svc #0x11EC
    ret
    .globl NtGdiDdDDIFreeGpuVirtualAddress
    .p2align 4
NtGdiDdDDIFreeGpuVirtualAddress:
    svc #0x11ED
    ret
    .globl NtGdiDdDDIGetAllocationPriority
    .p2align 4
NtGdiDdDDIGetAllocationPriority:
    svc #0x11EE
    ret
    .globl NtGdiDdDDIGetCachedHybridQueryValue
    .p2align 4
NtGdiDdDDIGetCachedHybridQueryValue:
    svc #0x11EF
    ret
    .globl NtGdiDdDDIGetContextInProcessSchedulingPriority
    .p2align 4
NtGdiDdDDIGetContextInProcessSchedulingPriority:
    svc #0x11F0
    ret
    .globl NtGdiDdDDIGetContextSchedulingPriority
    .p2align 4
NtGdiDdDDIGetContextSchedulingPriority:
    svc #0x11F1
    ret
    .globl NtGdiDdDDIGetDWMVerticalBlankEvent
    .p2align 4
NtGdiDdDDIGetDWMVerticalBlankEvent:
    svc #0x11F2
    ret
    .globl NtGdiDdDDIGetDeviceState
    .p2align 4
NtGdiDdDDIGetDeviceState:
    svc #0x11F3
    ret
    .globl NtGdiDdDDIGetDisplayModeList
    .p2align 4
NtGdiDdDDIGetDisplayModeList:
    svc #0x11F4
    ret
    .globl NtGdiDdDDIGetMemoryBudgetTarget
    .p2align 4
NtGdiDdDDIGetMemoryBudgetTarget:
    svc #0x11F5
    ret
    .globl NtGdiDdDDIGetMultiPlaneOverlayCaps
    .p2align 4
NtGdiDdDDIGetMultiPlaneOverlayCaps:
    svc #0x11F6
    ret
    .globl NtGdiDdDDIGetMultisampleMethodList
    .p2align 4
NtGdiDdDDIGetMultisampleMethodList:
    svc #0x11F7
    ret
    .globl NtGdiDdDDIGetOverlayState
    .p2align 4
NtGdiDdDDIGetOverlayState:
    svc #0x11F8
    ret
    .globl NtGdiDdDDIGetPostCompositionCaps
    .p2align 4
NtGdiDdDDIGetPostCompositionCaps:
    svc #0x11F9
    ret
    .globl NtGdiDdDDIGetPresentHistory
    .p2align 4
NtGdiDdDDIGetPresentHistory:
    svc #0x11FA
    ret
    .globl NtGdiDdDDIGetPresentQueueEvent
    .p2align 4
NtGdiDdDDIGetPresentQueueEvent:
    svc #0x11FB
    ret
    .globl NtGdiDdDDIGetProcessDeviceRemovalSupport
    .p2align 4
NtGdiDdDDIGetProcessDeviceRemovalSupport:
    svc #0x11FC
    ret
    .globl NtGdiDdDDIGetProcessSchedulingPriorityBand
    .p2align 4
NtGdiDdDDIGetProcessSchedulingPriorityBand:
    svc #0x11FD
    ret
    .globl NtGdiDdDDIGetProcessSchedulingPriorityClass
    .p2align 4
NtGdiDdDDIGetProcessSchedulingPriorityClass:
    svc #0x11FE
    ret
    .globl NtGdiDdDDIGetResourcePresentPrivateDriverData
    .p2align 4
NtGdiDdDDIGetResourcePresentPrivateDriverData:
    svc #0x11FF
    ret
    .globl NtGdiDdDDIGetRuntimeData
    .p2align 4
NtGdiDdDDIGetRuntimeData:
    svc #0x1200
    ret
    .globl NtGdiDdDDIGetScanLine
    .p2align 4
NtGdiDdDDIGetScanLine:
    svc #0x1201
    ret
    .globl NtGdiDdDDIGetSetSwapChainMetadata
    .p2align 4
NtGdiDdDDIGetSetSwapChainMetadata:
    svc #0x1202
    ret
    .globl NtGdiDdDDIGetSharedPrimaryHandle
    .p2align 4
NtGdiDdDDIGetSharedPrimaryHandle:
    svc #0x1203
    ret
    .globl NtGdiDdDDIGetSharedResourceAdapterLuid
    .p2align 4
NtGdiDdDDIGetSharedResourceAdapterLuid:
    svc #0x1204
    ret
    .globl NtGdiDdDDIGetSharedResourceAdapterLuidFlipManager
    .p2align 4
NtGdiDdDDIGetSharedResourceAdapterLuidFlipManager:
    svc #0x1205
    ret
    .globl NtGdiDdDDIGetSwapChainSurfacePhysicalAddress
    .p2align 4
NtGdiDdDDIGetSwapChainSurfacePhysicalAddress:
    svc #0x1206
    ret
    .globl NtGdiDdDDIGetYieldPercentage
    .p2align 4
NtGdiDdDDIGetYieldPercentage:
    svc #0x1207
    ret
    .globl NtGdiDdDDIInvalidateActiveVidPn
    .p2align 4
NtGdiDdDDIInvalidateActiveVidPn:
    svc #0x1208
    ret
    .globl NtGdiDdDDIInvalidateCache
    .p2align 4
NtGdiDdDDIInvalidateCache:
    svc #0x1209
    ret
    .globl NtGdiDdDDILock
    .p2align 4
NtGdiDdDDILock:
    svc #0x120A
    ret
    .globl NtGdiDdDDILock2
    .p2align 4
NtGdiDdDDILock2:
    svc #0x120B
    ret
    .globl NtGdiDdDDIMakeResident
    .p2align 4
NtGdiDdDDIMakeResident:
    svc #0x120C
    ret
    .globl NtGdiDdDDIMapGpuVirtualAddress
    .p2align 4
NtGdiDdDDIMapGpuVirtualAddress:
    svc #0x120D
    ret
    .globl NtGdiDdDDIMarkDeviceAsError
    .p2align 4
NtGdiDdDDIMarkDeviceAsError:
    svc #0x120E
    ret
    .globl NtGdiDdDDINetDispGetNextChunkInfo
    .p2align 4
NtGdiDdDDINetDispGetNextChunkInfo:
    svc #0x120F
    ret
    .globl NtGdiDdDDINetDispQueryMiracastDisplayDeviceStatus
    .p2align 4
NtGdiDdDDINetDispQueryMiracastDisplayDeviceStatus:
    svc #0x1210
    ret
    .globl NtGdiDdDDINetDispQueryMiracastDisplayDeviceSupport
    .p2align 4
NtGdiDdDDINetDispQueryMiracastDisplayDeviceSupport:
    svc #0x1211
    ret
    .globl NtGdiDdDDINetDispStartMiracastDisplayDevice
    .p2align 4
NtGdiDdDDINetDispStartMiracastDisplayDevice:
    svc #0x1212
    ret
    .globl NtGdiDdDDINetDispStopMiracastDisplayDevice
    .p2align 4
NtGdiDdDDINetDispStopMiracastDisplayDevice:
    svc #0x1213
    ret
    .globl NtGdiDdDDIOfferAllocations
    .p2align 4
NtGdiDdDDIOfferAllocations:
    svc #0x1214
    ret
    .globl NtGdiDdDDIOpenAdapterFromDeviceName
    .p2align 4
NtGdiDdDDIOpenAdapterFromDeviceName:
    svc #0x1215
    ret
    .globl NtGdiDdDDIOpenAdapterFromHdc
    .p2align 4
NtGdiDdDDIOpenAdapterFromHdc:
    svc #0x1216
    ret
    .globl NtGdiDdDDIOpenAdapterFromLuid
    .p2align 4
NtGdiDdDDIOpenAdapterFromLuid:
    svc #0x1217
    ret
    .globl NtGdiDdDDIOpenBundleObjectNtHandleFromName
    .p2align 4
NtGdiDdDDIOpenBundleObjectNtHandleFromName:
    svc #0x1218
    ret
    .globl NtGdiDdDDIOpenKeyedMutex
    .p2align 4
NtGdiDdDDIOpenKeyedMutex:
    svc #0x1219
    ret
    .globl NtGdiDdDDIOpenKeyedMutex2
    .p2align 4
NtGdiDdDDIOpenKeyedMutex2:
    svc #0x121A
    ret
    .globl NtGdiDdDDIOpenKeyedMutexFromNtHandle
    .p2align 4
NtGdiDdDDIOpenKeyedMutexFromNtHandle:
    svc #0x121B
    ret
    .globl NtGdiDdDDIOpenNtHandleFromName
    .p2align 4
NtGdiDdDDIOpenNtHandleFromName:
    svc #0x121C
    ret
    .globl NtGdiDdDDIOpenProtectedSessionFromNtHandle
    .p2align 4
NtGdiDdDDIOpenProtectedSessionFromNtHandle:
    svc #0x121D
    ret
    .globl NtGdiDdDDIOpenResource
    .p2align 4
NtGdiDdDDIOpenResource:
    svc #0x121E
    ret
    .globl NtGdiDdDDIOpenResourceFromNtHandle
    .p2align 4
NtGdiDdDDIOpenResourceFromNtHandle:
    svc #0x121F
    ret
    .globl NtGdiDdDDIOpenSwapChain
    .p2align 4
NtGdiDdDDIOpenSwapChain:
    svc #0x1220
    ret
    .globl NtGdiDdDDIOpenSyncObjectFromNtHandle
    .p2align 4
NtGdiDdDDIOpenSyncObjectFromNtHandle:
    svc #0x1221
    ret
    .globl NtGdiDdDDIOpenSyncObjectFromNtHandle2
    .p2align 4
NtGdiDdDDIOpenSyncObjectFromNtHandle2:
    svc #0x1222
    ret
    .globl NtGdiDdDDIOpenSyncObjectNtHandleFromName
    .p2align 4
NtGdiDdDDIOpenSyncObjectNtHandleFromName:
    svc #0x1223
    ret
    .globl NtGdiDdDDIOpenSynchronizationObject
    .p2align 4
NtGdiDdDDIOpenSynchronizationObject:
    svc #0x1224
    ret
    .globl NtGdiDdDDIOutputDuplGetFrameInfo
    .p2align 4
NtGdiDdDDIOutputDuplGetFrameInfo:
    svc #0x1225
    ret
    .globl NtGdiDdDDIOutputDuplGetMetaData
    .p2align 4
NtGdiDdDDIOutputDuplGetMetaData:
    svc #0x1226
    ret
    .globl NtGdiDdDDIOutputDuplGetPointerShapeData
    .p2align 4
NtGdiDdDDIOutputDuplGetPointerShapeData:
    svc #0x1227
    ret
    .globl NtGdiDdDDIOutputDuplPresent
    .p2align 4
NtGdiDdDDIOutputDuplPresent:
    svc #0x1228
    ret
    .globl NtGdiDdDDIOutputDuplReleaseFrame
    .p2align 4
NtGdiDdDDIOutputDuplReleaseFrame:
    svc #0x1229
    ret
    .globl NtGdiDdDDIPollDisplayChildren
    .p2align 4
NtGdiDdDDIPollDisplayChildren:
    svc #0x122A
    ret
    .globl NtGdiDdDDIPresent
    .p2align 4
NtGdiDdDDIPresent:
    svc #0x122B
    ret
    .globl NtGdiDdDDIPresentMultiPlaneOverlay
    .p2align 4
NtGdiDdDDIPresentMultiPlaneOverlay:
    svc #0x122C
    ret
    .globl NtGdiDdDDIPresentMultiPlaneOverlay2
    .p2align 4
NtGdiDdDDIPresentMultiPlaneOverlay2:
    svc #0x122D
    ret
    .globl NtGdiDdDDIPresentMultiPlaneOverlay3
    .p2align 4
NtGdiDdDDIPresentMultiPlaneOverlay3:
    svc #0x122E
    ret
    .globl NtGdiDdDDIPresentRedirected
    .p2align 4
NtGdiDdDDIPresentRedirected:
    svc #0x122F
    ret
    .globl NtGdiDdDDIQueryAdapterInfo
    .p2align 4
NtGdiDdDDIQueryAdapterInfo:
    svc #0x1230
    ret
    .globl NtGdiDdDDIQueryAllocationResidency
    .p2align 4
NtGdiDdDDIQueryAllocationResidency:
    svc #0x1231
    ret
    .globl NtGdiDdDDIQueryClockCalibration
    .p2align 4
NtGdiDdDDIQueryClockCalibration:
    svc #0x1232
    ret
    .globl NtGdiDdDDIQueryFSEBlock
    .p2align 4
NtGdiDdDDIQueryFSEBlock:
    svc #0x1233
    ret
    .globl NtGdiDdDDIQueryProcessOfferInfo
    .p2align 4
NtGdiDdDDIQueryProcessOfferInfo:
    svc #0x1234
    ret
    .globl NtGdiDdDDIQueryProtectedSessionInfoFromNtHandle
    .p2align 4
NtGdiDdDDIQueryProtectedSessionInfoFromNtHandle:
    svc #0x1235
    ret
    .globl NtGdiDdDDIQueryProtectedSessionStatus
    .p2align 4
NtGdiDdDDIQueryProtectedSessionStatus:
    svc #0x1236
    ret
    .globl NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName
    .p2align 4
NtGdiDdDDIQueryRemoteVidPnSourceFromGdiDisplayName:
    svc #0x1237
    ret
    .globl NtGdiDdDDIQueryResourceInfo
    .p2align 4
NtGdiDdDDIQueryResourceInfo:
    svc #0x1238
    ret
    .globl NtGdiDdDDIQueryResourceInfoFromNtHandle
    .p2align 4
NtGdiDdDDIQueryResourceInfoFromNtHandle:
    svc #0x1239
    ret
    .globl NtGdiDdDDIQueryStatistics
    .p2align 4
NtGdiDdDDIQueryStatistics:
    svc #0x123A
    ret
    .globl NtGdiDdDDIQueryVidPnExclusiveOwnership
    .p2align 4
NtGdiDdDDIQueryVidPnExclusiveOwnership:
    svc #0x123B
    ret
    .globl NtGdiDdDDIQueryVideoMemoryInfo
    .p2align 4
NtGdiDdDDIQueryVideoMemoryInfo:
    svc #0x123C
    ret
    .globl NtGdiDdDDIReclaimAllocations
    .p2align 4
NtGdiDdDDIReclaimAllocations:
    svc #0x123D
    ret
    .globl NtGdiDdDDIReclaimAllocations2
    .p2align 4
NtGdiDdDDIReclaimAllocations2:
    svc #0x123E
    ret
    .globl NtGdiDdDDIReleaseKeyedMutex
    .p2align 4
NtGdiDdDDIReleaseKeyedMutex:
    svc #0x123F
    ret
    .globl NtGdiDdDDIReleaseKeyedMutex2
    .p2align 4
NtGdiDdDDIReleaseKeyedMutex2:
    svc #0x1240
    ret
    .globl NtGdiDdDDIReleaseProcessVidPnSourceOwners
    .p2align 4
NtGdiDdDDIReleaseProcessVidPnSourceOwners:
    svc #0x1241
    ret
    .globl NtGdiDdDDIReleaseSwapChain
    .p2align 4
NtGdiDdDDIReleaseSwapChain:
    svc #0x1242
    ret
    .globl NtGdiDdDDIRemoveSurfaceFromSwapChain
    .p2align 4
NtGdiDdDDIRemoveSurfaceFromSwapChain:
    svc #0x1243
    ret
    .globl NtGdiDdDDIRender
    .p2align 4
NtGdiDdDDIRender:
    svc #0x1244
    ret
    .globl NtGdiDdDDIReserveGpuVirtualAddress
    .p2align 4
NtGdiDdDDIReserveGpuVirtualAddress:
    svc #0x1245
    ret
    .globl NtGdiDdDDISetAllocationPriority
    .p2align 4
NtGdiDdDDISetAllocationPriority:
    svc #0x1246
    ret
    .globl NtGdiDdDDISetContextInProcessSchedulingPriority
    .p2align 4
NtGdiDdDDISetContextInProcessSchedulingPriority:
    svc #0x1247
    ret
    .globl NtGdiDdDDISetContextSchedulingPriority
    .p2align 4
NtGdiDdDDISetContextSchedulingPriority:
    svc #0x1248
    ret
    .globl NtGdiDdDDISetDisplayMode
    .p2align 4
NtGdiDdDDISetDisplayMode:
    svc #0x1249
    ret
    .globl NtGdiDdDDISetDodIndirectSwapchain
    .p2align 4
NtGdiDdDDISetDodIndirectSwapchain:
    svc #0x124A
    ret
    .globl NtGdiDdDDISetFSEBlock
    .p2align 4
NtGdiDdDDISetFSEBlock:
    svc #0x124B
    ret
    .globl NtGdiDdDDISetGammaRamp
    .p2align 4
NtGdiDdDDISetGammaRamp:
    svc #0x124C
    ret
    .globl NtGdiDdDDISetHwProtectionTeardownRecovery
    .p2align 4
NtGdiDdDDISetHwProtectionTeardownRecovery:
    svc #0x124D
    ret
    .globl NtGdiDdDDISetMemoryBudgetTarget
    .p2align 4
NtGdiDdDDISetMemoryBudgetTarget:
    svc #0x124E
    ret
    .globl NtGdiDdDDISetMonitorColorSpaceTransform
    .p2align 4
NtGdiDdDDISetMonitorColorSpaceTransform:
    svc #0x124F
    ret
    .globl NtGdiDdDDISetProcessDeviceRemovalSupport
    .p2align 4
NtGdiDdDDISetProcessDeviceRemovalSupport:
    svc #0x1250
    ret
    .globl NtGdiDdDDISetProcessSchedulingPriorityBand
    .p2align 4
NtGdiDdDDISetProcessSchedulingPriorityBand:
    svc #0x1251
    ret
    .globl NtGdiDdDDISetProcessSchedulingPriorityClass
    .p2align 4
NtGdiDdDDISetProcessSchedulingPriorityClass:
    svc #0x1252
    ret
    .globl NtGdiDdDDISetQueuedLimit
    .p2align 4
NtGdiDdDDISetQueuedLimit:
    svc #0x1253
    ret
    .globl NtGdiDdDDISetStablePowerState
    .p2align 4
NtGdiDdDDISetStablePowerState:
    svc #0x1254
    ret
    .globl NtGdiDdDDISetStereoEnabled
    .p2align 4
NtGdiDdDDISetStereoEnabled:
    svc #0x1255
    ret
    .globl NtGdiDdDDISetSyncRefreshCountWaitTarget
    .p2align 4
NtGdiDdDDISetSyncRefreshCountWaitTarget:
    svc #0x1256
    ret
    .globl NtGdiDdDDISetVidPnSourceHwProtection
    .p2align 4
NtGdiDdDDISetVidPnSourceHwProtection:
    svc #0x1257
    ret
    .globl NtGdiDdDDISetVidPnSourceOwner
    .p2align 4
NtGdiDdDDISetVidPnSourceOwner:
    svc #0x1258
    ret
    .globl NtGdiDdDDISetYieldPercentage
    .p2align 4
NtGdiDdDDISetYieldPercentage:
    svc #0x1259
    ret
    .globl NtGdiDdDDIShareObjects
    .p2align 4
NtGdiDdDDIShareObjects:
    svc #0x125A
    ret
    .globl NtGdiDdDDISharedPrimaryLockNotification
    .p2align 4
NtGdiDdDDISharedPrimaryLockNotification:
    svc #0x125B
    ret
    .globl NtGdiDdDDISharedPrimaryUnLockNotification
    .p2align 4
NtGdiDdDDISharedPrimaryUnLockNotification:
    svc #0x125C
    ret
    .globl NtGdiDdDDISignalSynchronizationObject
    .p2align 4
NtGdiDdDDISignalSynchronizationObject:
    svc #0x125D
    ret
    .globl NtGdiDdDDISignalSynchronizationObjectFromCpu
    .p2align 4
NtGdiDdDDISignalSynchronizationObjectFromCpu:
    svc #0x125E
    ret
    .globl NtGdiDdDDISignalSynchronizationObjectFromGpu
    .p2align 4
NtGdiDdDDISignalSynchronizationObjectFromGpu:
    svc #0x125F
    ret
    .globl NtGdiDdDDISignalSynchronizationObjectFromGpu2
    .p2align 4
NtGdiDdDDISignalSynchronizationObjectFromGpu2:
    svc #0x1260
    ret
    .globl NtGdiDdDDISubmitCommand
    .p2align 4
NtGdiDdDDISubmitCommand:
    svc #0x1261
    ret
    .globl NtGdiDdDDISubmitCommandToHwQueue
    .p2align 4
NtGdiDdDDISubmitCommandToHwQueue:
    svc #0x1262
    ret
    .globl NtGdiDdDDISubmitSignalSyncObjectsToHwQueue
    .p2align 4
NtGdiDdDDISubmitSignalSyncObjectsToHwQueue:
    svc #0x1263
    ret
    .globl NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue
    .p2align 4
NtGdiDdDDISubmitWaitForSyncObjectsToHwQueue:
    svc #0x1264
    ret
    .globl NtGdiDdDDITrimProcessCommitment
    .p2align 4
NtGdiDdDDITrimProcessCommitment:
    svc #0x1265
    ret
    .globl NtGdiDdDDIUnOrderedPresentSwapChain
    .p2align 4
NtGdiDdDDIUnOrderedPresentSwapChain:
    svc #0x1266
    ret
    .globl NtGdiDdDDIUnlock
    .p2align 4
NtGdiDdDDIUnlock:
    svc #0x1267
    ret
    .globl NtGdiDdDDIUnlock2
    .p2align 4
NtGdiDdDDIUnlock2:
    svc #0x1268
    ret
    .globl NtGdiDdDDIUpdateAllocationProperty
    .p2align 4
NtGdiDdDDIUpdateAllocationProperty:
    svc #0x1269
    ret
    .globl NtGdiDdDDIUpdateGpuVirtualAddress
    .p2align 4
NtGdiDdDDIUpdateGpuVirtualAddress:
    svc #0x126A
    ret
    .globl NtGdiDdDDIUpdateOverlay
    .p2align 4
NtGdiDdDDIUpdateOverlay:
    svc #0x126B
    ret
    .globl NtGdiDdDDIWaitForIdle
    .p2align 4
NtGdiDdDDIWaitForIdle:
    svc #0x126C
    ret
    .globl NtGdiDdDDIWaitForSynchronizationObject
    .p2align 4
NtGdiDdDDIWaitForSynchronizationObject:
    svc #0x126D
    ret
    .globl NtGdiDdDDIWaitForSynchronizationObjectFromCpu
    .p2align 4
NtGdiDdDDIWaitForSynchronizationObjectFromCpu:
    svc #0x126E
    ret
    .globl NtGdiDdDDIWaitForSynchronizationObjectFromGpu
    .p2align 4
NtGdiDdDDIWaitForSynchronizationObjectFromGpu:
    svc #0x126F
    ret
    .globl NtGdiDdDDIWaitForVerticalBlankEvent
    .p2align 4
NtGdiDdDDIWaitForVerticalBlankEvent:
    svc #0x1270
    ret
    .globl NtGdiDdDDIWaitForVerticalBlankEvent2
    .p2align 4
NtGdiDdDDIWaitForVerticalBlankEvent2:
    svc #0x1271
    ret
    .globl NtGdiDdDestroyFullscreenSprite
    .p2align 4
NtGdiDdDestroyFullscreenSprite:
    svc #0x1272
    ret
    .globl NtGdiDdNotifyFullscreenSpriteUpdate
    .p2align 4
NtGdiDdNotifyFullscreenSpriteUpdate:
    svc #0x1273
    ret
    .globl NtGdiDdQueryVisRgnUniqueness
    .p2align 4
NtGdiDdQueryVisRgnUniqueness:
    svc #0x1274
    ret
    .globl NtGdiDeleteColorTransform
    .p2align 4
NtGdiDeleteColorTransform:
    svc #0x1275
    ret
    .globl NtGdiDescribePixelFormat
    .p2align 4
NtGdiDescribePixelFormat:
    svc #0x1276
    ret
    .globl NtGdiDestroyOPMProtectedOutput
    .p2align 4
NtGdiDestroyOPMProtectedOutput:
    svc #0x1277
    ret
    .globl NtGdiDestroyPhysicalMonitor
    .p2align 4
NtGdiDestroyPhysicalMonitor:
    svc #0x1278
    ret
    .globl NtGdiDisableUMPDSandboxing
    .p2align 4
NtGdiDisableUMPDSandboxing:
    svc #0x1279
    ret
    .globl NtGdiDoBanding
    .p2align 4
NtGdiDoBanding:
    svc #0x127A
    ret
    .globl NtGdiDrawEscape
    .p2align 4
NtGdiDrawEscape:
    svc #0x127B
    ret
    .globl NtGdiDwmCreatedBitmapRemotingOutput
    .p2align 4
NtGdiDwmCreatedBitmapRemotingOutput:
    svc #0x127C
    ret
    .globl NtGdiEllipse
    .p2align 4
NtGdiEllipse:
    svc #0x127D
    ret
    .globl NtGdiEnableEudc
    .p2align 4
NtGdiEnableEudc:
    svc #0x127E
    ret
    .globl NtGdiEndDoc
    .p2align 4
NtGdiEndDoc:
    svc #0x127F
    ret
    .globl NtGdiEndGdiRendering
    .p2align 4
NtGdiEndGdiRendering:
    svc #0x1280
    ret
    .globl NtGdiEndPage
    .p2align 4
NtGdiEndPage:
    svc #0x1281
    ret
    .globl NtGdiEngAlphaBlend
    .p2align 4
NtGdiEngAlphaBlend:
    svc #0x1282
    ret
    .globl NtGdiEngAssociateSurface
    .p2align 4
NtGdiEngAssociateSurface:
    svc #0x1283
    ret
    .globl NtGdiEngBitBlt
    .p2align 4
NtGdiEngBitBlt:
    svc #0x1284
    ret
    .globl NtGdiEngCheckAbort
    .p2align 4
NtGdiEngCheckAbort:
    svc #0x1285
    ret
    .globl NtGdiEngComputeGlyphSet
    .p2align 4
NtGdiEngComputeGlyphSet:
    svc #0x1286
    ret
    .globl NtGdiEngCopyBits
    .p2align 4
NtGdiEngCopyBits:
    svc #0x1287
    ret
    .globl NtGdiEngCreateBitmap
    .p2align 4
NtGdiEngCreateBitmap:
    svc #0x1288
    ret
    .globl NtGdiEngCreateClip
    .p2align 4
NtGdiEngCreateClip:
    svc #0x1289
    ret
    .globl NtGdiEngCreateDeviceBitmap
    .p2align 4
NtGdiEngCreateDeviceBitmap:
    svc #0x128A
    ret
    .globl NtGdiEngCreateDeviceSurface
    .p2align 4
NtGdiEngCreateDeviceSurface:
    svc #0x128B
    ret
    .globl NtGdiEngCreatePalette
    .p2align 4
NtGdiEngCreatePalette:
    svc #0x128C
    ret
    .globl NtGdiEngDeleteClip
    .p2align 4
NtGdiEngDeleteClip:
    svc #0x128D
    ret
    .globl NtGdiEngDeletePalette
    .p2align 4
NtGdiEngDeletePalette:
    svc #0x128E
    ret
    .globl NtGdiEngDeletePath
    .p2align 4
NtGdiEngDeletePath:
    svc #0x128F
    ret
    .globl NtGdiEngDeleteSurface
    .p2align 4
NtGdiEngDeleteSurface:
    svc #0x1290
    ret
    .globl NtGdiEngEraseSurface
    .p2align 4
NtGdiEngEraseSurface:
    svc #0x1291
    ret
    .globl NtGdiEngFillPath
    .p2align 4
NtGdiEngFillPath:
    svc #0x1292
    ret
    .globl NtGdiEngGradientFill
    .p2align 4
NtGdiEngGradientFill:
    svc #0x1293
    ret
    .globl NtGdiEngLineTo
    .p2align 4
NtGdiEngLineTo:
    svc #0x1294
    ret
    .globl NtGdiEngLockSurface
    .p2align 4
NtGdiEngLockSurface:
    svc #0x1295
    ret
    .globl NtGdiEngMarkBandingSurface
    .p2align 4
NtGdiEngMarkBandingSurface:
    svc #0x1296
    ret
    .globl NtGdiEngPaint
    .p2align 4
NtGdiEngPaint:
    svc #0x1297
    ret
    .globl NtGdiEngPlgBlt
    .p2align 4
NtGdiEngPlgBlt:
    svc #0x1298
    ret
    .globl NtGdiEngStretchBlt
    .p2align 4
NtGdiEngStretchBlt:
    svc #0x1299
    ret
    .globl NtGdiEngStretchBltROP
    .p2align 4
NtGdiEngStretchBltROP:
    svc #0x129A
    ret
    .globl NtGdiEngStrokeAndFillPath
    .p2align 4
NtGdiEngStrokeAndFillPath:
    svc #0x129B
    ret
    .globl NtGdiEngStrokePath
    .p2align 4
NtGdiEngStrokePath:
    svc #0x129C
    ret
    .globl NtGdiEngTextOut
    .p2align 4
NtGdiEngTextOut:
    svc #0x129D
    ret
    .globl NtGdiEngTransparentBlt
    .p2align 4
NtGdiEngTransparentBlt:
    svc #0x129E
    ret
    .globl NtGdiEngUnlockSurface
    .p2align 4
NtGdiEngUnlockSurface:
    svc #0x129F
    ret
    .globl NtGdiEnsureDpiDepDefaultGuiFontForPlateau
    .p2align 4
NtGdiEnsureDpiDepDefaultGuiFontForPlateau:
    svc #0x12A0
    ret
    .globl NtGdiEnumFonts
    .p2align 4
NtGdiEnumFonts:
    svc #0x12A1
    ret
    .globl NtGdiEnumObjects
    .p2align 4
NtGdiEnumObjects:
    svc #0x12A2
    ret
    .globl NtGdiEudcLoadUnloadLink
    .p2align 4
NtGdiEudcLoadUnloadLink:
    svc #0x12A3
    ret
    .globl NtGdiExtFloodFill
    .p2align 4
NtGdiExtFloodFill:
    svc #0x12A4
    ret
    .globl NtGdiFONTOBJ_cGetAllGlyphHandles
    .p2align 4
NtGdiFONTOBJ_cGetAllGlyphHandles:
    svc #0x12A5
    ret
    .globl NtGdiFONTOBJ_cGetGlyphs
    .p2align 4
NtGdiFONTOBJ_cGetGlyphs:
    svc #0x12A6
    ret
    .globl NtGdiFONTOBJ_pQueryGlyphAttrs
    .p2align 4
NtGdiFONTOBJ_pQueryGlyphAttrs:
    svc #0x12A7
    ret
    .globl NtGdiFONTOBJ_pfdg
    .p2align 4
NtGdiFONTOBJ_pfdg:
    svc #0x12A8
    ret
    .globl NtGdiFONTOBJ_pifi
    .p2align 4
NtGdiFONTOBJ_pifi:
    svc #0x12A9
    ret
    .globl NtGdiFONTOBJ_pvTrueTypeFontFile
    .p2align 4
NtGdiFONTOBJ_pvTrueTypeFontFile:
    svc #0x12AA
    ret
    .globl NtGdiFONTOBJ_pxoGetXform
    .p2align 4
NtGdiFONTOBJ_pxoGetXform:
    svc #0x12AB
    ret
    .globl NtGdiFONTOBJ_vGetInfo
    .p2align 4
NtGdiFONTOBJ_vGetInfo:
    svc #0x12AC
    ret
    .globl NtGdiFlattenPath
    .p2align 4
NtGdiFlattenPath:
    svc #0x12AD
    ret
    .globl NtGdiFontIsLinked
    .p2align 4
NtGdiFontIsLinked:
    svc #0x12AE
    ret
    .globl NtGdiForceUFIMapping
    .p2align 4
NtGdiForceUFIMapping:
    svc #0x12AF
    ret
    .globl NtGdiFrameRgn
    .p2align 4
NtGdiFrameRgn:
    svc #0x12B0
    ret
    .globl NtGdiFullscreenControl
    .p2align 4
NtGdiFullscreenControl:
    svc #0x12B1
    ret
    .globl NtGdiGetBitmapDpiScaleValue
    .p2align 4
NtGdiGetBitmapDpiScaleValue:
    svc #0x12B2
    ret
    .globl NtGdiGetBoundsRect
    .p2align 4
NtGdiGetBoundsRect:
    svc #0x12B3
    ret
    .globl NtGdiGetCOPPCompatibleOPMInformation
    .p2align 4
NtGdiGetCOPPCompatibleOPMInformation:
    svc #0x12B4
    ret
    .globl NtGdiGetCertificate
    .p2align 4
NtGdiGetCertificate:
    svc #0x12B5
    ret
    .globl NtGdiGetCertificateByHandle
    .p2align 4
NtGdiGetCertificateByHandle:
    svc #0x12B6
    ret
    .globl NtGdiGetCertificateSize
    .p2align 4
NtGdiGetCertificateSize:
    svc #0x12B7
    ret
    .globl NtGdiGetCertificateSizeByHandle
    .p2align 4
NtGdiGetCertificateSizeByHandle:
    svc #0x12B8
    ret
    .globl NtGdiGetCharABCWidthsW
    .p2align 4
NtGdiGetCharABCWidthsW:
    svc #0x12B9
    ret
    .globl NtGdiGetCharacterPlacementW
    .p2align 4
NtGdiGetCharacterPlacementW:
    svc #0x12BA
    ret
    .globl NtGdiGetColorAdjustment
    .p2align 4
NtGdiGetColorAdjustment:
    svc #0x12BB
    ret
    .globl NtGdiGetColorSpaceforBitmap
    .p2align 4
NtGdiGetColorSpaceforBitmap:
    svc #0x12BC
    ret
    .globl NtGdiGetCurrentDpiInfo
    .p2align 4
NtGdiGetCurrentDpiInfo:
    svc #0x12BD
    ret
    .globl NtGdiGetDCDpiScaleValue
    .p2align 4
NtGdiGetDCDpiScaleValue:
    svc #0x12BE
    ret
    .globl NtGdiGetDeviceCaps
    .p2align 4
NtGdiGetDeviceCaps:
    svc #0x12BF
    ret
    .globl NtGdiGetDeviceCapsAll
    .p2align 4
NtGdiGetDeviceCapsAll:
    svc #0x12C0
    ret
    .globl NtGdiGetDeviceWidth
    .p2align 4
NtGdiGetDeviceWidth:
    svc #0x12C1
    ret
    .globl NtGdiGetDhpdev
    .p2align 4
NtGdiGetDhpdev:
    svc #0x12C2
    ret
    .globl NtGdiGetETM
    .p2align 4
NtGdiGetETM:
    svc #0x12C3
    ret
    .globl NtGdiGetEmbUFI
    .p2align 4
NtGdiGetEmbUFI:
    svc #0x12C4
    ret
    .globl NtGdiGetEmbedFonts
    .p2align 4
NtGdiGetEmbedFonts:
    svc #0x12C5
    ret
    .globl NtGdiGetEntry
    .p2align 4
NtGdiGetEntry:
    svc #0x12C6
    ret
    .globl NtGdiGetEudcTimeStampEx
    .p2align 4
NtGdiGetEudcTimeStampEx:
    svc #0x12C7
    ret
    .globl NtGdiGetFontFileData
    .p2align 4
NtGdiGetFontFileData:
    svc #0x12C8
    ret
    .globl NtGdiGetFontFileInfo
    .p2align 4
NtGdiGetFontFileInfo:
    svc #0x12C9
    ret
    .globl NtGdiGetFontResourceInfoInternalW
    .p2align 4
NtGdiGetFontResourceInfoInternalW:
    svc #0x12CA
    ret
    .globl NtGdiGetFontUnicodeRanges
    .p2align 4
NtGdiGetFontUnicodeRanges:
    svc #0x12CB
    ret
    .globl NtGdiGetGlyphIndicesW
    .p2align 4
NtGdiGetGlyphIndicesW:
    svc #0x12CC
    ret
    .globl NtGdiGetGlyphIndicesWInternal
    .p2align 4
NtGdiGetGlyphIndicesWInternal:
    svc #0x12CD
    ret
    .globl NtGdiGetGlyphOutline
    .p2align 4
NtGdiGetGlyphOutline:
    svc #0x12CE
    ret
    .globl NtGdiGetKerningPairs
    .p2align 4
NtGdiGetKerningPairs:
    svc #0x12CF
    ret
    .globl NtGdiGetLinkedUFIs
    .p2align 4
NtGdiGetLinkedUFIs:
    svc #0x12D0
    ret
    .globl NtGdiGetMiterLimit
    .p2align 4
NtGdiGetMiterLimit:
    svc #0x12D1
    ret
    .globl NtGdiGetMonitorID
    .p2align 4
NtGdiGetMonitorID:
    svc #0x12D2
    ret
    .globl NtGdiGetNumberOfPhysicalMonitors
    .p2align 4
NtGdiGetNumberOfPhysicalMonitors:
    svc #0x12D3
    ret
    .globl NtGdiGetOPMInformation
    .p2align 4
NtGdiGetOPMInformation:
    svc #0x12D4
    ret
    .globl NtGdiGetOPMRandomNumber
    .p2align 4
NtGdiGetOPMRandomNumber:
    svc #0x12D5
    ret
    .globl NtGdiGetObjectBitmapHandle
    .p2align 4
NtGdiGetObjectBitmapHandle:
    svc #0x12D6
    ret
    .globl NtGdiGetPath
    .p2align 4
NtGdiGetPath:
    svc #0x12D7
    ret
    .globl NtGdiGetPerBandInfo
    .p2align 4
NtGdiGetPerBandInfo:
    svc #0x12D8
    ret
    .globl NtGdiGetPhysicalMonitorDescription
    .p2align 4
NtGdiGetPhysicalMonitorDescription:
    svc #0x12D9
    ret
    .globl NtGdiGetPhysicalMonitorFromTarget
    .p2align 4
NtGdiGetPhysicalMonitorFromTarget:
    svc #0x12DA
    ret
    .globl NtGdiGetPhysicalMonitors
    .p2align 4
NtGdiGetPhysicalMonitors:
    svc #0x12DB
    ret
    .globl NtGdiGetProcessSessionFonts
    .p2align 4
NtGdiGetProcessSessionFonts:
    svc #0x12DC
    ret
    .globl NtGdiGetPublicFontTableChangeCookie
    .p2align 4
NtGdiGetPublicFontTableChangeCookie:
    svc #0x12DD
    ret
    .globl NtGdiGetRealizationInfo
    .p2align 4
NtGdiGetRealizationInfo:
    svc #0x12DE
    ret
    .globl NtGdiGetServerMetaFileBits
    .p2align 4
NtGdiGetServerMetaFileBits:
    svc #0x12DF
    ret
    .globl NtGdiGetSpoolMessage
    .p2align 4
NtGdiGetSpoolMessage:
    svc #0x12E0
    ret
    .globl NtGdiGetStats
    .p2align 4
NtGdiGetStats:
    svc #0x12E1
    ret
    .globl NtGdiGetStringBitmapW
    .p2align 4
NtGdiGetStringBitmapW:
    svc #0x12E2
    ret
    .globl NtGdiGetSuggestedOPMProtectedOutputArraySize
    .p2align 4
NtGdiGetSuggestedOPMProtectedOutputArraySize:
    svc #0x12E3
    ret
    .globl NtGdiGetTextExtentExW
    .p2align 4
NtGdiGetTextExtentExW:
    svc #0x12E4
    ret
    .globl NtGdiGetUFI
    .p2align 4
NtGdiGetUFI:
    svc #0x12E5
    ret
    .globl NtGdiGetUFIPathname
    .p2align 4
NtGdiGetUFIPathname:
    svc #0x12E6
    ret
    .globl NtGdiGradientFill
    .p2align 4
NtGdiGradientFill:
    svc #0x12E7
    ret
    .globl NtGdiHLSurfGetInformation
    .p2align 4
NtGdiHLSurfGetInformation:
    svc #0x12E8
    ret
    .globl NtGdiHLSurfSetInformation
    .p2align 4
NtGdiHLSurfSetInformation:
    svc #0x12E9
    ret
    .globl NtGdiHT_Get8BPPFormatPalette
    .p2align 4
NtGdiHT_Get8BPPFormatPalette:
    svc #0x12EA
    ret
    .globl NtGdiHT_Get8BPPMaskPalette
    .p2align 4
NtGdiHT_Get8BPPMaskPalette:
    svc #0x12EB
    ret
    .globl NtGdiIcmBrushInfo
    .p2align 4
NtGdiIcmBrushInfo:
    svc #0x12EC
    ret
    .globl NtGdiInit
    .p2align 4
NtGdiInit:
    svc #0x12ED
    ret
    .globl NtGdiInit2
    .p2align 4
NtGdiInit2:
    svc #0x12EE
    ret
    .globl NtGdiInitSpool
    .p2align 4
NtGdiInitSpool:
    svc #0x12EF
    ret
    .globl NtGdiMakeFontDir
    .p2align 4
NtGdiMakeFontDir:
    svc #0x12F0
    ret
    .globl NtGdiMakeInfoDC
    .p2align 4
NtGdiMakeInfoDC:
    svc #0x12F1
    ret
    .globl NtGdiMakeObjectUnXferable
    .p2align 4
NtGdiMakeObjectUnXferable:
    svc #0x12F2
    ret
    .globl NtGdiMakeObjectXferable
    .p2align 4
NtGdiMakeObjectXferable:
    svc #0x12F3
    ret
    .globl NtGdiMirrorWindowOrg
    .p2align 4
NtGdiMirrorWindowOrg:
    svc #0x12F4
    ret
    .globl NtGdiMonoBitmap
    .p2align 4
NtGdiMonoBitmap:
    svc #0x12F5
    ret
    .globl NtGdiMoveTo
    .p2align 4
NtGdiMoveTo:
    svc #0x12F6
    ret
    .globl NtGdiOffsetClipRgn
    .p2align 4
NtGdiOffsetClipRgn:
    svc #0x12F7
    ret
    .globl NtGdiPATHOBJ_bEnum
    .p2align 4
NtGdiPATHOBJ_bEnum:
    svc #0x12F8
    ret
    .globl NtGdiPATHOBJ_bEnumClipLines
    .p2align 4
NtGdiPATHOBJ_bEnumClipLines:
    svc #0x12F9
    ret
    .globl NtGdiPATHOBJ_vEnumStart
    .p2align 4
NtGdiPATHOBJ_vEnumStart:
    svc #0x12FA
    ret
    .globl NtGdiPATHOBJ_vEnumStartClipLines
    .p2align 4
NtGdiPATHOBJ_vEnumStartClipLines:
    svc #0x12FB
    ret
    .globl NtGdiPATHOBJ_vGetBounds
    .p2align 4
NtGdiPATHOBJ_vGetBounds:
    svc #0x12FC
    ret
    .globl NtGdiPathToRegion
    .p2align 4
NtGdiPathToRegion:
    svc #0x12FD
    ret
    .globl NtGdiPlgBlt
    .p2align 4
NtGdiPlgBlt:
    svc #0x12FE
    ret
    .globl NtGdiPolyDraw
    .p2align 4
NtGdiPolyDraw:
    svc #0x12FF
    ret
    .globl NtGdiPolyTextOutW
    .p2align 4
NtGdiPolyTextOutW:
    svc #0x1300
    ret
    .globl NtGdiPtInRegion
    .p2align 4
NtGdiPtInRegion:
    svc #0x1301
    ret
    .globl NtGdiPtVisible
    .p2align 4
NtGdiPtVisible:
    svc #0x1302
    ret
    .globl NtGdiQueryFonts
    .p2align 4
NtGdiQueryFonts:
    svc #0x1303
    ret
    .globl NtGdiRemoveFontResourceW
    .p2align 4
NtGdiRemoveFontResourceW:
    svc #0x1304
    ret
    .globl NtGdiRemoveMergeFont
    .p2align 4
NtGdiRemoveMergeFont:
    svc #0x1305
    ret
    .globl NtGdiResetDC
    .p2align 4
NtGdiResetDC:
    svc #0x1306
    ret
    .globl NtGdiResizePalette
    .p2align 4
NtGdiResizePalette:
    svc #0x1307
    ret
    .globl NtGdiRoundRect
    .p2align 4
NtGdiRoundRect:
    svc #0x1308
    ret
    .globl NtGdiSTROBJ_bEnum
    .p2align 4
NtGdiSTROBJ_bEnum:
    svc #0x1309
    ret
    .globl NtGdiSTROBJ_bEnumPositionsOnly
    .p2align 4
NtGdiSTROBJ_bEnumPositionsOnly:
    svc #0x130A
    ret
    .globl NtGdiSTROBJ_bGetAdvanceWidths
    .p2align 4
NtGdiSTROBJ_bGetAdvanceWidths:
    svc #0x130B
    ret
    .globl NtGdiSTROBJ_dwGetCodePage
    .p2align 4
NtGdiSTROBJ_dwGetCodePage:
    svc #0x130C
    ret
    .globl NtGdiSTROBJ_vEnumStart
    .p2align 4
NtGdiSTROBJ_vEnumStart:
    svc #0x130D
    ret
    .globl NtGdiScaleRgn
    .p2align 4
NtGdiScaleRgn:
    svc #0x130E
    ret
    .globl NtGdiScaleValues
    .p2align 4
NtGdiScaleValues:
    svc #0x130F
    ret
    .globl NtGdiScaleViewportExtEx
    .p2align 4
NtGdiScaleViewportExtEx:
    svc #0x1310
    ret
    .globl NtGdiScaleWindowExtEx
    .p2align 4
NtGdiScaleWindowExtEx:
    svc #0x1311
    ret
    .globl NtGdiSelectBrush
    .p2align 4
NtGdiSelectBrush:
    svc #0x1312
    ret
    .globl NtGdiSelectClipPath
    .p2align 4
NtGdiSelectClipPath:
    svc #0x1313
    ret
    .globl NtGdiSelectPen
    .p2align 4
NtGdiSelectPen:
    svc #0x1314
    ret
    .globl NtGdiSetBitmapAttributes
    .p2align 4
NtGdiSetBitmapAttributes:
    svc #0x1315
    ret
    .globl NtGdiSetBrushAttributes
    .p2align 4
NtGdiSetBrushAttributes:
    svc #0x1316
    ret
    .globl NtGdiSetColorAdjustment
    .p2align 4
NtGdiSetColorAdjustment:
    svc #0x1317
    ret
    .globl NtGdiSetColorSpace
    .p2align 4
NtGdiSetColorSpace:
    svc #0x1318
    ret
    .globl NtGdiSetFontXform
    .p2align 4
NtGdiSetFontXform:
    svc #0x1319
    ret
    .globl NtGdiSetIcmMode
    .p2align 4
NtGdiSetIcmMode:
    svc #0x131A
    ret
    .globl NtGdiSetLinkedUFIs
    .p2align 4
NtGdiSetLinkedUFIs:
    svc #0x131B
    ret
    .globl NtGdiSetMagicColors
    .p2align 4
NtGdiSetMagicColors:
    svc #0x131C
    ret
    .globl NtGdiSetOPMSigningKeyAndSequenceNumbers
    .p2align 4
NtGdiSetOPMSigningKeyAndSequenceNumbers:
    svc #0x131D
    ret
    .globl NtGdiSetPUMPDOBJ
    .p2align 4
NtGdiSetPUMPDOBJ:
    svc #0x131E
    ret
    .globl NtGdiSetPixelFormat
    .p2align 4
NtGdiSetPixelFormat:
    svc #0x131F
    ret
    .globl NtGdiSetRectRgn
    .p2align 4
NtGdiSetRectRgn:
    svc #0x1320
    ret
    .globl NtGdiSetSizeDevice
    .p2align 4
NtGdiSetSizeDevice:
    svc #0x1321
    ret
    .globl NtGdiSetSystemPaletteUse
    .p2align 4
NtGdiSetSystemPaletteUse:
    svc #0x1322
    ret
    .globl NtGdiSetTextJustification
    .p2align 4
NtGdiSetTextJustification:
    svc #0x1323
    ret
    .globl NtGdiSetUMPDSandboxState
    .p2align 4
NtGdiSetUMPDSandboxState:
    svc #0x1324
    ret
    .globl NtGdiStartDoc
    .p2align 4
NtGdiStartDoc:
    svc #0x1325
    ret
    .globl NtGdiStartPage
    .p2align 4
NtGdiStartPage:
    svc #0x1326
    ret
    .globl NtGdiStrokeAndFillPath
    .p2align 4
NtGdiStrokeAndFillPath:
    svc #0x1327
    ret
    .globl NtGdiStrokePath
    .p2align 4
NtGdiStrokePath:
    svc #0x1328
    ret
    .globl NtGdiSwapBuffers
    .p2align 4
NtGdiSwapBuffers:
    svc #0x1329
    ret
    .globl NtGdiTransparentBlt
    .p2align 4
NtGdiTransparentBlt:
    svc #0x132A
    ret
    .globl NtGdiUMPDEngFreeUserMem
    .p2align 4
NtGdiUMPDEngFreeUserMem:
    svc #0x132B
    ret
    .globl NtGdiUnloadPrinterDriver
    .p2align 4
NtGdiUnloadPrinterDriver:
    svc #0x132C
    ret
    .globl NtGdiUnmapMemFont
    .p2align 4
NtGdiUnmapMemFont:
    svc #0x132D
    ret
    .globl NtGdiUpdateColors
    .p2align 4
NtGdiUpdateColors:
    svc #0x132E
    ret
    .globl NtGdiUpdateTransform
    .p2align 4
NtGdiUpdateTransform:
    svc #0x132F
    ret
    .globl NtGdiWaitForTextReady
    .p2align 4
NtGdiWaitForTextReady:
    svc #0x1330
    ret
    .globl NtGdiWidenPath
    .p2align 4
NtGdiWidenPath:
    svc #0x1331
    ret
    .globl NtGdiXFORMOBJ_bApplyXform
    .p2align 4
NtGdiXFORMOBJ_bApplyXform:
    svc #0x1332
    ret
    .globl NtGdiXFORMOBJ_iGetXform
    .p2align 4
NtGdiXFORMOBJ_iGetXform:
    svc #0x1333
    ret
    .globl NtGdiXLATEOBJ_cGetPalette
    .p2align 4
NtGdiXLATEOBJ_cGetPalette:
    svc #0x1334
    ret
    .globl NtGdiXLATEOBJ_hGetColorTransform
    .p2align 4
NtGdiXLATEOBJ_hGetColorTransform:
    svc #0x1335
    ret
    .globl NtUserGetOwnerTransformedMonitorRect
    .p2align 4
NtUserGetOwnerTransformedMonitorRect:
    svc #0x1336
    ret
    .globl NtGdiXLATEOBJ_iXlate
    .p2align 4
NtGdiXLATEOBJ_iXlate:
    svc #0x1337
    ret
    .globl NtHWCursorUpdatePointer
    .p2align 4
NtHWCursorUpdatePointer:
    svc #0x1338
    ret
    .globl NtInputSpaceRegionFromPoint
    .p2align 4
NtInputSpaceRegionFromPoint:
    svc #0x1339
    ret
    .globl NtIsOneCoreTransformMode
    .p2align 4
NtIsOneCoreTransformMode:
    svc #0x133A
    ret
    .globl NtKSTInitialize
    .p2align 4
NtKSTInitialize:
    svc #0x133B
    ret
    .globl NtKSTWait
    .p2align 4
NtKSTWait:
    svc #0x133C
    ret
    .globl NtMITAccessibilityTimerNotification
    .p2align 4
NtMITAccessibilityTimerNotification:
    svc #0x133D
    ret
    .globl NtMITActivateInputProcessing
    .p2align 4
NtMITActivateInputProcessing:
    svc #0x133E
    ret
    .globl NtMITConfigureVirtualTouchpad
    .p2align 4
NtMITConfigureVirtualTouchpad:
    svc #0x133F
    ret
    .globl NtMITCoreMsgKOpenConnectionTo
    .p2align 4
NtMITCoreMsgKOpenConnectionTo:
    svc #0x1340
    ret
    .globl NtMITDeactivateInputProcessing
    .p2align 4
NtMITDeactivateInputProcessing:
    svc #0x1341
    ret
    .globl NtMITDisableMouseIntercept
    .p2align 4
NtMITDisableMouseIntercept:
    svc #0x1342
    ret
    .globl NtMITDispatchCompletion
    .p2align 4
NtMITDispatchCompletion:
    svc #0x1343
    ret
    .globl NtMITEnableMouseIntercept
    .p2align 4
NtMITEnableMouseIntercept:
    svc #0x1344
    ret
    .globl NtMITGetCursorUpdateHandle
    .p2align 4
NtMITGetCursorUpdateHandle:
    svc #0x1345
    ret
    .globl NtMITInitMinuserThread
    .p2align 4
NtMITInitMinuserThread:
    svc #0x1346
    ret
    .globl NtMITMinuserSetInputTransformOffset
    .p2align 4
NtMITMinuserSetInputTransformOffset:
    svc #0x1347
    ret
    .globl NtMITMinuserWindowDestroyed
    .p2align 4
NtMITMinuserWindowDestroyed:
    svc #0x1348
    ret
    .globl NtMITPostMouseInputMessage
    .p2align 4
NtMITPostMouseInputMessage:
    svc #0x1349
    ret
    .globl NtMITPostThreadEventMessage
    .p2align 4
NtMITPostThreadEventMessage:
    svc #0x134A
    ret
    .globl NtMITPostWindowEventMessage
    .p2align 4
NtMITPostWindowEventMessage:
    svc #0x134B
    ret
    .globl NtMITPrepareReceiveInputMessage
    .p2align 4
NtMITPrepareReceiveInputMessage:
    svc #0x134C
    ret
    .globl NtMITPrepareSendInputMessage
    .p2align 4
NtMITPrepareSendInputMessage:
    svc #0x134D
    ret
    .globl NtMITProcessDelegateCapturedPointers
    .p2align 4
NtMITProcessDelegateCapturedPointers:
    svc #0x134E
    ret
    .globl NtMITSetInputCallbacks
    .p2align 4
NtMITSetInputCallbacks:
    svc #0x134F
    ret
    .globl NtMITSetInputDelegationMode
    .p2align 4
NtMITSetInputDelegationMode:
    svc #0x1350
    ret
    .globl NtMITSetInputObservationState
    .p2align 4
NtMITSetInputObservationState:
    svc #0x1351
    ret
    .globl NtMITSetKeyboardInputRoutingPolicy
    .p2align 4
NtMITSetKeyboardInputRoutingPolicy:
    svc #0x1352
    ret
    .globl NtMITSetKeyboardOverriderState
    .p2align 4
NtMITSetKeyboardOverriderState:
    svc #0x1353
    ret
    .globl NtMITSetLastInputRecipient
    .p2align 4
NtMITSetLastInputRecipient:
    svc #0x1354
    ret
    .globl NtMITSynthesizeKeyboardInput
    .p2align 4
NtMITSynthesizeKeyboardInput:
    svc #0x1355
    ret
    .globl NtMITSynthesizeMouseInput
    .p2align 4
NtMITSynthesizeMouseInput:
    svc #0x1356
    ret
    .globl NtMITSynthesizeTouchInput
    .p2align 4
NtMITSynthesizeTouchInput:
    svc #0x1357
    ret
    .globl NtMITUninitMinuserThread
    .p2align 4
NtMITUninitMinuserThread:
    svc #0x1358
    ret
    .globl NtMITUpdateInputGlobals
    .p2align 4
NtMITUpdateInputGlobals:
    svc #0x1359
    ret
    .globl NtMapVisualRelativePoints
    .p2align 4
NtMapVisualRelativePoints:
    svc #0x135A
    ret
    .globl NtMinGetInputTransform
    .p2align 4
NtMinGetInputTransform:
    svc #0x135B
    ret
    .globl NtMinInteropCoreMessagingWithInput
    .p2align 4
NtMinInteropCoreMessagingWithInput:
    svc #0x135C
    ret
    .globl NtMinQPeekForInput
    .p2align 4
NtMinQPeekForInput:
    svc #0x135D
    ret
    .globl NtMinQSuspendInputProcessing
    .p2align 4
NtMinQSuspendInputProcessing:
    svc #0x135E
    ret
    .globl NtMinQUpdateWakeMask
    .p2align 4
NtMinQUpdateWakeMask:
    svc #0x135F
    ret
    .globl NtModerncoreBeginLayoutUpdate
    .p2align 4
NtModerncoreBeginLayoutUpdate:
    svc #0x1360
    ret
    .globl NtModerncoreCreateDCompositionHwndTarget
    .p2align 4
NtModerncoreCreateDCompositionHwndTarget:
    svc #0x1361
    ret
    .globl NtModerncoreCreateGDIHwndTarget
    .p2align 4
NtModerncoreCreateGDIHwndTarget:
    svc #0x1362
    ret
    .globl NtModerncoreDestroyDCompositionHwndTarget
    .p2align 4
NtModerncoreDestroyDCompositionHwndTarget:
    svc #0x1363
    ret
    .globl NtModerncoreDestroyGDIHwndTarget
    .p2align 4
NtModerncoreDestroyGDIHwndTarget:
    svc #0x1364
    ret
    .globl NtModerncoreEnableResizeLayoutSynchronization
    .p2align 4
NtModerncoreEnableResizeLayoutSynchronization:
    svc #0x1365
    ret
    .globl NtModerncoreGetNavigationWindowVisual
    .p2align 4
NtModerncoreGetNavigationWindowVisual:
    svc #0x1366
    ret
    .globl NtModerncoreGetResizeDCompositionSynchronizationObject
    .p2align 4
NtModerncoreGetResizeDCompositionSynchronizationObject:
    svc #0x1367
    ret
    .globl NtModerncoreGetWindowContentVisual
    .p2align 4
NtModerncoreGetWindowContentVisual:
    svc #0x1368
    ret
    .globl NtModerncoreIdleTimerThread
    .p2align 4
NtModerncoreIdleTimerThread:
    svc #0x1369
    ret
    .globl NtModerncoreIsResizeLayoutSynchronizationEnabled
    .p2align 4
NtModerncoreIsResizeLayoutSynchronizationEnabled:
    svc #0x136A
    ret
    .globl NtModerncoreProcessConnect
    .p2align 4
NtModerncoreProcessConnect:
    svc #0x136B
    ret
    .globl NtModerncoreRegisterEnhancedNavigationWindowHandle
    .p2align 4
NtModerncoreRegisterEnhancedNavigationWindowHandle:
    svc #0x136C
    ret
    .globl NtModerncoreRegisterNavigationWindowHandle
    .p2align 4
NtModerncoreRegisterNavigationWindowHandle:
    svc #0x136D
    ret
    .globl NtModerncoreSetNavigationServiceSid
    .p2align 4
NtModerncoreSetNavigationServiceSid:
    svc #0x136E
    ret
    .globl NtModerncoreUnregisterNavigationWindowHandle
    .p2align 4
NtModerncoreUnregisterNavigationWindowHandle:
    svc #0x136F
    ret
    .globl NtNotifyPresentToCompositionSurface
    .p2align 4
NtNotifyPresentToCompositionSurface:
    svc #0x1370
    ret
    .globl NtOpenCompositionSurfaceDirtyRegion
    .p2align 4
NtOpenCompositionSurfaceDirtyRegion:
    svc #0x1371
    ret
    .globl NtOpenCompositionSurfaceRealizationInfo
    .p2align 4
NtOpenCompositionSurfaceRealizationInfo:
    svc #0x1372
    ret
    .globl NtOpenCompositionSurfaceSectionInfo
    .p2align 4
NtOpenCompositionSurfaceSectionInfo:
    svc #0x1373
    ret
    .globl NtQueryCompositionInputIsImplicit
    .p2align 4
NtQueryCompositionInputIsImplicit:
    svc #0x1374
    ret
    .globl NtQueryCompositionInputQueueAndTransform
    .p2align 4
NtQueryCompositionInputQueueAndTransform:
    svc #0x1375
    ret
    .globl NtQueryCompositionInputSink
    .p2align 4
NtQueryCompositionInputSink:
    svc #0x1376
    ret
    .globl NtQueryCompositionInputSinkLuid
    .p2align 4
NtQueryCompositionInputSinkLuid:
    svc #0x1377
    ret
    .globl NtQueryCompositionInputSinkViewId
    .p2align 4
NtQueryCompositionInputSinkViewId:
    svc #0x1378
    ret
    .globl NtQueryCompositionSurfaceBinding
    .p2align 4
NtQueryCompositionSurfaceBinding:
    svc #0x1379
    ret
    .globl NtQueryCompositionSurfaceFrameRate
    .p2align 4
NtQueryCompositionSurfaceFrameRate:
    svc #0x137A
    ret
    .globl NtQueryCompositionSurfaceHDRMetaData
    .p2align 4
NtQueryCompositionSurfaceHDRMetaData:
    svc #0x137B
    ret
    .globl NtQueryCompositionSurfaceRenderingRealization
    .p2align 4
NtQueryCompositionSurfaceRenderingRealization:
    svc #0x137C
    ret
    .globl NtQueryCompositionSurfaceStatistics
    .p2align 4
NtQueryCompositionSurfaceStatistics:
    svc #0x137D
    ret
    .globl NtRIMAddInputObserver
    .p2align 4
NtRIMAddInputObserver:
    svc #0x137E
    ret
    .globl NtRIMAreSiblingDevices
    .p2align 4
NtRIMAreSiblingDevices:
    svc #0x137F
    ret
    .globl NtRIMDeviceIoControl
    .p2align 4
NtRIMDeviceIoControl:
    svc #0x1380
    ret
    .globl NtRIMEnableMonitorMappingForDevice
    .p2align 4
NtRIMEnableMonitorMappingForDevice:
    svc #0x1381
    ret
    .globl NtRIMFreeInputBuffer
    .p2align 4
NtRIMFreeInputBuffer:
    svc #0x1382
    ret
    .globl NtRIMGetDevicePreparsedData
    .p2align 4
NtRIMGetDevicePreparsedData:
    svc #0x1383
    ret
    .globl NtRIMGetDevicePreparsedDataLockfree
    .p2align 4
NtRIMGetDevicePreparsedDataLockfree:
    svc #0x1384
    ret
    .globl NtRIMGetDeviceProperties
    .p2align 4
NtRIMGetDeviceProperties:
    svc #0x1385
    ret
    .globl NtRIMGetDevicePropertiesLockfree
    .p2align 4
NtRIMGetDevicePropertiesLockfree:
    svc #0x1386
    ret
    .globl NtRIMGetPhysicalDeviceRect
    .p2align 4
NtRIMGetPhysicalDeviceRect:
    svc #0x1387
    ret
    .globl NtRIMGetSourceProcessId
    .p2align 4
NtRIMGetSourceProcessId:
    svc #0x1388
    ret
    .globl NtRIMObserveNextInput
    .p2align 4
NtRIMObserveNextInput:
    svc #0x1389
    ret
    .globl NtRIMOnAsyncPnpWorkNotification
    .p2align 4
NtRIMOnAsyncPnpWorkNotification:
    svc #0x138A
    ret
    .globl NtRIMOnPnpNotification
    .p2align 4
NtRIMOnPnpNotification:
    svc #0x138B
    ret
    .globl NtRIMOnTimerNotification
    .p2align 4
NtRIMOnTimerNotification:
    svc #0x138C
    ret
    .globl NtRIMQueryDevicePath
    .p2align 4
NtRIMQueryDevicePath:
    svc #0x138D
    ret
    .globl NtRIMReadInput
    .p2align 4
NtRIMReadInput:
    svc #0x138E
    ret
    .globl NtRIMRegisterForInputEx
    .p2align 4
NtRIMRegisterForInputEx:
    svc #0x138F
    ret
    .globl NtRIMRemoveInputObserver
    .p2align 4
NtRIMRemoveInputObserver:
    svc #0x1390
    ret
    .globl NtRIMSetDeadzoneRotation
    .p2align 4
NtRIMSetDeadzoneRotation:
    svc #0x1391
    ret
    .globl NtRIMSetExtendedDeviceProperty
    .p2align 4
NtRIMSetExtendedDeviceProperty:
    svc #0x1392
    ret
    .globl NtRIMSetTestModeStatus
    .p2align 4
NtRIMSetTestModeStatus:
    svc #0x1393
    ret
    .globl NtRIMUnregisterForInput
    .p2align 4
NtRIMUnregisterForInput:
    svc #0x1394
    ret
    .globl NtRIMUpdateInputObserverRegistration
    .p2align 4
NtRIMUpdateInputObserverRegistration:
    svc #0x1395
    ret
    .globl NtSetCompositionSurfaceAnalogExclusive
    .p2align 4
NtSetCompositionSurfaceAnalogExclusive:
    svc #0x1396
    ret
    .globl NtSetCompositionSurfaceBufferUsage
    .p2align 4
NtSetCompositionSurfaceBufferUsage:
    svc #0x1397
    ret
    .globl NtSetCompositionSurfaceDirectFlipState
    .p2align 4
NtSetCompositionSurfaceDirectFlipState:
    svc #0x1398
    ret
    .globl NtSetCompositionSurfaceIndependentFlipInfo
    .p2align 4
NtSetCompositionSurfaceIndependentFlipInfo:
    svc #0x1399
    ret
    .globl NtSetCompositionSurfaceStatistics
    .p2align 4
NtSetCompositionSurfaceStatistics:
    svc #0x139A
    ret
    .globl NtSetCursorInputSpace
    .p2align 4
NtSetCursorInputSpace:
    svc #0x139B
    ret
    .globl NtSetPointerDeviceInputSpace
    .p2align 4
NtSetPointerDeviceInputSpace:
    svc #0x139C
    ret
    .globl NtSetShellCursorState
    .p2align 4
NtSetShellCursorState:
    svc #0x139D
    ret
    .globl NtTokenManagerConfirmOutstandingAnalogToken
    .p2align 4
NtTokenManagerConfirmOutstandingAnalogToken:
    svc #0x139E
    ret
    .globl NtTokenManagerCreateCompositionTokenHandle
    .p2align 4
NtTokenManagerCreateCompositionTokenHandle:
    svc #0x139F
    ret
    .globl NtTokenManagerCreateFlipObjectReturnTokenHandle
    .p2align 4
NtTokenManagerCreateFlipObjectReturnTokenHandle:
    svc #0x13A0
    ret
    .globl NtTokenManagerCreateFlipObjectTokenHandle
    .p2align 4
NtTokenManagerCreateFlipObjectTokenHandle:
    svc #0x13A1
    ret
    .globl NtTokenManagerGetAnalogExclusiveSurfaceUpdates
    .p2align 4
NtTokenManagerGetAnalogExclusiveSurfaceUpdates:
    svc #0x13A2
    ret
    .globl NtTokenManagerGetAnalogExclusiveTokenEvent
    .p2align 4
NtTokenManagerGetAnalogExclusiveTokenEvent:
    svc #0x13A3
    ret
    .globl NtTokenManagerOpenSectionAndEvents
    .p2align 4
NtTokenManagerOpenSectionAndEvents:
    svc #0x13A4
    ret
    .globl NtTokenManagerThread
    .p2align 4
NtTokenManagerThread:
    svc #0x13A5
    ret
    .globl NtUnBindCompositionSurface
    .p2align 4
NtUnBindCompositionSurface:
    svc #0x13A6
    ret
    .globl NtUpdateInputSinkTransforms
    .p2align 4
NtUpdateInputSinkTransforms:
    svc #0x13A7
    ret
    .globl NtUserAcquireIAMKey
    .p2align 4
NtUserAcquireIAMKey:
    svc #0x13A8
    ret
    .globl NtUserAcquireInteractiveControlBackgroundAccess
    .p2align 4
NtUserAcquireInteractiveControlBackgroundAccess:
    svc #0x13A9
    ret
    .globl NtUserAddClipboardFormatListener
    .p2align 4
NtUserAddClipboardFormatListener:
    svc #0x13AA
    ret
    .globl NtUserAddVisualIdentifier
    .p2align 4
NtUserAddVisualIdentifier:
    svc #0x13AB
    ret
    .globl NtUserAllowSetForegroundWindow
    .p2align 4
NtUserAllowSetForegroundWindow:
    svc #0x13AC
    ret
    .globl NtUserApplyWindowAction
    .p2align 4
NtUserApplyWindowAction:
    svc #0x13AD
    ret
    .globl NtUserArrangeIconicWindows
    .p2align 4
NtUserArrangeIconicWindows:
    svc #0x13AE
    ret
    .globl NtUserAssociateInputContext
    .p2align 4
NtUserAssociateInputContext:
    svc #0x13AF
    ret
    .globl NtUserAutoPromoteMouseInPointer
    .p2align 4
NtUserAutoPromoteMouseInPointer:
    svc #0x13B0
    ret
    .globl NtUserAutoRotateScreen
    .p2align 4
NtUserAutoRotateScreen:
    svc #0x13B1
    ret
    .globl NtUserBeginDeferWindowPos
    .p2align 4
NtUserBeginDeferWindowPos:
    svc #0x13B2
    ret
    .globl NtUserBeginLayoutUpdate
    .p2align 4
NtUserBeginLayoutUpdate:
    svc #0x13B3
    ret
    .globl NtUserBlockInput
    .p2align 4
NtUserBlockInput:
    svc #0x13B4
    ret
    .globl NtUserBroadcastImeShowStatusChange
    .p2align 4
NtUserBroadcastImeShowStatusChange:
    svc #0x13B5
    ret
    .globl NtUserBroadcastThemeChangeEvent
    .p2align 4
NtUserBroadcastThemeChangeEvent:
    svc #0x13B6
    ret
    .globl NtUserBuildHimcList
    .p2align 4
NtUserBuildHimcList:
    svc #0x13B7
    ret
    .globl NtUserBuildPropList
    .p2align 4
NtUserBuildPropList:
    svc #0x13B8
    ret
    .globl NtUserCalculatePopupWindowPosition
    .p2align 4
NtUserCalculatePopupWindowPosition:
    svc #0x13B9
    ret
    .globl NtUserCanCurrentThreadChangeForeground
    .p2align 4
NtUserCanCurrentThreadChangeForeground:
    svc #0x13BA
    ret
    .globl NtUserCancelQueueEventCompletionPacket
    .p2align 4
NtUserCancelQueueEventCompletionPacket:
    svc #0x13BB
    ret
    .globl NtUserChangeDisplaySettings
    .p2align 4
NtUserChangeDisplaySettings:
    svc #0x13BC
    ret
    .globl NtUserChangeWindowMessageFilter
    .p2align 4
NtUserChangeWindowMessageFilter:
    svc #0x13BD
    ret
    .globl NtUserChangeWindowMessageFilterEx
    .p2align 4
NtUserChangeWindowMessageFilterEx:
    svc #0x13BE
    ret
    .globl NtUserCheckImeShowStatusInThread
    .p2align 4
NtUserCheckImeShowStatusInThread:
    svc #0x13BF
    ret
    .globl NtUserCheckProcessForClipboardAccess
    .p2align 4
NtUserCheckProcessForClipboardAccess:
    svc #0x13C0
    ret
    .globl NtUserCheckProcessSession
    .p2align 4
NtUserCheckProcessSession:
    svc #0x13C1
    ret
    .globl NtUserCheckWindowThreadDesktop
    .p2align 4
NtUserCheckWindowThreadDesktop:
    svc #0x13C2
    ret
    .globl NtUserChildWindowFromPointEx
    .p2align 4
NtUserChildWindowFromPointEx:
    svc #0x13C3
    ret
    .globl NtUserCitSetInfo
    .p2align 4
NtUserCitSetInfo:
    svc #0x13C4
    ret
    .globl NtUserClearForeground
    .p2align 4
NtUserClearForeground:
    svc #0x13C5
    ret
    .globl NtUserClearWakeMask
    .p2align 4
NtUserClearWakeMask:
    svc #0x13C6
    ret
    .globl NtUserClearWindowState
    .p2align 4
NtUserClearWindowState:
    svc #0x13C7
    ret
    .globl NtUserClipCursor
    .p2align 4
NtUserClipCursor:
    svc #0x13C8
    ret
    .globl NtUserCompositionInputSinkLuidFromPoint
    .p2align 4
NtUserCompositionInputSinkLuidFromPoint:
    svc #0x13C9
    ret
    .globl NtUserCompositionInputSinkViewInstanceIdFromPoint
    .p2align 4
NtUserCompositionInputSinkViewInstanceIdFromPoint:
    svc #0x13CA
    ret
    .globl NtUserConfigureActivationObject
    .p2align 4
NtUserConfigureActivationObject:
    svc #0x13CB
    ret
    .globl NtUserConfirmResizeCommit
    .p2align 4
NtUserConfirmResizeCommit:
    svc #0x13CC
    ret
    .globl NtUserConvertPrimaryPointerToMouseDrag
    .p2align 4
NtUserConvertPrimaryPointerToMouseDrag:
    svc #0x13CD
    ret
    .globl NtUserConvertToInterceptWindow
    .p2align 4
NtUserConvertToInterceptWindow:
    svc #0x13CE
    ret
    .globl NtUserCreateActivationObject
    .p2align 4
NtUserCreateActivationObject:
    svc #0x13CF
    ret
    .globl NtUserCreateBaseWindow
    .p2align 4
NtUserCreateBaseWindow:
    svc #0x13D0
    ret
    .globl NtUserCreateDCompositionHwndTarget
    .p2align 4
NtUserCreateDCompositionHwndTarget:
    svc #0x13D1
    ret
    .globl NtUserCreateDesktopEx
    .p2align 4
NtUserCreateDesktopEx:
    svc #0x13D2
    ret
    .globl NtUserCreateEmptyCursorObject
    .p2align 4
NtUserCreateEmptyCursorObject:
    svc #0x13D3
    ret
    .globl NtUserCreateInputContext
    .p2align 4
NtUserCreateInputContext:
    svc #0x13D4
    ret
    .globl NtUserCreateLayoutSyncForHwnd
    .p2align 4
NtUserCreateLayoutSyncForHwnd:
    svc #0x13D5
    ret
    .globl NtUserCreateMenu
    .p2align 4
NtUserCreateMenu:
    svc #0x13D6
    ret
    .globl NtUserCreatePalmRejectionDelayZone
    .p2align 4
NtUserCreatePalmRejectionDelayZone:
    svc #0x13D7
    ret
    .globl NtUserCreatePopupMenu
    .p2align 4
NtUserCreatePopupMenu:
    svc #0x13D8
    ret
    .globl NtUserCreateSyntheticPointerDevice2
    .p2align 4
NtUserCreateSyntheticPointerDevice2:
    svc #0x13D9
    ret
    .globl NtUserCreateSystemThreads
    .p2align 4
NtUserCreateSystemThreads:
    svc #0x13DA
    ret
    .globl NtUserCreateWindowStation
    .p2align 4
NtUserCreateWindowStation:
    svc #0x13DB
    ret
    .globl NtUserCreateWindowStationEx
    .p2align 4
NtUserCreateWindowStationEx:
    svc #0x13DC
    ret
    .globl NtUserCsDdeUninitialize
    .p2align 4
NtUserCsDdeUninitialize:
    svc #0x13DD
    ret
    .globl NtUserCtxDisplayIOCtl
    .p2align 4
NtUserCtxDisplayIOCtl:
    svc #0x13DE
    ret
    .globl NtUserDWP_GetEnabledPopupOffset
    .p2align 4
NtUserDWP_GetEnabledPopupOffset:
    svc #0x13DF
    ret
    .globl NtUserDeferWindowDpiChanges
    .p2align 4
NtUserDeferWindowDpiChanges:
    svc #0x13E0
    ret
    .globl NtUserDeferWindowPosAndBand
    .p2align 4
NtUserDeferWindowPosAndBand:
    svc #0x13E1
    ret
    .globl NtUserDeferredDesktopRotation
    .p2align 4
NtUserDeferredDesktopRotation:
    svc #0x13E2
    ret
    .globl NtUserDelegateCapturePointers
    .p2align 4
NtUserDelegateCapturePointers:
    svc #0x13E3
    ret
    .globl NtUserDelegateInput
    .p2align 4
NtUserDelegateInput:
    svc #0x13E4
    ret
    .globl NtUserDeregisterShellHookWindow
    .p2align 4
NtUserDeregisterShellHookWindow:
    svc #0x13E5
    ret
    .globl NtUserDestroyActivationObject
    .p2align 4
NtUserDestroyActivationObject:
    svc #0x13E6
    ret
    .globl NtUserDestroyCaret
    .p2align 4
NtUserDestroyCaret:
    svc #0x13E7
    ret
    .globl NtUserDestroyDCompositionHwndTarget
    .p2align 4
NtUserDestroyDCompositionHwndTarget:
    svc #0x13E8
    ret
    .globl NtUserDestroyInputContext
    .p2align 4
NtUserDestroyInputContext:
    svc #0x13E9
    ret
    .globl NtUserDestroyPalmRejectionDelayZone
    .p2align 4
NtUserDestroyPalmRejectionDelayZone:
    svc #0x13EA
    ret
    .globl NtUserDisableImmersiveOwner
    .p2align 4
NtUserDisableImmersiveOwner:
    svc #0x13EB
    ret
    .globl NtUserDisableProcessWindowFiltering
    .p2align 4
NtUserDisableProcessWindowFiltering:
    svc #0x13EC
    ret
    .globl NtUserDisableProcessWindowsGhosting
    .p2align 4
NtUserDisableProcessWindowsGhosting:
    svc #0x13ED
    ret
    .globl NtUserDisableThreadIme
    .p2align 4
NtUserDisableThreadIme:
    svc #0x13EE
    ret
    .globl NtUserDiscardPointerFrameMessages
    .p2align 4
NtUserDiscardPointerFrameMessages:
    svc #0x13EF
    ret
    .globl NtUserDisplayConfigGetDeviceInfo
    .p2align 4
NtUserDisplayConfigGetDeviceInfo:
    svc #0x13F0
    ret
    .globl NtUserDisplayConfigSetDeviceInfo
    .p2align 4
NtUserDisplayConfigSetDeviceInfo:
    svc #0x13F1
    ret
    .globl NtUserDoInitMessagePumpHook
    .p2align 4
NtUserDoInitMessagePumpHook:
    svc #0x13F2
    ret
    .globl NtUserDoMenuOperation
    .p2align 4
NtUserDoMenuOperation:
    svc #0x13F3
    ret
    .globl NtUserDoSoundConnect
    .p2align 4
NtUserDoSoundConnect:
    svc #0x13F4
    ret
    .globl NtUserDoSoundDisconnect
    .p2align 4
NtUserDoSoundDisconnect:
    svc #0x13F5
    ret
    .globl NtUserDoUninitMessagePumpHook
    .p2align 4
NtUserDoUninitMessagePumpHook:
    svc #0x13F6
    ret
    .globl NtUserDownlevelTouchpad
    .p2align 4
NtUserDownlevelTouchpad:
    svc #0x13F7
    ret
    .globl NtUserDragDetect
    .p2align 4
NtUserDragDetect:
    svc #0x13F8
    ret
    .globl NtUserDragObject
    .p2align 4
NtUserDragObject:
    svc #0x13F9
    ret
    .globl NtUserDrainThreadCoreMessagingCompletions
    .p2align 4
NtUserDrainThreadCoreMessagingCompletions:
    svc #0x13FA
    ret
    .globl NtUserDrainThreadCoreMessagingCompletions2
    .p2align 4
NtUserDrainThreadCoreMessagingCompletions2:
    svc #0x13FB
    ret
    .globl NtUserDrawAnimatedRects
    .p2align 4
NtUserDrawAnimatedRects:
    svc #0x13FC
    ret
    .globl NtUserDrawCaption
    .p2align 4
NtUserDrawCaption:
    svc #0x13FD
    ret
    .globl NtUserDrawCaptionTemp
    .p2align 4
NtUserDrawCaptionTemp:
    svc #0x13FE
    ret
    .globl NtUserDrawMenuBar
    .p2align 4
NtUserDrawMenuBar:
    svc #0x13FF
    ret
    .globl NtUserDrawMenuBarTemp
    .p2align 4
NtUserDrawMenuBarTemp:
    svc #0x1400
    ret
    .globl NtUserDwmGetRemoteSessionOcclusionEvent
    .p2align 4
NtUserDwmGetRemoteSessionOcclusionEvent:
    svc #0x1401
    ret
    .globl NtUserDwmGetRemoteSessionOcclusionState
    .p2align 4
NtUserDwmGetRemoteSessionOcclusionState:
    svc #0x1402
    ret
    .globl NtUserDwmKernelShutdown
    .p2align 4
NtUserDwmKernelShutdown:
    svc #0x1403
    ret
    .globl NtUserDwmKernelStartup
    .p2align 4
NtUserDwmKernelStartup:
    svc #0x1404
    ret
    .globl NtUserDwmLockScreenUpdates
    .p2align 4
NtUserDwmLockScreenUpdates:
    svc #0x1405
    ret
    .globl NtUserDwmValidateWindow
    .p2align 4
NtUserDwmValidateWindow:
    svc #0x1406
    ret
    .globl NtUserDwmWindowNotificationsEnabled
    .p2align 4
NtUserDwmWindowNotificationsEnabled:
    svc #0x1407
    ret
    .globl NtUserEnableChildWindowDpiMessage
    .p2align 4
NtUserEnableChildWindowDpiMessage:
    svc #0x1408
    ret
    .globl NtUserEnableIAMAccess
    .p2align 4
NtUserEnableIAMAccess:
    svc #0x1409
    ret
    .globl NtUserEnableModernAppWindowKeyboardIntercept
    .p2align 4
NtUserEnableModernAppWindowKeyboardIntercept:
    svc #0x140A
    ret
    .globl NtUserEnableMouseInPointer
    .p2align 4
NtUserEnableMouseInPointer:
    svc #0x140B
    ret
    .globl NtUserEnableMouseInPointerForThread
    .p2align 4
NtUserEnableMouseInPointerForThread:
    svc #0x140C
    ret
    .globl NtUserEnableMouseInPointerForWindow
    .p2align 4
NtUserEnableMouseInPointerForWindow:
    svc #0x140D
    ret
    .globl NtUserEnableMouseInputForCursorSuppression
    .p2align 4
NtUserEnableMouseInputForCursorSuppression:
    svc #0x140E
    ret
    .globl NtUserEnableNonClientDpiScaling
    .p2align 4
NtUserEnableNonClientDpiScaling:
    svc #0x140F
    ret
    .globl NtUserEnableResizeLayoutSynchronization
    .p2align 4
NtUserEnableResizeLayoutSynchronization:
    svc #0x1410
    ret
    .globl NtUserEnableSessionForMMCSS
    .p2align 4
NtUserEnableSessionForMMCSS:
    svc #0x1411
    ret
    .globl NtUserEnableShellWindowManagementBehavior
    .p2align 4
NtUserEnableShellWindowManagementBehavior:
    svc #0x1412
    ret
    .globl NtUserEnableSoftwareCursorForScreenCapture
    .p2align 4
NtUserEnableSoftwareCursorForScreenCapture:
    svc #0x1413
    ret
    .globl NtUserEnableSynchronizedLayout
    .p2align 4
NtUserEnableSynchronizedLayout:
    svc #0x1414
    ret
    .globl NtUserEnableTouchPad
    .p2align 4
NtUserEnableTouchPad:
    svc #0x1415
    ret
    .globl NtUserEnableWindow
    .p2align 4
NtUserEnableWindow:
    svc #0x1416
    ret
    .globl NtUserEnableWindowGDIScaledDpiMessage
    .p2align 4
NtUserEnableWindowGDIScaledDpiMessage:
    svc #0x1417
    ret
    .globl NtUserEnableWindowResizeOptimization
    .p2align 4
NtUserEnableWindowResizeOptimization:
    svc #0x1418
    ret
    .globl NtUserEnableWindowShellWindowManagementBehavior
    .p2align 4
NtUserEnableWindowShellWindowManagementBehavior:
    svc #0x1419
    ret
    .globl NtUserSetSensorPresence
    .p2align 4
NtUserSetSensorPresence:
    svc #0x141A
    ret
    .globl NtUserEndMenu
    .p2align 4
NtUserEndMenu:
    svc #0x141B
    ret
    .globl NtUserEnsureDpiDepSysMetCacheForPlateau
    .p2align 4
NtUserEnsureDpiDepSysMetCacheForPlateau:
    svc #0x141C
    ret
    .globl NtUserEnsureDpiMetricsForDpi
    .p2align 4
NtUserEnsureDpiMetricsForDpi:
    svc #0x141D
    ret
    .globl NtUserEnsureDpiServerInfoForDpi
    .p2align 4
NtUserEnsureDpiServerInfoForDpi:
    svc #0x141E
    ret
    .globl NtUserEnsureOemBitmapInfoForDpi
    .p2align 4
NtUserEnsureOemBitmapInfoForDpi:
    svc #0x141F
    ret
    .globl NtUserEnterMoveSizeLoop
    .p2align 4
NtUserEnterMoveSizeLoop:
    svc #0x1420
    ret
    .globl NtUserEnumClipboardFormats
    .p2align 4
NtUserEnumClipboardFormats:
    svc #0x1421
    ret
    .globl NtUserEvent
    .p2align 4
NtUserEvent:
    svc #0x1422
    ret
    .globl NtUserFlashWindowEx
    .p2align 4
NtUserFlashWindowEx:
    svc #0x1423
    ret
    .globl NtUserForceEnableNumpadTranslation
    .p2align 4
NtUserForceEnableNumpadTranslation:
    svc #0x1424
    ret
    .globl NtUserForceWindowToDpiForTest
    .p2align 4
NtUserForceWindowToDpiForTest:
    svc #0x1425
    ret
    .globl NtUserFrostCrashedWindow
    .p2align 4
NtUserFrostCrashedWindow:
    svc #0x1426
    ret
    .globl NtUserFunctionalizeDisplayConfig
    .p2align 4
NtUserFunctionalizeDisplayConfig:
    svc #0x1427
    ret
    .globl NtUserGetActiveProcessesDpis
    .p2align 4
NtUserGetActiveProcessesDpis:
    svc #0x1428
    ret
    .globl NtUserGetAppImeLevel
    .p2align 4
NtUserGetAppImeLevel:
    svc #0x1429
    ret
    .globl NtUserGetAutoRotationState
    .p2align 4
NtUserGetAutoRotationState:
    svc #0x142A
    ret
    .globl NtUserGetCIMSSM
    .p2align 4
NtUserGetCIMSSM:
    svc #0x142B
    ret
    .globl NtUserGetCaretPos
    .p2align 4
NtUserGetCaretPos:
    svc #0x142C
    ret
    .globl NtUserGetClassIcoCur
    .p2align 4
NtUserGetClassIcoCur:
    svc #0x142D
    ret
    .globl NtUserGetClipCursor
    .p2align 4
NtUserGetClipCursor:
    svc #0x142E
    ret
    .globl NtUserGetClipboardAccessToken
    .p2align 4
NtUserGetClipboardAccessToken:
    svc #0x142F
    ret
    .globl NtUserGetClipboardMetadata
    .p2align 4
NtUserGetClipboardMetadata:
    svc #0x1430
    ret
    .globl NtUserGetClipboardViewer
    .p2align 4
NtUserGetClipboardViewer:
    svc #0x1431
    ret
    .globl NtUserGetComboBoxInfo
    .p2align 4
NtUserGetComboBoxInfo:
    svc #0x1432
    ret
    .globl NtUserGetCurrentDpiInfoForWindow
    .p2align 4
NtUserGetCurrentDpiInfoForWindow:
    svc #0x1433
    ret
    .globl NtUserGetCurrentInputMessageSource
    .p2align 4
NtUserGetCurrentInputMessageSource:
    svc #0x1434
    ret
    .globl NtUserGetCursor
    .p2align 4
NtUserGetCursor:
    svc #0x1435
    ret
    .globl NtUserGetCursorInfo
    .p2align 4
NtUserGetCursorInfo:
    svc #0x1436
    ret
    .globl NtUserGetCursorPos
    .p2align 4
NtUserGetCursorPos:
    svc #0x1437
    ret
    .globl NtUserGetDCompositionHwndBitmap
    .p2align 4
NtUserGetDCompositionHwndBitmap:
    svc #0x1438
    ret
    .globl NtUserGetDManipHookInitFunction
    .p2align 4
NtUserGetDManipHookInitFunction:
    svc #0x1439
    ret
    .globl NtUserGetDesktopID
    .p2align 4
NtUserGetDesktopID:
    svc #0x143A
    ret
    .globl NtUserGetDesktopVisualTransform
    .p2align 4
NtUserGetDesktopVisualTransform:
    svc #0x143B
    ret
    .globl NtUserGetDeviceChangeInfo
    .p2align 4
NtUserGetDeviceChangeInfo:
    svc #0x143C
    ret
    .globl NtUserGetDisplayAutoRotationPreferences
    .p2align 4
NtUserGetDisplayAutoRotationPreferences:
    svc #0x143D
    ret
    .globl NtUserGetDisplayAutoRotationPreferencesByProcessId
    .p2align 4
NtUserGetDisplayAutoRotationPreferencesByProcessId:
    svc #0x143E
    ret
    .globl NtUserGetDisplayConfigBufferSizes
    .p2align 4
NtUserGetDisplayConfigBufferSizes:
    svc #0x143F
    ret
    .globl NtUserGetDpiForCurrentProcess
    .p2align 4
NtUserGetDpiForCurrentProcess:
    svc #0x1440
    ret
    .globl NtUserGetDpiForMonitor
    .p2align 4
NtUserGetDpiForMonitor:
    svc #0x1441
    ret
    .globl NtUserGetDwmCursorShape
    .p2align 4
NtUserGetDwmCursorShape:
    svc #0x1442
    ret
    .globl NtUserGetExtendedPointerDeviceProperty
    .p2align 4
NtUserGetExtendedPointerDeviceProperty:
    svc #0x1443
    ret
    .globl NtUserGetGestureConfig
    .p2align 4
NtUserGetGestureConfig:
    svc #0x1444
    ret
    .globl NtUserGetGestureExtArgs
    .p2align 4
NtUserGetGestureExtArgs:
    svc #0x1445
    ret
    .globl NtUserGetGestureInfo
    .p2align 4
NtUserGetGestureInfo:
    svc #0x1446
    ret
    .globl NtUserGetGuiResources
    .p2align 4
NtUserGetGuiResources:
    svc #0x1447
    ret
    .globl NtUserGetHDevName
    .p2align 4
NtUserGetHDevName:
    svc #0x1448
    ret
    .globl NtUserGetHimetricScaleFactorFromPixelLocation
    .p2align 4
NtUserGetHimetricScaleFactorFromPixelLocation:
    svc #0x1449
    ret
    .globl NtUserGetIMEShowStatus
    .p2align 4
NtUserGetIMEShowStatus:
    svc #0x144A
    ret
    .globl NtUserGetImeHotKey
    .p2align 4
NtUserGetImeHotKey:
    svc #0x144B
    ret
    .globl NtUserGetImeInfoEx
    .p2align 4
NtUserGetImeInfoEx:
    svc #0x144C
    ret
    .globl NtUserGetInputContainerId
    .p2align 4
NtUserGetInputContainerId:
    svc #0x144D
    ret
    .globl NtUserGetInputDesktop
    .p2align 4
NtUserGetInputDesktop:
    svc #0x144E
    ret
    .globl NtUserGetInputEvent
    .p2align 4
NtUserGetInputEvent:
    svc #0x144F
    ret
    .globl NtUserGetInputLocaleInfo
    .p2align 4
NtUserGetInputLocaleInfo:
    svc #0x1450
    ret
    .globl NtUserGetInteractiveControlDeviceInfo
    .p2align 4
NtUserGetInteractiveControlDeviceInfo:
    svc #0x1451
    ret
    .globl NtUserGetInteractiveControlInfo
    .p2align 4
NtUserGetInteractiveControlInfo:
    svc #0x1452
    ret
    .globl NtUserGetInteractiveCtrlSupportedWaveforms
    .p2align 4
NtUserGetInteractiveCtrlSupportedWaveforms:
    svc #0x1453
    ret
    .globl NtUserGetKeyNameText
    .p2align 4
NtUserGetKeyNameText:
    svc #0x1454
    ret
    .globl NtUserGetKeyboardInputThreadId
    .p2align 4
NtUserGetKeyboardInputThreadId:
    svc #0x1455
    ret
    .globl NtUserGetKeyboardLayout
    .p2align 4
NtUserGetKeyboardLayout:
    svc #0x1456
    ret
    .globl NtUserGetKeyboardLayoutName
    .p2align 4
NtUserGetKeyboardLayoutName:
    svc #0x1457
    ret
    .globl NtUserGetKeyboardType
    .p2align 4
NtUserGetKeyboardType:
    svc #0x1458
    ret
    .globl NtUserGetLatestInputMessageData
    .p2align 4
NtUserGetLatestInputMessageData:
    svc #0x1459
    ret
    .globl NtUserGetLayeredWindowAttributes
    .p2align 4
NtUserGetLayeredWindowAttributes:
    svc #0x145A
    ret
    .globl NtUserGetListBoxInfo
    .p2align 4
NtUserGetListBoxInfo:
    svc #0x145B
    ret
    .globl NtUserGetMenuIndex
    .p2align 4
NtUserGetMenuIndex:
    svc #0x145C
    ret
    .globl NtUserGetMenuItemRect
    .p2align 4
NtUserGetMenuItemRect:
    svc #0x145D
    ret
    .globl NtUserGetMessagePos
    .p2align 4
NtUserGetMessagePos:
    svc #0x145E
    ret
    .globl NtUserGetMinuserIdForBaseWindow
    .p2align 4
NtUserGetMinuserIdForBaseWindow:
    svc #0x145F
    ret
    .globl NtUserGetModernAppWindow
    .p2align 4
NtUserGetModernAppWindow:
    svc #0x1460
    ret
    .globl NtUserGetMouseMovePointsEx
    .p2align 4
NtUserGetMouseMovePointsEx:
    svc #0x1461
    ret
    .globl NtUserGetOemBitmapSize
    .p2align 4
NtUserGetOemBitmapSize:
    svc #0x1462
    ret
    .globl NtUserGetPhysicalDeviceRect
    .p2align 4
NtUserGetPhysicalDeviceRect:
    svc #0x1463
    ret
    .globl NtUserGetPointerCursorId
    .p2align 4
NtUserGetPointerCursorId:
    svc #0x1464
    ret
    .globl NtUserGetPointerDevice
    .p2align 4
NtUserGetPointerDevice:
    svc #0x1465
    ret
    .globl NtUserGetPointerDeviceCursors
    .p2align 4
NtUserGetPointerDeviceCursors:
    svc #0x1466
    ret
    .globl NtUserGetPointerDeviceInputSpace
    .p2align 4
NtUserGetPointerDeviceInputSpace:
    svc #0x1467
    ret
    .globl NtUserGetPointerDeviceOrientation
    .p2align 4
NtUserGetPointerDeviceOrientation:
    svc #0x1468
    ret
    .globl NtUserGetPointerDeviceProperties
    .p2align 4
NtUserGetPointerDeviceProperties:
    svc #0x1469
    ret
    .globl NtUserGetPointerDeviceRects
    .p2align 4
NtUserGetPointerDeviceRects:
    svc #0x146A
    ret
    .globl NtUserGetPointerDevices
    .p2align 4
NtUserGetPointerDevices:
    svc #0x146B
    ret
    .globl NtUserGetPointerFrameTimes
    .p2align 4
NtUserGetPointerFrameTimes:
    svc #0x146C
    ret
    .globl NtUserGetPointerIdForPromotion
    .p2align 4
NtUserGetPointerIdForPromotion:
    svc #0x146D
    ret
    .globl NtUserGetPointerInfoList
    .p2align 4
NtUserGetPointerInfoList:
    svc #0x146E
    ret
    .globl NtUserGetPointerInputTransform
    .p2align 4
NtUserGetPointerInputTransform:
    svc #0x146F
    ret
    .globl NtUserGetPointerProprietaryId
    .p2align 4
NtUserGetPointerProprietaryId:
    svc #0x1470
    ret
    .globl NtUserGetPointerType
    .p2align 4
NtUserGetPointerType:
    svc #0x1471
    ret
    .globl NtUserGetPriorityClipboardFormat
    .p2align 4
NtUserGetPriorityClipboardFormat:
    svc #0x1472
    ret
    .globl NtUserGetProcessDefaultLayout
    .p2align 4
NtUserGetProcessDefaultLayout:
    svc #0x1473
    ret
    .globl NtUserGetProcessDpiAwarenessContext
    .p2align 4
NtUserGetProcessDpiAwarenessContext:
    svc #0x1474
    ret
    .globl NtUserGetProcessUIContextInformation
    .p2align 4
NtUserGetProcessUIContextInformation:
    svc #0x1475
    ret
    .globl NtUserGetProp2
    .p2align 4
NtUserGetProp2:
    svc #0x1476
    ret
    .globl NtUserGetQueueIocp
    .p2align 4
NtUserGetQueueIocp:
    svc #0x1477
    ret
    .globl NtUserGetQueueStatus
    .p2align 4
NtUserGetQueueStatus:
    svc #0x1478
    ret
    .globl NtUserGetQueueStatusReadonly
    .p2align 4
NtUserGetQueueStatusReadonly:
    svc #0x1479
    ret
    .globl NtUserGetRawInputBuffer
    .p2align 4
NtUserGetRawInputBuffer:
    svc #0x147A
    ret
    .globl NtUserGetRawInputData
    .p2align 4
NtUserGetRawInputData:
    svc #0x147B
    ret
    .globl NtUserGetRawInputDeviceInfo
    .p2align 4
NtUserGetRawInputDeviceInfo:
    svc #0x147C
    ret
    .globl NtUserGetRawInputDeviceList
    .p2align 4
NtUserGetRawInputDeviceList:
    svc #0x147D
    ret
    .globl NtUserGetRawPointerDeviceData
    .p2align 4
NtUserGetRawPointerDeviceData:
    svc #0x147E
    ret
    .globl NtUserGetRegisteredRawInputDevices
    .p2align 4
NtUserGetRegisteredRawInputDevices:
    svc #0x147F
    ret
    .globl NtUserGetRequiredCursorSizes
    .p2align 4
NtUserGetRequiredCursorSizes:
    svc #0x1480
    ret
    .globl NtUserGetResizeDCompositionSynchronizationObject
    .p2align 4
NtUserGetResizeDCompositionSynchronizationObject:
    svc #0x1481
    ret
    .globl NtUserGetSendMessageReceiver
    .p2align 4
NtUserGetSendMessageReceiver:
    svc #0x1482
    ret
    .globl NtUserGetSharedWindowData
    .p2align 4
NtUserGetSharedWindowData:
    svc #0x1483
    ret
    .globl NtUserGetSysMenuOffset
    .p2align 4
NtUserGetSysMenuOffset:
    svc #0x1484
    ret
    .globl NtUserGetSystemContentRects
    .p2align 4
NtUserGetSystemContentRects:
    svc #0x1485
    ret
    .globl NtUserGetSystemDpiForProcess
    .p2align 4
NtUserGetSystemDpiForProcess:
    svc #0x1486
    ret
    .globl NtUserGetTopLevelWindow
    .p2align 4
NtUserGetTopLevelWindow:
    svc #0x1487
    ret
    .globl NtUserGetTouchInputInfo
    .p2align 4
NtUserGetTouchInputInfo:
    svc #0x1488
    ret
    .globl NtUserGetTouchValidationStatus
    .p2align 4
NtUserGetTouchValidationStatus:
    svc #0x1489
    ret
    .globl NtUserGetUniformSpaceMapping
    .p2align 4
NtUserGetUniformSpaceMapping:
    svc #0x148A
    ret
    .globl NtUserGetUnpredictedMessagePos
    .p2align 4
NtUserGetUnpredictedMessagePos:
    svc #0x148B
    ret
    .globl NtUserGetUpdatedClipboardFormats
    .p2align 4
NtUserGetUpdatedClipboardFormats:
    svc #0x148C
    ret
    .globl NtUserGetWOWClass
    .p2align 4
NtUserGetWOWClass:
    svc #0x148D
    ret
    .globl NtUserGetWinStationInfo
    .p2align 4
NtUserGetWinStationInfo:
    svc #0x148E
    ret
    .globl NtUserGetWindowBand
    .p2align 4
NtUserGetWindowBand:
    svc #0x148F
    ret
    .globl NtUserGetWindowCompositionAttribute
    .p2align 4
NtUserGetWindowCompositionAttribute:
    svc #0x1490
    ret
    .globl NtUserGetWindowCompositionInfo
    .p2align 4
NtUserGetWindowCompositionInfo:
    svc #0x1491
    ret
    .globl NtUserGetWindowContextHelpId
    .p2align 4
NtUserGetWindowContextHelpId:
    svc #0x1492
    ret
    .globl NtUserGetWindowDisplayAffinity
    .p2align 4
NtUserGetWindowDisplayAffinity:
    svc #0x1493
    ret
    .globl NtUserGetWindowFeedbackSetting
    .p2align 4
NtUserGetWindowFeedbackSetting:
    svc #0x1494
    ret
    .globl NtUserGetWindowMinimizeRect
    .p2align 4
NtUserGetWindowMinimizeRect:
    svc #0x1495
    ret
    .globl NtUserGetWindowProcessHandle
    .p2align 4
NtUserGetWindowProcessHandle:
    svc #0x1496
    ret
    .globl NtUserGetWindowRgnEx
    .p2align 4
NtUserGetWindowRgnEx:
    svc #0x1497
    ret
    .globl NtUserGetWindowThreadProcessId
    .p2align 4
NtUserGetWindowThreadProcessId:
    svc #0x1498
    ret
    .globl NtUserGetWindowTrackInfoAsync
    .p2align 4
NtUserGetWindowTrackInfoAsync:
    svc #0x1499
    ret
    .globl NtUserGhostWindowFromHungWindow
    .p2align 4
NtUserGhostWindowFromHungWindow:
    svc #0x149A
    ret
    .globl NtUserHandleDelegatedInput
    .p2align 4
NtUserHandleDelegatedInput:
    svc #0x149B
    ret
    .globl NtUserHandleSystemThreadCreationFailure
    .p2align 4
NtUserHandleSystemThreadCreationFailure:
    svc #0x149C
    ret
    .globl NtUserHardErrorControl
    .p2align 4
NtUserHardErrorControl:
    svc #0x149D
    ret
    .globl NtUserHideCursorNoCapture
    .p2align 4
NtUserHideCursorNoCapture:
    svc #0x149E
    ret
    .globl NtUserHidePointerContactVisualization
    .p2align 4
NtUserHidePointerContactVisualization:
    svc #0x149F
    ret
    .globl NtUserHiliteMenuItem
    .p2align 4
NtUserHiliteMenuItem:
    svc #0x14A0
    ret
    .globl NtUserHungWindowFromGhostWindow
    .p2align 4
NtUserHungWindowFromGhostWindow:
    svc #0x14A1
    ret
    .globl NtUserHwndQueryRedirectionInfo
    .p2align 4
NtUserHwndQueryRedirectionInfo:
    svc #0x14A2
    ret
    .globl NtUserHwndSetRedirectionInfo
    .p2align 4
NtUserHwndSetRedirectionInfo:
    svc #0x14A3
    ret
    .globl NtUserImpersonateDdeClientWindow
    .p2align 4
NtUserImpersonateDdeClientWindow:
    svc #0x14A4
    ret
    .globl NtUserInheritWindowMonitor
    .p2align 4
NtUserInheritWindowMonitor:
    svc #0x14A5
    ret
    .globl NtUserInitAnsiOem
    .p2align 4
NtUserInitAnsiOem:
    svc #0x14A6
    ret
    .globl NtUserInitThreadCoreMessagingIocp
    .p2align 4
NtUserInitThreadCoreMessagingIocp:
    svc #0x14A7
    ret
    .globl NtUserInitThreadCoreMessagingIocp2
    .p2align 4
NtUserInitThreadCoreMessagingIocp2:
    svc #0x14A8
    ret
    .globl NtUserInitialize
    .p2align 4
NtUserInitialize:
    svc #0x14A9
    ret
    .globl NtUserInitializeClientPfnArrays
    .p2align 4
NtUserInitializeClientPfnArrays:
    svc #0x14AA
    ret
    .globl NtUserInitializeGenericHidInjection
    .p2align 4
NtUserInitializeGenericHidInjection:
    svc #0x14AB
    ret
    .globl NtUserInitializeInputDeviceInjection
    .p2align 4
NtUserInitializeInputDeviceInjection:
    svc #0x14AC
    ret
    .globl NtUserInitializeTouchInjection
    .p2align 4
NtUserInitializeTouchInjection:
    svc #0x14AD
    ret
    .globl NtUserInitializeUserModeMenus
    .p2align 4
NtUserInitializeUserModeMenus:
    svc #0x14AE
    ret
    .globl NtUserInjectDeviceInput
    .p2align 4
NtUserInjectDeviceInput:
    svc #0x14AF
    ret
    .globl NtUserInjectGenericHidInput
    .p2align 4
NtUserInjectGenericHidInput:
    svc #0x14B0
    ret
    .globl NtUserInjectGesture
    .p2align 4
NtUserInjectGesture:
    svc #0x14B1
    ret
    .globl NtUserInjectKeyboardInput
    .p2align 4
NtUserInjectKeyboardInput:
    svc #0x14B2
    ret
    .globl NtUserInjectMouseInput
    .p2align 4
NtUserInjectMouseInput:
    svc #0x14B3
    ret
    .globl NtUserInjectPointerInput
    .p2align 4
NtUserInjectPointerInput:
    svc #0x14B4
    ret
    .globl NtUserInjectTouchInput
    .p2align 4
NtUserInjectTouchInput:
    svc #0x14B5
    ret
    .globl NtUserInjectTouchpadAction
    .p2align 4
NtUserInjectTouchpadAction:
    svc #0x14B6
    ret
    .globl NtUserInteractiveControlQueryUsage
    .p2align 4
NtUserInteractiveControlQueryUsage:
    svc #0x14B7
    ret
    .globl NtUserInternalGetWindowIcon
    .p2align 4
NtUserInternalGetWindowIcon:
    svc #0x14B8
    ret
    .globl NtUserInternalToUnicode
    .p2align 4
NtUserInternalToUnicode:
    svc #0x14B9
    ret
    .globl NtUserIsChildWindowDpiMessageEnabled
    .p2align 4
NtUserIsChildWindowDpiMessageEnabled:
    svc #0x14BA
    ret
    .globl NtUserIsInterceptWindow
    .p2align 4
NtUserIsInterceptWindow:
    svc #0x14BB
    ret
    .globl NtUserIsMouseInPointerEnabled
    .p2align 4
NtUserIsMouseInPointerEnabled:
    svc #0x14BC
    ret
    .globl NtUserIsMouseInputEnabled
    .p2align 4
NtUserIsMouseInputEnabled:
    svc #0x14BD
    ret
    .globl NtUserIsNonClientDpiScalingEnabled
    .p2align 4
NtUserIsNonClientDpiScalingEnabled:
    svc #0x14BE
    ret
    .globl NtUserIsResizeLayoutSynchronizationEnabled
    .p2align 4
NtUserIsResizeLayoutSynchronizationEnabled:
    svc #0x14BF
    ret
    .globl NtUserIsTopLevelWindow
    .p2align 4
NtUserIsTopLevelWindow:
    svc #0x14C0
    ret
    .globl NtUserIsTouchWindow
    .p2align 4
NtUserIsTouchWindow:
    svc #0x14C1
    ret
    .globl NtUserIsUtilitySession
    .p2align 4
NtUserIsUtilitySession:
    svc #0x14C2
    ret
    .globl NtUserIsWindowBroadcastingDpiToChildren
    .p2align 4
NtUserIsWindowBroadcastingDpiToChildren:
    svc #0x14C3
    ret
    .globl NtUserIsWindowDisplayChangeSuppressed
    .p2align 4
NtUserIsWindowDisplayChangeSuppressed:
    svc #0x14C4
    ret
    .globl NtUserIsWindowGDIScaledDpiMessageEnabled
    .p2align 4
NtUserIsWindowGDIScaledDpiMessageEnabled:
    svc #0x14C5
    ret
    .globl NtUserKillSystemTimer
    .p2align 4
NtUserKillSystemTimer:
    svc #0x14C6
    ret
    .globl NtUserLW_LoadFonts
    .p2align 4
NtUserLW_LoadFonts:
    svc #0x14C7
    ret
    .globl NtUserLayoutCompleted
    .p2align 4
NtUserLayoutCompleted:
    svc #0x14C8
    ret
    .globl NtUserLinkDpiCursor
    .p2align 4
NtUserLinkDpiCursor:
    svc #0x14C9
    ret
    .globl NtUserLoadCursorsAndIcons
    .p2align 4
NtUserLoadCursorsAndIcons:
    svc #0x14CA
    ret
    .globl NtUserLoadKeyboardLayoutEx
    .p2align 4
NtUserLoadKeyboardLayoutEx:
    svc #0x14CB
    ret
    .globl NtUserLoadUserApiHook
    .p2align 4
NtUserLoadUserApiHook:
    svc #0x14CC
    ret
    .globl NtUserLockCursor
    .p2align 4
NtUserLockCursor:
    svc #0x14CD
    ret
    .globl NtUserLockSetForegroundWindow
    .p2align 4
NtUserLockSetForegroundWindow:
    svc #0x14CE
    ret
    .globl NtUserLockWindowStation
    .p2align 4
NtUserLockWindowStation:
    svc #0x14CF
    ret
    .globl NtUserLockWorkStation
    .p2align 4
NtUserLockWorkStation:
    svc #0x14D0
    ret
    .globl NtUserLogicalToPerMonitorDPIPhysicalPoint
    .p2align 4
NtUserLogicalToPerMonitorDPIPhysicalPoint:
    svc #0x14D1
    ret
    .globl NtUserLogicalToPhysicalDpiPointForWindow
    .p2align 4
NtUserLogicalToPhysicalDpiPointForWindow:
    svc #0x14D2
    ret
    .globl NtUserLogicalToPhysicalPoint
    .p2align 4
NtUserLogicalToPhysicalPoint:
    svc #0x14D3
    ret
    .globl NtUserMNDragLeave
    .p2align 4
NtUserMNDragLeave:
    svc #0x14D4
    ret
    .globl NtUserMNDragOver
    .p2align 4
NtUserMNDragOver:
    svc #0x14D5
    ret
    .globl NtUserMagControl
    .p2align 4
NtUserMagControl:
    svc #0x14D6
    ret
    .globl NtUserMagGetContextInformation
    .p2align 4
NtUserMagGetContextInformation:
    svc #0x14D7
    ret
    .globl NtUserMagSetContextInformation
    .p2align 4
NtUserMagSetContextInformation:
    svc #0x14D8
    ret
    .globl NtUserMapDesktopObject
    .p2align 4
NtUserMapDesktopObject:
    svc #0x14D9
    ret
    .globl NtUserMapPointsByVisualIdentifier
    .p2align 4
NtUserMapPointsByVisualIdentifier:
    svc #0x14DA
    ret
    .globl NtUserMarkWindowForRawMouse
    .p2align 4
NtUserMarkWindowForRawMouse:
    svc #0x14DB
    ret
    .globl NtUserMenuItemFromPoint
    .p2align 4
NtUserMenuItemFromPoint:
    svc #0x14DC
    ret
    .globl NtUserMessageBeep
    .p2align 4
NtUserMessageBeep:
    svc #0x14DD
    ret
    .globl NtUserMinInitialize
    .p2align 4
NtUserMinInitialize:
    svc #0x14DE
    ret
    .globl NtUserMinMaximize
    .p2align 4
NtUserMinMaximize:
    svc #0x14DF
    ret
    .globl NtUserModifyWindowTouchCapability
    .p2align 4
NtUserModifyWindowTouchCapability:
    svc #0x14E0
    ret
    .globl NtUserMsgWaitForMultipleObjectsEx
    .p2align 4
NtUserMsgWaitForMultipleObjectsEx:
    svc #0x14E1
    ret
    .globl NtUserNavigateFocus
    .p2align 4
NtUserNavigateFocus:
    svc #0x14E2
    ret
    .globl NtUserNlsKbdSendIMENotification
    .p2align 4
NtUserNlsKbdSendIMENotification:
    svc #0x14E3
    ret
    .globl NtUserNotifyIMEStatus
    .p2align 4
NtUserNotifyIMEStatus:
    svc #0x14E4
    ret
    .globl NtUserNotifyOverlayWindow
    .p2align 4
NtUserNotifyOverlayWindow:
    svc #0x14E5
    ret
    .globl NtUserOpenInputDesktop
    .p2align 4
NtUserOpenInputDesktop:
    svc #0x14E6
    ret
    .globl NtUserOpenThreadDesktop
    .p2align 4
NtUserOpenThreadDesktop:
    svc #0x14E7
    ret
    .globl NtUserPaintMonitor
    .p2align 4
NtUserPaintMonitor:
    svc #0x14E8
    ret
    .globl NtUserPerMonitorDPIPhysicalToLogicalPoint
    .p2align 4
NtUserPerMonitorDPIPhysicalToLogicalPoint:
    svc #0x14E9
    ret
    .globl NtUserPhysicalToLogicalDpiPointForWindow
    .p2align 4
NtUserPhysicalToLogicalDpiPointForWindow:
    svc #0x14EA
    ret
    .globl NtUserPhysicalToLogicalPoint
    .p2align 4
NtUserPhysicalToLogicalPoint:
    svc #0x14EB
    ret
    .globl NtUserPlayEventSound
    .p2align 4
NtUserPlayEventSound:
    svc #0x14EC
    ret
    .globl NtUserPostKeyboardInputMessage
    .p2align 4
NtUserPostKeyboardInputMessage:
    svc #0x14ED
    ret
    .globl NtUserPostQuitMessage
    .p2align 4
NtUserPostQuitMessage:
    svc #0x14EE
    ret
    .globl NtUserPrepareForLogoff
    .p2align 4
NtUserPrepareForLogoff:
    svc #0x14EF
    ret
    .globl NtUserPrintWindow
    .p2align 4
NtUserPrintWindow:
    svc #0x14F0
    ret
    .globl NtUserProcessInkFeedbackCommand
    .p2align 4
NtUserProcessInkFeedbackCommand:
    svc #0x14F1
    ret
    .globl NtUserPromoteMouseInPointer
    .p2align 4
NtUserPromoteMouseInPointer:
    svc #0x14F2
    ret
    .globl NtUserPromotePointer
    .p2align 4
NtUserPromotePointer:
    svc #0x14F3
    ret
    .globl NtUserQueryBSDRWindow
    .p2align 4
NtUserQueryBSDRWindow:
    svc #0x14F4
    ret
    .globl NtUserQueryDisplayConfig
    .p2align 4
NtUserQueryDisplayConfig:
    svc #0x14F5
    ret
    .globl NtUserQueryInformationThread
    .p2align 4
NtUserQueryInformationThread:
    svc #0x14F6
    ret
    .globl NtUserQueryInputContext
    .p2align 4
NtUserQueryInputContext:
    svc #0x14F7
    ret
    .globl NtUserRaiseLowerShellWindow
    .p2align 4
NtUserRaiseLowerShellWindow:
    svc #0x14F8
    ret
    .globl NtUserRealChildWindowFromPoint
    .p2align 4
NtUserRealChildWindowFromPoint:
    svc #0x14F9
    ret
    .globl NtUserRealWaitMessageEx
    .p2align 4
NtUserRealWaitMessageEx:
    svc #0x14FA
    ret
    .globl NtUserRealizePalette
    .p2align 4
NtUserRealizePalette:
    svc #0x14FB
    ret
    .globl NtUserReassociateQueueEventCompletionPacket
    .p2align 4
NtUserReassociateQueueEventCompletionPacket:
    svc #0x14FC
    ret
    .globl NtUserRedrawFrame
    .p2align 4
NtUserRedrawFrame:
    svc #0x14FD
    ret
    .globl NtUserRedrawFrameAndHook
    .p2align 4
NtUserRedrawFrameAndHook:
    svc #0x14FE
    ret
    .globl NtUserRedrawTitle
    .p2align 4
NtUserRedrawTitle:
    svc #0x14FF
    ret
    .globl NtUserRegisterBSDRWindow
    .p2align 4
NtUserRegisterBSDRWindow:
    svc #0x1500
    ret
    .globl NtUserRegisterCloakedNotification
    .p2align 4
NtUserRegisterCloakedNotification:
    svc #0x1501
    ret
    .globl NtUserRegisterCoreMessagingEndPoint
    .p2align 4
NtUserRegisterCoreMessagingEndPoint:
    svc #0x1502
    ret
    .globl NtUserRegisterDManipHook
    .p2align 4
NtUserRegisterDManipHook:
    svc #0x1503
    ret
    .globl NtUserRegisterEdgy
    .p2align 4
NtUserRegisterEdgy:
    svc #0x1504
    ret
    .globl NtUserRegisterErrorReportingDialog
    .p2align 4
NtUserRegisterErrorReportingDialog:
    svc #0x1505
    ret
    .globl NtUserRegisterForCustomDockTargets
    .p2align 4
NtUserRegisterForCustomDockTargets:
    svc #0x1506
    ret
    .globl NtUserRegisterForTooltipDismissNotification
    .p2align 4
NtUserRegisterForTooltipDismissNotification:
    svc #0x1507
    ret
    .globl NtUserRegisterGhostWindow
    .p2align 4
NtUserRegisterGhostWindow:
    svc #0x1508
    ret
    .globl NtUserRegisterHotKey
    .p2align 4
NtUserRegisterHotKey:
    svc #0x1509
    ret
    .globl NtUserRegisterLPK
    .p2align 4
NtUserRegisterLPK:
    svc #0x150A
    ret
    .globl NtUserRegisterLogonProcess
    .p2align 4
NtUserRegisterLogonProcess:
    svc #0x150B
    ret
    .globl NtUserRegisterManipulationThread
    .p2align 4
NtUserRegisterManipulationThread:
    svc #0x150C
    ret
    .globl NtUserRegisterPointerDeviceNotifications
    .p2align 4
NtUserRegisterPointerDeviceNotifications:
    svc #0x150D
    ret
    .globl NtUserRegisterPointerInputTarget
    .p2align 4
NtUserRegisterPointerInputTarget:
    svc #0x150E
    ret
    .globl NtUserRegisterRawInputDevices
    .p2align 4
NtUserRegisterRawInputDevices:
    svc #0x150F
    ret
    .globl NtUserRegisterServicesProcess
    .p2align 4
NtUserRegisterServicesProcess:
    svc #0x1510
    ret
    .globl NtUserRegisterSessionPort
    .p2align 4
NtUserRegisterSessionPort:
    svc #0x1511
    ret
    .globl NtUserRegisterShellHookWindow
    .p2align 4
NtUserRegisterShellHookWindow:
    svc #0x1512
    ret
    .globl NtUserRegisterSiblingFrostWindow
    .p2align 4
NtUserRegisterSiblingFrostWindow:
    svc #0x1513
    ret
    .globl NtUserRegisterSystemThread
    .p2align 4
NtUserRegisterSystemThread:
    svc #0x1514
    ret
    .globl NtUserRegisterTasklist
    .p2align 4
NtUserRegisterTasklist:
    svc #0x1515
    ret
    .globl NtUserRegisterTouchHitTestingWindow
    .p2align 4
NtUserRegisterTouchHitTestingWindow:
    svc #0x1516
    ret
    .globl NtUserRegisterTouchPadCapable
    .p2align 4
NtUserRegisterTouchPadCapable:
    svc #0x1517
    ret
    .globl NtUserRegisterTouchpadCapableWindow
    .p2align 4
NtUserRegisterTouchpadCapableWindow:
    svc #0x1518
    ret
    .globl NtUserRegisterUserApiHook
    .p2align 4
NtUserRegisterUserApiHook:
    svc #0x1519
    ret
    .globl NtUserRegisterWindowArrangementCallout
    .p2align 4
NtUserRegisterWindowArrangementCallout:
    svc #0x151A
    ret
    .globl NtUserReleaseCapture
    .p2align 4
NtUserReleaseCapture:
    svc #0x151B
    ret
    .globl NtUserReleaseDC
    .p2align 4
NtUserReleaseDC:
    svc #0x151C
    ret
    .globl NtUserReleaseDwmHitTestWaiters
    .p2align 4
NtUserReleaseDwmHitTestWaiters:
    svc #0x151D
    ret
    .globl NtUserRemoteConnect
    .p2align 4
NtUserRemoteConnect:
    svc #0x151E
    ret
    .globl NtUserRemoteConnectState
    .p2align 4
NtUserRemoteConnectState:
    svc #0x151F
    ret
    .globl NtUserRemoteConsoleShadowStop
    .p2align 4
NtUserRemoteConsoleShadowStop:
    svc #0x1520
    ret
    .globl NtUserRemoteDisconnect
    .p2align 4
NtUserRemoteDisconnect:
    svc #0x1521
    ret
    .globl NtUserRemoteNotify
    .p2align 4
NtUserRemoteNotify:
    svc #0x1522
    ret
    .globl NtUserRemotePassthruDisable
    .p2align 4
NtUserRemotePassthruDisable:
    svc #0x1523
    ret
    .globl NtUserRemotePassthruEnable
    .p2align 4
NtUserRemotePassthruEnable:
    svc #0x1524
    ret
    .globl NtUserRemoteReconnect
    .p2align 4
NtUserRemoteReconnect:
    svc #0x1525
    ret
    .globl NtUserRemoteRedrawRectangle
    .p2align 4
NtUserRemoteRedrawRectangle:
    svc #0x1526
    ret
    .globl NtUserRemoteRedrawScreen
    .p2align 4
NtUserRemoteRedrawScreen:
    svc #0x1527
    ret
    .globl NtUserRemoteShadowCleanup
    .p2align 4
NtUserRemoteShadowCleanup:
    svc #0x1528
    ret
    .globl NtUserRemoteShadowSetup
    .p2align 4
NtUserRemoteShadowSetup:
    svc #0x1529
    ret
    .globl NtUserRemoteShadowStart
    .p2align 4
NtUserRemoteShadowStart:
    svc #0x152A
    ret
    .globl NtUserRemoteShadowStop
    .p2align 4
NtUserRemoteShadowStop:
    svc #0x152B
    ret
    .globl NtUserRemoteStopScreenUpdates
    .p2align 4
NtUserRemoteStopScreenUpdates:
    svc #0x152C
    ret
    .globl NtUserRemoteThinwireStats
    .p2align 4
NtUserRemoteThinwireStats:
    svc #0x152D
    ret
    .globl NtUserRemoveClipboardFormatListener
    .p2align 4
NtUserRemoveClipboardFormatListener:
    svc #0x152E
    ret
    .globl NtUserRemoveInjectionDevice
    .p2align 4
NtUserRemoveInjectionDevice:
    svc #0x152F
    ret
    .globl NtUserRemoveQueueCompletion
    .p2align 4
NtUserRemoveQueueCompletion:
    svc #0x1530
    ret
    .globl NtUserRemoveVisualIdentifier
    .p2align 4
NtUserRemoveVisualIdentifier:
    svc #0x1531
    ret
    .globl NtUserReplyMessage
    .p2align 4
NtUserReplyMessage:
    svc #0x1532
    ret
    .globl NtUserReportInertia
    .p2align 4
NtUserReportInertia:
    svc #0x1533
    ret
    .globl NtUserRequestLowLatencyProfile
    .p2align 4
NtUserRequestLowLatencyProfile:
    svc #0x1534
    ret
    .globl NtUserResetDblClk
    .p2align 4
NtUserResetDblClk:
    svc #0x1535
    ret
    .globl NtUserResolveDesktopForWOW
    .p2align 4
NtUserResolveDesktopForWOW:
    svc #0x1536
    ret
    .globl NtUserRestoreWindowDpiChanges
    .p2align 4
NtUserRestoreWindowDpiChanges:
    svc #0x1537
    ret
    .globl NtUserScaleSystemMetricForDPIWithoutCache
    .p2align 4
NtUserScaleSystemMetricForDPIWithoutCache:
    svc #0x1538
    ret
    .globl NtUserScheduleDispatchNotification
    .p2align 4
NtUserScheduleDispatchNotification:
    svc #0x1539
    ret
    .globl NtUserSendEventMessage
    .p2align 4
NtUserSendEventMessage:
    svc #0x153A
    ret
    .globl NtUserSendInteractiveControlHapticsReport
    .p2align 4
NtUserSendInteractiveControlHapticsReport:
    svc #0x153B
    ret
    .globl NtUserSetActivationFilter
    .p2align 4
NtUserSetActivationFilter:
    svc #0x153C
    ret
    .globl NtUserSetActiveProcessForMonitor
    .p2align 4
NtUserSetActiveProcessForMonitor:
    svc #0x153D
    ret
    .globl NtUserSetAdditionalForegroundBoostProcesses
    .p2align 4
NtUserSetAdditionalForegroundBoostProcesses:
    svc #0x153E
    ret
    .globl NtUserSetAdditionalPowerThrottlingProcess
    .p2align 4
NtUserSetAdditionalPowerThrottlingProcess:
    svc #0x153F
    ret
    .globl NtUserSetAppImeLevel
    .p2align 4
NtUserSetAppImeLevel:
    svc #0x1540
    ret
    .globl NtUserSetAutoRotation
    .p2align 4
NtUserSetAutoRotation:
    svc #0x1541
    ret
    .globl NtUserSetBridgeWindowChild
    .p2align 4
NtUserSetBridgeWindowChild:
    svc #0x1542
    ret
    .globl NtUserSetBrokeredForeground
    .p2align 4
NtUserSetBrokeredForeground:
    svc #0x1543
    ret
    .globl NtUserSetCalibrationData
    .p2align 4
NtUserSetCalibrationData:
    svc #0x1544
    ret
    .globl NtUserSetCancelRotationDelayHintWindow
    .p2align 4
NtUserSetCancelRotationDelayHintWindow:
    svc #0x1545
    ret
    .globl NtUserSetCaretBlinkTime
    .p2align 4
NtUserSetCaretBlinkTime:
    svc #0x1546
    ret
    .globl NtUserSetCaretPos
    .p2align 4
NtUserSetCaretPos:
    svc #0x1547
    ret
    .globl NtUserSetChildWindowNoActivate
    .p2align 4
NtUserSetChildWindowNoActivate:
    svc #0x1548
    ret
    .globl NtUserSetClassWord
    .p2align 4
NtUserSetClassWord:
    svc #0x1549
    ret
    .globl NtUserSetCoreWindow
    .p2align 4
NtUserSetCoreWindow:
    svc #0x154A
    ret
    .globl NtUserSetCoreWindowPartner
    .p2align 4
NtUserSetCoreWindowPartner:
    svc #0x154B
    ret
    .globl NtUserSetCoveredWindowStates
    .p2align 4
NtUserSetCoveredWindowStates:
    svc #0x154C
    ret
    .globl NtUserSetCursorIconDataEx
    .p2align 4
NtUserSetCursorIconDataEx:
    svc #0x154D
    ret
    .globl NtUserSetCursorPos
    .p2align 4
NtUserSetCursorPos:
    svc #0x154E
    ret
    .globl NtUserSetDesktopColorTransform
    .p2align 4
NtUserSetDesktopColorTransform:
    svc #0x154F
    ret
    .globl NtUserSetDesktopVisualInputSink
    .p2align 4
NtUserSetDesktopVisualInputSink:
    svc #0x1550
    ret
    .globl NtUserSetDialogControlDpiChangeBehavior
    .p2align 4
NtUserSetDialogControlDpiChangeBehavior:
    svc #0x1551
    ret
    .globl NtUserSetDialogPointer
    .p2align 4
NtUserSetDialogPointer:
    svc #0x1552
    ret
    .globl NtUserSetDialogSystemMenu
    .p2align 4
NtUserSetDialogSystemMenu:
    svc #0x1553
    ret
    .globl NtUserSetDisplayAutoRotationPreferences
    .p2align 4
NtUserSetDisplayAutoRotationPreferences:
    svc #0x1554
    ret
    .globl NtUserSetDisplayConfig
    .p2align 4
NtUserSetDisplayConfig:
    svc #0x1555
    ret
    .globl NtUserSetDisplayMapping
    .p2align 4
NtUserSetDisplayMapping:
    svc #0x1556
    ret
    .globl NtUserSetDoubleClickTime
    .p2align 4
NtUserSetDoubleClickTime:
    svc #0x1557
    ret
    .globl NtUserSetDpiForWindow
    .p2align 4
NtUserSetDpiForWindow:
    svc #0x1558
    ret
    .globl NtUserSetFeatureReportResponse
    .p2align 4
NtUserSetFeatureReportResponse:
    svc #0x1559
    ret
    .globl NtUserSetForegroundRedirectionForActivationObject
    .p2align 4
NtUserSetForegroundRedirectionForActivationObject:
    svc #0x155A
    ret
    .globl NtUserSetForegroundWindow
    .p2align 4
NtUserSetForegroundWindow:
    svc #0x155B
    ret
    .globl NtUserSetForegroundWindowForApplication
    .p2align 4
NtUserSetForegroundWindowForApplication:
    svc #0x155C
    ret
    .globl NtUserSetFullscreenMagnifierOffsetsDWMUpdated
    .p2align 4
NtUserSetFullscreenMagnifierOffsetsDWMUpdated:
    svc #0x155D
    ret
    .globl NtUserSetGUISystemInformation
    .p2align 4
NtUserSetGUISystemInformation:
    svc #0x155E
    ret
    .globl NtUserSetGestureConfig
    .p2align 4
NtUserSetGestureConfig:
    svc #0x155F
    ret
    .globl NtUserSetImeHotKey
    .p2align 4
NtUserSetImeHotKey:
    svc #0x1560
    ret
    .globl NtUserSetImeInfoEx
    .p2align 4
NtUserSetImeInfoEx:
    svc #0x1561
    ret
    .globl NtUserSetImeOwnerWindow
    .p2align 4
NtUserSetImeOwnerWindow:
    svc #0x1562
    ret
    .globl NtUserSetInputServiceState
    .p2align 4
NtUserSetInputServiceState:
    svc #0x1563
    ret
    .globl NtUserSetInteractiveControlFocus
    .p2align 4
NtUserSetInteractiveControlFocus:
    svc #0x1564
    ret
    .globl NtUserSetInteractiveCtrlRotationAngle
    .p2align 4
NtUserSetInteractiveCtrlRotationAngle:
    svc #0x1565
    ret
    .globl NtUserSetInternalWindowPos
    .p2align 4
NtUserSetInternalWindowPos:
    svc #0x1566
    ret
    .globl NtUserSetJobUILimits
    .p2align 4
NtUserSetJobUILimits:
    svc #0x1567
    ret
    .globl NtUserSetLayeredWindowAttributes
    .p2align 4
NtUserSetLayeredWindowAttributes:
    svc #0x1568
    ret
    .globl NtUserSetMagnificationDesktopMagnifierOffsetsDWMUpdated
    .p2align 4
NtUserSetMagnificationDesktopMagnifierOffsetsDWMUpdated:
    svc #0x1569
    ret
    .globl NtUserSetManipulationInputTarget
    .p2align 4
NtUserSetManipulationInputTarget:
    svc #0x156A
    ret
    .globl NtUserSetMaxTouchpadSensitivity
    .p2align 4
NtUserSetMaxTouchpadSensitivity:
    svc #0x156B
    ret
    .globl NtUserSetMenu
    .p2align 4
NtUserSetMenu:
    svc #0x156C
    ret
    .globl NtUserSetMenuContextHelpId
    .p2align 4
NtUserSetMenuContextHelpId:
    svc #0x156D
    ret
    .globl NtUserSetMenuFlagRtoL
    .p2align 4
NtUserSetMenuFlagRtoL:
    svc #0x156E
    ret
    .globl NtUserSetMessageExtraInfo
    .p2align 4
NtUserSetMessageExtraInfo:
    svc #0x156F
    ret
    .globl NtUserSetMirrorRendering
    .p2align 4
NtUserSetMirrorRendering:
    svc #0x1570
    ret
    .globl NtUserSetModernAppWindow
    .p2align 4
NtUserSetModernAppWindow:
    svc #0x1571
    ret
    .globl NtUserSetMonitorWorkArea
    .p2align 4
NtUserSetMonitorWorkArea:
    svc #0x1572
    ret
    .globl NtUserSetMsgBox
    .p2align 4
NtUserSetMsgBox:
    svc #0x1573
    ret
    .globl NtUserSetObjectInformation
    .p2align 4
NtUserSetObjectInformation:
    svc #0x1574
    ret
    .globl NtUserSetProcessDefaultLayout
    .p2align 4
NtUserSetProcessDefaultLayout:
    svc #0x1575
    ret
    .globl NtUserSetProcessDpiAwarenessContext
    .p2align 4
NtUserSetProcessDpiAwarenessContext:
    svc #0x1576
    ret
    .globl NtUserSetProcessInteractionFlags
    .p2align 4
NtUserSetProcessInteractionFlags:
    svc #0x1577
    ret
    .globl NtUserSetProcessLaunchForegroundPolicy
    .p2align 4
NtUserSetProcessLaunchForegroundPolicy:
    svc #0x1578
    ret
    .globl NtUserSetProcessMousewheelRoutingMode
    .p2align 4
NtUserSetProcessMousewheelRoutingMode:
    svc #0x1579
    ret
    .globl NtUserSetProcessRestrictionExemption
    .p2align 4
NtUserSetProcessRestrictionExemption:
    svc #0x157A
    ret
    .globl NtUserSetProcessUIAccessZorder
    .p2align 4
NtUserSetProcessUIAccessZorder:
    svc #0x157B
    ret
    .globl NtUserSetProcessWin32Capabilities
    .p2align 4
NtUserSetProcessWin32Capabilities:
    svc #0x157C
    ret
    .globl NtUserSetProgmanWindow
    .p2align 4
NtUserSetProgmanWindow:
    svc #0x157D
    ret
    .globl NtUserSetProp2
    .p2align 4
NtUserSetProp2:
    svc #0x157E
    ret
    .globl NtUserSetSharedWindowData
    .p2align 4
NtUserSetSharedWindowData:
    svc #0x157F
    ret
    .globl NtUserSetShellChangeNotifyHWND
    .p2align 4
NtUserSetShellChangeNotifyHWND:
    svc #0x1580
    ret
    .globl NtUserSetShellSpecialWindow
    .p2align 4
NtUserSetShellSpecialWindow:
    svc #0x1581
    ret
    .globl NtUserSetShellWindowEx
    .p2align 4
NtUserSetShellWindowEx:
    svc #0x1582
    ret
    .globl NtUserSetSysColors
    .p2align 4
NtUserSetSysColors:
    svc #0x1583
    ret
    .globl NtUserSetSysMenu
    .p2align 4
NtUserSetSysMenu:
    svc #0x1584
    ret
    .globl NtUserSetSystemContentRects
    .p2align 4
NtUserSetSystemContentRects:
    svc #0x1585
    ret
    .globl NtUserSetSystemCursor
    .p2align 4
NtUserSetSystemCursor:
    svc #0x1586
    ret
    .globl NtUserSetSystemTimer
    .p2align 4
NtUserSetSystemTimer:
    svc #0x1587
    ret
    .globl NtUserSetTSFEventState
    .p2align 4
NtUserSetTSFEventState:
    svc #0x1588
    ret
    .globl NtUserSetTargetForResourceBrokering
    .p2align 4
NtUserSetTargetForResourceBrokering:
    svc #0x1589
    ret
    .globl NtUserSetTaskmanWindow
    .p2align 4
NtUserSetTaskmanWindow:
    svc #0x158A
    ret
    .globl NtUserSetThreadInputBlocked
    .p2align 4
NtUserSetThreadInputBlocked:
    svc #0x158B
    ret
    .globl NtUserSetThreadLayoutHandles
    .p2align 4
NtUserSetThreadLayoutHandles:
    svc #0x158C
    ret
    .globl NtUserSetThreadQueueMergeSetting
    .p2align 4
NtUserSetThreadQueueMergeSetting:
    svc #0x158D
    ret
    .globl NtUserSetUserObjectCapability
    .p2align 4
NtUserSetUserObjectCapability:
    svc #0x158E
    ret
    .globl NtUserSetVisible
    .p2align 4
NtUserSetVisible:
    svc #0x158F
    ret
    .globl NtUserSetWaitForQueueAttach
    .p2align 4
NtUserSetWaitForQueueAttach:
    svc #0x1590
    ret
    .globl NtUserSetWatermarkStrings
    .p2align 4
NtUserSetWatermarkStrings:
    svc #0x1591
    ret
    .globl NtUserSetWindowBand
    .p2align 4
NtUserSetWindowBand:
    svc #0x1592
    ret
    .globl NtUserSetWindowCompositionAttribute
    .p2align 4
NtUserSetWindowCompositionAttribute:
    svc #0x1593
    ret
    .globl NtUserSetWindowCompositionTransition
    .p2align 4
NtUserSetWindowCompositionTransition:
    svc #0x1594
    ret
    .globl NtUserSetWindowContextHelpId
    .p2align 4
NtUserSetWindowContextHelpId:
    svc #0x1595
    ret
    .globl NtUserSetWindowDisplayAffinity
    .p2align 4
NtUserSetWindowDisplayAffinity:
    svc #0x1596
    ret
    .globl NtUserSetWindowFeedbackSetting
    .p2align 4
NtUserSetWindowFeedbackSetting:
    svc #0x1597
    ret
    .globl NtUserSetWindowMessageCapability
    .p2align 4
NtUserSetWindowMessageCapability:
    svc #0x1598
    ret
    .globl NtUserSetWindowRgnEx
    .p2align 4
NtUserSetWindowRgnEx:
    svc #0x1599
    ret
    .globl NtUserSetWindowShowState
    .p2align 4
NtUserSetWindowShowState:
    svc #0x159A
    ret
    .globl NtUserSetWindowState
    .p2align 4
NtUserSetWindowState:
    svc #0x159B
    ret
    .globl NtUserSetWindowStationUser
    .p2align 4
NtUserSetWindowStationUser:
    svc #0x159C
    ret
    .globl NtUserShellForegroundBoostProcess
    .p2align 4
NtUserShellForegroundBoostProcess:
    svc #0x159D
    ret
    .globl NtUserShellHandwritingDelegateInput
    .p2align 4
NtUserShellHandwritingDelegateInput:
    svc #0x159E
    ret
    .globl NtUserShellHandwritingHandleDelegatedInput
    .p2align 4
NtUserShellHandwritingHandleDelegatedInput:
    svc #0x159F
    ret
    .globl NtUserShellHandwritingUndelegateInput
    .p2align 4
NtUserShellHandwritingUndelegateInput:
    svc #0x15A0
    ret
    .globl NtUserShellMigrateWindow
    .p2align 4
NtUserShellMigrateWindow:
    svc #0x15A1
    ret
    .globl NtUserShellRegisterHotKey
    .p2align 4
NtUserShellRegisterHotKey:
    svc #0x15A2
    ret
    .globl NtUserShellSetWindowPos
    .p2align 4
NtUserShellSetWindowPos:
    svc #0x15A3
    ret
    .globl NtUserShowCursor
    .p2align 4
NtUserShowCursor:
    svc #0x15A4
    ret
    .globl NtUserShowOwnedPopups
    .p2align 4
NtUserShowOwnedPopups:
    svc #0x15A5
    ret
    .globl NtUserShowStartGlass
    .p2align 4
NtUserShowStartGlass:
    svc #0x15A6
    ret
    .globl NtUserShowSystemCursor
    .p2align 4
NtUserShowSystemCursor:
    svc #0x15A7
    ret
    .globl NtUserShutdownBlockReasonCreate
    .p2align 4
NtUserShutdownBlockReasonCreate:
    svc #0x15A8
    ret
    .globl NtUserShutdownBlockReasonQuery
    .p2align 4
NtUserShutdownBlockReasonQuery:
    svc #0x15A9
    ret
    .globl NtUserShutdownReasonDestroy
    .p2align 4
NtUserShutdownReasonDestroy:
    svc #0x15AA
    ret
    .globl NtUserSignalRedirectionStartComplete
    .p2align 4
NtUserSignalRedirectionStartComplete:
    svc #0x15AB
    ret
    .globl NtUserSlicerControl
    .p2align 4
NtUserSlicerControl:
    svc #0x15AC
    ret
    .globl NtUserSoundSentry
    .p2align 4
NtUserSoundSentry:
    svc #0x15AD
    ret
    .globl NtUserStopAndEndInertia
    .p2align 4
NtUserStopAndEndInertia:
    svc #0x15AE
    ret
    .globl NtUserSuppressWindowDisplayChange
    .p2align 4
NtUserSuppressWindowDisplayChange:
    svc #0x15AF
    ret
    .globl NtUserSwapMouseButton
    .p2align 4
NtUserSwapMouseButton:
    svc #0x15B0
    ret
    .globl NtUserSwitchDesktop
    .p2align 4
NtUserSwitchDesktop:
    svc #0x15B1
    ret
    .globl NtUserSwitchToThisWindow
    .p2align 4
NtUserSwitchToThisWindow:
    svc #0x15B2
    ret
    .globl NtUserSystemParametersInfoForDpi
    .p2align 4
NtUserSystemParametersInfoForDpi:
    svc #0x15B3
    ret
    .globl NtUserTestForInteractiveUser
    .p2align 4
NtUserTestForInteractiveUser:
    svc #0x15B4
    ret
    .globl NtUserThreadMessageQueueAttached
    .p2align 4
NtUserThreadMessageQueueAttached:
    svc #0x15B5
    ret
    .globl NtUserTraceLoggingSendMixedModeTelemetry
    .p2align 4
NtUserTraceLoggingSendMixedModeTelemetry:
    svc #0x15B6
    ret
    .globl NtUserTrackPopupMenuEx
    .p2align 4
NtUserTrackPopupMenuEx:
    svc #0x15B7
    ret
    .globl NtUserTransformPoint
    .p2align 4
NtUserTransformPoint:
    svc #0x15B8
    ret
    .globl NtUserTransformRect
    .p2align 4
NtUserTransformRect:
    svc #0x15B9
    ret
    .globl NtUserUndelegateInput
    .p2align 4
NtUserUndelegateInput:
    svc #0x15BA
    ret
    .globl NtUserUnhookWindowsHook
    .p2align 4
NtUserUnhookWindowsHook:
    svc #0x15BB
    ret
    .globl NtUserUnloadKeyboardLayout
    .p2align 4
NtUserUnloadKeyboardLayout:
    svc #0x15BC
    ret
    .globl NtUserUnlockWindowStation
    .p2align 4
NtUserUnlockWindowStation:
    svc #0x15BD
    ret
    .globl NtUserUnregisterHotKey
    .p2align 4
NtUserUnregisterHotKey:
    svc #0x15BE
    ret
    .globl NtUserUnregisterSessionPort
    .p2align 4
NtUserUnregisterSessionPort:
    svc #0x15BF
    ret
    .globl NtUserUnregisterUserApiHook
    .p2align 4
NtUserUnregisterUserApiHook:
    svc #0x15C0
    ret
    .globl NtUserUpdateClientRect
    .p2align 4
NtUserUpdateClientRect:
    svc #0x15C1
    ret
    .globl NtUserUpdateDefaultDesktopThumbnail
    .p2align 4
NtUserUpdateDefaultDesktopThumbnail:
    svc #0x15C2
    ret
    .globl NtUserUpdateInputContext
    .p2align 4
NtUserUpdateInputContext:
    svc #0x15C3
    ret
    .globl NtUserUpdateInstance
    .p2align 4
NtUserUpdateInstance:
    svc #0x15C4
    ret
    .globl NtUserUpdateLayeredWindow
    .p2align 4
NtUserUpdateLayeredWindow:
    svc #0x15C5
    ret
    .globl NtUserUpdatePerUserImmEnabling
    .p2align 4
NtUserUpdatePerUserImmEnabling:
    svc #0x15C6
    ret
    .globl NtUserUpdatePerUserSystemParameters
    .p2align 4
NtUserUpdatePerUserSystemParameters:
    svc #0x15C7
    ret
    .globl NtUserUpdateWindow
    .p2align 4
NtUserUpdateWindow:
    svc #0x15C8
    ret
    .globl NtUserUpdateWindowInputSinkHints
    .p2align 4
NtUserUpdateWindowInputSinkHints:
    svc #0x15C9
    ret
    .globl NtUserUpdateWindowTrackingInfo
    .p2align 4
NtUserUpdateWindowTrackingInfo:
    svc #0x15CA
    ret
    .globl NtUserUpdateWindows
    .p2align 4
NtUserUpdateWindows:
    svc #0x15CB
    ret
    .globl NtUserUserHandleGrantAccess
    .p2align 4
NtUserUserHandleGrantAccess:
    svc #0x15CC
    ret
    .globl NtUserUserPowerCalloutWorker
    .p2align 4
NtUserUserPowerCalloutWorker:
    svc #0x15CD
    ret
    .globl NtUserValidateHandleSecure
    .p2align 4
NtUserValidateHandleSecure:
    svc #0x15CE
    ret
    .globl NtUserValidateRgn
    .p2align 4
NtUserValidateRgn:
    svc #0x15CF
    ret
    .globl NtUserWaitAvailableMessageEx
    .p2align 4
NtUserWaitAvailableMessageEx:
    svc #0x15D0
    ret
    .globl NtUserWaitForInputIdle
    .p2align 4
NtUserWaitForInputIdle:
    svc #0x15D1
    ret
    .globl NtUserWaitForRedirectionStartComplete
    .p2align 4
NtUserWaitForRedirectionStartComplete:
    svc #0x15D2
    ret
    .globl NtUserWakeRITForShutdown
    .p2align 4
NtUserWakeRITForShutdown:
    svc #0x15D3
    ret
    .globl NtUserWindowFromDC
    .p2align 4
NtUserWindowFromDC:
    svc #0x15D4
    ret
    .globl NtUserWindowFromPhysicalPoint
    .p2align 4
NtUserWindowFromPhysicalPoint:
    svc #0x15D5
    ret
    .globl NtUserZapActiveAndFocus
    .p2align 4
NtUserZapActiveAndFocus:
    svc #0x15D6
    ret
    .globl NtValidateCompositionSurfaceHandle
    .p2align 4
NtValidateCompositionSurfaceHandle:
    svc #0x15D7
    ret
    .globl NtVisualCaptureBits
    .p2align 4
NtVisualCaptureBits:
    svc #0x15D8
    ret
    .globl NtUserSetClassLongPtr
    .p2align 4
NtUserSetClassLongPtr:
    svc #0x15D9
    ret
    .globl NtUserSetWindowLongPtr
    .p2align 4
NtUserSetWindowLongPtr:
    svc #0x15DA
    ret
    .globl NtUserCallNoParam
    .p2align 4
NtUserCallNoParam:
    svc #0x1700
    ret
    .globl NtUserCallOneParam
    .p2align 4
NtUserCallOneParam:
    svc #0x1701
    ret
    .globl NtUserCallTwoParam
    .p2align 4
NtUserCallTwoParam:
    svc #0x1702
    ret
    .globl NtUserCallHwnd
    .p2align 4
NtUserCallHwnd:
    svc #0x1703
    ret
    .globl NtUserCallHwndParam
    .p2align 4
NtUserCallHwndParam:
    svc #0x1704
    ret
    .globl NtUserCreateEmptyCurObject
    .p2align 4
NtUserCreateEmptyCurObject:
    svc #0x1705
    ret
    .globl NtGdiGetStockObject
    .p2align 4
NtGdiGetStockObject:
    svc #0x1706
    ret
    .globl NtGdiSetViewportOrgEx
    .p2align 4
NtGdiSetViewportOrgEx:
    svc #0x1707
    ret
    .globl NtGdiSetWindowOrgEx
    .p2align 4
NtGdiSetWindowOrgEx:
    svc #0x1708
    ret
    .globl NtUserMimeGetMenuItemInfo
    .p2align 4
NtUserMimeGetMenuItemInfo:
    svc #0x1709
    ret
    .globl NtUserMimeCallMenu
    .p2align 4
NtUserMimeCallMenu:
    svc #0x170A
    ret
    .globl NtGdiMimeGpuOpen
    .p2align 4
NtGdiMimeGpuOpen:
    svc #0x170B
    ret
    .globl NtGdiMimeGpuAddBlock
    .p2align 4
NtGdiMimeGpuAddBlock:
    svc #0x170C
    ret
    .globl NtGdiMimeGpuKick
    .p2align 4
NtGdiMimeGpuKick:
    svc #0x170D
    ret
    .globl NtGdiMimeGpuWait
    .p2align 4
NtGdiMimeGpuWait:
    svc #0x170E
    ret
    .globl NtGdiMimeGpuGetSurface
    .p2align 4
NtGdiMimeGpuGetSurface:
    svc #0x170F
    ret
    .globl NtGdiMimeAudioCreate
    .p2align 4
NtGdiMimeAudioCreate:
    svc #0x1710
    ret
    .globl NtGdiMimeAudioControl
    .p2align 4
NtGdiMimeAudioControl:
    svc #0x1711
    ret
    .globl NtGdiMimeVkOpen
    .p2align 4
NtGdiMimeVkOpen:
    svc #0x1712
    ret
    .globl NtGdiMimeVkAlloc
    .p2align 4
NtGdiMimeVkAlloc:
    svc #0x1713
    ret
    .globl NtGdiMimeVkFree
    .p2align 4
NtGdiMimeVkFree:
    svc #0x1714
    ret
    .globl NtGdiMimeVkKick
    .p2align 4
NtGdiMimeVkKick:
    svc #0x1715
    ret
    .globl NtGdiMimeVkWait
    .p2align 4
NtGdiMimeVkWait:
    svc #0x1716
    ret
    .globl NtGdiMimeGamma
    .p2align 4
NtGdiMimeGamma:
    svc #0x1717
    ret
    .globl NtGdiMimeSetExtent
    .p2align 4
NtGdiMimeSetExtent:
    svc #0x1718
    ret
    .globl NtUserMimeDnd
    .p2align 4
NtUserMimeDnd:
    svc #0x1719
    ret
    .globl NtUserMimeDndData
    .p2align 4
NtUserMimeDndData:
    svc #0x171A
    ret
    .globl NtUserMimeDndProvide
    .p2align 4
NtUserMimeDndProvide:
    svc #0x171B
    ret
    .globl NtUserMimeIme
    .p2align 4
NtUserMimeIme:
    svc #0x171C
    ret
    .globl NtUserMimeTray
    .p2align 4
NtUserMimeTray:
    svc #0x171D
    ret
    .globl NtUserMimeExitWindows
    .p2align 4
NtUserMimeExitWindows:
    svc #0x171E
    ret
    .globl NtUserMimeCapture
    .p2align 4
NtUserMimeCapture:
    svc #0x1750
    ret
    .globl NtGdiMimeGfxShare
    .p2align 4
NtGdiMimeGfxShare:
    svc #0x1760
    ret
    .globl NtUserMimeShell
    .p2align 4
NtUserMimeShell:
    svc #0x1770
    ret
