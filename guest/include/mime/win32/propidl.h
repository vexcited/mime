/* property values and their ownership. */
#ifndef MIME_WIN32_PROPIDL_H
#define MIME_WIN32_PROPIDL_H

#include <mime/win32/oleauto.h>

#define VT_STREAM 66
#define VT_STORAGE 67
#define VT_STREAMED_OBJECT 68
#define VT_STORED_OBJECT 69
#define VT_BLOBOBJECT 70
#define VT_CF 71
#define VT_VERSIONED_STREAM 73
#define VT_BSTR_BLOB 0x0fff

typedef struct tagPROPVARIANT PROPVARIANT, *LPPROPVARIANT;
typedef struct IStream IStream;
typedef struct IStorage IStorage;
typedef struct tagBLOB { ULONG cbSize; BYTE *pBlobData; } BLOB;
typedef struct tagBSTRBLOB { ULONG cbSize; BYTE *pData; } BSTRBLOB;
typedef struct tagCLIPDATA { ULONG cbSize; LONG ulClipFmt; BYTE *pClipData; } CLIPDATA;
typedef struct tagVERSIONEDSTREAM { GUID guidVersion; IStream *pStream; } VERSIONEDSTREAM;

#define MIME_PROPERTY_ARRAY(type, name) typedef struct tag##name { ULONG cElems; type *pElems; } name
MIME_PROPERTY_ARRAY(CHAR, CAC);
MIME_PROPERTY_ARRAY(UCHAR, CAUB);
MIME_PROPERTY_ARRAY(SHORT, CAI);
MIME_PROPERTY_ARRAY(USHORT, CAUI);
MIME_PROPERTY_ARRAY(LONG, CAL);
MIME_PROPERTY_ARRAY(ULONG, CAUL);
MIME_PROPERTY_ARRAY(LARGE_INTEGER, CAH);
MIME_PROPERTY_ARRAY(ULARGE_INTEGER, CAUH);
MIME_PROPERTY_ARRAY(FLOAT, CAFLT);
MIME_PROPERTY_ARRAY(DOUBLE, CADBL);
MIME_PROPERTY_ARRAY(VARIANT_BOOL, CABOOL);
MIME_PROPERTY_ARRAY(SCODE, CASCODE);
MIME_PROPERTY_ARRAY(CY, CACY);
MIME_PROPERTY_ARRAY(DATE, CADATE);
MIME_PROPERTY_ARRAY(FILETIME, CAFILETIME);
MIME_PROPERTY_ARRAY(CLSID, CACLSID);
MIME_PROPERTY_ARRAY(CLIPDATA, CACLIPDATA);
MIME_PROPERTY_ARRAY(BSTR, CABSTR);
MIME_PROPERTY_ARRAY(BSTRBLOB, CABSTRBLOB);
MIME_PROPERTY_ARRAY(LPSTR, CALPSTR);
MIME_PROPERTY_ARRAY(LPWSTR, CALPWSTR);
MIME_PROPERTY_ARRAY(PROPVARIANT, CAPROPVARIANT);
#undef MIME_PROPERTY_ARRAY

struct tagPROPVARIANT {
    union {
        struct {
            VARTYPE vt;
            WORD wReserved1, wReserved2, wReserved3;
            union {
                CHAR cVal;
                UCHAR bVal;
                SHORT iVal;
                USHORT uiVal;
                LONG lVal;
                ULONG ulVal;
                INT intVal;
                UINT uintVal;
                LARGE_INTEGER hVal;
                ULARGE_INTEGER uhVal;
                FLOAT fltVal;
                DOUBLE dblVal;
                VARIANT_BOOL boolVal;
                SCODE scode;
                CY cyVal;
                DATE date;
                FILETIME filetime;
                CLSID *puuid;
                CLIPDATA *pclipdata;
                BSTR bstrVal;
                BSTRBLOB bstrblobVal;
                BLOB blob;
                LPSTR pszVal;
                LPWSTR pwszVal;
                IUnknown *punkVal;
                IDispatch *pdispVal;
                IStream *pStream;
                IStorage *pStorage;
                VERSIONEDSTREAM *pVersionedStream;
                SAFEARRAY *parray;
                CAC cac;
                CAUB caub;
                CAI cai;
                CAUI caui;
                CAL cal;
                CAUL caul;
                CAH cah;
                CAUH cauh;
                CAFLT caflt;
                CADBL cadbl;
                CABOOL cabool;
                CASCODE cascode;
                CACY cacy;
                CADATE cadate;
                CAFILETIME cafiletime;
                CACLSID cauuid;
                CACLIPDATA caclipdata;
                CABSTR cabstr;
                CABSTRBLOB cabstrblob;
                CALPSTR calpstr;
                CALPWSTR calpwstr;
                CAPROPVARIANT capropvar;
                void *byref;
                IUnknown **ppunkVal;
                PROPVARIANT *pvarVal;
            };
        };
        DECIMAL decVal;
    };
};
MIME_STATIC_ASSERT(sizeof(PROPVARIANT) == (sizeof(void *) == 4 ? 16 : 24), "PROPVARIANT size");
MIME_FIELD_AT(PROPVARIANT, pwszVal, 8);
MIME_FIELD_AT(PROPVARIANT, capropvar.pElems, sizeof(void *) == 4 ? 12 : 16);

WINOLEAPI HRESULT WINAPI PropVariantClear(PROPVARIANT *value);
WINOLEAPI HRESULT WINAPI PropVariantCopy(PROPVARIANT *dest, const PROPVARIANT *source);

#endif
