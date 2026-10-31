/*
 * the interfaces of the active directory service interfaces that the WinNT provider serves, with the calls between
 * main.c and winnt.c.
 */
#ifndef ACTIVEDS_H
#define ACTIVEDS_H

#include <mime/win32/winbase.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winerror.h>

#define E_ADS_BAD_PATHNAME ((HRESULT)0x80005000)
#define E_ADS_UNKNOWN_OBJECT ((HRESULT)0x80005004)
#define E_ADS_PROPERTY_NOT_SUPPORTED ((HRESULT)0x80005006)
#define E_ADS_BAD_PARAMETER ((HRESULT)0x80005008)
#define E_ADS_PROPERTY_NOT_FOUND ((HRESULT)0x8000500D)
#define E_ADS_INVALID_FILTER ((HRESULT)0x80005014)

typedef struct IADs IADs;
typedef struct IADsContainer IADsContainer;
typedef struct IADsMembers IADsMembers;
typedef struct IADsGroup IADsGroup;

#define IADs_METHODS(T)                                                                                                \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Class)(T * This, BSTR * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_GUID)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_ADsPath)(T * This, BSTR * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_Parent)(T * This, BSTR * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_Schema)(T * This, BSTR * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetInfo)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *SetInfo)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Get)(T * This, BSTR name, VARIANT * value);                                             \
    HRESULT(STDMETHODCALLTYPE *Put)(T * This, BSTR name, VARIANT value);                                               \
    HRESULT(STDMETHODCALLTYPE *GetEx)(T * This, BSTR name, VARIANT * value);                                           \
    HRESULT(STDMETHODCALLTYPE *PutEx)(T * This, LONG control, BSTR name, VARIANT value);                               \
    HRESULT(STDMETHODCALLTYPE *GetInfoEx)(T * This, VARIANT names, LONG reserved);
MIME_COM_INTERFACE(IADs)

#define IADsContainer_METHODS(T)                                                                                       \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);                                                     \
    HRESULT(STDMETHODCALLTYPE *get__NewEnum)(T * This, IUnknown * *enumerator);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Filter)(T * This, VARIANT * filter);                                                \
    HRESULT(STDMETHODCALLTYPE *put_Filter)(T * This, VARIANT filter);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_Hints)(T * This, VARIANT * hints);                                                  \
    HRESULT(STDMETHODCALLTYPE *put_Hints)(T * This, VARIANT hints);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetObject)(T * This, BSTR class_name, BSTR name, IDispatch * *object);                  \
    HRESULT(STDMETHODCALLTYPE *Create)(T * This, BSTR class_name, BSTR name, IDispatch * *object);                     \
    HRESULT(STDMETHODCALLTYPE *Delete)(T * This, BSTR class_name, BSTR name);                                          \
    HRESULT(STDMETHODCALLTYPE *CopyHere)(T * This, BSTR source, BSTR name, IDispatch * *object);                       \
    HRESULT(STDMETHODCALLTYPE *MoveHere)(T * This, BSTR source, BSTR name, IDispatch * *object);
MIME_COM_INTERFACE(IADsContainer)

#define IADsMembers_METHODS(T)                                                                                         \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);                                                     \
    HRESULT(STDMETHODCALLTYPE *get__NewEnum)(T * This, IUnknown * *enumerator);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Filter)(T * This, VARIANT * filter);                                                \
    HRESULT(STDMETHODCALLTYPE *put_Filter)(T * This, VARIANT filter);
MIME_COM_INTERFACE(IADsMembers)

#define IADsGroup_METHODS(T)                                                                                           \
    IADs_METHODS(T)                                                                                                    \
    HRESULT(STDMETHODCALLTYPE *get_Description)(T * This, BSTR * value);                                               \
    HRESULT(STDMETHODCALLTYPE *put_Description)(T * This, BSTR value);                                                 \
    HRESULT(STDMETHODCALLTYPE *Members)(T * This, IADsMembers * *members);                                             \
    HRESULT(STDMETHODCALLTYPE *IsMember)(T * This, BSTR member, VARIANT_BOOL * result);                                \
    HRESULT(STDMETHODCALLTYPE *Add)(T * This, BSTR member);                                                            \
    HRESULT(STDMETHODCALLTYPE *Remove)(T * This, BSTR member);
MIME_COM_INTERFACE(IADsGroup)

MIME_DEFINE_GUID(IID_IADs, 0xFD8256D0, 0xFD15, 0x11CE, 0xAB, 0xC4, 0x02, 0x60, 0x8C, 0x9E, 0x75, 0x53);
MIME_DEFINE_GUID(IID_IADsContainer, 0x001677D0, 0xFD16, 0x11CE, 0xAB, 0xC4, 0x02, 0x60, 0x8C, 0x9E, 0x75, 0x53);
MIME_DEFINE_GUID(IID_IADsGroup, 0x27636B00, 0x410F, 0x11CF, 0xB1, 0xFF, 0x02, 0x60, 0x8C, 0x9E, 0x75, 0x53);
MIME_DEFINE_GUID(IID_IADsMembers, 0x451A0030, 0x72EC, 0x11CF, 0xB0, 0x3B, 0x00, 0xAA, 0x00, 0x6E, 0x09, 0x75);

/* opens an object of the WinNT provider by a path that starts with its name. */
HRESULT WinNtOpen(LPCWSTR path, REFIID iid, void **object);

#endif
