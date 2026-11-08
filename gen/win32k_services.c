/* generated, do not edit. */
#if defined(__x86_64__)
#define REGISTER_ARGS 4
#else
#define REGISTER_ARGS 8
#endif
#define STACK_ARGS(n) ((n) > REGISTER_ARGS ? (n) - REGISTER_ARGS : 0)
#include "mime_svc_win32k.h"

typedef struct { void *Routine; unsigned char StackArgs, Implemented; unsigned short Reserved; } SERVICE;
void NtGdiAbortPath(void);
void NtGdiAddFontMemResourceEx(void);
void NtGdiAddFontResourceW(void);
void NtGdiAlphaBlend(void);
void NtGdiBeginPath(void);
void NtGdiBitBlt(void);
void NtGdiCloseFigure(void);
void NtGdiCombineRgn(void);
void NtGdiCreateBitmap(void);
void NtGdiCreateCompatibleBitmap(void);
void NtGdiCreateCompatibleDC(void);
void NtGdiCreateDIBSection(void);
void NtGdiCreateMetafileDC(void);
void NtGdiCreatePen(void);
void NtGdiCreateRectRgn(void);
void NtGdiCreateRoundRectRgn(void);
void NtGdiCreateSolidBrush(void);
void NtGdiDdDDICreateDCFromMemory(void);
void NtGdiDdDDIDestroyDCFromMemory(void);
void NtGdiDeleteObjectApp(void);
void NtGdiDoPalette(void);
void NtGdiEllipse(void);
void NtGdiEndPath(void);
void NtGdiEnumFonts(void);
void NtGdiEqualRgn(void);
void NtGdiExcludeClipRect(void);
void NtGdiExtCreatePen(void);
void NtGdiExtCreateRegion(void);
void NtGdiExtGetObjectW(void);
void NtGdiExtSelectClipRgn(void);
void NtGdiExtTextOutW(void);
void NtGdiFillPath(void);
void NtGdiFillRgn(void);
void NtGdiFlattenPath(void);
void NtGdiFlush(void);
void NtGdiFrameRgn(void);
void NtGdiGetAndSetDCDword(void);
void NtGdiGetAppClipBox(void);
void NtGdiGetBitmapBits(void);
void NtGdiGetCharABCWidthsW(void);
void NtGdiGetCharWidthW(void);
void NtGdiGetDCDword(void);
void NtGdiGetDCObject(void);
void NtGdiGetDCPoint(void);
void NtGdiGetDIBitsInternal(void);
void NtGdiGetDeviceCaps(void);
void NtGdiGetFontData(void);
void NtGdiGetFontUnicodeRanges(void);
void NtGdiGetGlyphIndicesW(void);
void NtGdiGetGlyphOutline(void);
void NtGdiGetMiterLimit(void);
void NtGdiGetOutlineTextMetricsInternalW(void);
void NtGdiGetPath(void);
void NtGdiGetPixel(void);
void NtGdiGetRandomRgn(void);
void NtGdiGetRegionData(void);
void NtGdiGetRgnBox(void);
void NtGdiGetStockObject(void);
void NtGdiGetTextExtentExW(void);
void NtGdiGetTextFaceW(void);
void NtGdiGetTextMetricsW(void);
void NtGdiGetTransform(void);
void NtGdiHfontCreate(void);
void NtGdiIntersectClipRect(void);
void NtGdiInvertRgn(void);
void NtGdiLineTo(void);
void NtGdiMimeAudioControl(void);
void NtGdiMimeAudioCreate(void);
void NtGdiMimeGamma(void);
void NtGdiMimeGfxShare(void);
void NtGdiMimeGpuAddBlock(void);
void NtGdiMimeGpuGetSurface(void);
void NtGdiMimeGpuKick(void);
void NtGdiMimeGpuOpen(void);
void NtGdiMimeGpuWait(void);
void NtGdiMimeSetExtent(void);
void NtGdiMimeVkAlloc(void);
void NtGdiMimeVkFree(void);
void NtGdiMimeVkKick(void);
void NtGdiMimeVkOpen(void);
void NtGdiMimeVkWait(void);
void NtGdiModifyWorldTransform(void);
void NtGdiMoveTo(void);
void NtGdiOffsetRgn(void);
void NtGdiOpenDCW(void);
void NtGdiPatBlt(void);
void NtGdiPathToRegion(void);
void NtGdiPolyPolyDraw(void);
void NtGdiPtInRegion(void);
void NtGdiRectInRegion(void);
void NtGdiRectangle(void);
void NtGdiRemoveFontMemResourceEx(void);
void NtGdiRemoveFontResourceW(void);
void NtGdiRestoreDC(void);
void NtGdiRoundRect(void);
void NtGdiSaveDC(void);
void NtGdiSelectBitmap(void);
void NtGdiSelectBrush(void);
void NtGdiSelectClipPath(void);
void NtGdiSelectFont(void);
void NtGdiSelectPen(void);
void NtGdiSetBitmapBits(void);
void NtGdiSetDIBitsToDeviceInternal(void);
void NtGdiSetMetaRgn(void);
void NtGdiSetMiterLimit(void);
void NtGdiSetPixel(void);
void NtGdiSetPixelFormat(void);
void NtGdiSetRectRgn(void);
void NtGdiSetViewportOrgEx(void);
void NtGdiSetVirtualResolution(void);
void NtGdiSetWindowOrgEx(void);
void NtGdiStretchBlt(void);
void NtGdiStretchDIBitsInternal(void);
void NtGdiStrokeAndFillPath(void);
void NtGdiStrokePath(void);
void NtGdiTransformPoints(void);
void NtGdiWidenPath(void);
void NtUserAddClipboardFormatListener(void);
void NtUserAttachThreadInput(void);
void NtUserBeginPaint(void);
void NtUserBuildHwndList(void);
void NtUserQueryWindow(void);
void NtUserBuildNameList(void);
void NtUserBuildPropList(void);
void NtUserCallHwnd(void);
void NtUserCallHwndParam(void);
void NtUserCallNextHookEx(void);
void NtUserCallNoParam(void);
void NtUserCallOneParam(void);
void NtUserCallTwoParam(void);
void NtUserChangeClipboardChain(void);
void NtUserChangeDisplaySettings(void);
void NtUserCheckMenuItem(void);
void NtUserClipCursor(void);
void NtUserCloseClipboard(void);
void NtUserCloseDesktop(void);
void NtUserCloseWindowStation(void);
void NtUserCountClipboardFormats(void);
void NtUserCreateDesktopEx(void);
void NtUserCreateEmptyCurObject(void);
void NtUserCreateMenu(void);
void NtUserCreatePopupMenu(void);
void NtUserCreateWindowEx(void);
void NtUserCreateWindowStation(void);
void NtUserCreateWindowStationEx(void);
void NtUserDefSetText(void);
void NtUserDeferWindowDpiChanges(void);
void NtUserDeleteMenu(void);
void NtUserDestroyCursor(void);
void NtUserDestroyMenu(void);
void NtUserDestroyWindow(void);
void NtUserDispatchMessage(void);
void NtUserDrawIconEx(void);
void NtUserDrawMenuBar(void);
void NtUserEmptyClipboard(void);
void NtUserEnableChildWindowDpiMessage(void);
void NtUserEnableMenuItem(void);
void NtUserEnableWindow(void);
void NtUserEndMenu(void);
void NtUserEndPaint(void);
void NtUserEnumClipboardFormats(void);
void NtUserEnumDisplaySettings(void);
void NtUserFindWindowEx(void);
void NtUserForceWindowToDpiForTest(void);
void NtUserGetAncestor(void);
void NtUserGetAsyncKeyState(void);
void NtUserGetClassInfoEx(void);
void NtUserGetClassName(void);
void NtUserGetClipCursor(void);
void NtUserGetClipboardData(void);
void NtUserGetClipboardFormatName(void);
void NtUserGetClipboardOwner(void);
void NtUserGetClipboardSequenceNumber(void);
void NtUserGetClipboardViewer(void);
void NtUserGetCursorInfo(void);
void NtUserGetCursorPos(void);
void NtUserGetDC(void);
void NtUserGetDCEx(void);
void NtUserGetForegroundWindow(void);
void NtUserGetGuiResources(void);
void NtUserGetIconInfo(void);
void NtUserGetKeyState(void);
void NtUserGetKeyboardState(void);
void NtUserGetLayeredWindowAttributes(void);
void NtUserGetMessage(void);
void NtUserGetMessagePos(void);
void NtUserGetObjectInformation(void);
void NtUserGetOpenClipboardWindow(void);
void NtUserGetProcessWindowStation(void);
void NtUserGetProp(void);
void NtUserGetRawInputBuffer(void);
void NtUserGetRawInputData(void);
void NtUserGetRawInputDeviceInfo(void);
void NtUserGetRawInputDeviceList(void);
void NtUserGetRegisteredRawInputDevices(void);
void NtUserGetSystemMenu(void);
void NtUserGetThreadDesktop(void);
void NtUserGetThreadState(void);
void NtUserGetUpdateRect(void);
void NtUserGetUpdatedClipboardFormats(void);
void NtUserGetWindowDC(void);
void NtUserGetWindowPlacement(void);
void NtUserGetWindowRgnEx(void);
void NtUserInitializeClientPfnArrays(void);
void NtUserInternalGetWindowText(void);
void NtUserInvalidateRect(void);
void NtUserIsChildWindowDpiMessageEnabled(void);
void NtUserIsClipboardFormatAvailable(void);
void NtUserIsTouchWindow(void);
void NtUserIsWindowBroadcastingDpiToChildren(void);
void NtUserKillTimer(void);
void NtUserMessageBeep(void);
void NtUserMessageCall(void);
void NtUserMimeCallMenu(void);
void NtUserMimeCapture(void);
void NtUserMimeDnd(void);
void NtUserMimeDndData(void);
void NtUserMimeDndProvide(void);
void NtUserMimeExitWindows(void);
void NtUserMimeGetMenuItemInfo(void);
void NtUserMimeIme(void);
void NtUserMimeShell(void);
void NtUserMimeTray(void);
void NtUserMsgWaitForMultipleObjectsEx(void);
void NtUserOpenClipboard(void);
void NtUserOpenDesktop(void);
void NtUserOpenInputDesktop(void);
void NtUserOpenWindowStation(void);
void NtUserPeekMessage(void);
void NtUserPostMessage(void);
void NtUserPostThreadMessage(void);
void NtUserPrepareForLogoff(void);
void NtUserRedrawWindow(void);
void NtUserRegisterClassExWOW(void);
void NtUserRegisterRawInputDevices(void);
void NtUserRegisterWindowMessage(void);
void NtUserReleaseDC(void);
void NtUserRemoveClipboardFormatListener(void);
void NtUserRemoveMenu(void);
void NtUserRemoveProp(void);
void NtUserRestoreWindowDpiChanges(void);
void NtUserSendInput(void);
void NtUserSetActiveWindow(void);
void NtUserSetCapture(void);
void NtUserSetClassLong(void);
void NtUserSetClassLongPtr(void);
void NtUserSetClassWord(void);
void NtUserSetClipboardData(void);
void NtUserSetClipboardViewer(void);
void NtUserSetCursor(void);
void NtUserSetCursorIconData(void);
void NtUserSetDpiForWindow(void);
void NtUserSetFocus(void);
void NtUserSetKeyboardState(void);
void NtUserSetLayeredWindowAttributes(void);
void NtUserSetMenu(void);
void NtUserSetMenuDefaultItem(void);
void NtUserSetObjectInformation(void);
void NtUserSetParent(void);
void NtUserSetProcessWindowStation(void);
void NtUserSetProp(void);
void NtUserSetThreadDesktop(void);
void NtUserSetTimer(void);
void NtUserSetWindowLong(void);
void NtUserSetWindowLongPtr(void);
void NtUserSetWindowPlacement(void);
void NtUserSetWindowPos(void);
void NtUserSetWindowRgn(void);
void NtUserSetWindowsHookEx(void);
void NtUserShowWindow(void);
void NtUserShowWindowAsync(void);
void NtUserShutdownBlockReasonCreate(void);
void NtUserShutdownBlockReasonQuery(void);
void NtUserShutdownReasonDestroy(void);
void NtUserSwitchDesktop(void);
void NtUserSystemParametersInfo(void);
void NtUserSystemParametersInfoForDpi(void);
void NtUserThunkedMenuInfo(void);
void NtUserThunkedMenuItemInfo(void);
void NtUserTrackMouseEvent(void);
void NtUserTranslateMessage(void);
void NtUserUnhookWindowsHookEx(void);
void NtUserUnregisterClass(void);
void NtUserUpdateLayeredWindow(void);
void NtUserValidateRect(void);
void NtUserWaitForInputIdle(void);
void NtUserWaitMessage(void);

