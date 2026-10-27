    case MIME_SVC_NtUserSetClassLongPtr:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetClassLong:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetClassWord:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserDestroyWindow:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserShowWindow:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserShowWindowAsync:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetWindowPos:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = (LONG64)(LONG)a[5];
        v[6] = a[6];
        return MxCallService(number, v);
    case MIME_SVC_NtUserEnableWindow:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetParent:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetAncestor:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetWindowLongPtr:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetWindowLong:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserInternalGetWindowText:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetKeyboardState:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetGuiResources:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetKeyboardState:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserOpenInputDesktop:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetProcessWindowStation:
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetProcessWindowStation:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetThreadDesktop:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetThreadDesktop:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCloseWindowStation:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCloseDesktop:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSwitchDesktop:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetWindowPlacement:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetWindowPlacement:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserPostMessage:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserPostThreadMessage:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserWaitMessage:
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetThreadState:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetMessagePos:
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetDC:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetDCEx:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetWindowDC:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserReleaseDC:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserInvalidateRect:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserValidateRect:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserRedrawWindow:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetUpdateRect:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetFocus:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetActiveWindow:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetForegroundWindow:
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetCapture:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetKeyState:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserUnhookWindowsHookEx:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCallNextHookEx:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetAsyncKeyState:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetCursorPos:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetTimer:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtUserKillTimer:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMessageBeep:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCreateEmptyCurObject:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserDestroyCursor:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserDrawIconEx:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = (LONG64)(LONG)a[5];
        v[6] = a[6];
        v[7] = a[7];
        v[8] = a[8];
        v[9] = a[9];
        v[10] = a[10];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetCursor:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCreateMenu:
        return MxCallService(number, v);
    case MIME_SVC_NtUserCreatePopupMenu:
        return MxCallService(number, v);
    case MIME_SVC_NtUserDestroyMenu:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserDeleteMenu:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserRemoveMenu:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserEnableMenuItem:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCheckMenuItem:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetMenuDefaultItem:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetMenu:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetSystemMenu:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserDrawMenuBar:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserEndMenu:
        return MxCallService(number, v);
    case MIME_SVC_NtUserShutdownBlockReasonCreate:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserShutdownReasonDestroy:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserShutdownBlockReasonQuery:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserPrepareForLogoff:
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeExitWindows:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMimeGpuKick:
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMimeAudioControl:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMimeVkKick:
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMimeGfxShare:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMimeGamma:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeDnd:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeDndData:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeIme:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeTray:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeShell:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeDndProvide:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtUserMimeCapture:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtUserOpenClipboard:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCloseClipboard:
        return MxCallService(number, v);
    case MIME_SVC_NtUserEmptyClipboard:
        return MxCallService(number, v);
    case MIME_SVC_NtUserIsClipboardFormatAvailable:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetUpdatedClipboardFormats:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCountClipboardFormats:
        return MxCallService(number, v);
    case MIME_SVC_NtUserEnumClipboardFormats:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetClipboardFormatName:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetClipboardOwner:
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetOpenClipboardWindow:
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetClipboardViewer:
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetClipboardViewer:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserChangeClipboardChain:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetClipboardSequenceNumber:
        return MxCallService(number, v);
    case MIME_SVC_NtUserAddClipboardFormatListener:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserRemoveClipboardFormatListener:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetLayeredWindowAttributes:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetLayeredWindowAttributes:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtUserUpdateLayeredWindow:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        v[6] = a[6];
        v[7] = a[7];
        v[8] = a[8];
        v[9] = a[9];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetWindowRgn:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetWindowRgnEx:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserClipCursor:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetClipCursor:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtUserAttachThreadInput:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserIsTouchWindow:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserSetProp:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserGetProp:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserRemoveProp:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtUserCallNoParam:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetStockObject:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiDeleteObjectApp:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetDCObject:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateCompatibleDC:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSaveDC:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiRestoreDC:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetDeviceCaps:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSelectBitmap:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSelectBrush:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSelectPen:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSelectFont:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetAndSetDCDword:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetDCDword:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetDCPoint:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMoveTo:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetViewportOrgEx:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetWindowOrgEx:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiTransformPoints:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiFlush:
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateSolidBrush:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreatePen:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateBitmap:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateCompatibleBitmap:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtUserWaitForInputIdle:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiDoPalette:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetDIBitsInternal:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        v[6] = a[6];
        v[7] = a[7];
        v[8] = a[8];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetDIBitsToDeviceInternal:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = (LONG64)(LONG)a[5];
        v[6] = (LONG64)(LONG)a[6];
        v[7] = a[7];
        v[8] = a[8];
        v[9] = a[9];
        v[10] = a[10];
        v[11] = a[11];
        v[12] = a[12];
        v[13] = a[13];
        v[14] = a[14];
        v[15] = (LONG64)(LONG)a[15];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiStretchDIBitsInternal:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = (LONG64)(LONG)a[5];
        v[6] = (LONG64)(LONG)a[6];
        v[7] = (LONG64)(LONG)a[7];
        v[8] = (LONG64)(LONG)a[8];
        v[9] = a[9];
        v[10] = a[10];
        v[11] = a[11];
        v[12] = a[12];
        v[13] = a[13];
        v[14] = a[14];
        v[15] = (LONG64)(LONG)a[15];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiBitBlt:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = a[5];
        v[6] = (LONG64)(LONG)a[6];
        v[7] = (LONG64)(LONG)a[7];
        v[8] = a[8];
        v[9] = a[9];
        v[10] = a[10];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiStretchBlt:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = a[5];
        v[6] = (LONG64)(LONG)a[6];
        v[7] = (LONG64)(LONG)a[7];
        v[8] = (LONG64)(LONG)a[8];
        v[9] = (LONG64)(LONG)a[9];
        v[10] = a[10];
        v[11] = a[11];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiPatBlt:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiAlphaBlend:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = a[5];
        v[6] = (LONG64)(LONG)a[6];
        v[7] = (LONG64)(LONG)a[7];
        v[8] = (LONG64)(LONG)a[8];
        v[9] = (LONG64)(LONG)a[9];
        v[10] = a[10];
        v[11] = (LONG64)(LONG)a[11];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiLineTo:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiRectangle:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiRoundRect:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = (LONG64)(LONG)a[5];
        v[6] = (LONG64)(LONG)a[6];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiEllipse:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiPolyPolyDraw:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetPixel:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetPixel:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiExtTextOutW:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        v[6] = (LONG64)(LONG)a[6];
        v[7] = a[7];
        v[8] = a[8];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetTextExtentExW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        v[6] = a[6];
        v[7] = a[7];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetTextMetricsW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetTextFaceW:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = a[2];
        v[3] = a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetCharWidthW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetCharABCWidthsW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiAddFontResourceW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetFontData:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiRemoveFontResourceW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiAddFontMemResourceEx:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiRemoveFontMemResourceEx:
        v[0] = (LONG64)(LONG)a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetFontUnicodeRanges:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetGlyphOutline:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        v[6] = a[6];
        v[7] = a[7];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetGlyphIndicesW:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiBeginPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiEndPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiAbortPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCloseFigure:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiFlattenPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiFillPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiStrokePath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiStrokeAndFillPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSelectClipPath:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiPathToRegion:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetPath:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiModifyWorldTransform:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetTransform:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetBitmapBits:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetBitmapBits:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetPixelFormat:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateRectRgn:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateRoundRectRgn:
        v[0] = (LONG64)(LONG)a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        v[5] = (LONG64)(LONG)a[5];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiExtCreateRegion:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCombineRgn:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetRectRgn:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiOffsetRgn:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetRgnBox:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiPtInRegion:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiRectInRegion:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiEqualRgn:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetRegionData:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiFillRgn:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiFrameRgn:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiInvertRgn:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiExtSelectClipRgn:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetAppClipBox:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetRandomRgn:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiIntersectClipRect:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiExcludeClipRect:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetMetaRgn:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiCreateMetafileDC:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiMimeSetExtent:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetVirtualResolution:
        v[0] = a[0];
        v[1] = (LONG64)(LONG)a[1];
        v[2] = (LONG64)(LONG)a[2];
        v[3] = (LONG64)(LONG)a[3];
        v[4] = (LONG64)(LONG)a[4];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiSetMiterLimit:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiGetMiterLimit:
        v[0] = a[0];
        v[1] = a[1];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiWidenPath:
        v[0] = a[0];
        return MxCallService(number, v);
    case MIME_SVC_NtGdiExtCreatePen:
        v[0] = a[0];
        v[1] = a[1];
        v[2] = a[2];
        v[3] = a[3];
        v[4] = a[4];
        v[5] = a[5];
        v[6] = a[6];
        return MxCallService(number, v);
