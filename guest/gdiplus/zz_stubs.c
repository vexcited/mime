/*
 * exports that are not written yet. Each reports that it is not implemented.
 */
#include "gp_private.h"

WINGDIPAPI GpStatus WINAPI GdipAddPathString(GpPath*, const WCHAR*, INT, const GpFontFamily*, INT, REAL, const RectF*, const GpStringFormat*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipAddPathStringI(GpPath*, const WCHAR*, INT, const GpFontFamily*, INT, REAL, const Rect*, const GpStringFormat*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipBitmapApplyEffect(GpBitmap*, CGpEffect*, RECT*, BOOL, VOID**, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipBitmapCreateApplyEffect(GpBitmap**, INT, CGpEffect*, RECT*, RECT*, GpBitmap**, BOOL, VOID**, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogram(GpBitmap*, HistogramFormat, UINT, UINT*, UINT*, UINT*, UINT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipBitmapGetHistogramSize(HistogramFormat, UINT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCloneFont(GpFont*, GpFont**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCloneFontFamily(GpFontFamily*, GpFontFamily**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCloneStringFormat(const GpStringFormat*, GpStringFormat**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlus(const GpGraphics*, GpMetafile*, BOOL*, EmfType, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlusToFile(const GpGraphics*, GpMetafile*, BOOL*, const WCHAR*, EmfType, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipConvertToEmfPlusToStream(const GpGraphics*, GpMetafile*, BOOL*, IStream*, EmfType, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateCachedBitmap(GpBitmap*, GpGraphics*, GpCachedBitmap**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateEffect(const GUID, CGpEffect**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateFont(const GpFontFamily*, REAL, INT, Unit, GpFont**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateFontFamilyFromName(const WCHAR*, GpFontCollection*, GpFontFamily**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateFontFromDC(HDC, GpFont**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontA(HDC, const LOGFONTA*, GpFont**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateFontFromLogfontW(HDC, const LOGFONTW*, GpFont**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromEmf(HENHMETAFILE, BOOL, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromFile(const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromStream(IStream*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromWmf(HMETAFILE, BOOL, const WmfPlaceableFileHeader*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromWmfFile(const WCHAR*, const WmfPlaceableFileHeader*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateStreamOnFile(const WCHAR*, UINT, IStream**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipCreateStringFormat(INT, LANGID, GpStringFormat**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDeleteCachedBitmap(GpCachedBitmap*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDeleteEffect(CGpEffect*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDeleteFont(GpFont*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDeleteFontFamily(GpFontFamily*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDeletePrivateFontCollection(GpFontCollection**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDeleteStringFormat(GpStringFormat*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDrawCachedBitmap(GpGraphics*, GpCachedBitmap*, INT, INT) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDrawDriverString(GpGraphics*, const UINT16*, INT, const GpFont*, const GpBrush*, const PointF*, INT, const GpMatrix*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDrawImageFX(GpGraphics*, GpImage*, GpRectF*, GpMatrix*, CGpEffect*, GpImageAttributes*, GpUnit) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipDrawString(GpGraphics*, const WCHAR*, INT, const GpFont*, const RectF*, const GpStringFormat*, const GpBrush*) { return NotImplemented; }
WINGDIPAPI UINT WINAPI GdipEmfToWmfBits(HENHMETAFILE, UINT, LPBYTE, INT, INT) { return 0; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPoint(GpGraphics*, const GpMetafile*, const PointF*, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPointI(GpGraphics*, const GpMetafile*, const Point*, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPoints(GpGraphics*, const GpMetafile*, const PointF*, INT, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestPointsI(GpGraphics*, const GpMetafile*, const Point*, INT, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestRect(GpGraphics*, const GpMetafile*, const RectF*, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileDestRectI(GpGraphics*, const GpMetafile*, const Rect*, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPoint(GpGraphics*, const GpMetafile*, const PointF*, const RectF*, Unit, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPointI(GpGraphics*, const GpMetafile*, const Point*, const Rect*, Unit, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPoints(GpGraphics*, const GpMetafile*, const PointF*, INT, const RectF*, Unit, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestPointsI(GpGraphics*, const GpMetafile*, const Point*, INT, const Rect*, Unit, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestRect(GpGraphics*, const GpMetafile*, const RectF*, const RectF*, Unit, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipEnumerateMetafileSrcRectDestRectI(GpGraphics*, const GpMetafile*, const Rect*, const Rect*, Unit, EnumerateMetafileProc, VOID*, const GpImageAttributes*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipFindFirstImageItem(GpImage*, ImageItemData*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipFindNextImageItem(GpImage*, ImageItemData*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipFontCollectionEnumerable(GpFontCollection*, GpGraphics*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipFontCollectionEnumerate(GpFontCollection*, INT, GpFontFamily**, INT*, GpGraphics*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetCellAscent(const GpFontFamily*, INT, UINT16*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetCellDescent(const GpFontFamily*, INT, UINT16*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetEffectParameterSize(CGpEffect*, UINT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetEffectParameters(CGpEffect*, UINT*, VOID*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetEmHeight(const GpFontFamily*, INT, UINT16*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFamily(GpFont*, GpFontFamily**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFamilyName(const GpFontFamily*, WCHAR[LF_FACESIZE], LANGID) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyCount(GpFontCollection*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontCollectionFamilyList(GpFontCollection*, INT, GpFontFamily**, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontHeight(const GpFont*, const GpGraphics*, REAL*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontHeightGivenDPI(const GpFont*, REAL, REAL*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontSize(GpFont*, REAL*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontStyle(GpFont*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetFontUnit(GpFont*, Unit*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilyMonospace(GpFontFamily**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySansSerif(GpFontFamily**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetGenericFontFamilySerif(GpFontFamily**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetHemfFromMetafile(GpMetafile*, HENHMETAFILE*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetImageItemData(GpImage*, ImageItemData*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetLineSpacing(const GpFontFamily*, INT, UINT16*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetLogFontA(GpFont*, GpGraphics*, LOGFONTA*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetLogFontW(GpFont*, GpGraphics*, LOGFONTW*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetMetafileDownLevelRasterizationLimit(const GpMetafile*, UINT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromEmf(HENHMETAFILE, MetafileHeader*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromFile(const WCHAR*, MetafileHeader*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromMetafile(GpMetafile*, MetafileHeader*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromStream(IStream*, MetafileHeader*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatAlign(const GpStringFormat*, StringAlignment*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatDigitSubstitution(const GpStringFormat*, LANGID*, StringDigitSubstitute*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatFlags(const GpStringFormat*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatHotkeyPrefix(const GpStringFormat*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatLineAlign(const GpStringFormat*, StringAlignment*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatMeasurableCharacterRangeCount(const GpStringFormat*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStopCount(const GpStringFormat*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTabStops(const GpStringFormat*, INT, REAL*, REAL*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipGetStringFormatTrimming(const GpStringFormat*, StringTrimming*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipImageForceValidation(GpImage*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipImageSetAbort(GpImage*, GdiplusAbort*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipIsStyleAvailable(const GpFontFamily*, INT, BOOL*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipMeasureCharacterRanges(GpGraphics*, const WCHAR*, INT, const GpFont*, const RectF*, const GpStringFormat*, INT, GpRegion**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipMeasureDriverString(GpGraphics*, const UINT16*, INT, const GpFont*, const PointF*, INT, const GpMatrix*, RectF*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipMeasureString(GpGraphics*, const WCHAR*, INT, const GpFont*, const RectF*, const GpStringFormat*, RectF*, INT*, INT*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipNewInstalledFontCollection(GpFontCollection**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipNewPrivateFontCollection(GpFontCollection**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipPlayMetafileRecord(const GpMetafile*, EmfPlusRecordType, UINT, UINT, const BYTE*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipPrivateAddFontFile(GpFontCollection*, const WCHAR*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipPrivateAddMemoryFont(GpFontCollection*, const void*, INT) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipRecordMetafile(HDC, EmfType, const GpRectF*, MetafileFrameUnit, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileFileName(const WCHAR*, HDC, EmfType, const GpRectF*, MetafileFrameUnit, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileFileNameI(const WCHAR*, HDC, EmfType, const GpRect*, MetafileFrameUnit, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileI(HDC, EmfType, const GpRect*, MetafileFrameUnit, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileStream(IStream*, HDC, EmfType, const GpRectF*, MetafileFrameUnit, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipRecordMetafileStreamI(IStream*, HDC, EmfType, const GpRect*, MetafileFrameUnit, const WCHAR*, GpMetafile**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetEffectParameters(CGpEffect*, const VOID*, UINT) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetMetafileDownLevelRasterizationLimit(GpMetafile*, UINT) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatAlign(GpStringFormat*, StringAlignment) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatDigitSubstitution(GpStringFormat*, LANGID, StringDigitSubstitute) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatFlags(GpStringFormat*, INT) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatHotkeyPrefix(GpStringFormat*, INT) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatLineAlign(GpStringFormat*, StringAlignment) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatMeasurableCharacterRanges(GpStringFormat*, INT, const CharacterRange*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTabStops(GpStringFormat*, REAL, INT, const REAL*) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipSetStringFormatTrimming(GpStringFormat*, StringTrimming) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericDefault(GpStringFormat**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipStringFormatGetGenericTypographic(GpStringFormat**) { return NotImplemented; }
WINGDIPAPI GpStatus WINAPI GdipTestControl(GpTestControlEnum, void*) { return NotImplemented; }