void Win32kInitializeServices(SERVICE *table)
{
#ifdef MIME_SVC_NtGdiAbortPath
    table[MIME_SVC_NtGdiAbortPath & 0xFFF] = (SERVICE){(void *)NtGdiAbortPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiAbortPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiAddFontMemResourceEx
    table[MIME_SVC_NtGdiAddFontMemResourceEx & 0xFFF] = (SERVICE){(void *)NtGdiAddFontMemResourceEx, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiAddFontMemResourceEx has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiAddFontResourceW
    table[MIME_SVC_NtGdiAddFontResourceW & 0xFFF] = (SERVICE){(void *)NtGdiAddFontResourceW, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiAddFontResourceW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiAlphaBlend
    table[MIME_SVC_NtGdiAlphaBlend & 0xFFF] = (SERVICE){(void *)NtGdiAlphaBlend, STACK_ARGS(12), 1, 0};
#else
#warning "NtGdiAlphaBlend has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiBeginPath
    table[MIME_SVC_NtGdiBeginPath & 0xFFF] = (SERVICE){(void *)NtGdiBeginPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiBeginPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiBitBlt
    table[MIME_SVC_NtGdiBitBlt & 0xFFF] = (SERVICE){(void *)NtGdiBitBlt, STACK_ARGS(11), 1, 0};
#else
#warning "NtGdiBitBlt has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCloseFigure
    table[MIME_SVC_NtGdiCloseFigure & 0xFFF] = (SERVICE){(void *)NtGdiCloseFigure, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiCloseFigure has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCombineRgn
    table[MIME_SVC_NtGdiCombineRgn & 0xFFF] = (SERVICE){(void *)NtGdiCombineRgn, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiCombineRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateBitmap
    table[MIME_SVC_NtGdiCreateBitmap & 0xFFF] = (SERVICE){(void *)NtGdiCreateBitmap, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiCreateBitmap has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateCompatibleBitmap
    table[MIME_SVC_NtGdiCreateCompatibleBitmap & 0xFFF] = (SERVICE){(void *)NtGdiCreateCompatibleBitmap, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiCreateCompatibleBitmap has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateCompatibleDC
    table[MIME_SVC_NtGdiCreateCompatibleDC & 0xFFF] = (SERVICE){(void *)NtGdiCreateCompatibleDC, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiCreateCompatibleDC has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateDIBSection
    table[MIME_SVC_NtGdiCreateDIBSection & 0xFFF] = (SERVICE){(void *)NtGdiCreateDIBSection, STACK_ARGS(9), 1, 0};
#else
#warning "NtGdiCreateDIBSection has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateMetafileDC
    table[MIME_SVC_NtGdiCreateMetafileDC & 0xFFF] = (SERVICE){(void *)NtGdiCreateMetafileDC, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiCreateMetafileDC has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreatePen
    table[MIME_SVC_NtGdiCreatePen & 0xFFF] = (SERVICE){(void *)NtGdiCreatePen, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiCreatePen has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateRectRgn
    table[MIME_SVC_NtGdiCreateRectRgn & 0xFFF] = (SERVICE){(void *)NtGdiCreateRectRgn, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiCreateRectRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateRoundRectRgn
    table[MIME_SVC_NtGdiCreateRoundRectRgn & 0xFFF] = (SERVICE){(void *)NtGdiCreateRoundRectRgn, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiCreateRoundRectRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateSolidBrush
    table[MIME_SVC_NtGdiCreateSolidBrush & 0xFFF] = (SERVICE){(void *)NtGdiCreateSolidBrush, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiCreateSolidBrush has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDdDDICreateDCFromMemory
    table[MIME_SVC_NtGdiDdDDICreateDCFromMemory & 0xFFF] = (SERVICE){(void *)NtGdiDdDDICreateDCFromMemory, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDdDDICreateDCFromMemory has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDdDDIDestroyDCFromMemory
    table[MIME_SVC_NtGdiDdDDIDestroyDCFromMemory & 0xFFF] = (SERVICE){(void *)NtGdiDdDDIDestroyDCFromMemory, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDdDDIDestroyDCFromMemory has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDeleteObjectApp
    table[MIME_SVC_NtGdiDeleteObjectApp & 0xFFF] = (SERVICE){(void *)NtGdiDeleteObjectApp, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDeleteObjectApp has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDoPalette
    table[MIME_SVC_NtGdiDoPalette & 0xFFF] = (SERVICE){(void *)NtGdiDoPalette, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiDoPalette has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiEllipse
    table[MIME_SVC_NtGdiEllipse & 0xFFF] = (SERVICE){(void *)NtGdiEllipse, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiEllipse has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiEndPath
    table[MIME_SVC_NtGdiEndPath & 0xFFF] = (SERVICE){(void *)NtGdiEndPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiEndPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiEnumFonts
    table[MIME_SVC_NtGdiEnumFonts & 0xFFF] = (SERVICE){(void *)NtGdiEnumFonts, STACK_ARGS(8), 1, 0};
#else
#warning "NtGdiEnumFonts has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiEqualRgn
    table[MIME_SVC_NtGdiEqualRgn & 0xFFF] = (SERVICE){(void *)NtGdiEqualRgn, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiEqualRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiExcludeClipRect
    table[MIME_SVC_NtGdiExcludeClipRect & 0xFFF] = (SERVICE){(void *)NtGdiExcludeClipRect, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiExcludeClipRect has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiExtCreatePen
    table[MIME_SVC_NtGdiExtCreatePen & 0xFFF] = (SERVICE){(void *)NtGdiExtCreatePen, STACK_ARGS(7), 1, 0};
#else
#warning "NtGdiExtCreatePen has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiExtCreateRegion
    table[MIME_SVC_NtGdiExtCreateRegion & 0xFFF] = (SERVICE){(void *)NtGdiExtCreateRegion, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiExtCreateRegion has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiExtGetObjectW
    table[MIME_SVC_NtGdiExtGetObjectW & 0xFFF] = (SERVICE){(void *)NtGdiExtGetObjectW, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiExtGetObjectW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiExtSelectClipRgn
    table[MIME_SVC_NtGdiExtSelectClipRgn & 0xFFF] = (SERVICE){(void *)NtGdiExtSelectClipRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiExtSelectClipRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiExtTextOutW
    table[MIME_SVC_NtGdiExtTextOutW & 0xFFF] = (SERVICE){(void *)NtGdiExtTextOutW, STACK_ARGS(9), 1, 0};
#else
#warning "NtGdiExtTextOutW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiFillPath
    table[MIME_SVC_NtGdiFillPath & 0xFFF] = (SERVICE){(void *)NtGdiFillPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiFillPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiFillRgn
    table[MIME_SVC_NtGdiFillRgn & 0xFFF] = (SERVICE){(void *)NtGdiFillRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiFillRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiFlattenPath
    table[MIME_SVC_NtGdiFlattenPath & 0xFFF] = (SERVICE){(void *)NtGdiFlattenPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiFlattenPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiFlush
    table[MIME_SVC_NtGdiFlush & 0xFFF] = (SERVICE){(void *)NtGdiFlush, STACK_ARGS(0), 1, 0};
#else
#warning "NtGdiFlush has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiFrameRgn
    table[MIME_SVC_NtGdiFrameRgn & 0xFFF] = (SERVICE){(void *)NtGdiFrameRgn, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiFrameRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetAndSetDCDword
    table[MIME_SVC_NtGdiGetAndSetDCDword & 0xFFF] = (SERVICE){(void *)NtGdiGetAndSetDCDword, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiGetAndSetDCDword has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetAppClipBox
    table[MIME_SVC_NtGdiGetAppClipBox & 0xFFF] = (SERVICE){(void *)NtGdiGetAppClipBox, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetAppClipBox has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetBitmapBits
    table[MIME_SVC_NtGdiGetBitmapBits & 0xFFF] = (SERVICE){(void *)NtGdiGetBitmapBits, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetBitmapBits has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetCharABCWidthsW
    table[MIME_SVC_NtGdiGetCharABCWidthsW & 0xFFF] = (SERVICE){(void *)NtGdiGetCharABCWidthsW, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiGetCharABCWidthsW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetCharWidthW
    table[MIME_SVC_NtGdiGetCharWidthW & 0xFFF] = (SERVICE){(void *)NtGdiGetCharWidthW, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiGetCharWidthW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDCDword
    table[MIME_SVC_NtGdiGetDCDword & 0xFFF] = (SERVICE){(void *)NtGdiGetDCDword, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetDCDword has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDCObject
    table[MIME_SVC_NtGdiGetDCObject & 0xFFF] = (SERVICE){(void *)NtGdiGetDCObject, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetDCObject has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDCPoint
    table[MIME_SVC_NtGdiGetDCPoint & 0xFFF] = (SERVICE){(void *)NtGdiGetDCPoint, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetDCPoint has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDIBitsInternal
    table[MIME_SVC_NtGdiGetDIBitsInternal & 0xFFF] = (SERVICE){(void *)NtGdiGetDIBitsInternal, STACK_ARGS(9), 1, 0};
#else
#warning "NtGdiGetDIBitsInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDeviceCaps
    table[MIME_SVC_NtGdiGetDeviceCaps & 0xFFF] = (SERVICE){(void *)NtGdiGetDeviceCaps, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetDeviceCaps has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetFontData
    table[MIME_SVC_NtGdiGetFontData & 0xFFF] = (SERVICE){(void *)NtGdiGetFontData, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiGetFontData has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetFontUnicodeRanges
    table[MIME_SVC_NtGdiGetFontUnicodeRanges & 0xFFF] = (SERVICE){(void *)NtGdiGetFontUnicodeRanges, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetFontUnicodeRanges has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetGlyphIndicesW
    table[MIME_SVC_NtGdiGetGlyphIndicesW & 0xFFF] = (SERVICE){(void *)NtGdiGetGlyphIndicesW, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiGetGlyphIndicesW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetGlyphOutline
    table[MIME_SVC_NtGdiGetGlyphOutline & 0xFFF] = (SERVICE){(void *)NtGdiGetGlyphOutline, STACK_ARGS(8), 1, 0};
#else
#warning "NtGdiGetGlyphOutline has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetMiterLimit
    table[MIME_SVC_NtGdiGetMiterLimit & 0xFFF] = (SERVICE){(void *)NtGdiGetMiterLimit, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetMiterLimit has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetOutlineTextMetricsInternalW
    table[MIME_SVC_NtGdiGetOutlineTextMetricsInternalW & 0xFFF] = (SERVICE){(void *)NtGdiGetOutlineTextMetricsInternalW, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiGetOutlineTextMetricsInternalW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetPath
    table[MIME_SVC_NtGdiGetPath & 0xFFF] = (SERVICE){(void *)NtGdiGetPath, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiGetPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetPixel
    table[MIME_SVC_NtGdiGetPixel & 0xFFF] = (SERVICE){(void *)NtGdiGetPixel, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetPixel has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetRandomRgn
    table[MIME_SVC_NtGdiGetRandomRgn & 0xFFF] = (SERVICE){(void *)NtGdiGetRandomRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetRandomRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetRegionData
    table[MIME_SVC_NtGdiGetRegionData & 0xFFF] = (SERVICE){(void *)NtGdiGetRegionData, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetRegionData has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetRgnBox
    table[MIME_SVC_NtGdiGetRgnBox & 0xFFF] = (SERVICE){(void *)NtGdiGetRgnBox, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetRgnBox has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetStockObject
    table[MIME_SVC_NtGdiGetStockObject & 0xFFF] = (SERVICE){(void *)NtGdiGetStockObject, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiGetStockObject has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetTextExtentExW
    table[MIME_SVC_NtGdiGetTextExtentExW & 0xFFF] = (SERVICE){(void *)NtGdiGetTextExtentExW, STACK_ARGS(8), 1, 0};
#else
#warning "NtGdiGetTextExtentExW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetTextFaceW
    table[MIME_SVC_NtGdiGetTextFaceW & 0xFFF] = (SERVICE){(void *)NtGdiGetTextFaceW, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiGetTextFaceW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetTextMetricsW
    table[MIME_SVC_NtGdiGetTextMetricsW & 0xFFF] = (SERVICE){(void *)NtGdiGetTextMetricsW, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetTextMetricsW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetTransform
    table[MIME_SVC_NtGdiGetTransform & 0xFFF] = (SERVICE){(void *)NtGdiGetTransform, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetTransform has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiHfontCreate
    table[MIME_SVC_NtGdiHfontCreate & 0xFFF] = (SERVICE){(void *)NtGdiHfontCreate, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiHfontCreate has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiIntersectClipRect
    table[MIME_SVC_NtGdiIntersectClipRect & 0xFFF] = (SERVICE){(void *)NtGdiIntersectClipRect, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiIntersectClipRect has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiInvertRgn
    table[MIME_SVC_NtGdiInvertRgn & 0xFFF] = (SERVICE){(void *)NtGdiInvertRgn, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiInvertRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiLineTo
    table[MIME_SVC_NtGdiLineTo & 0xFFF] = (SERVICE){(void *)NtGdiLineTo, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiLineTo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeAudioControl
    table[MIME_SVC_NtGdiMimeAudioControl & 0xFFF] = (SERVICE){(void *)NtGdiMimeAudioControl, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeAudioControl has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeAudioCreate
    table[MIME_SVC_NtGdiMimeAudioCreate & 0xFFF] = (SERVICE){(void *)NtGdiMimeAudioCreate, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeAudioCreate has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGamma
    table[MIME_SVC_NtGdiMimeGamma & 0xFFF] = (SERVICE){(void *)NtGdiMimeGamma, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeGamma has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGfxShare
    table[MIME_SVC_NtGdiMimeGfxShare & 0xFFF] = (SERVICE){(void *)NtGdiMimeGfxShare, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeGfxShare has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGpuAddBlock
    table[MIME_SVC_NtGdiMimeGpuAddBlock & 0xFFF] = (SERVICE){(void *)NtGdiMimeGpuAddBlock, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiMimeGpuAddBlock has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGpuGetSurface
    table[MIME_SVC_NtGdiMimeGpuGetSurface & 0xFFF] = (SERVICE){(void *)NtGdiMimeGpuGetSurface, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeGpuGetSurface has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGpuKick
    table[MIME_SVC_NtGdiMimeGpuKick & 0xFFF] = (SERVICE){(void *)NtGdiMimeGpuKick, STACK_ARGS(0), 1, 0};
#else
#warning "NtGdiMimeGpuKick has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGpuOpen
    table[MIME_SVC_NtGdiMimeGpuOpen & 0xFFF] = (SERVICE){(void *)NtGdiMimeGpuOpen, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeGpuOpen has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeGpuWait
    table[MIME_SVC_NtGdiMimeGpuWait & 0xFFF] = (SERVICE){(void *)NtGdiMimeGpuWait, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeGpuWait has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeSetExtent
    table[MIME_SVC_NtGdiMimeSetExtent & 0xFFF] = (SERVICE){(void *)NtGdiMimeSetExtent, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiMimeSetExtent has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeVkAlloc
    table[MIME_SVC_NtGdiMimeVkAlloc & 0xFFF] = (SERVICE){(void *)NtGdiMimeVkAlloc, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiMimeVkAlloc has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeVkFree
    table[MIME_SVC_NtGdiMimeVkFree & 0xFFF] = (SERVICE){(void *)NtGdiMimeVkFree, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiMimeVkFree has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeVkKick
    table[MIME_SVC_NtGdiMimeVkKick & 0xFFF] = (SERVICE){(void *)NtGdiMimeVkKick, STACK_ARGS(0), 1, 0};
#else
#warning "NtGdiMimeVkKick has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeVkOpen
    table[MIME_SVC_NtGdiMimeVkOpen & 0xFFF] = (SERVICE){(void *)NtGdiMimeVkOpen, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiMimeVkOpen has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMimeVkWait
    table[MIME_SVC_NtGdiMimeVkWait & 0xFFF] = (SERVICE){(void *)NtGdiMimeVkWait, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiMimeVkWait has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiModifyWorldTransform
    table[MIME_SVC_NtGdiModifyWorldTransform & 0xFFF] = (SERVICE){(void *)NtGdiModifyWorldTransform, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiModifyWorldTransform has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMoveTo
    table[MIME_SVC_NtGdiMoveTo & 0xFFF] = (SERVICE){(void *)NtGdiMoveTo, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiMoveTo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiOffsetRgn
    table[MIME_SVC_NtGdiOffsetRgn & 0xFFF] = (SERVICE){(void *)NtGdiOffsetRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiOffsetRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiOpenDCW
    table[MIME_SVC_NtGdiOpenDCW & 0xFFF] = (SERVICE){(void *)NtGdiOpenDCW, STACK_ARGS(8), 1, 0};
#else
#warning "NtGdiOpenDCW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPatBlt
    table[MIME_SVC_NtGdiPatBlt & 0xFFF] = (SERVICE){(void *)NtGdiPatBlt, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiPatBlt has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPathToRegion
    table[MIME_SVC_NtGdiPathToRegion & 0xFFF] = (SERVICE){(void *)NtGdiPathToRegion, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiPathToRegion has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPolyPolyDraw
    table[MIME_SVC_NtGdiPolyPolyDraw & 0xFFF] = (SERVICE){(void *)NtGdiPolyPolyDraw, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiPolyPolyDraw has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPtInRegion
    table[MIME_SVC_NtGdiPtInRegion & 0xFFF] = (SERVICE){(void *)NtGdiPtInRegion, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiPtInRegion has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRectInRegion
    table[MIME_SVC_NtGdiRectInRegion & 0xFFF] = (SERVICE){(void *)NtGdiRectInRegion, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiRectInRegion has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRectangle
    table[MIME_SVC_NtGdiRectangle & 0xFFF] = (SERVICE){(void *)NtGdiRectangle, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiRectangle has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRemoveFontMemResourceEx
    table[MIME_SVC_NtGdiRemoveFontMemResourceEx & 0xFFF] = (SERVICE){(void *)NtGdiRemoveFontMemResourceEx, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiRemoveFontMemResourceEx has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRemoveFontResourceW
    table[MIME_SVC_NtGdiRemoveFontResourceW & 0xFFF] = (SERVICE){(void *)NtGdiRemoveFontResourceW, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiRemoveFontResourceW has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRestoreDC
    table[MIME_SVC_NtGdiRestoreDC & 0xFFF] = (SERVICE){(void *)NtGdiRestoreDC, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiRestoreDC has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRoundRect
    table[MIME_SVC_NtGdiRoundRect & 0xFFF] = (SERVICE){(void *)NtGdiRoundRect, STACK_ARGS(7), 1, 0};
#else
#warning "NtGdiRoundRect has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSaveDC
    table[MIME_SVC_NtGdiSaveDC & 0xFFF] = (SERVICE){(void *)NtGdiSaveDC, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiSaveDC has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSelectBitmap
    table[MIME_SVC_NtGdiSelectBitmap & 0xFFF] = (SERVICE){(void *)NtGdiSelectBitmap, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSelectBitmap has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSelectBrush
    table[MIME_SVC_NtGdiSelectBrush & 0xFFF] = (SERVICE){(void *)NtGdiSelectBrush, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSelectBrush has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSelectClipPath
    table[MIME_SVC_NtGdiSelectClipPath & 0xFFF] = (SERVICE){(void *)NtGdiSelectClipPath, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSelectClipPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSelectFont
    table[MIME_SVC_NtGdiSelectFont & 0xFFF] = (SERVICE){(void *)NtGdiSelectFont, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSelectFont has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSelectPen
    table[MIME_SVC_NtGdiSelectPen & 0xFFF] = (SERVICE){(void *)NtGdiSelectPen, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSelectPen has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetBitmapBits
    table[MIME_SVC_NtGdiSetBitmapBits & 0xFFF] = (SERVICE){(void *)NtGdiSetBitmapBits, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiSetBitmapBits has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetDIBitsToDeviceInternal
    table[MIME_SVC_NtGdiSetDIBitsToDeviceInternal & 0xFFF] = (SERVICE){(void *)NtGdiSetDIBitsToDeviceInternal, STACK_ARGS(16), 1, 0};
#else
#warning "NtGdiSetDIBitsToDeviceInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetMetaRgn
    table[MIME_SVC_NtGdiSetMetaRgn & 0xFFF] = (SERVICE){(void *)NtGdiSetMetaRgn, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiSetMetaRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetMiterLimit
    table[MIME_SVC_NtGdiSetMiterLimit & 0xFFF] = (SERVICE){(void *)NtGdiSetMiterLimit, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiSetMiterLimit has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetPixel
    table[MIME_SVC_NtGdiSetPixel & 0xFFF] = (SERVICE){(void *)NtGdiSetPixel, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiSetPixel has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetPixelFormat
    table[MIME_SVC_NtGdiSetPixelFormat & 0xFFF] = (SERVICE){(void *)NtGdiSetPixelFormat, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSetPixelFormat has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetRectRgn
    table[MIME_SVC_NtGdiSetRectRgn & 0xFFF] = (SERVICE){(void *)NtGdiSetRectRgn, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiSetRectRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetViewportOrgEx
    table[MIME_SVC_NtGdiSetViewportOrgEx & 0xFFF] = (SERVICE){(void *)NtGdiSetViewportOrgEx, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiSetViewportOrgEx has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetVirtualResolution
    table[MIME_SVC_NtGdiSetVirtualResolution & 0xFFF] = (SERVICE){(void *)NtGdiSetVirtualResolution, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiSetVirtualResolution has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetWindowOrgEx
    table[MIME_SVC_NtGdiSetWindowOrgEx & 0xFFF] = (SERVICE){(void *)NtGdiSetWindowOrgEx, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiSetWindowOrgEx has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiStretchBlt
    table[MIME_SVC_NtGdiStretchBlt & 0xFFF] = (SERVICE){(void *)NtGdiStretchBlt, STACK_ARGS(12), 1, 0};
#else
#warning "NtGdiStretchBlt has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiStretchDIBitsInternal
    table[MIME_SVC_NtGdiStretchDIBitsInternal & 0xFFF] = (SERVICE){(void *)NtGdiStretchDIBitsInternal, STACK_ARGS(16), 1, 0};
#else
#warning "NtGdiStretchDIBitsInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiStrokeAndFillPath
    table[MIME_SVC_NtGdiStrokeAndFillPath & 0xFFF] = (SERVICE){(void *)NtGdiStrokeAndFillPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiStrokeAndFillPath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiStrokePath
    table[MIME_SVC_NtGdiStrokePath & 0xFFF] = (SERVICE){(void *)NtGdiStrokePath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiStrokePath has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiTransformPoints
    table[MIME_SVC_NtGdiTransformPoints & 0xFFF] = (SERVICE){(void *)NtGdiTransformPoints, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiTransformPoints has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiWidenPath
    table[MIME_SVC_NtGdiWidenPath & 0xFFF] = (SERVICE){(void *)NtGdiWidenPath, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiWidenPath has no syscall number"
#endif
#ifdef MIME_SVC_NtUserAddClipboardFormatListener
    table[MIME_SVC_NtUserAddClipboardFormatListener & 0xFFF] = (SERVICE){(void *)NtUserAddClipboardFormatListener, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserAddClipboardFormatListener has no syscall number"
#endif
#ifdef MIME_SVC_NtUserAttachThreadInput
    table[MIME_SVC_NtUserAttachThreadInput & 0xFFF] = (SERVICE){(void *)NtUserAttachThreadInput, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserAttachThreadInput has no syscall number"
#endif
#ifdef MIME_SVC_NtUserBeginPaint
    table[MIME_SVC_NtUserBeginPaint & 0xFFF] = (SERVICE){(void *)NtUserBeginPaint, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserBeginPaint has no syscall number"
#endif
#ifdef MIME_SVC_NtUserQueryWindow
    table[MIME_SVC_NtUserQueryWindow & 0xFFF] = (SERVICE){(void *)NtUserQueryWindow, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserQueryWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserBuildHwndList
    table[MIME_SVC_NtUserBuildHwndList & 0xFFF] = (SERVICE){(void *)NtUserBuildHwndList, STACK_ARGS(8), 1, 0};
#else
#warning "NtUserBuildHwndList has no syscall number"
#endif
#ifdef MIME_SVC_NtUserBuildNameList
    table[MIME_SVC_NtUserBuildNameList & 0xFFF] = (SERVICE){(void *)NtUserBuildNameList, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserBuildNameList has no syscall number"
#endif
#ifdef MIME_SVC_NtUserBuildPropList
    table[MIME_SVC_NtUserBuildPropList & 0xFFF] = (SERVICE){(void *)NtUserBuildPropList, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserBuildPropList has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCallHwnd
    table[MIME_SVC_NtUserCallHwnd & 0xFFF] = (SERVICE){(void *)NtUserCallHwnd, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserCallHwnd has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCallHwndParam
    table[MIME_SVC_NtUserCallHwndParam & 0xFFF] = (SERVICE){(void *)NtUserCallHwndParam, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserCallHwndParam has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCallNextHookEx
    table[MIME_SVC_NtUserCallNextHookEx & 0xFFF] = (SERVICE){(void *)NtUserCallNextHookEx, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserCallNextHookEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCallNoParam
    table[MIME_SVC_NtUserCallNoParam & 0xFFF] = (SERVICE){(void *)NtUserCallNoParam, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserCallNoParam has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCallOneParam
    table[MIME_SVC_NtUserCallOneParam & 0xFFF] = (SERVICE){(void *)NtUserCallOneParam, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserCallOneParam has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCallTwoParam
    table[MIME_SVC_NtUserCallTwoParam & 0xFFF] = (SERVICE){(void *)NtUserCallTwoParam, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserCallTwoParam has no syscall number"
#endif
#ifdef MIME_SVC_NtUserChangeClipboardChain
    table[MIME_SVC_NtUserChangeClipboardChain & 0xFFF] = (SERVICE){(void *)NtUserChangeClipboardChain, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserChangeClipboardChain has no syscall number"
#endif
#ifdef MIME_SVC_NtUserChangeDisplaySettings
    table[MIME_SVC_NtUserChangeDisplaySettings & 0xFFF] = (SERVICE){(void *)NtUserChangeDisplaySettings, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserChangeDisplaySettings has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCheckMenuItem
    table[MIME_SVC_NtUserCheckMenuItem & 0xFFF] = (SERVICE){(void *)NtUserCheckMenuItem, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserCheckMenuItem has no syscall number"
#endif
#ifdef MIME_SVC_NtUserClipCursor
    table[MIME_SVC_NtUserClipCursor & 0xFFF] = (SERVICE){(void *)NtUserClipCursor, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserClipCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCloseClipboard
    table[MIME_SVC_NtUserCloseClipboard & 0xFFF] = (SERVICE){(void *)NtUserCloseClipboard, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserCloseClipboard has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCloseDesktop
    table[MIME_SVC_NtUserCloseDesktop & 0xFFF] = (SERVICE){(void *)NtUserCloseDesktop, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserCloseDesktop has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCloseWindowStation
    table[MIME_SVC_NtUserCloseWindowStation & 0xFFF] = (SERVICE){(void *)NtUserCloseWindowStation, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserCloseWindowStation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCountClipboardFormats
    table[MIME_SVC_NtUserCountClipboardFormats & 0xFFF] = (SERVICE){(void *)NtUserCountClipboardFormats, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserCountClipboardFormats has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateDesktopEx
    table[MIME_SVC_NtUserCreateDesktopEx & 0xFFF] = (SERVICE){(void *)NtUserCreateDesktopEx, STACK_ARGS(6), 1, 0};
#else
#warning "NtUserCreateDesktopEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateEmptyCurObject
    table[MIME_SVC_NtUserCreateEmptyCurObject & 0xFFF] = (SERVICE){(void *)NtUserCreateEmptyCurObject, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserCreateEmptyCurObject has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateMenu
    table[MIME_SVC_NtUserCreateMenu & 0xFFF] = (SERVICE){(void *)NtUserCreateMenu, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserCreateMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreatePopupMenu
    table[MIME_SVC_NtUserCreatePopupMenu & 0xFFF] = (SERVICE){(void *)NtUserCreatePopupMenu, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserCreatePopupMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateWindowEx
    table[MIME_SVC_NtUserCreateWindowEx & 0xFFF] = (SERVICE){(void *)NtUserCreateWindowEx, STACK_ARGS(13), 1, 0};
#else
#warning "NtUserCreateWindowEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateWindowStation
    table[MIME_SVC_NtUserCreateWindowStation & 0xFFF] = (SERVICE){(void *)NtUserCreateWindowStation, STACK_ARGS(7), 1, 0};
#else
#warning "NtUserCreateWindowStation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateWindowStationEx
    table[MIME_SVC_NtUserCreateWindowStationEx & 0xFFF] = (SERVICE){(void *)NtUserCreateWindowStationEx, STACK_ARGS(7), 1, 0};
#else
#warning "NtUserCreateWindowStationEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDefSetText
    table[MIME_SVC_NtUserDefSetText & 0xFFF] = (SERVICE){(void *)NtUserDefSetText, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserDefSetText has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDeferWindowDpiChanges
    table[MIME_SVC_NtUserDeferWindowDpiChanges & 0xFFF] = (SERVICE){(void *)NtUserDeferWindowDpiChanges, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDeferWindowDpiChanges has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDeleteMenu
    table[MIME_SVC_NtUserDeleteMenu & 0xFFF] = (SERVICE){(void *)NtUserDeleteMenu, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserDeleteMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDestroyCursor
    table[MIME_SVC_NtUserDestroyCursor & 0xFFF] = (SERVICE){(void *)NtUserDestroyCursor, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserDestroyCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDestroyMenu
    table[MIME_SVC_NtUserDestroyMenu & 0xFFF] = (SERVICE){(void *)NtUserDestroyMenu, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDestroyMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDestroyWindow
    table[MIME_SVC_NtUserDestroyWindow & 0xFFF] = (SERVICE){(void *)NtUserDestroyWindow, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDestroyWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDispatchMessage
    table[MIME_SVC_NtUserDispatchMessage & 0xFFF] = (SERVICE){(void *)NtUserDispatchMessage, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDispatchMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDrawIconEx
    table[MIME_SVC_NtUserDrawIconEx & 0xFFF] = (SERVICE){(void *)NtUserDrawIconEx, STACK_ARGS(11), 1, 0};
#else
#warning "NtUserDrawIconEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDrawMenuBar
    table[MIME_SVC_NtUserDrawMenuBar & 0xFFF] = (SERVICE){(void *)NtUserDrawMenuBar, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDrawMenuBar has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEmptyClipboard
    table[MIME_SVC_NtUserEmptyClipboard & 0xFFF] = (SERVICE){(void *)NtUserEmptyClipboard, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserEmptyClipboard has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnableChildWindowDpiMessage
    table[MIME_SVC_NtUserEnableChildWindowDpiMessage & 0xFFF] = (SERVICE){(void *)NtUserEnableChildWindowDpiMessage, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserEnableChildWindowDpiMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnableMenuItem
    table[MIME_SVC_NtUserEnableMenuItem & 0xFFF] = (SERVICE){(void *)NtUserEnableMenuItem, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserEnableMenuItem has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnableWindow
    table[MIME_SVC_NtUserEnableWindow & 0xFFF] = (SERVICE){(void *)NtUserEnableWindow, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserEnableWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEndMenu
    table[MIME_SVC_NtUserEndMenu & 0xFFF] = (SERVICE){(void *)NtUserEndMenu, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserEndMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEndPaint
    table[MIME_SVC_NtUserEndPaint & 0xFFF] = (SERVICE){(void *)NtUserEndPaint, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserEndPaint has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnumClipboardFormats
    table[MIME_SVC_NtUserEnumClipboardFormats & 0xFFF] = (SERVICE){(void *)NtUserEnumClipboardFormats, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserEnumClipboardFormats has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnumDisplaySettings
    table[MIME_SVC_NtUserEnumDisplaySettings & 0xFFF] = (SERVICE){(void *)NtUserEnumDisplaySettings, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserEnumDisplaySettings has no syscall number"
#endif
#ifdef MIME_SVC_NtUserFindWindowEx
    table[MIME_SVC_NtUserFindWindowEx & 0xFFF] = (SERVICE){(void *)NtUserFindWindowEx, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserFindWindowEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserForceWindowToDpiForTest
    table[MIME_SVC_NtUserForceWindowToDpiForTest & 0xFFF] = (SERVICE){(void *)NtUserForceWindowToDpiForTest, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserForceWindowToDpiForTest has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetAncestor
    table[MIME_SVC_NtUserGetAncestor & 0xFFF] = (SERVICE){(void *)NtUserGetAncestor, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetAncestor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetAsyncKeyState
    table[MIME_SVC_NtUserGetAsyncKeyState & 0xFFF] = (SERVICE){(void *)NtUserGetAsyncKeyState, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetAsyncKeyState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClassInfoEx
    table[MIME_SVC_NtUserGetClassInfoEx & 0xFFF] = (SERVICE){(void *)NtUserGetClassInfoEx, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserGetClassInfoEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClassName
    table[MIME_SVC_NtUserGetClassName & 0xFFF] = (SERVICE){(void *)NtUserGetClassName, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetClassName has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClipCursor
    table[MIME_SVC_NtUserGetClipCursor & 0xFFF] = (SERVICE){(void *)NtUserGetClipCursor, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetClipCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClipboardData
    table[MIME_SVC_NtUserGetClipboardData & 0xFFF] = (SERVICE){(void *)NtUserGetClipboardData, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetClipboardData has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClipboardFormatName
    table[MIME_SVC_NtUserGetClipboardFormatName & 0xFFF] = (SERVICE){(void *)NtUserGetClipboardFormatName, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetClipboardFormatName has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClipboardOwner
    table[MIME_SVC_NtUserGetClipboardOwner & 0xFFF] = (SERVICE){(void *)NtUserGetClipboardOwner, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetClipboardOwner has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClipboardSequenceNumber
    table[MIME_SVC_NtUserGetClipboardSequenceNumber & 0xFFF] = (SERVICE){(void *)NtUserGetClipboardSequenceNumber, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetClipboardSequenceNumber has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClipboardViewer
    table[MIME_SVC_NtUserGetClipboardViewer & 0xFFF] = (SERVICE){(void *)NtUserGetClipboardViewer, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetClipboardViewer has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetCursorInfo
    table[MIME_SVC_NtUserGetCursorInfo & 0xFFF] = (SERVICE){(void *)NtUserGetCursorInfo, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetCursorInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetCursorPos
    table[MIME_SVC_NtUserGetCursorPos & 0xFFF] = (SERVICE){(void *)NtUserGetCursorPos, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetCursorPos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDC
    table[MIME_SVC_NtUserGetDC & 0xFFF] = (SERVICE){(void *)NtUserGetDC, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetDC has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDCEx
    table[MIME_SVC_NtUserGetDCEx & 0xFFF] = (SERVICE){(void *)NtUserGetDCEx, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetDCEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetForegroundWindow
    table[MIME_SVC_NtUserGetForegroundWindow & 0xFFF] = (SERVICE){(void *)NtUserGetForegroundWindow, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetForegroundWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetGuiResources
    table[MIME_SVC_NtUserGetGuiResources & 0xFFF] = (SERVICE){(void *)NtUserGetGuiResources, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetGuiResources has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetIconInfo
    table[MIME_SVC_NtUserGetIconInfo & 0xFFF] = (SERVICE){(void *)NtUserGetIconInfo, STACK_ARGS(6), 1, 0};
#else
#warning "NtUserGetIconInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetKeyState
    table[MIME_SVC_NtUserGetKeyState & 0xFFF] = (SERVICE){(void *)NtUserGetKeyState, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetKeyState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetKeyboardState
    table[MIME_SVC_NtUserGetKeyboardState & 0xFFF] = (SERVICE){(void *)NtUserGetKeyboardState, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetKeyboardState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetLayeredWindowAttributes
    table[MIME_SVC_NtUserGetLayeredWindowAttributes & 0xFFF] = (SERVICE){(void *)NtUserGetLayeredWindowAttributes, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetLayeredWindowAttributes has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetMessage
    table[MIME_SVC_NtUserGetMessage & 0xFFF] = (SERVICE){(void *)NtUserGetMessage, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetMessagePos
    table[MIME_SVC_NtUserGetMessagePos & 0xFFF] = (SERVICE){(void *)NtUserGetMessagePos, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetMessagePos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetObjectInformation
    table[MIME_SVC_NtUserGetObjectInformation & 0xFFF] = (SERVICE){(void *)NtUserGetObjectInformation, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserGetObjectInformation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetOpenClipboardWindow
    table[MIME_SVC_NtUserGetOpenClipboardWindow & 0xFFF] = (SERVICE){(void *)NtUserGetOpenClipboardWindow, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetOpenClipboardWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetProcessWindowStation
    table[MIME_SVC_NtUserGetProcessWindowStation & 0xFFF] = (SERVICE){(void *)NtUserGetProcessWindowStation, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetProcessWindowStation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetProp
    table[MIME_SVC_NtUserGetProp & 0xFFF] = (SERVICE){(void *)NtUserGetProp, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetProp has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetRawInputBuffer
    table[MIME_SVC_NtUserGetRawInputBuffer & 0xFFF] = (SERVICE){(void *)NtUserGetRawInputBuffer, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetRawInputBuffer has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetRawInputData
    table[MIME_SVC_NtUserGetRawInputData & 0xFFF] = (SERVICE){(void *)NtUserGetRawInputData, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserGetRawInputData has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetRawInputDeviceInfo
    table[MIME_SVC_NtUserGetRawInputDeviceInfo & 0xFFF] = (SERVICE){(void *)NtUserGetRawInputDeviceInfo, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetRawInputDeviceInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetRawInputDeviceList
    table[MIME_SVC_NtUserGetRawInputDeviceList & 0xFFF] = (SERVICE){(void *)NtUserGetRawInputDeviceList, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetRawInputDeviceList has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetRegisteredRawInputDevices
    table[MIME_SVC_NtUserGetRegisteredRawInputDevices & 0xFFF] = (SERVICE){(void *)NtUserGetRegisteredRawInputDevices, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetRegisteredRawInputDevices has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetSystemMenu
    table[MIME_SVC_NtUserGetSystemMenu & 0xFFF] = (SERVICE){(void *)NtUserGetSystemMenu, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetSystemMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetThreadDesktop
    table[MIME_SVC_NtUserGetThreadDesktop & 0xFFF] = (SERVICE){(void *)NtUserGetThreadDesktop, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetThreadDesktop has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetThreadState
    table[MIME_SVC_NtUserGetThreadState & 0xFFF] = (SERVICE){(void *)NtUserGetThreadState, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetThreadState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetUpdateRect
    table[MIME_SVC_NtUserGetUpdateRect & 0xFFF] = (SERVICE){(void *)NtUserGetUpdateRect, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetUpdateRect has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetUpdatedClipboardFormats
    table[MIME_SVC_NtUserGetUpdatedClipboardFormats & 0xFFF] = (SERVICE){(void *)NtUserGetUpdatedClipboardFormats, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetUpdatedClipboardFormats has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetWindowDC
    table[MIME_SVC_NtUserGetWindowDC & 0xFFF] = (SERVICE){(void *)NtUserGetWindowDC, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetWindowDC has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetWindowPlacement
    table[MIME_SVC_NtUserGetWindowPlacement & 0xFFF] = (SERVICE){(void *)NtUserGetWindowPlacement, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetWindowPlacement has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetWindowRgnEx
    table[MIME_SVC_NtUserGetWindowRgnEx & 0xFFF] = (SERVICE){(void *)NtUserGetWindowRgnEx, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetWindowRgnEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserInitializeClientPfnArrays
    table[MIME_SVC_NtUserInitializeClientPfnArrays & 0xFFF] = (SERVICE){(void *)NtUserInitializeClientPfnArrays, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserInitializeClientPfnArrays has no syscall number"
#endif
#ifdef MIME_SVC_NtUserInternalGetWindowText
    table[MIME_SVC_NtUserInternalGetWindowText & 0xFFF] = (SERVICE){(void *)NtUserInternalGetWindowText, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserInternalGetWindowText has no syscall number"
#endif
#ifdef MIME_SVC_NtUserInvalidateRect
    table[MIME_SVC_NtUserInvalidateRect & 0xFFF] = (SERVICE){(void *)NtUserInvalidateRect, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserInvalidateRect has no syscall number"
#endif
#ifdef MIME_SVC_NtUserIsChildWindowDpiMessageEnabled
    table[MIME_SVC_NtUserIsChildWindowDpiMessageEnabled & 0xFFF] = (SERVICE){(void *)NtUserIsChildWindowDpiMessageEnabled, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserIsChildWindowDpiMessageEnabled has no syscall number"
#endif
#ifdef MIME_SVC_NtUserIsClipboardFormatAvailable
    table[MIME_SVC_NtUserIsClipboardFormatAvailable & 0xFFF] = (SERVICE){(void *)NtUserIsClipboardFormatAvailable, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserIsClipboardFormatAvailable has no syscall number"
#endif
#ifdef MIME_SVC_NtUserIsTouchWindow
    table[MIME_SVC_NtUserIsTouchWindow & 0xFFF] = (SERVICE){(void *)NtUserIsTouchWindow, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserIsTouchWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserIsWindowBroadcastingDpiToChildren
    table[MIME_SVC_NtUserIsWindowBroadcastingDpiToChildren & 0xFFF] = (SERVICE){(void *)NtUserIsWindowBroadcastingDpiToChildren, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserIsWindowBroadcastingDpiToChildren has no syscall number"
#endif
#ifdef MIME_SVC_NtUserKillTimer
    table[MIME_SVC_NtUserKillTimer & 0xFFF] = (SERVICE){(void *)NtUserKillTimer, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserKillTimer has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMessageBeep
    table[MIME_SVC_NtUserMessageBeep & 0xFFF] = (SERVICE){(void *)NtUserMessageBeep, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserMessageBeep has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMessageCall
    table[MIME_SVC_NtUserMessageCall & 0xFFF] = (SERVICE){(void *)NtUserMessageCall, STACK_ARGS(7), 1, 0};
#else
#warning "NtUserMessageCall has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeCallMenu
    table[MIME_SVC_NtUserMimeCallMenu & 0xFFF] = (SERVICE){(void *)NtUserMimeCallMenu, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserMimeCallMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeCapture
    table[MIME_SVC_NtUserMimeCapture & 0xFFF] = (SERVICE){(void *)NtUserMimeCapture, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserMimeCapture has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeDnd
    table[MIME_SVC_NtUserMimeDnd & 0xFFF] = (SERVICE){(void *)NtUserMimeDnd, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserMimeDnd has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeDndData
    table[MIME_SVC_NtUserMimeDndData & 0xFFF] = (SERVICE){(void *)NtUserMimeDndData, STACK_ARGS(6), 1, 0};
#else
#warning "NtUserMimeDndData has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeDndProvide
    table[MIME_SVC_NtUserMimeDndProvide & 0xFFF] = (SERVICE){(void *)NtUserMimeDndProvide, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserMimeDndProvide has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeExitWindows
    table[MIME_SVC_NtUserMimeExitWindows & 0xFFF] = (SERVICE){(void *)NtUserMimeExitWindows, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserMimeExitWindows has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeGetMenuItemInfo
    table[MIME_SVC_NtUserMimeGetMenuItemInfo & 0xFFF] = (SERVICE){(void *)NtUserMimeGetMenuItemInfo, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserMimeGetMenuItemInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeIme
    table[MIME_SVC_NtUserMimeIme & 0xFFF] = (SERVICE){(void *)NtUserMimeIme, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserMimeIme has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeShell
    table[MIME_SVC_NtUserMimeShell & 0xFFF] = (SERVICE){(void *)NtUserMimeShell, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserMimeShell has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMimeTray
    table[MIME_SVC_NtUserMimeTray & 0xFFF] = (SERVICE){(void *)NtUserMimeTray, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserMimeTray has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMsgWaitForMultipleObjectsEx
    table[MIME_SVC_NtUserMsgWaitForMultipleObjectsEx & 0xFFF] = (SERVICE){(void *)NtUserMsgWaitForMultipleObjectsEx, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserMsgWaitForMultipleObjectsEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserOpenClipboard
    table[MIME_SVC_NtUserOpenClipboard & 0xFFF] = (SERVICE){(void *)NtUserOpenClipboard, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserOpenClipboard has no syscall number"
#endif
#ifdef MIME_SVC_NtUserOpenDesktop
    table[MIME_SVC_NtUserOpenDesktop & 0xFFF] = (SERVICE){(void *)NtUserOpenDesktop, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserOpenDesktop has no syscall number"
#endif
#ifdef MIME_SVC_NtUserOpenInputDesktop
    table[MIME_SVC_NtUserOpenInputDesktop & 0xFFF] = (SERVICE){(void *)NtUserOpenInputDesktop, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserOpenInputDesktop has no syscall number"
#endif
#ifdef MIME_SVC_NtUserOpenWindowStation
    table[MIME_SVC_NtUserOpenWindowStation & 0xFFF] = (SERVICE){(void *)NtUserOpenWindowStation, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserOpenWindowStation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserPeekMessage
    table[MIME_SVC_NtUserPeekMessage & 0xFFF] = (SERVICE){(void *)NtUserPeekMessage, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserPeekMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserPostMessage
    table[MIME_SVC_NtUserPostMessage & 0xFFF] = (SERVICE){(void *)NtUserPostMessage, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserPostMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserPostThreadMessage
    table[MIME_SVC_NtUserPostThreadMessage & 0xFFF] = (SERVICE){(void *)NtUserPostThreadMessage, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserPostThreadMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserPrepareForLogoff
    table[MIME_SVC_NtUserPrepareForLogoff & 0xFFF] = (SERVICE){(void *)NtUserPrepareForLogoff, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserPrepareForLogoff has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRedrawWindow
    table[MIME_SVC_NtUserRedrawWindow & 0xFFF] = (SERVICE){(void *)NtUserRedrawWindow, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserRedrawWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRegisterClassExWOW
    table[MIME_SVC_NtUserRegisterClassExWOW & 0xFFF] = (SERVICE){(void *)NtUserRegisterClassExWOW, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserRegisterClassExWOW has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRegisterRawInputDevices
    table[MIME_SVC_NtUserRegisterRawInputDevices & 0xFFF] = (SERVICE){(void *)NtUserRegisterRawInputDevices, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserRegisterRawInputDevices has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRegisterWindowMessage
    table[MIME_SVC_NtUserRegisterWindowMessage & 0xFFF] = (SERVICE){(void *)NtUserRegisterWindowMessage, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserRegisterWindowMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserReleaseDC
    table[MIME_SVC_NtUserReleaseDC & 0xFFF] = (SERVICE){(void *)NtUserReleaseDC, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserReleaseDC has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRemoveClipboardFormatListener
    table[MIME_SVC_NtUserRemoveClipboardFormatListener & 0xFFF] = (SERVICE){(void *)NtUserRemoveClipboardFormatListener, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserRemoveClipboardFormatListener has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRemoveMenu
    table[MIME_SVC_NtUserRemoveMenu & 0xFFF] = (SERVICE){(void *)NtUserRemoveMenu, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserRemoveMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRemoveProp
    table[MIME_SVC_NtUserRemoveProp & 0xFFF] = (SERVICE){(void *)NtUserRemoveProp, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserRemoveProp has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRestoreWindowDpiChanges
    table[MIME_SVC_NtUserRestoreWindowDpiChanges & 0xFFF] = (SERVICE){(void *)NtUserRestoreWindowDpiChanges, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserRestoreWindowDpiChanges has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSendInput
    table[MIME_SVC_NtUserSendInput & 0xFFF] = (SERVICE){(void *)NtUserSendInput, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSendInput has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetActiveWindow
    table[MIME_SVC_NtUserSetActiveWindow & 0xFFF] = (SERVICE){(void *)NtUserSetActiveWindow, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetActiveWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCapture
    table[MIME_SVC_NtUserSetCapture & 0xFFF] = (SERVICE){(void *)NtUserSetCapture, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetCapture has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetClassLong
    table[MIME_SVC_NtUserSetClassLong & 0xFFF] = (SERVICE){(void *)NtUserSetClassLong, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetClassLong has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetClassLongPtr
    table[MIME_SVC_NtUserSetClassLongPtr & 0xFFF] = (SERVICE){(void *)NtUserSetClassLongPtr, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetClassLongPtr has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetClassWord
    table[MIME_SVC_NtUserSetClassWord & 0xFFF] = (SERVICE){(void *)NtUserSetClassWord, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetClassWord has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetClipboardData
    table[MIME_SVC_NtUserSetClipboardData & 0xFFF] = (SERVICE){(void *)NtUserSetClipboardData, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetClipboardData has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetClipboardViewer
    table[MIME_SVC_NtUserSetClipboardViewer & 0xFFF] = (SERVICE){(void *)NtUserSetClipboardViewer, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetClipboardViewer has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCursor
    table[MIME_SVC_NtUserSetCursor & 0xFFF] = (SERVICE){(void *)NtUserSetCursor, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCursorIconData
    table[MIME_SVC_NtUserSetCursorIconData & 0xFFF] = (SERVICE){(void *)NtUserSetCursorIconData, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetCursorIconData has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetDpiForWindow
    table[MIME_SVC_NtUserSetDpiForWindow & 0xFFF] = (SERVICE){(void *)NtUserSetDpiForWindow, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetDpiForWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetFocus
    table[MIME_SVC_NtUserSetFocus & 0xFFF] = (SERVICE){(void *)NtUserSetFocus, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetFocus has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetKeyboardState
    table[MIME_SVC_NtUserSetKeyboardState & 0xFFF] = (SERVICE){(void *)NtUserSetKeyboardState, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetKeyboardState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetLayeredWindowAttributes
    table[MIME_SVC_NtUserSetLayeredWindowAttributes & 0xFFF] = (SERVICE){(void *)NtUserSetLayeredWindowAttributes, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetLayeredWindowAttributes has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetMenu
    table[MIME_SVC_NtUserSetMenu & 0xFFF] = (SERVICE){(void *)NtUserSetMenu, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetMenuDefaultItem
    table[MIME_SVC_NtUserSetMenuDefaultItem & 0xFFF] = (SERVICE){(void *)NtUserSetMenuDefaultItem, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetMenuDefaultItem has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetObjectInformation
    table[MIME_SVC_NtUserSetObjectInformation & 0xFFF] = (SERVICE){(void *)NtUserSetObjectInformation, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetObjectInformation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetParent
    table[MIME_SVC_NtUserSetParent & 0xFFF] = (SERVICE){(void *)NtUserSetParent, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetParent has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetProcessWindowStation
    table[MIME_SVC_NtUserSetProcessWindowStation & 0xFFF] = (SERVICE){(void *)NtUserSetProcessWindowStation, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetProcessWindowStation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetProp
    table[MIME_SVC_NtUserSetProp & 0xFFF] = (SERVICE){(void *)NtUserSetProp, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetProp has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetThreadDesktop
    table[MIME_SVC_NtUserSetThreadDesktop & 0xFFF] = (SERVICE){(void *)NtUserSetThreadDesktop, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetThreadDesktop has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetTimer
    table[MIME_SVC_NtUserSetTimer & 0xFFF] = (SERVICE){(void *)NtUserSetTimer, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserSetTimer has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowLong
    table[MIME_SVC_NtUserSetWindowLong & 0xFFF] = (SERVICE){(void *)NtUserSetWindowLong, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetWindowLong has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowLongPtr
    table[MIME_SVC_NtUserSetWindowLongPtr & 0xFFF] = (SERVICE){(void *)NtUserSetWindowLongPtr, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetWindowLongPtr has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowPlacement
    table[MIME_SVC_NtUserSetWindowPlacement & 0xFFF] = (SERVICE){(void *)NtUserSetWindowPlacement, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetWindowPlacement has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowPos
    table[MIME_SVC_NtUserSetWindowPos & 0xFFF] = (SERVICE){(void *)NtUserSetWindowPos, STACK_ARGS(7), 1, 0};
#else
#warning "NtUserSetWindowPos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowRgn
    table[MIME_SVC_NtUserSetWindowRgn & 0xFFF] = (SERVICE){(void *)NtUserSetWindowRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetWindowRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowsHookEx
    table[MIME_SVC_NtUserSetWindowsHookEx & 0xFFF] = (SERVICE){(void *)NtUserSetWindowsHookEx, STACK_ARGS(6), 1, 0};
#else
#warning "NtUserSetWindowsHookEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShowWindow
    table[MIME_SVC_NtUserShowWindow & 0xFFF] = (SERVICE){(void *)NtUserShowWindow, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserShowWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShowWindowAsync
    table[MIME_SVC_NtUserShowWindowAsync & 0xFFF] = (SERVICE){(void *)NtUserShowWindowAsync, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserShowWindowAsync has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShutdownBlockReasonCreate
    table[MIME_SVC_NtUserShutdownBlockReasonCreate & 0xFFF] = (SERVICE){(void *)NtUserShutdownBlockReasonCreate, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserShutdownBlockReasonCreate has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShutdownBlockReasonQuery
    table[MIME_SVC_NtUserShutdownBlockReasonQuery & 0xFFF] = (SERVICE){(void *)NtUserShutdownBlockReasonQuery, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserShutdownBlockReasonQuery has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShutdownReasonDestroy
    table[MIME_SVC_NtUserShutdownReasonDestroy & 0xFFF] = (SERVICE){(void *)NtUserShutdownReasonDestroy, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserShutdownReasonDestroy has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSwitchDesktop
    table[MIME_SVC_NtUserSwitchDesktop & 0xFFF] = (SERVICE){(void *)NtUserSwitchDesktop, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSwitchDesktop has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSystemParametersInfo
    table[MIME_SVC_NtUserSystemParametersInfo & 0xFFF] = (SERVICE){(void *)NtUserSystemParametersInfo, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSystemParametersInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSystemParametersInfoForDpi
    table[MIME_SVC_NtUserSystemParametersInfoForDpi & 0xFFF] = (SERVICE){(void *)NtUserSystemParametersInfoForDpi, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserSystemParametersInfoForDpi has no syscall number"
#endif
#ifdef MIME_SVC_NtUserThunkedMenuInfo
    table[MIME_SVC_NtUserThunkedMenuInfo & 0xFFF] = (SERVICE){(void *)NtUserThunkedMenuInfo, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserThunkedMenuInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserThunkedMenuItemInfo
    table[MIME_SVC_NtUserThunkedMenuItemInfo & 0xFFF] = (SERVICE){(void *)NtUserThunkedMenuItemInfo, STACK_ARGS(6), 1, 0};
#else
#warning "NtUserThunkedMenuItemInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserTrackMouseEvent
    table[MIME_SVC_NtUserTrackMouseEvent & 0xFFF] = (SERVICE){(void *)NtUserTrackMouseEvent, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserTrackMouseEvent has no syscall number"
#endif
#ifdef MIME_SVC_NtUserTranslateMessage
    table[MIME_SVC_NtUserTranslateMessage & 0xFFF] = (SERVICE){(void *)NtUserTranslateMessage, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserTranslateMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserUnhookWindowsHookEx
    table[MIME_SVC_NtUserUnhookWindowsHookEx & 0xFFF] = (SERVICE){(void *)NtUserUnhookWindowsHookEx, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserUnhookWindowsHookEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserUnregisterClass
    table[MIME_SVC_NtUserUnregisterClass & 0xFFF] = (SERVICE){(void *)NtUserUnregisterClass, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserUnregisterClass has no syscall number"
#endif
#ifdef MIME_SVC_NtUserUpdateLayeredWindow
    table[MIME_SVC_NtUserUpdateLayeredWindow & 0xFFF] = (SERVICE){(void *)NtUserUpdateLayeredWindow, STACK_ARGS(10), 1, 0};
#else
#warning "NtUserUpdateLayeredWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserValidateRect
    table[MIME_SVC_NtUserValidateRect & 0xFFF] = (SERVICE){(void *)NtUserValidateRect, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserValidateRect has no syscall number"
#endif
#ifdef MIME_SVC_NtUserWaitForInputIdle
    table[MIME_SVC_NtUserWaitForInputIdle & 0xFFF] = (SERVICE){(void *)NtUserWaitForInputIdle, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserWaitForInputIdle has no syscall number"
#endif
#ifdef MIME_SVC_NtUserWaitMessage
    table[MIME_SVC_NtUserWaitMessage & 0xFFF] = (SERVICE){(void *)NtUserWaitMessage, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserWaitMessage has no syscall number"
#endif
}
