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
void NtDxgkEnumAdapters3(void);
void NtDxgkIsFeatureEnabled(void);
void NtGdiDdDDIOpenAdapterFromLuid(void);
void NtGdiDdDDICloseAdapter(void);
void NtGdiDdDDIQueryAdapterInfo(void);
void NtGdiDdDDIQueryStatistics(void);
void NtGdiDdDDIDestroyDCFromMemory(void);
void NtGdiDeleteObjectApp(void);
void NtGdiDoPalette(void);
void NtGdiGetTextExtent(void);
void NtGdiGetDCDpiScaleValue(void);
void NtGdiFontIsLinked(void);
void NtGdiGetCharWidthInfo(void);
void NtGdiGetRealizationInfo(void);
void NtGdiAnyLinkedFonts(void);
void NtGdiQueryFontAssocInfo(void);
void NtGdiGetTextCharsetInfo(void);
void NtGdiGetCharSet(void);
void NtGdiGetEntry(void);
void NtGdiRectVisible(void);
void NtGdiPtVisible(void);
void NtGdiOffsetClipRgn(void);
void NtGdiCreateEllipticRgn(void);
void NtGdiSetBrushOrg(void);
void NtGdiCreateDIBBrush(void);
void NtGdiCreatePatternBrushInternal(void);
void NtGdiCreateHatchBrushInternal(void);
void NtGdiPolyPatBlt(void);
void NtGdiUnrealizeObject(void);
void NtGdiSetSystemPaletteUse(void);
void NtGdiGetSystemPaletteUse(void);
void NtGdiGetNearestColor(void);
void NtGdiGetNearestPaletteIndex(void);
void NtGdiResizePalette(void);
void NtGdiCreateHalftonePalette(void);
void NtGdiCreatePaletteInternal(void);
void NtGdiCreateDIBitmapInternal(void);
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
void NtGdiGetBoundsRect(void);
void NtGdiGetCharABCWidthsW(void);
void NtGdiGetCharWidthW(void);
void NtGdiGetDCforBitmap(void);
void NtGdiGetDCDword(void);
void NtGdiSetLayout(void);
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
void NtGdiInit2(void);
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
void NtGdiArcInternal(void);
void NtGdiAngleArc(void);
void NtGdiPolyDraw(void);
void NtGdiGradientFill(void);
void NtGdiTransparentBlt(void);
void NtGdiMaskBlt(void);
void NtGdiComputeXformCoefficients(void);
void NtGdiUpdateTransform(void);
void NtGdiScaleViewportExtEx(void);
void NtGdiScaleWindowExtEx(void);
void NtGdiCombineTransform(void);
void NtGdiGetDeviceWidth(void);
void NtGdiMirrorWindowOrg(void);
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
void NtGdiSetBoundsRect(void);
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
void NtUserClearWindowState(void);
void NtUserClipCursor(void);
void NtUserCloseClipboard(void);
void NtUserCloseDesktop(void);
void NtUserCloseWindowStation(void);
void NtUserCountClipboardFormats(void);
void NtUserCreateDesktopEx(void);
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
void NtUserEnumDisplayDevices(void);
void NtUserLoadUserApiHook(void);
void NtUserGetDpiForCurrentProcess(void);
void NtUserInternalGetWindowIcon(void);
void NtUserEnumDisplaySettings(void);
void NtUserEnumDisplayMonitors(void);
void NtUserMapVirtualKeyEx(void);
void NtUserToUnicodeEx(void);
void NtUserVkKeyScanEx(void);
void NtUserGetKeyNameText(void);
void NtUserActivateKeyboardLayout(void);
void NtUserBitBltSysBmp(void);
void NtUserGetOemBitmapSize(void);
void NtUserSetForegroundWindow(void);
void NtUserAllowSetForegroundWindow(void);
void NtUserLockSetForegroundWindow(void);
void NtUserWindowFromPoint(void);
void NtUserGetHDevName(void);
void NtUserGetDpiForMonitor(void);
void NtUserGetAutoRotationState(void);
void NtUserGetDisplayAutoRotationPreferences(void);
void NtUserSetDisplayAutoRotationPreferences(void);
void NtUserWindowFromDC(void);
void NtUserSetProcessDpiAwarenessContext(void);
void NtUserGetProcessDpiAwarenessContext(void);
void NtUserGetSystemDpiForProcess(void);
void NtUserGetDisplayConfigBufferSizes(void);
void NtUserQueryDisplayConfig(void);
void NtUserDisplayConfigGetDeviceInfo(void);
void NtUserFindWindowEx(void);
void NtUserForceWindowToDpiForTest(void);
void NtUserGetAncestor(void);
void NtUserMapDesktopObject(void);
void NtUserSetWindowWord(void);
void NtUserSetWindowContextHelpId(void);
void NtUserGetWindowContextHelpId(void);
void NtUserGetAsyncKeyState(void);
void NtUserGetClassInfoEx(void);
void NtUserGetClassName(void);
void NtUserGetClipCursor(void);
void NtUserGetProp2(void);
void NtUserGetComboBoxInfo(void);
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
void NtUserGetGUIThreadInfo(void);
void NtUserGetGuiResources(void);
void NtUserGetIconInfo(void);
void NtUserGetKeyState(void);
void NtUserGetQueueStatus(void);
void NtUserGetQueueStatusReadonly(void);
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
void NtUserGetUpdateRgn(void);
void NtUserGetUpdatedClipboardFormats(void);
void NtUserGetWindowDC(void);
void NtUserGetWindowPlacement(void);
void NtUserGetWindowRgnEx(void);
void NtUserInitializeClientPfnArrays(void);
void NtUserInternalGetWindowText(void);
void NtUserInheritWindowMonitor(void);
void NtUserAlterWindowStyle(void);
void NtUserSetSysColors(void);
void NtUserEnsureDpiDepSysMetCacheForPlateau(void);
void NtGdiEnsureDpiDepDefaultGuiFontForPlateau(void);
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
void NtUserReleaseCapture(void);
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
void NtUserSetCursorPos(void);
void NtUserSetCursorIconData(void);
void NtUserGetDoubleClickTime(void);
void NtUserSetThreadState(void);
void NtUserSetMsgBox(void);
void NtUserSetDialogSystemMenu(void);
void NtUserNotifyWinEvent(void);
void NtUserGetControlColor(void);
void NtUserGetControlBrush(void);
void NtUserSetDialogPointer(void);
void NtUserSetWindowFNID(void);
void NtUserGetAtomName(void);
void NtUserFillWindow(void);
void NtUserModifyUserStartupInfoFlags(void);
void NtUserGetKeyboardLayoutList(void);
void NtUserGetKeyboardLayout(void);
void NtUserTranslateAccelerator(void);
void NtUserCopyAcceleratorTable(void);
void NtUserDestroyAcceleratorTable(void);
void NtUserCreateAcceleratorTable(void);
void NtUserGetScrollBarInfo(void);
void NtUserEnableScrollBar(void);
void NtUserShowScrollBar(void);
void NtUserSBGetParms(void);
void NtUserSetScrollInfo(void);
void NtUserSetCaretBlinkTime(void);
void NtUserGetCaretBlinkTime(void);
void NtUserGetCaretPos(void);
void NtUserSetCaretPos(void);
void NtUserShowCaret(void);
void NtUserHideCaret(void);
void NtUserDestroyCaret(void);
void NtUserCreateCaret(void);
void NtUserGetProcessUIContextInformation(void);
void NtUserRegisterLPK(void);
void NtUserRemoteConnectState(void);
void NtUserTransformRect(void);
void NtUserTransformPoint(void);
void NtUserPrintWindow(void);
void NtUserMoveWindow(void);
void NtUserUpdateWindow(void);
void NtUserRealizePalette(void);
void NtUserSelectPalette(void);
void NtUserGetCursor(void);
void NtUserGetClassIcoCur(void);
void NtUserLinkDpiCursor(void);
void NtUserGetRequiredCursorSizes(void);
void NtUserGetCursorFrameInfo(void);
void NtUserGetIconSize(void);
void NtUserFindExistingCursorIcon(void);
void NtUserSetCursorIconDataEx(void);
void NtUserCreateEmptyCursorObject(void);
void NtUserSetDpiForWindow(void);
void NtUserSetFocus(void);
void NtUserSetKeyboardState(void);
void NtUserSetLayeredWindowAttributes(void);
void NtUserSetMenu(void);
void NtUserSetMenuDefaultItem(void);
void NtUserSetObjectInformation(void);
void NtUserSetParent(void);
void NtUserSetProp2(void);
void NtUserSetProcessWindowStation(void);
void NtUserSetProp(void);
void NtUserSetThreadDesktop(void);
void NtUserSetTimer(void);
void NtUserSetWindowLong(void);
void NtUserSetWindowLongPtr(void);
void NtUserSetWindowPlacement(void);
void NtUserSetWindowPos(void);
void NtUserSetWindowRgn(void);
void NtUserSetWindowState(void);
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
void NtUserInvalidateRgn(void);
void NtUserValidateRgn(void);
void NtUserBeginDeferWindowPos(void);
void NtUserDeferWindowPosAndBand(void);
void NtUserEndDeferWindowPosEx(void);
void NtUserLockWindowUpdate(void);
void NtUserWaitForInputIdle(void);
void NtUserWaitMessage(void);
void NtGdiGetPublicFontTableChangeCookie(void);
void NtUserChangeWindowMessageFilterEx(void);
void NtUserChildWindowFromPointEx(void);
void NtUserGetKeyboardLayoutName(void);
void NtUserGetPointerDevices(void);
void NtUserGetWindowDisplayAffinity(void);
void NtUserPostQuitMessage(void);
void NtUserRegisterPointerDeviceNotifications(void);
void NtUserSetAdditionalForegroundBoostProcesses(void);
void NtUserSetProcessLaunchForegroundPolicy(void);
void NtUserSetWindowDisplayAffinity(void);
void NtUserThreadMessageQueueAttached(void);
void NtUserZapActiveAndFocus(void);

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
#ifdef MIME_SVC_NtDxgkEnumAdapters3
    table[MIME_SVC_NtDxgkEnumAdapters3 & 0xFFF] = (SERVICE){(void *)NtDxgkEnumAdapters3, STACK_ARGS(1), 1, 0};
