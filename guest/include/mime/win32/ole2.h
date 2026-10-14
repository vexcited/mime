/*
 * the ole layer of ole32.dll: running objects, embedding, menu descriptors and
 * the class information of HKCR\CLSID.
 */
#ifndef MIME_WIN32_OLE2_H
#define MIME_WIN32_OLE2_H

#include <mime/win32/oleidl.h>

WINOLEAPI BOOL WINAPI OleIsRunning(IOleObject *object);
WINOLEAPI HRESULT WINAPI OleRun(IUnknown *object);
WINOLEAPI HRESULT WINAPI OleLockRunning(IUnknown *object, BOOL lock, BOOL last_unlock_closes);
WINOLEAPI HRESULT WINAPI OleSetContainedObject(IUnknown *object, BOOL contained);

WINOLEAPI HRESULT WINAPI CreateDataAdviseHolder(IDataAdviseHolder **holder);
WINOLEAPI HRESULT WINAPI CreateOleAdviseHolder(IOleAdviseHolder **holder);

WINOLEAPI HOLEMENU WINAPI OleCreateMenuDescriptor(HMENU shared, LPOLEMENUGROUPWIDTHS widths);
WINOLEAPI HRESULT WINAPI OleDestroyMenuDescriptor(HOLEMENU descriptor);
WINOLEAPI HRESULT WINAPI OleSetMenuDescriptor(HOLEMENU descriptor, HWND frame, HWND active_object,
                                              IOleInPlaceFrame *frame_object, IOleInPlaceActiveObject *active);
WINOLEAPI HRESULT WINAPI OleTranslateAccelerator(IOleInPlaceFrame *frame, OLEINPLACEFRAMEINFO *info, MSG *msg);
WINOLEAPI BOOL WINAPI IsAccelerator(HACCEL accel, int count, MSG *msg, WORD *command);

WINOLEAPI HRESULT WINAPI OleRegGetUserType(REFCLSID clsid, DWORD form, LPOLESTR *user_type);
WINOLEAPI HRESULT WINAPI OleRegGetMiscStatus(REFCLSID clsid, DWORD aspect, DWORD *status);
WINOLEAPI HRESULT WINAPI OleRegEnumVerbs(REFCLSID clsid, IEnumOLEVERB **enumerator);

WINOLEAPI HRESULT WINAPI OleCreate(REFCLSID clsid, REFIID riid, DWORD render, FORMATETC *format,
                                   IOleClientSite *site, IStorage *storage, void **object);
WINOLEAPI HRESULT WINAPI OleCreateFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                           IOleClientSite *site, IStorage *storage, void **object);
WINOLEAPI HRESULT WINAPI OleCreateLinkFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                               IOleClientSite *site, IStorage *storage, void **object);
WINOLEAPI HRESULT WINAPI OleCreateStaticFromData(IDataObject *data, REFIID riid, DWORD render, FORMATETC *format,
                                                 IOleClientSite *site, IStorage *storage, void **object);
WINOLEAPI HRESULT WINAPI OleCreateLinkToFile(LPCOLESTR path, REFIID riid, DWORD render, FORMATETC *format,
                                             IOleClientSite *site, IStorage *storage, void **object);
WINOLEAPI HRESULT WINAPI OleCreateFromFile(REFCLSID clsid, LPCOLESTR path, REFIID riid, DWORD render,
                                           FORMATETC *format, IOleClientSite *site, IStorage *storage,
                                           void **object);
WINOLEAPI HRESULT WINAPI OleLoad(IStorage *storage, REFIID riid, IOleClientSite *site, void **object);
WINOLEAPI HRESULT WINAPI OleSave(IPersistStorage *object, IStorage *storage, BOOL same_as_load);
WINOLEAPI HRESULT WINAPI OleSaveToStream(IPersistStream *object, IStream *stream);
WINOLEAPI HRESULT WINAPI OleQueryLinkFromData(IDataObject *data);
WINOLEAPI HRESULT WINAPI OleQueryCreateFromData(IDataObject *data);
WINOLEAPI HGLOBAL WINAPI OleGetIconOfClass(REFCLSID clsid, LPOLESTR label, BOOL use_type_as_label);
WINOLEAPI HANDLE WINAPI OleDuplicateData(HANDLE source, CLIPFORMAT format, UINT flags);

#endif