#else
#warning "NtDxgkEnumAdapters3 has no syscall number"
#endif
#ifdef MIME_SVC_NtDxgkIsFeatureEnabled
    table[MIME_SVC_NtDxgkIsFeatureEnabled & 0xFFF] = (SERVICE){(void *)NtDxgkIsFeatureEnabled, STACK_ARGS(1), 1, 0};
#else
#warning "NtDxgkIsFeatureEnabled has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDdDDIOpenAdapterFromLuid
    table[MIME_SVC_NtGdiDdDDIOpenAdapterFromLuid & 0xFFF] = (SERVICE){(void *)NtGdiDdDDIOpenAdapterFromLuid, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDdDDIOpenAdapterFromLuid has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDdDDICloseAdapter
    table[MIME_SVC_NtGdiDdDDICloseAdapter & 0xFFF] = (SERVICE){(void *)NtGdiDdDDICloseAdapter, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDdDDICloseAdapter has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDdDDIQueryAdapterInfo
    table[MIME_SVC_NtGdiDdDDIQueryAdapterInfo & 0xFFF] = (SERVICE){(void *)NtGdiDdDDIQueryAdapterInfo, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDdDDIQueryAdapterInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiDdDDIQueryStatistics
    table[MIME_SVC_NtGdiDdDDIQueryStatistics & 0xFFF] = (SERVICE){(void *)NtGdiDdDDIQueryStatistics, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiDdDDIQueryStatistics has no syscall number"
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
#ifdef MIME_SVC_NtGdiCreateDIBitmapInternal
    table[MIME_SVC_NtGdiCreateDIBitmapInternal & 0xFFF] = (SERVICE){(void *)NtGdiCreateDIBitmapInternal, STACK_ARGS(11), 1, 0};
#else
#warning "NtGdiCreateDIBitmapInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreatePaletteInternal
    table[MIME_SVC_NtGdiCreatePaletteInternal & 0xFFF] = (SERVICE){(void *)NtGdiCreatePaletteInternal, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiCreatePaletteInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateHalftonePalette
    table[MIME_SVC_NtGdiCreateHalftonePalette & 0xFFF] = (SERVICE){(void *)NtGdiCreateHalftonePalette, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiCreateHalftonePalette has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiResizePalette
    table[MIME_SVC_NtGdiResizePalette & 0xFFF] = (SERVICE){(void *)NtGdiResizePalette, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiResizePalette has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetNearestPaletteIndex
    table[MIME_SVC_NtGdiGetNearestPaletteIndex & 0xFFF] = (SERVICE){(void *)NtGdiGetNearestPaletteIndex, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetNearestPaletteIndex has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetNearestColor
    table[MIME_SVC_NtGdiGetNearestColor & 0xFFF] = (SERVICE){(void *)NtGdiGetNearestColor, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetNearestColor has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetSystemPaletteUse
    table[MIME_SVC_NtGdiGetSystemPaletteUse & 0xFFF] = (SERVICE){(void *)NtGdiGetSystemPaletteUse, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiGetSystemPaletteUse has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetSystemPaletteUse
    table[MIME_SVC_NtGdiSetSystemPaletteUse & 0xFFF] = (SERVICE){(void *)NtGdiSetSystemPaletteUse, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiSetSystemPaletteUse has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiUnrealizeObject
    table[MIME_SVC_NtGdiUnrealizeObject & 0xFFF] = (SERVICE){(void *)NtGdiUnrealizeObject, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiUnrealizeObject has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPolyPatBlt
    table[MIME_SVC_NtGdiPolyPatBlt & 0xFFF] = (SERVICE){(void *)NtGdiPolyPatBlt, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiPolyPatBlt has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateHatchBrushInternal
    table[MIME_SVC_NtGdiCreateHatchBrushInternal & 0xFFF] = (SERVICE){(void *)NtGdiCreateHatchBrushInternal, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiCreateHatchBrushInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreatePatternBrushInternal
    table[MIME_SVC_NtGdiCreatePatternBrushInternal & 0xFFF] = (SERVICE){(void *)NtGdiCreatePatternBrushInternal, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiCreatePatternBrushInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateDIBBrush
    table[MIME_SVC_NtGdiCreateDIBBrush & 0xFFF] = (SERVICE){(void *)NtGdiCreateDIBBrush, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiCreateDIBBrush has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiSetBrushOrg
    table[MIME_SVC_NtGdiSetBrushOrg & 0xFFF] = (SERVICE){(void *)NtGdiSetBrushOrg, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiSetBrushOrg has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCreateEllipticRgn
    table[MIME_SVC_NtGdiCreateEllipticRgn & 0xFFF] = (SERVICE){(void *)NtGdiCreateEllipticRgn, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiCreateEllipticRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiOffsetClipRgn
    table[MIME_SVC_NtGdiOffsetClipRgn & 0xFFF] = (SERVICE){(void *)NtGdiOffsetClipRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiOffsetClipRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPtVisible
    table[MIME_SVC_NtGdiPtVisible & 0xFFF] = (SERVICE){(void *)NtGdiPtVisible, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiPtVisible has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiRectVisible
    table[MIME_SVC_NtGdiRectVisible & 0xFFF] = (SERVICE){(void *)NtGdiRectVisible, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiRectVisible has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetEntry
    table[MIME_SVC_NtGdiGetEntry & 0xFFF] = (SERVICE){(void *)NtGdiGetEntry, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetEntry has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetCharSet
    table[MIME_SVC_NtGdiGetCharSet & 0xFFF] = (SERVICE){(void *)NtGdiGetCharSet, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiGetCharSet has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetTextCharsetInfo
    table[MIME_SVC_NtGdiGetTextCharsetInfo & 0xFFF] = (SERVICE){(void *)NtGdiGetTextCharsetInfo, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetTextCharsetInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiQueryFontAssocInfo
    table[MIME_SVC_NtGdiQueryFontAssocInfo & 0xFFF] = (SERVICE){(void *)NtGdiQueryFontAssocInfo, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiQueryFontAssocInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiAnyLinkedFonts
    table[MIME_SVC_NtGdiAnyLinkedFonts & 0xFFF] = (SERVICE){(void *)NtGdiAnyLinkedFonts, STACK_ARGS(0), 1, 0};
#else
#warning "NtGdiAnyLinkedFonts has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetRealizationInfo
    table[MIME_SVC_NtGdiGetRealizationInfo & 0xFFF] = (SERVICE){(void *)NtGdiGetRealizationInfo, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetRealizationInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetCharWidthInfo
    table[MIME_SVC_NtGdiGetCharWidthInfo & 0xFFF] = (SERVICE){(void *)NtGdiGetCharWidthInfo, STACK_ARGS(2), 1, 0};
#else
#warning "NtGdiGetCharWidthInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiFontIsLinked
    table[MIME_SVC_NtGdiFontIsLinked & 0xFFF] = (SERVICE){(void *)NtGdiFontIsLinked, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiFontIsLinked has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDCDpiScaleValue
    table[MIME_SVC_NtGdiGetDCDpiScaleValue & 0xFFF] = (SERVICE){(void *)NtGdiGetDCDpiScaleValue, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiGetDCDpiScaleValue has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetTextExtent
    table[MIME_SVC_NtGdiGetTextExtent & 0xFFF] = (SERVICE){(void *)NtGdiGetTextExtent, STACK_ARGS(5), 1, 0};
#else
#warning "NtGdiGetTextExtent has no syscall number"
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
    table[MIME_SVC_NtGdiExtCreatePen & 0xFFF] = (SERVICE){(void *)NtGdiExtCreatePen, STACK_ARGS(11), 1, 0};
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
#ifdef MIME_SVC_NtGdiGetBoundsRect
    table[MIME_SVC_NtGdiGetBoundsRect & 0xFFF] = (SERVICE){(void *)NtGdiGetBoundsRect, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiGetBoundsRect has no syscall number"
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
#ifdef MIME_SVC_NtGdiSetLayout
    table[MIME_SVC_NtGdiSetLayout & 0xFFF] = (SERVICE){(void *)NtGdiSetLayout, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiSetLayout has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDCforBitmap
    table[MIME_SVC_NtGdiGetDCforBitmap & 0xFFF] = (SERVICE){(void *)NtGdiGetDCforBitmap, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiGetDCforBitmap has no syscall number"
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
#ifdef MIME_SVC_NtGdiInit2
    table[MIME_SVC_NtGdiInit2 & 0xFFF] = (SERVICE){(void *)NtGdiInit2, STACK_ARGS(0), 1, 0};
#else
#warning "NtGdiInit2 has no syscall number"
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
    table[MIME_SVC_NtGdiOpenDCW & 0xFFF] = (SERVICE){(void *)NtGdiOpenDCW, STACK_ARGS(9), 1, 0};
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
#ifdef MIME_SVC_NtGdiArcInternal
    table[MIME_SVC_NtGdiArcInternal & 0xFFF] = (SERVICE){(void *)NtGdiArcInternal, STACK_ARGS(10), 1, 0};
#else
#warning "NtGdiArcInternal has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiAngleArc
    table[MIME_SVC_NtGdiAngleArc & 0xFFF] = (SERVICE){(void *)NtGdiAngleArc, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiAngleArc has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiPolyDraw
    table[MIME_SVC_NtGdiPolyDraw & 0xFFF] = (SERVICE){(void *)NtGdiPolyDraw, STACK_ARGS(4), 1, 0};
#else
#warning "NtGdiPolyDraw has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGradientFill
    table[MIME_SVC_NtGdiGradientFill & 0xFFF] = (SERVICE){(void *)NtGdiGradientFill, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiGradientFill has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiTransparentBlt
    table[MIME_SVC_NtGdiTransparentBlt & 0xFFF] = (SERVICE){(void *)NtGdiTransparentBlt, STACK_ARGS(11), 1, 0};
#else
#warning "NtGdiTransparentBlt has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMaskBlt
    table[MIME_SVC_NtGdiMaskBlt & 0xFFF] = (SERVICE){(void *)NtGdiMaskBlt, STACK_ARGS(13), 1, 0};
#else
#warning "NtGdiMaskBlt has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiComputeXformCoefficients
    table[MIME_SVC_NtGdiComputeXformCoefficients & 0xFFF] = (SERVICE){(void *)NtGdiComputeXformCoefficients, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiComputeXformCoefficients has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiUpdateTransform
    table[MIME_SVC_NtGdiUpdateTransform & 0xFFF] = (SERVICE){(void *)NtGdiUpdateTransform, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiUpdateTransform has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiScaleViewportExtEx
    table[MIME_SVC_NtGdiScaleViewportExtEx & 0xFFF] = (SERVICE){(void *)NtGdiScaleViewportExtEx, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiScaleViewportExtEx has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiScaleWindowExtEx
    table[MIME_SVC_NtGdiScaleWindowExtEx & 0xFFF] = (SERVICE){(void *)NtGdiScaleWindowExtEx, STACK_ARGS(6), 1, 0};
#else
#warning "NtGdiScaleWindowExtEx has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiCombineTransform
    table[MIME_SVC_NtGdiCombineTransform & 0xFFF] = (SERVICE){(void *)NtGdiCombineTransform, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiCombineTransform has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiGetDeviceWidth
    table[MIME_SVC_NtGdiGetDeviceWidth & 0xFFF] = (SERVICE){(void *)NtGdiGetDeviceWidth, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiGetDeviceWidth has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiMirrorWindowOrg
    table[MIME_SVC_NtGdiMirrorWindowOrg & 0xFFF] = (SERVICE){(void *)NtGdiMirrorWindowOrg, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiMirrorWindowOrg has no syscall number"
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
#ifdef MIME_SVC_NtGdiSetBoundsRect
    table[MIME_SVC_NtGdiSetBoundsRect & 0xFFF] = (SERVICE){(void *)NtGdiSetBoundsRect, STACK_ARGS(3), 1, 0};
#else
#warning "NtGdiSetBoundsRect has no syscall number"
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
#ifdef MIME_SVC_NtUserClearWindowState
    table[MIME_SVC_NtUserClearWindowState & 0xFFF] = (SERVICE){(void *)NtUserClearWindowState, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserClearWindowState has no syscall number"
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
    table[MIME_SVC_NtUserCreateWindowEx & 0xFFF] = (SERVICE){(void *)NtUserCreateWindowEx, STACK_ARGS(17), 1, 0};
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
#ifdef MIME_SVC_NtUserInternalGetWindowIcon
    table[MIME_SVC_NtUserInternalGetWindowIcon & 0xFFF] = (SERVICE){(void *)NtUserInternalGetWindowIcon, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserInternalGetWindowIcon has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDpiForCurrentProcess
    table[MIME_SVC_NtUserGetDpiForCurrentProcess & 0xFFF] = (SERVICE){(void *)NtUserGetDpiForCurrentProcess, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetDpiForCurrentProcess has no syscall number"
#endif
#ifdef MIME_SVC_NtUserLoadUserApiHook
    table[MIME_SVC_NtUserLoadUserApiHook & 0xFFF] = (SERVICE){(void *)NtUserLoadUserApiHook, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserLoadUserApiHook has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnumDisplayDevices
    table[MIME_SVC_NtUserEnumDisplayDevices & 0xFFF] = (SERVICE){(void *)NtUserEnumDisplayDevices, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserEnumDisplayDevices has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnumDisplaySettings
    table[MIME_SVC_NtUserEnumDisplaySettings & 0xFFF] = (SERVICE){(void *)NtUserEnumDisplaySettings, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserEnumDisplaySettings has no syscall number"
#endif
#ifdef MIME_SVC_NtUserAllowSetForegroundWindow
    table[MIME_SVC_NtUserAllowSetForegroundWindow & 0xFFF] = (SERVICE){(void *)NtUserAllowSetForegroundWindow, STACK_ARGS(1), 1, 0};
#endif
#ifdef MIME_SVC_NtUserLockSetForegroundWindow
    table[MIME_SVC_NtUserLockSetForegroundWindow & 0xFFF] = (SERVICE){(void *)NtUserLockSetForegroundWindow, STACK_ARGS(1), 1, 0};
#endif
#ifdef MIME_SVC_NtUserSetForegroundWindow
    table[MIME_SVC_NtUserSetForegroundWindow & 0xFFF] = (SERVICE){(void *)NtUserSetForegroundWindow, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetForegroundWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserWindowFromPoint
    table[MIME_SVC_NtUserWindowFromPoint & 0xFFF] = (SERVICE){(void *)NtUserWindowFromPoint, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserWindowFromPoint has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMapVirtualKeyEx
    table[MIME_SVC_NtUserMapVirtualKeyEx & 0xFFF] = (SERVICE){(void *)NtUserMapVirtualKeyEx, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserMapVirtualKeyEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserToUnicodeEx
    table[MIME_SVC_NtUserToUnicodeEx & 0xFFF] = (SERVICE){(void *)NtUserToUnicodeEx, STACK_ARGS(7), 1, 0};
#else
#warning "NtUserToUnicodeEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserVkKeyScanEx
    table[MIME_SVC_NtUserVkKeyScanEx & 0xFFF] = (SERVICE){(void *)NtUserVkKeyScanEx, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserVkKeyScanEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetKeyNameText
    table[MIME_SVC_NtUserGetKeyNameText & 0xFFF] = (SERVICE){(void *)NtUserGetKeyNameText, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetKeyNameText has no syscall number"
#endif
#ifdef MIME_SVC_NtUserActivateKeyboardLayout
    table[MIME_SVC_NtUserActivateKeyboardLayout & 0xFFF] = (SERVICE){(void *)NtUserActivateKeyboardLayout, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserActivateKeyboardLayout has no syscall number"
#endif
#ifdef MIME_SVC_NtUserBitBltSysBmp
    table[MIME_SVC_NtUserBitBltSysBmp & 0xFFF] = (SERVICE){(void *)NtUserBitBltSysBmp, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserBitBltSysBmp has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetOemBitmapSize
    table[MIME_SVC_NtUserGetOemBitmapSize & 0xFFF] = (SERVICE){(void *)NtUserGetOemBitmapSize, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetOemBitmapSize has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnumDisplayMonitors
    table[MIME_SVC_NtUserEnumDisplayMonitors & 0xFFF] = (SERVICE){(void *)NtUserEnumDisplayMonitors, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserEnumDisplayMonitors has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetHDevName
    table[MIME_SVC_NtUserGetHDevName & 0xFFF] = (SERVICE){(void *)NtUserGetHDevName, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetHDevName has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDpiForMonitor
    table[MIME_SVC_NtUserGetDpiForMonitor & 0xFFF] = (SERVICE){(void *)NtUserGetDpiForMonitor, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetDpiForMonitor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetAutoRotationState
    table[MIME_SVC_NtUserGetAutoRotationState & 0xFFF] = (SERVICE){(void *)NtUserGetAutoRotationState, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetAutoRotationState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDisplayAutoRotationPreferences
    table[MIME_SVC_NtUserGetDisplayAutoRotationPreferences & 0xFFF] = (SERVICE){(void *)NtUserGetDisplayAutoRotationPreferences, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetDisplayAutoRotationPreferences has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetDisplayAutoRotationPreferences
    table[MIME_SVC_NtUserSetDisplayAutoRotationPreferences & 0xFFF] = (SERVICE){(void *)NtUserSetDisplayAutoRotationPreferences, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetDisplayAutoRotationPreferences has no syscall number"
#endif
#ifdef MIME_SVC_NtUserWindowFromDC
    table[MIME_SVC_NtUserWindowFromDC & 0xFFF] = (SERVICE){(void *)NtUserWindowFromDC, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserWindowFromDC has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetProcessDpiAwarenessContext
    table[MIME_SVC_NtUserSetProcessDpiAwarenessContext & 0xFFF] = (SERVICE){(void *)NtUserSetProcessDpiAwarenessContext, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetProcessDpiAwarenessContext has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetProcessDpiAwarenessContext
    table[MIME_SVC_NtUserGetProcessDpiAwarenessContext & 0xFFF] = (SERVICE){(void *)NtUserGetProcessDpiAwarenessContext, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetProcessDpiAwarenessContext has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetSystemDpiForProcess
    table[MIME_SVC_NtUserGetSystemDpiForProcess & 0xFFF] = (SERVICE){(void *)NtUserGetSystemDpiForProcess, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetSystemDpiForProcess has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDisplayConfigBufferSizes
    table[MIME_SVC_NtUserGetDisplayConfigBufferSizes & 0xFFF] = (SERVICE){(void *)NtUserGetDisplayConfigBufferSizes, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetDisplayConfigBufferSizes has no syscall number"
#endif
#ifdef MIME_SVC_NtUserQueryDisplayConfig
    table[MIME_SVC_NtUserQueryDisplayConfig & 0xFFF] = (SERVICE){(void *)NtUserQueryDisplayConfig, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserQueryDisplayConfig has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDisplayConfigGetDeviceInfo
    table[MIME_SVC_NtUserDisplayConfigGetDeviceInfo & 0xFFF] = (SERVICE){(void *)NtUserDisplayConfigGetDeviceInfo, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDisplayConfigGetDeviceInfo has no syscall number"
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
#ifdef MIME_SVC_NtUserSetWindowContextHelpId
    table[MIME_SVC_NtUserSetWindowContextHelpId & 0xFFF] = (SERVICE){(void *)NtUserSetWindowContextHelpId, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetWindowContextHelpId has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetWindowContextHelpId
    table[MIME_SVC_NtUserGetWindowContextHelpId & 0xFFF] = (SERVICE){(void *)NtUserGetWindowContextHelpId, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetWindowContextHelpId has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowWord
    table[MIME_SVC_NtUserSetWindowWord & 0xFFF] = (SERVICE){(void *)NtUserSetWindowWord, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetWindowWord has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMapDesktopObject
    table[MIME_SVC_NtUserMapDesktopObject & 0xFFF] = (SERVICE){(void *)NtUserMapDesktopObject, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserMapDesktopObject has no syscall number"
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
#ifdef MIME_SVC_NtUserGetComboBoxInfo
    table[MIME_SVC_NtUserGetComboBoxInfo & 0xFFF] = (SERVICE){(void *)NtUserGetComboBoxInfo, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetComboBoxInfo has no syscall number"
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
#ifdef MIME_SVC_NtUserGetGUIThreadInfo
    table[MIME_SVC_NtUserGetGUIThreadInfo & 0xFFF] = (SERVICE){(void *)NtUserGetGUIThreadInfo, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetGUIThreadInfo has no syscall number"
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
#ifdef MIME_SVC_NtUserGetQueueStatus
    table[MIME_SVC_NtUserGetQueueStatus & 0xFFF] = (SERVICE){(void *)NtUserGetQueueStatus, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetQueueStatus has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetQueueStatusReadonly
    table[MIME_SVC_NtUserGetQueueStatusReadonly & 0xFFF] = (SERVICE){(void *)NtUserGetQueueStatusReadonly, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetQueueStatusReadonly has no syscall number"
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
#ifdef MIME_SVC_NtUserGetProp2
    table[MIME_SVC_NtUserGetProp2 & 0xFFF] = (SERVICE){(void *)NtUserGetProp2, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetProp2 has no syscall number"
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
#ifdef MIME_SVC_NtUserGetUpdateRgn
    table[MIME_SVC_NtUserGetUpdateRgn & 0xFFF] = (SERVICE){(void *)NtUserGetUpdateRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetUpdateRgn has no syscall number"
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
    table[MIME_SVC_NtUserInitializeClientPfnArrays & 0xFFF] = (SERVICE){(void *)NtUserInitializeClientPfnArrays, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserInitializeClientPfnArrays has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnsureDpiDepSysMetCacheForPlateau
    table[MIME_SVC_NtUserEnsureDpiDepSysMetCacheForPlateau & 0xFFF] = (SERVICE){(void *)NtUserEnsureDpiDepSysMetCacheForPlateau, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserEnsureDpiDepSysMetCacheForPlateau has no syscall number"
#endif
#ifdef MIME_SVC_NtGdiEnsureDpiDepDefaultGuiFontForPlateau
    table[MIME_SVC_NtGdiEnsureDpiDepDefaultGuiFontForPlateau & 0xFFF] = (SERVICE){(void *)NtGdiEnsureDpiDepDefaultGuiFontForPlateau, STACK_ARGS(1), 1, 0};
#else
#warning "NtGdiEnsureDpiDepDefaultGuiFontForPlateau has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetSysColors
    table[MIME_SVC_NtUserSetSysColors & 0xFFF] = (SERVICE){(void *)NtUserSetSysColors, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetSysColors has no syscall number"
#endif
#ifdef MIME_SVC_NtUserAlterWindowStyle
    table[MIME_SVC_NtUserAlterWindowStyle & 0xFFF] = (SERVICE){(void *)NtUserAlterWindowStyle, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserAlterWindowStyle has no syscall number"
#endif
#ifdef MIME_SVC_NtUserInheritWindowMonitor
    table[MIME_SVC_NtUserInheritWindowMonitor & 0xFFF] = (SERVICE){(void *)NtUserInheritWindowMonitor, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserInheritWindowMonitor has no syscall number"
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
    table[MIME_SVC_NtUserRegisterClassExWOW & 0xFFF] = (SERVICE){(void *)NtUserRegisterClassExWOW, STACK_ARGS(7), 1, 0};
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
#ifdef MIME_SVC_NtUserReleaseCapture
    table[MIME_SVC_NtUserReleaseCapture & 0xFFF] = (SERVICE){(void *)NtUserReleaseCapture, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserReleaseCapture has no syscall number"
#endif
#ifdef MIME_SVC_NtUserReleaseDC
    table[MIME_SVC_NtUserReleaseDC & 0xFFF] = (SERVICE){(void *)NtUserReleaseDC, STACK_ARGS(1), 1, 0};
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
#ifdef MIME_SVC_NtUserSetCursorPos
    table[MIME_SVC_NtUserSetCursorPos & 0xFFF] = (SERVICE){(void *)NtUserSetCursorPos, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetCursorPos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCursor
    table[MIME_SVC_NtUserSetCursor & 0xFFF] = (SERVICE){(void *)NtUserSetCursor, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateEmptyCursorObject
    table[MIME_SVC_NtUserCreateEmptyCursorObject & 0xFFF] = (SERVICE){(void *)NtUserCreateEmptyCursorObject, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserCreateEmptyCursorObject has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCursorIconDataEx
    table[MIME_SVC_NtUserSetCursorIconDataEx & 0xFFF] = (SERVICE){(void *)NtUserSetCursorIconDataEx, STACK_ARGS(5), 1, 0};
#else
#warning "NtUserSetCursorIconDataEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserFindExistingCursorIcon
    table[MIME_SVC_NtUserFindExistingCursorIcon & 0xFFF] = (SERVICE){(void *)NtUserFindExistingCursorIcon, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserFindExistingCursorIcon has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetIconSize
    table[MIME_SVC_NtUserGetIconSize & 0xFFF] = (SERVICE){(void *)NtUserGetIconSize, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetIconSize has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetCursorFrameInfo
    table[MIME_SVC_NtUserGetCursorFrameInfo & 0xFFF] = (SERVICE){(void *)NtUserGetCursorFrameInfo, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetCursorFrameInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetRequiredCursorSizes
    table[MIME_SVC_NtUserGetRequiredCursorSizes & 0xFFF] = (SERVICE){(void *)NtUserGetRequiredCursorSizes, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetRequiredCursorSizes has no syscall number"
#endif
#ifdef MIME_SVC_NtUserLinkDpiCursor
    table[MIME_SVC_NtUserLinkDpiCursor & 0xFFF] = (SERVICE){(void *)NtUserLinkDpiCursor, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserLinkDpiCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetClassIcoCur
    table[MIME_SVC_NtUserGetClassIcoCur & 0xFFF] = (SERVICE){(void *)NtUserGetClassIcoCur, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetClassIcoCur has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetCursor
    table[MIME_SVC_NtUserGetCursor & 0xFFF] = (SERVICE){(void *)NtUserGetCursor, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetCursor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSelectPalette
    table[MIME_SVC_NtUserSelectPalette & 0xFFF] = (SERVICE){(void *)NtUserSelectPalette, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSelectPalette has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRealizePalette
    table[MIME_SVC_NtUserRealizePalette & 0xFFF] = (SERVICE){(void *)NtUserRealizePalette, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserRealizePalette has no syscall number"
#endif
#ifdef MIME_SVC_NtUserUpdateWindow
    table[MIME_SVC_NtUserUpdateWindow & 0xFFF] = (SERVICE){(void *)NtUserUpdateWindow, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserUpdateWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserMoveWindow
    table[MIME_SVC_NtUserMoveWindow & 0xFFF] = (SERVICE){(void *)NtUserMoveWindow, STACK_ARGS(6), 1, 0};
#else
#warning "NtUserMoveWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserPrintWindow
    table[MIME_SVC_NtUserPrintWindow & 0xFFF] = (SERVICE){(void *)NtUserPrintWindow, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserPrintWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserTransformPoint
    table[MIME_SVC_NtUserTransformPoint & 0xFFF] = (SERVICE){(void *)NtUserTransformPoint, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserTransformPoint has no syscall number"
#endif
#ifdef MIME_SVC_NtUserTransformRect
    table[MIME_SVC_NtUserTransformRect & 0xFFF] = (SERVICE){(void *)NtUserTransformRect, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserTransformRect has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRemoteConnectState
    table[MIME_SVC_NtUserRemoteConnectState & 0xFFF] = (SERVICE){(void *)NtUserRemoteConnectState, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserRemoteConnectState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRegisterLPK
    table[MIME_SVC_NtUserRegisterLPK & 0xFFF] = (SERVICE){(void *)NtUserRegisterLPK, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserRegisterLPK has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetProcessUIContextInformation
    table[MIME_SVC_NtUserGetProcessUIContextInformation & 0xFFF] = (SERVICE){(void *)NtUserGetProcessUIContextInformation, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetProcessUIContextInformation has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateCaret
    table[MIME_SVC_NtUserCreateCaret & 0xFFF] = (SERVICE){(void *)NtUserCreateCaret, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserCreateCaret has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDestroyCaret
    table[MIME_SVC_NtUserDestroyCaret & 0xFFF] = (SERVICE){(void *)NtUserDestroyCaret, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserDestroyCaret has no syscall number"
#endif
#ifdef MIME_SVC_NtUserHideCaret
    table[MIME_SVC_NtUserHideCaret & 0xFFF] = (SERVICE){(void *)NtUserHideCaret, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserHideCaret has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShowCaret
    table[MIME_SVC_NtUserShowCaret & 0xFFF] = (SERVICE){(void *)NtUserShowCaret, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserShowCaret has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCaretPos
    table[MIME_SVC_NtUserSetCaretPos & 0xFFF] = (SERVICE){(void *)NtUserSetCaretPos, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetCaretPos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetCaretPos
    table[MIME_SVC_NtUserGetCaretPos & 0xFFF] = (SERVICE){(void *)NtUserGetCaretPos, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetCaretPos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetCaretBlinkTime
    table[MIME_SVC_NtUserGetCaretBlinkTime & 0xFFF] = (SERVICE){(void *)NtUserGetCaretBlinkTime, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetCaretBlinkTime has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetCaretBlinkTime
    table[MIME_SVC_NtUserSetCaretBlinkTime & 0xFFF] = (SERVICE){(void *)NtUserSetCaretBlinkTime, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetCaretBlinkTime has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetScrollInfo
    table[MIME_SVC_NtUserSetScrollInfo & 0xFFF] = (SERVICE){(void *)NtUserSetScrollInfo, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSetScrollInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSBGetParms
    table[MIME_SVC_NtUserSBGetParms & 0xFFF] = (SERVICE){(void *)NtUserSBGetParms, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserSBGetParms has no syscall number"
#endif
#ifdef MIME_SVC_NtUserShowScrollBar
    table[MIME_SVC_NtUserShowScrollBar & 0xFFF] = (SERVICE){(void *)NtUserShowScrollBar, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserShowScrollBar has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEnableScrollBar
    table[MIME_SVC_NtUserEnableScrollBar & 0xFFF] = (SERVICE){(void *)NtUserEnableScrollBar, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserEnableScrollBar has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetScrollBarInfo
    table[MIME_SVC_NtUserGetScrollBarInfo & 0xFFF] = (SERVICE){(void *)NtUserGetScrollBarInfo, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetScrollBarInfo has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCreateAcceleratorTable
    table[MIME_SVC_NtUserCreateAcceleratorTable & 0xFFF] = (SERVICE){(void *)NtUserCreateAcceleratorTable, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserCreateAcceleratorTable has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDestroyAcceleratorTable
    table[MIME_SVC_NtUserDestroyAcceleratorTable & 0xFFF] = (SERVICE){(void *)NtUserDestroyAcceleratorTable, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserDestroyAcceleratorTable has no syscall number"
#endif
#ifdef MIME_SVC_NtUserCopyAcceleratorTable
    table[MIME_SVC_NtUserCopyAcceleratorTable & 0xFFF] = (SERVICE){(void *)NtUserCopyAcceleratorTable, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserCopyAcceleratorTable has no syscall number"
#endif
#ifdef MIME_SVC_NtUserTranslateAccelerator
    table[MIME_SVC_NtUserTranslateAccelerator & 0xFFF] = (SERVICE){(void *)NtUserTranslateAccelerator, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserTranslateAccelerator has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetKeyboardLayout
    table[MIME_SVC_NtUserGetKeyboardLayout & 0xFFF] = (SERVICE){(void *)NtUserGetKeyboardLayout, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetKeyboardLayout has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetKeyboardLayoutList
    table[MIME_SVC_NtUserGetKeyboardLayoutList & 0xFFF] = (SERVICE){(void *)NtUserGetKeyboardLayoutList, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetKeyboardLayoutList has no syscall number"
#endif
#ifdef MIME_SVC_NtUserModifyUserStartupInfoFlags
    table[MIME_SVC_NtUserModifyUserStartupInfoFlags & 0xFFF] = (SERVICE){(void *)NtUserModifyUserStartupInfoFlags, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserModifyUserStartupInfoFlags has no syscall number"
#endif
#ifdef MIME_SVC_NtUserFillWindow
    table[MIME_SVC_NtUserFillWindow & 0xFFF] = (SERVICE){(void *)NtUserFillWindow, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserFillWindow has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetAtomName
    table[MIME_SVC_NtUserGetAtomName & 0xFFF] = (SERVICE){(void *)NtUserGetAtomName, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetAtomName has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowFNID
    table[MIME_SVC_NtUserSetWindowFNID & 0xFFF] = (SERVICE){(void *)NtUserSetWindowFNID, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetWindowFNID has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetDialogPointer
    table[MIME_SVC_NtUserSetDialogPointer & 0xFFF] = (SERVICE){(void *)NtUserSetDialogPointer, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetDialogPointer has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetControlBrush
    table[MIME_SVC_NtUserGetControlBrush & 0xFFF] = (SERVICE){(void *)NtUserGetControlBrush, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserGetControlBrush has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetControlColor
    table[MIME_SVC_NtUserGetControlColor & 0xFFF] = (SERVICE){(void *)NtUserGetControlColor, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserGetControlColor has no syscall number"
#endif
#ifdef MIME_SVC_NtUserNotifyWinEvent
    table[MIME_SVC_NtUserNotifyWinEvent & 0xFFF] = (SERVICE){(void *)NtUserNotifyWinEvent, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserNotifyWinEvent has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetDialogSystemMenu
    table[MIME_SVC_NtUserSetDialogSystemMenu & 0xFFF] = (SERVICE){(void *)NtUserSetDialogSystemMenu, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetDialogSystemMenu has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetMsgBox
    table[MIME_SVC_NtUserSetMsgBox & 0xFFF] = (SERVICE){(void *)NtUserSetMsgBox, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserSetMsgBox has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetThreadState
    table[MIME_SVC_NtUserSetThreadState & 0xFFF] = (SERVICE){(void *)NtUserSetThreadState, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetThreadState has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetDoubleClickTime
    table[MIME_SVC_NtUserGetDoubleClickTime & 0xFFF] = (SERVICE){(void *)NtUserGetDoubleClickTime, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserGetDoubleClickTime has no syscall number"
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
#ifdef MIME_SVC_NtUserSetProp2
    table[MIME_SVC_NtUserSetProp2 & 0xFFF] = (SERVICE){(void *)NtUserSetProp2, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetProp2 has no syscall number"
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
#ifdef MIME_SVC_NtUserSetWindowState
    table[MIME_SVC_NtUserSetWindowState & 0xFFF] = (SERVICE){(void *)NtUserSetWindowState, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetWindowState has no syscall number"
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
    table[MIME_SVC_NtUserUnregisterClass & 0xFFF] = (SERVICE){(void *)NtUserUnregisterClass, STACK_ARGS(3), 1, 0};
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
#ifdef MIME_SVC_NtUserInvalidateRgn
    table[MIME_SVC_NtUserInvalidateRgn & 0xFFF] = (SERVICE){(void *)NtUserInvalidateRgn, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserInvalidateRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtUserValidateRgn
    table[MIME_SVC_NtUserValidateRgn & 0xFFF] = (SERVICE){(void *)NtUserValidateRgn, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserValidateRgn has no syscall number"
#endif
#ifdef MIME_SVC_NtUserBeginDeferWindowPos
    table[MIME_SVC_NtUserBeginDeferWindowPos & 0xFFF] = (SERVICE){(void *)NtUserBeginDeferWindowPos, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserBeginDeferWindowPos has no syscall number"
#endif
#ifdef MIME_SVC_NtUserDeferWindowPosAndBand
    table[MIME_SVC_NtUserDeferWindowPosAndBand & 0xFFF] = (SERVICE){(void *)NtUserDeferWindowPosAndBand, STACK_ARGS(10), 1, 0};
#else
#warning "NtUserDeferWindowPosAndBand has no syscall number"
#endif
#ifdef MIME_SVC_NtUserEndDeferWindowPosEx
    table[MIME_SVC_NtUserEndDeferWindowPosEx & 0xFFF] = (SERVICE){(void *)NtUserEndDeferWindowPosEx, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserEndDeferWindowPosEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserLockWindowUpdate
    table[MIME_SVC_NtUserLockWindowUpdate & 0xFFF] = (SERVICE){(void *)NtUserLockWindowUpdate, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserLockWindowUpdate has no syscall number"
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
#ifdef MIME_SVC_NtGdiGetPublicFontTableChangeCookie
    table[MIME_SVC_NtGdiGetPublicFontTableChangeCookie & 0xFFF] = (SERVICE){(void *)NtGdiGetPublicFontTableChangeCookie, STACK_ARGS(0), 1, 0};
#else
#warning "NtGdiGetPublicFontTableChangeCookie has no syscall number"
#endif
#ifdef MIME_SVC_NtUserChangeWindowMessageFilterEx
    table[MIME_SVC_NtUserChangeWindowMessageFilterEx & 0xFFF] = (SERVICE){(void *)NtUserChangeWindowMessageFilterEx, STACK_ARGS(4), 1, 0};
#else
#warning "NtUserChangeWindowMessageFilterEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserChildWindowFromPointEx
    table[MIME_SVC_NtUserChildWindowFromPointEx & 0xFFF] = (SERVICE){(void *)NtUserChildWindowFromPointEx, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserChildWindowFromPointEx has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetKeyboardLayoutName
    table[MIME_SVC_NtUserGetKeyboardLayoutName & 0xFFF] = (SERVICE){(void *)NtUserGetKeyboardLayoutName, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserGetKeyboardLayoutName has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetPointerDevices
    table[MIME_SVC_NtUserGetPointerDevices & 0xFFF] = (SERVICE){(void *)NtUserGetPointerDevices, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetPointerDevices has no syscall number"
#endif
#ifdef MIME_SVC_NtUserGetWindowDisplayAffinity
    table[MIME_SVC_NtUserGetWindowDisplayAffinity & 0xFFF] = (SERVICE){(void *)NtUserGetWindowDisplayAffinity, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserGetWindowDisplayAffinity has no syscall number"
#endif
#ifdef MIME_SVC_NtUserPostQuitMessage
    table[MIME_SVC_NtUserPostQuitMessage & 0xFFF] = (SERVICE){(void *)NtUserPostQuitMessage, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserPostQuitMessage has no syscall number"
#endif
#ifdef MIME_SVC_NtUserRegisterPointerDeviceNotifications
    table[MIME_SVC_NtUserRegisterPointerDeviceNotifications & 0xFFF] = (SERVICE){(void *)NtUserRegisterPointerDeviceNotifications, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserRegisterPointerDeviceNotifications has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetAdditionalForegroundBoostProcesses
    table[MIME_SVC_NtUserSetAdditionalForegroundBoostProcesses & 0xFFF] = (SERVICE){(void *)NtUserSetAdditionalForegroundBoostProcesses, STACK_ARGS(3), 1, 0};
#else
#warning "NtUserSetAdditionalForegroundBoostProcesses has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetProcessLaunchForegroundPolicy
    table[MIME_SVC_NtUserSetProcessLaunchForegroundPolicy & 0xFFF] = (SERVICE){(void *)NtUserSetProcessLaunchForegroundPolicy, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetProcessLaunchForegroundPolicy has no syscall number"
#endif
#ifdef MIME_SVC_NtUserSetWindowDisplayAffinity
    table[MIME_SVC_NtUserSetWindowDisplayAffinity & 0xFFF] = (SERVICE){(void *)NtUserSetWindowDisplayAffinity, STACK_ARGS(2), 1, 0};
#else
#warning "NtUserSetWindowDisplayAffinity has no syscall number"
#endif
#ifdef MIME_SVC_NtUserThreadMessageQueueAttached
    table[MIME_SVC_NtUserThreadMessageQueueAttached & 0xFFF] = (SERVICE){(void *)NtUserThreadMessageQueueAttached, STACK_ARGS(1), 1, 0};
#else
#warning "NtUserThreadMessageQueueAttached has no syscall number"
#endif
#ifdef MIME_SVC_NtUserZapActiveAndFocus
    table[MIME_SVC_NtUserZapActiveAndFocus & 0xFFF] = (SERVICE){(void *)NtUserZapActiveAndFocus, STACK_ARGS(0), 1, 0};
#else
#warning "NtUserZapActiveAndFocus has no syscall number"
#endif
}
