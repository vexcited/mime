/*
 * the text services framework: how a program with a document and the text services that edit it meet. a thread
 * manager keeps the document managers of a thread, each a stack of contexts, and a context is the program's text
 * store as the services see it. interfaces are declared the C way, a struct whose first member points to a table
 * of functions, as msctf.h declares them.
 */
#ifndef MIME_WIN32_MSCTF_H
#define MIME_WIN32_MSCTF_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/objidl.h>
#include <mime/win32/oleidl.h>
#include <mime/win32/textstor.h>

#ifndef TFAPI
#define TFAPI DECLSPEC_IMPORT
#endif

#define TF_E_LOCKED ((HRESULT)0x80040500L)
#define TF_E_STACKFULL ((HRESULT)0x80040501L)
#define TF_E_NOTOWNEDRANGE ((HRESULT)0x80040502L)
#define TF_E_NOPROVIDER ((HRESULT)0x80040503L)
#define TF_E_DISCONNECTED ((HRESULT)0x80040504L)
#define TF_E_INVALIDVIEW ((HRESULT)0x80040505L)
#define TF_E_ALREADY_EXISTS ((HRESULT)0x80040506L)
#define TF_E_RANGE_NOT_COVERED ((HRESULT)0x80040507L)
#define TF_E_COMPOSITION_REJECTED ((HRESULT)0x80040508L)
#define TF_E_EMPTYCONTEXT ((HRESULT)0x80040509L)
#define TF_E_INVALIDPOS TS_E_INVALIDPOS
#define TF_E_NOLOCK TS_E_NOLOCK
#define TF_E_NOOBJECT TS_E_NOOBJECT
#define TF_E_NOSERVICE TS_E_NOSERVICE
#define TF_E_NOINTERFACE TS_E_NOINTERFACE
#define TF_E_NOSELECTION TS_E_NOSELECTION
#define TF_E_NOLAYOUT TS_E_NOLAYOUT
#define TF_E_INVALIDPOINT TS_E_INVALIDPOINT
#define TF_E_SYNCHRONOUS TS_E_SYNCHRONOUS
#define TF_E_READONLY TS_E_READONLY
#define TF_E_FORMAT TS_E_FORMAT
#define TF_S_ASYNC TS_S_ASYNC

#ifndef CONNECT_E_NOCONNECTION
#define CONNECT_E_NOCONNECTION ((HRESULT)0x80040200L)
#define CONNECT_E_ADVISELIMIT ((HRESULT)0x80040201L)
#define CONNECT_E_CANNOTCONNECT ((HRESULT)0x80040202L)
#endif

#define TF_CLIENTID_NULL ((TfClientId)0)
#define TF_INVALID_COOKIE 0xFFFFFFFFu
#define TF_DEFAULT_SELECTION TS_DEFAULT_SELECTION
#define TF_INVALID_EDIT_COOKIE ((TfEditCookie)0)

/* ActivateEx. */
#define TF_TMAE_NOACTIVATETIP 0x1
#define TF_TMAE_SECUREMODE 0x2
#define TF_TMAE_UIELEMENTENABLEDONLY 0x4
#define TF_TMAE_COMLESS 0x8
#define TF_TMAE_WOW16 0x10
#define TF_TMAE_NOACTIVATEKEYBOARDLAYOUT 0x20
#define TF_TMAE_CONSOLE 0x40

#define TF_POPF_ALL 0x1

/* RequestEditSession. */
#define TF_ES_ASYNCDONTCARE 0x0
#define TF_ES_SYNC 0x1
#define TF_ES_READ 0x2
#define TF_ES_READWRITE 0x6
#define TF_ES_ASYNC 0x8

#define TF_SD_READONLY TS_SD_READONLY
#define TF_SD_LOADING TS_SD_LOADING
#define TF_SS_DISJOINTSEL TS_SS_DISJOINTSEL
#define TF_SS_REGIONS TS_SS_REGIONS
#define TF_SS_TRANSITORY TS_SS_TRANSITORY

#define TF_GTP_INCL_TEXT 0x1
#define TF_TF_MOVESTART 1
#define TF_TF_IGNOREEND 2
#define TF_ST_CORRECTION 1
#define TF_IE_CORRECTION 1
#define TF_IAS_NOQUERY 0x1
#define TF_IAS_QUERYONLY 0x2
#define TF_IAS_NO_DEFAULT_COMPOSITION 0x80000000u
#define TF_HF_OBJECT 1
#define TF_CHAR_EMBEDDED TS_CHAR_EMBEDDED

#define TF_PROFILETYPE_INPUTPROCESSOR 0x1
#define TF_PROFILETYPE_KEYBOARDLAYOUT 0x2
#define TF_IPSINK_FLAG_ACTIVE 0x1
#define TF_IPP_FLAG_ACTIVE 0x1
#define TF_IPP_FLAG_ENABLED 0x2
#define TF_IPP_FLAG_SUBSTITUTEDBYINPUTPROCESSOR 0x4
#define TF_IPP_CAPS_DISABLEONTRANSITORY 0x1
#define TF_IPP_CAPS_SECUREMODESUPPORT 0x2
#define TF_IPP_CAPS_UIELEMENTENABLED 0x4
#define TF_IPP_CAPS_COMLESSSUPPORT 0x8
#define TF_IPP_CAPS_WOW16SUPPORT 0x10
#define TF_IPP_CAPS_IMMERSIVESUPPORT 0x10000
#define TF_IPP_CAPS_SYSTRAYSUPPORT 0x20000

typedef DWORD TfEditCookie;
typedef DWORD TfClientId;
typedef DWORD TfGuidAtom;

typedef enum {
    TF_ANCHOR_START = 0,
    TF_ANCHOR_END = 1,
} TfAnchor;

typedef enum {
    TF_GRAVITY_BACKWARD = 0,
    TF_GRAVITY_FORWARD = 1,
} TfGravity;

typedef enum {
    TF_SD_BACKWARD = 0,
    TF_SD_FORWARD = 1,
} TfShiftDir;

typedef enum {
    TF_AE_NONE = 0,
    TF_AE_START = 1,
    TF_AE_END = 2,
} TfActiveSelEnd;

typedef enum {
    TF_LS_NONE = 0,
    TF_LS_SOLID = 1,
    TF_LS_DOT = 2,
    TF_LS_DASH = 3,
    TF_LS_SQUIGGLE = 4,
} TF_DA_LINESTYLE;

typedef enum {
    TF_CT_NONE = 0,
    TF_CT_SYSCOLOR = 1,
    TF_CT_COLORREF = 2,
} TF_DA_COLORTYPE;

typedef enum {
    TF_ATTR_INPUT = 0,
    TF_ATTR_TARGET_CONVERTED = 1,
    TF_ATTR_CONVERTED = 2,
    TF_ATTR_TARGET_NOTCONVERTED = 3,
    TF_ATTR_INPUT_ERROR = 4,
    TF_ATTR_FIXEDCONVERTED = 5,
    TF_ATTR_OTHER = -1,
} TF_DA_ATTR_INFO;

typedef struct ITfRange ITfRange;
typedef struct ITfPersistentPropertyLoaderACP ITfPersistentPropertyLoaderACP;

typedef struct TF_HALTCOND {
    ITfRange *pHaltRange;
    TfAnchor aHaltPos;
    DWORD dwFlags;
} TF_HALTCOND;

typedef struct TF_SELECTIONSTYLE {
    TfActiveSelEnd ase;
    BOOL fInterimChar;
} TF_SELECTIONSTYLE;

typedef struct TF_SELECTION {
    ITfRange *range;
    TF_SELECTIONSTYLE style;
} TF_SELECTION;

typedef struct TF_STATUS {
    DWORD dwDynamicFlags;
    DWORD dwStaticFlags;
} TF_STATUS;

typedef struct TF_PRESERVEDKEY {
    UINT uVKey;
    UINT uModifiers;
} TF_PRESERVEDKEY;

typedef struct TF_DA_COLOR {
    TF_DA_COLORTYPE type;
    union {
        int nIndex;
        COLORREF cr;
    };
} TF_DA_COLOR;

typedef struct TF_DISPLAYATTRIBUTE {
    TF_DA_COLOR crText;
    TF_DA_COLOR crBk;
    TF_DA_LINESTYLE lsStyle;
    BOOL fBoldLine;
    TF_DA_COLOR crLine;
    TF_DA_ATTR_INFO bAttr;
} TF_DISPLAYATTRIBUTE;

typedef struct TF_INPUTPROCESSORPROFILE {
    DWORD dwProfileType;
    LANGID langid;
    CLSID clsid;
    GUID guidProfile;
    GUID catid;
    HKL hklSubstitute;
    DWORD dwCaps;
    HKL hkl;
    DWORD dwFlags;
} TF_INPUTPROCESSORPROFILE;

typedef struct TF_LANGUAGEPROFILE {
    CLSID clsid;
    LANGID langid;
    GUID catid;
    BOOL fActive;
    GUID guidProfile;
} TF_LANGUAGEPROFILE;

typedef struct TF_PROPERTYVAL {
    GUID guidId;
    VARIANT varValue;
} TF_PROPERTYVAL;

typedef struct TF_PERSISTENT_PROPERTY_HEADER_ACP {
    GUID guidType;
    LONG ichStart;
    LONG cch;
    ULONG cb;
    DWORD dwPrivate;
    CLSID clsidTIP;
} TF_PERSISTENT_PROPERTY_HEADER_ACP;

/* ------------------------------------------------------------------ classes and interface ids */

MIME_DEFINE_GUID(CLSID_TF_ThreadMgr, 0x529A9E6B, 0x6587, 0x4F23, 0xAB, 0x9E, 0x9C, 0x7D, 0x68, 0x3E, 0x3C, 0x50);
MIME_DEFINE_GUID(CLSID_TF_InputProcessorProfiles, 0x33C53A50, 0xF456, 0x4884, 0xB0, 0x49, 0x85, 0xFD, 0x64, 0x3E, 0xCF,
                 0xED);
MIME_DEFINE_GUID(CLSID_TF_CategoryMgr, 0xA4B544A1, 0x438D, 0x4B41, 0x93, 0x25, 0x86, 0x95, 0x23, 0xE2, 0xD6, 0xC7);
MIME_DEFINE_GUID(CLSID_TF_DisplayAttributeMgr, 0x3CE74DE4, 0x53D3, 0x4D74, 0x8B, 0x83, 0x43, 0x1B, 0x38, 0x28, 0xBA,
                 0x53);
MIME_DEFINE_GUID(CLSID_TF_LangBarMgr, 0xEBB08C45, 0x6C4A, 0x4FDC, 0xAE, 0x53, 0x4E, 0xB8, 0xC4, 0xC7, 0xDB, 0x8E);
MIME_DEFINE_GUID(IID_ITfContextOwner, 0xAA80E80C, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_IEnumGUID, 0x0002E000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

MIME_DEFINE_GUID(GUID_PROP_ATTRIBUTE, 0x34B45670, 0x7526, 0x11D2, 0xA1, 0x47, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(GUID_PROP_COMPOSING, 0xE12AC060, 0xAF15, 0x11D2, 0xAF, 0xC5, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 0x58273AAD, 0x01BB, 0x4164, 0x95, 0xC6, 0x75, 0x5B, 0xA0, 0xB5,
                 0x16, 0x2D);
MIME_DEFINE_GUID(GUID_COMPARTMENT_KEYBOARD_DISABLED, 0x71A5B253, 0x1951, 0x466B, 0x9F, 0xBC, 0x9C, 0x88, 0x08, 0xFA,
                 0x84, 0xF2);
MIME_DEFINE_GUID(GUID_COMPARTMENT_EMPTYCONTEXT, 0xD7487DBF, 0x804E, 0x41C5, 0x89, 0x4D, 0xAD, 0x96, 0xFD, 0x4E, 0xEA,
                 0x13);
MIME_DEFINE_GUID(GUID_TFCAT_TIP_KEYBOARD, 0x34745C63, 0xB2F0, 0x4784, 0x8B, 0x67, 0x5E, 0x12, 0xC8, 0x70, 0x1A, 0x31);
MIME_DEFINE_GUID(GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, 0x046B8C80, 0x1647, 0x40F7, 0x9B, 0x21, 0xB9, 0x3B, 0x81, 0xAA,
                 0xBC, 0x1B);

MIME_DEFINE_GUID(IID_ITfFunctionProvider, 0x101D6610, 0x0990, 0x11D3, 0x8D, 0xF0, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(IID_IEnumTfFunctionProviders, 0xE4B24DB0, 0x0990, 0x11D3, 0x8D, 0xF0, 0x00, 0x10,
                 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(IID_ITfThreadMgr, 0xAA80E801, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfThreadMgrEx, 0x3E90ADE3, 0x7594, 0x4CB0, 0xBB, 0x58, 0x69, 0x62, 0x8F, 0x5F, 0x45, 0x8C);
MIME_DEFINE_GUID(IID_ITfCompositionView, 0xD7540241, 0xF9A1, 0x4364, 0xBE, 0xFC, 0xDB, 0xCD, 0x2C, 0x43, 0x95, 0xB7);
MIME_DEFINE_GUID(IID_ITfDocumentMgr, 0xAA80E7F4, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfContextView, 0x2433BF8E, 0x0F9B, 0x435C, 0xBA, 0x2C, 0x18, 0x06, 0x11, 0x97, 0x8C, 0x30);
MIME_DEFINE_GUID(IID_IEnumTfContextViews, 0xF0C0F8DD, 0xCF38, 0x44E1, 0xBB, 0x0F, 0x68, 0xCF, 0x0D, 0x55, 0x1C, 0x78);
MIME_DEFINE_GUID(IID_IEnumTfProperties, 0x19188CB0, 0xACA9, 0x11D2, 0xAF, 0xC5, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(IID_IEnumTfPropertyValue, 0x8ED8981B, 0x7C10, 0x4D7D, 0x9F, 0xB3, 0xAB, 0x72,
                 0xE9, 0xC7, 0x5F, 0x72);
MIME_DEFINE_GUID(IID_ITfRangeBackup, 0x463A506D, 0x6992, 0x49D2, 0x9B, 0x88, 0x93, 0xD5, 0x5E, 0x70, 0xBB, 0x16);
MIME_DEFINE_GUID(IID_ITfContext, 0xAA80E7FD, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfSource, 0x4EA48A35, 0x60AE, 0x446F, 0x8F, 0xD6, 0xE6, 0xA8, 0xD8, 0x24, 0x59, 0xF7);
MIME_DEFINE_GUID(IID_ITfInputProcessorProfiles, 0x1F02B6C5, 0x7842, 0x4EE6, 0x8A, 0x0B, 0x9A, 0x24,
                 0x18, 0x3A, 0x95, 0xCA);
MIME_DEFINE_GUID(IID_IEnumTfInputProcessorProfiles, 0x71C6E74D, 0x0F28, 0x11D8, 0xA8, 0x2A, 0x00, 0x06,
                 0x5B, 0x84, 0x43, 0x5C);
MIME_DEFINE_GUID(IID_ITfInputProcessorProfileMgr, 0x71C6E74C, 0x0F28, 0x11D8, 0xA8, 0x2A, 0x00, 0x06,
                 0x5B, 0x84, 0x43, 0x5C);
MIME_DEFINE_GUID(IID_ITfDisplayAttributeInfo, 0x70528852, 0x2F26, 0x4AEA, 0x8C, 0x96, 0x21, 0x51,
                 0x50, 0x57, 0x89, 0x32);
MIME_DEFINE_GUID(IID_IEnumTfDisplayAttributeInfo, 0x7CEF04D7, 0xCB75, 0x4E80, 0xA7, 0xAB, 0x5F, 0x5B,
                 0xC7, 0xD3, 0x32, 0xDE);
MIME_DEFINE_GUID(IID_ITfDisplayAttributeMgr, 0x8DED7393, 0x5DB1, 0x475C, 0x9E, 0x71, 0xA3, 0x91,
                 0x11, 0xB0, 0xFF, 0x67);
MIME_DEFINE_GUID(IID_ITfCategoryMgr, 0xC3ACEFB5, 0xF69D, 0x4905, 0x93, 0x8F, 0xFC, 0xAD, 0xCF, 0x4B, 0xE8, 0x30);
MIME_DEFINE_GUID(IID_IEnumTfRanges, 0xF99D3F40, 0x8E32, 0x11D2, 0xBF, 0x46, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(IID_ITfEditRecord, 0x42D4D099, 0x7C1A, 0x4A89, 0xB8, 0x36, 0x6C, 0x6F, 0x22, 0x16, 0x0D, 0xF0);
MIME_DEFINE_GUID(IID_ITfTextEditSink, 0x8127D409, 0xCCD3, 0x4683, 0x96, 0x7A, 0xB4, 0x3D, 0x5B, 0x48, 0x2B, 0xF7);
MIME_DEFINE_GUID(IID_ITfContextOwnerCompositionSink, 0x5F20AA40, 0xB57A, 0x4F34, 0x96, 0xAB, 0x35, 0x76,
                 0xF3, 0x77, 0xCC, 0x79);
MIME_DEFINE_GUID(IID_ITfActiveLanguageProfileNotifySink, 0xB246CB75, 0xA93E, 0x4652, 0xBF, 0x8C, 0xB3, 0xFE,
                 0x0C, 0xFD, 0x7E, 0x57);
MIME_DEFINE_GUID(IID_IEnumTfLanguageProfiles, 0x3D61BF11, 0xAC5F, 0x42C8, 0xA4, 0xCB, 0x93, 0x1B,
                 0xCC, 0x28, 0xC7, 0x44);
MIME_DEFINE_GUID(IID_ITfTextInputProcessor, 0xAA80E7F7, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60,
                 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfThreadMgrEventSink, 0xAA80E80E, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60,
                 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfKeystrokeMgr, 0xAA80E7F0, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfKeyEventSink, 0xAA80E7F5, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfKeyTraceEventSink, 0x1CD4C13B, 0x1C36, 0x4191, 0xA7, 0x0A, 0x7F, 0x3E,
                 0x61, 0x1F, 0x36, 0x7D);
MIME_DEFINE_GUID(IID_ITfUIElementSink, 0xEA1EA136, 0x19DF, 0x11D7, 0xA6, 0xD2, 0x00, 0x06, 0x5B, 0x84, 0x43, 0x5C);
MIME_DEFINE_GUID(IID_ITfMessagePump, 0x8F1B8AD8, 0x0B6B, 0x4874, 0x90, 0xC5, 0xBD, 0x76, 0x01, 0x1E, 0x8F, 0x7C);
MIME_DEFINE_GUID(IID_ITfClientId, 0xD60A7B49, 0x1B9F, 0x4BE2, 0xB7, 0x02, 0x47, 0xE9, 0xDC, 0x05, 0xDE, 0xC3);
MIME_DEFINE_GUID(IID_ITfLanguageProfileNotifySink, 0x43C9FE15, 0xF494, 0x4C17, 0x9D, 0xE2, 0xB8, 0xA4,
                 0xAC, 0x35, 0x0A, 0xA8);
MIME_DEFINE_GUID(IID_ITfEditSession, 0xAA80E803, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfRange, 0xAA80E7FF, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfRangeACP, 0x057A6296, 0x029B, 0x4154, 0xB7, 0x9A, 0x0D, 0x46, 0x1D, 0x4E, 0xA9, 0x4C);
MIME_DEFINE_GUID(IID_ITfInsertAtSelection, 0x55CE16BA, 0x3014, 0x41C1, 0x9C, 0xEB, 0xFA, 0xDE,
                 0x14, 0x46, 0xAC, 0x6C);
MIME_DEFINE_GUID(IID_ITfPropertyStore, 0x6834B120, 0x88CB, 0x11D2, 0xBF, 0x45, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(IID_IEnumITfCompositionView, 0x5EFD22BA, 0x7838, 0x46CB, 0x88, 0xE2, 0xCA, 0xDB,
                 0x14, 0x12, 0x4F, 0x8F);
MIME_DEFINE_GUID(IID_ITfComposition, 0x20168D64, 0x5A8F, 0x4A5A, 0xB7, 0xBD, 0xCF, 0xA2, 0x9F, 0x4D, 0x0F, 0xD9);
MIME_DEFINE_GUID(IID_ITfCompositionSink, 0xA781718C, 0x579A, 0x4B15, 0xA2, 0x80, 0x32, 0xB8, 0x57, 0x7A, 0xCC, 0x5E);
MIME_DEFINE_GUID(IID_ITfContextComposition, 0xD40C8AAE, 0xAC92, 0x4FC7, 0x9A, 0x11, 0x0E, 0xE0,
                 0xE2, 0x3A, 0xA3, 0x9B);
MIME_DEFINE_GUID(IID_ITfContextOwnerCompositionServices, 0x86462810, 0x593B, 0x4916, 0x97, 0x64, 0x19, 0xC0,
                 0x8E, 0x9C, 0xE1, 0x10);
MIME_DEFINE_GUID(IID_ITfContextOwnerServices, 0xB23EB630, 0x3E1C, 0x11D3, 0xA7, 0x45, 0x00, 0x50,
                 0x04, 0x0A, 0xB4, 0x07);
MIME_DEFINE_GUID(IID_ITfReadOnlyProperty, 0x17D49A3D, 0xF8B8, 0x4B2F, 0xB2, 0x54, 0x52, 0x31, 0x9D, 0xD6, 0x4C, 0x53);
MIME_DEFINE_GUID(IID_ITfProperty, 0xE2449660, 0x9542, 0x11D2, 0xBF, 0x46, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5);
MIME_DEFINE_GUID(IID_ITfCompartment, 0xBB08F7A9, 0x607A, 0x4384, 0x86, 0x23, 0x05, 0x68, 0x92, 0xB6, 0x43, 0x71);
MIME_DEFINE_GUID(IID_ITfCompartmentMgr, 0x7DCF57AC, 0x18AD, 0x438B, 0x82, 0x4D, 0x97, 0x9B, 0xFF, 0xB7, 0x4B, 0x7C);
MIME_DEFINE_GUID(IID_ITfCompartmentEventSink, 0x743ABD5F, 0xF26D, 0x48DF, 0x8C, 0xC5, 0x23, 0x84,
                 0x92, 0x41, 0x9B, 0x64);
MIME_DEFINE_GUID(IID_IEnumTfContexts, 0x8F1A7EA6, 0x1654, 0x4502, 0xA8, 0x6E, 0xB2, 0x90, 0x23, 0x44, 0xD5, 0x07);
MIME_DEFINE_GUID(IID_IEnumTfDocumentMgrs, 0xAA80E808, 0x2021, 0x11D2, 0x93, 0xE0, 0x00, 0x60, 0xB0, 0x67, 0xB8, 0x6E);
MIME_DEFINE_GUID(IID_ITfUIElement, 0xEA1EA137, 0x19DF, 0x11D7, 0xA6, 0xD2, 0x00, 0x06, 0x5B, 0x84, 0x43, 0x5C);
MIME_DEFINE_GUID(IID_IEnumTfUIElements, 0x887AA91E, 0xACBA, 0x4931, 0x84, 0xDA, 0x3C, 0x52, 0x08, 0xCF, 0x54, 0x3F);
MIME_DEFINE_GUID(IID_ITfUIElementMgr, 0xEA1EA135, 0x19DF, 0x11D7, 0xA6, 0xD2, 0x00, 0x06, 0x5B, 0x84, 0x43, 0x5C);
MIME_DEFINE_GUID(IID_ITfSourceSingle, 0x73131F9C, 0x56A9, 0x49DD, 0xB0, 0xEE, 0xD0, 0x46, 0x63, 0x3F, 0x75, 0x28);
MIME_DEFINE_GUID(IID_ITfThreadFocusSink, 0xC0F1DB0C, 0x3A20, 0x405C, 0xA3, 0x03, 0x96, 0xB6, 0x01, 0x0A, 0x88, 0x5F);
MIME_DEFINE_GUID(IID_ITfInputProcessorProfileActivationSink, 0x71C6E74E, 0x0F28, 0x11D8, 0xA8, 0x2A, 0x00, 0x06,
                 0x5B, 0x84, 0x43, 0x5C);

/* ------------------------------------------------------------------ interfaces */

typedef struct IEnumGUID IEnumGUID;
typedef struct IEnumGUIDVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumGUID *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumGUID *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumGUID *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumGUID *This, ULONG celt, GUID *rgelt, ULONG *pceltFetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumGUID *This, ULONG celt);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumGUID *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumGUID *This, IEnumGUID **ppenum);
} IEnumGUIDVtbl;
struct IEnumGUID {
    const IEnumGUIDVtbl *lpVtbl;
};

typedef struct ITfThreadMgr ITfThreadMgr;
typedef struct ITfThreadMgrEx ITfThreadMgrEx;
typedef struct ITfDocumentMgr ITfDocumentMgr;
typedef struct ITfContext ITfContext;
typedef struct ITfSource ITfSource;
typedef struct ITfInsertAtSelection ITfInsertAtSelection;
typedef struct ITfContextComposition ITfContextComposition;
typedef struct ITfComposition ITfComposition;
typedef struct ITfCompositionView ITfCompositionView;
typedef struct ITfRange ITfRange;
typedef struct ITfRangeACP ITfRangeACP;
typedef struct ITfEditRecord ITfEditRecord;
typedef struct ITfEditSession ITfEditSession;
typedef struct ITfContextOwnerCompositionSink ITfContextOwnerCompositionSink;
typedef struct ITfContextOwnerCompositionServices ITfContextOwnerCompositionServices;
typedef struct ITfTextEditSink ITfTextEditSink;
typedef struct ITfCompositionSink ITfCompositionSink;
typedef struct ITfReadOnlyProperty ITfReadOnlyProperty;
typedef struct ITfProperty ITfProperty;
typedef struct IEnumTfRanges IEnumTfRanges;
typedef struct ITfCompartmentMgr ITfCompartmentMgr;
typedef struct ITfCompartment ITfCompartment;
typedef struct ITfCompartmentEventSink ITfCompartmentEventSink;
typedef struct IEnumTfContexts IEnumTfContexts;
typedef struct IEnumTfDocumentMgrs IEnumTfDocumentMgrs;
typedef struct ITfKeystrokeMgr ITfKeystrokeMgr;
typedef struct ITfMessagePump ITfMessagePump;
typedef struct ITfClientId ITfClientId;
typedef struct ITfUIElementMgr ITfUIElementMgr;
typedef struct ITfUIElement ITfUIElement;
typedef struct IEnumTfUIElements IEnumTfUIElements;
typedef struct ITfThreadMgrEventSink ITfThreadMgrEventSink;
typedef struct ITfUIElementSink ITfUIElementSink;
typedef struct ITfCategoryMgr ITfCategoryMgr;
typedef struct ITfDisplayAttributeMgr ITfDisplayAttributeMgr;
typedef struct ITfDisplayAttributeInfo ITfDisplayAttributeInfo;
typedef struct IEnumTfDisplayAttributeInfo IEnumTfDisplayAttributeInfo;
typedef struct ITfContextView ITfContextView;
typedef struct IEnumTfContextViews IEnumTfContextViews;
typedef struct IEnumITfCompositionView IEnumITfCompositionView;
typedef struct ITfThreadFocusSink ITfThreadFocusSink;
typedef struct ITfInputProcessorProfileActivationSink ITfInputProcessorProfileActivationSink;
typedef struct ITfContextOwnerServices ITfContextOwnerServices;
typedef struct ITfInputProcessorProfiles ITfInputProcessorProfiles;
typedef struct ITfInputProcessorProfileMgr ITfInputProcessorProfileMgr;
typedef struct ITfRangeBackup ITfRangeBackup;
typedef struct ITfFunctionProvider ITfFunctionProvider;
typedef struct IEnumTfFunctionProviders IEnumTfFunctionProviders;
typedef struct IEnumTfProperties IEnumTfProperties;
typedef struct ITfTextInputProcessor ITfTextInputProcessor;
typedef struct ITfKeyEventSink ITfKeyEventSink;
typedef struct IEnumTfLanguageProfiles IEnumTfLanguageProfiles;
typedef struct IEnumTfInputProcessorProfiles IEnumTfInputProcessorProfiles;
typedef struct ITfActiveLanguageProfileNotifySink ITfActiveLanguageProfileNotifySink;
typedef struct ITfPropertyStore ITfPropertyStore;

typedef struct ITfThreadMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfThreadMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfThreadMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfThreadMgr *This);
    HRESULT(STDMETHODCALLTYPE *Activate)(ITfThreadMgr *This, TfClientId *ptid);
    HRESULT(STDMETHODCALLTYPE *Deactivate)(ITfThreadMgr *This);
    HRESULT(STDMETHODCALLTYPE *CreateDocumentMgr)(ITfThreadMgr *This, ITfDocumentMgr **ppdim);
    HRESULT(STDMETHODCALLTYPE *EnumDocumentMgrs)(ITfThreadMgr *This, IEnumTfDocumentMgrs **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetFocus)(ITfThreadMgr *This, ITfDocumentMgr **ppdimFocus);
    HRESULT(STDMETHODCALLTYPE *SetFocus)(ITfThreadMgr *This, ITfDocumentMgr *pdimFocus);
    HRESULT(STDMETHODCALLTYPE *AssociateFocus)(ITfThreadMgr *This, HWND hwnd, ITfDocumentMgr *pdimNew, ITfDocumentMgr **ppdimPrev);
    HRESULT(STDMETHODCALLTYPE *IsThreadFocus)(ITfThreadMgr *This, BOOL *pfThreadFocus);
    HRESULT(STDMETHODCALLTYPE *GetFunctionProvider)(ITfThreadMgr *This, REFCLSID clsid, ITfFunctionProvider **ppFuncProv);
    HRESULT(STDMETHODCALLTYPE *EnumFunctionProviders)(ITfThreadMgr *This, IEnumTfFunctionProviders **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetGlobalCompartment)(ITfThreadMgr *This, ITfCompartmentMgr **ppCompMgr);
} ITfThreadMgrVtbl;
struct ITfThreadMgr {
    const ITfThreadMgrVtbl *lpVtbl;
};

typedef struct ITfThreadMgrExVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfThreadMgrEx *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfThreadMgrEx *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfThreadMgrEx *This);
    HRESULT(STDMETHODCALLTYPE *Activate)(ITfThreadMgrEx *This, TfClientId *ptid);
    HRESULT(STDMETHODCALLTYPE *Deactivate)(ITfThreadMgrEx *This);
    HRESULT(STDMETHODCALLTYPE *CreateDocumentMgr)(ITfThreadMgrEx *This, ITfDocumentMgr **ppdim);
    HRESULT(STDMETHODCALLTYPE *EnumDocumentMgrs)(ITfThreadMgrEx *This, IEnumTfDocumentMgrs **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetFocus)(ITfThreadMgrEx *This, ITfDocumentMgr **ppdimFocus);
    HRESULT(STDMETHODCALLTYPE *SetFocus)(ITfThreadMgrEx *This, ITfDocumentMgr *pdimFocus);
    HRESULT(STDMETHODCALLTYPE *AssociateFocus)(ITfThreadMgrEx *This, HWND hwnd, ITfDocumentMgr *pdimNew, ITfDocumentMgr **ppdimPrev);
    HRESULT(STDMETHODCALLTYPE *IsThreadFocus)(ITfThreadMgrEx *This, BOOL *pfThreadFocus);
    HRESULT(STDMETHODCALLTYPE *GetFunctionProvider)(ITfThreadMgrEx *This, REFCLSID clsid, ITfFunctionProvider **ppFuncProv);
    HRESULT(STDMETHODCALLTYPE *EnumFunctionProviders)(ITfThreadMgrEx *This, IEnumTfFunctionProviders **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetGlobalCompartment)(ITfThreadMgrEx *This, ITfCompartmentMgr **ppCompMgr);
    HRESULT(STDMETHODCALLTYPE *ActivateEx)(ITfThreadMgrEx *This, TfClientId *id, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetActiveFlags)(ITfThreadMgrEx *This, DWORD *flags);
} ITfThreadMgrExVtbl;
struct ITfThreadMgrEx {
    const ITfThreadMgrExVtbl *lpVtbl;
};

typedef struct ITfDocumentMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfDocumentMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfDocumentMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfDocumentMgr *This);
    HRESULT(STDMETHODCALLTYPE *CreateContext)(ITfDocumentMgr *This, TfClientId tidOwner, DWORD dwFlags, IUnknown *punk, ITfContext **ppic, TfEditCookie *pecTextStore);
    HRESULT(STDMETHODCALLTYPE *Push)(ITfDocumentMgr *This, ITfContext *pic);
    HRESULT(STDMETHODCALLTYPE *Pop)(ITfDocumentMgr *This, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *GetTop)(ITfDocumentMgr *This, ITfContext **ppic);
    HRESULT(STDMETHODCALLTYPE *GetBase)(ITfDocumentMgr *This, ITfContext **ppic);
    HRESULT(STDMETHODCALLTYPE *EnumContexts)(ITfDocumentMgr *This, IEnumTfContexts **ppEnum);
} ITfDocumentMgrVtbl;
struct ITfDocumentMgr {
    const ITfDocumentMgrVtbl *lpVtbl;
};

typedef struct ITfContextVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContext *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContext *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContext *This);
    HRESULT(STDMETHODCALLTYPE *RequestEditSession)(ITfContext *This, TfClientId tid, ITfEditSession *pes, DWORD dwFlags, HRESULT *phrSession);
    HRESULT(STDMETHODCALLTYPE *InWriteSession)(ITfContext *This, TfClientId tid, BOOL *pfWriteSession);
    HRESULT(STDMETHODCALLTYPE *GetSelection)(ITfContext *This, TfEditCookie ec, ULONG ulIndex, ULONG ulCount, TF_SELECTION *pSelection, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *SetSelection)(ITfContext *This, TfEditCookie ec, ULONG ulCount, const TF_SELECTION *pSelection);
    HRESULT(STDMETHODCALLTYPE *GetStart)(ITfContext *This, TfEditCookie ec, ITfRange **ppStart);
    HRESULT(STDMETHODCALLTYPE *GetEnd)(ITfContext *This, TfEditCookie ec, ITfRange **ppEnd);
    HRESULT(STDMETHODCALLTYPE *GetActiveView)(ITfContext *This, ITfContextView **ppView);
    HRESULT(STDMETHODCALLTYPE *EnumViews)(ITfContext *This, IEnumTfContextViews **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(ITfContext *This, TF_STATUS *pdcs);
    HRESULT(STDMETHODCALLTYPE *GetProperty)(ITfContext *This, REFGUID guidProp, ITfProperty **ppProp);
    HRESULT(STDMETHODCALLTYPE *GetAppProperty)(ITfContext *This, REFGUID guidProp, ITfReadOnlyProperty **ppProp);
    HRESULT(STDMETHODCALLTYPE *TrackProperties)(ITfContext *This, const GUID **prgProp, ULONG cProp, const GUID **prgAppProp, ULONG cAppProp, ITfReadOnlyProperty **ppProperty);
    HRESULT(STDMETHODCALLTYPE *EnumProperties)(ITfContext *This, IEnumTfProperties **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetDocumentMgr)(ITfContext *This, ITfDocumentMgr **ppDm);
    HRESULT(STDMETHODCALLTYPE *CreateRangeBackup)(ITfContext *This, TfEditCookie ec, ITfRange *pRange, ITfRangeBackup **ppBackup);
} ITfContextVtbl;
struct ITfContext {
    const ITfContextVtbl *lpVtbl;
};

typedef struct ITfSourceVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfSource *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfSource *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfSource *This);
    HRESULT(STDMETHODCALLTYPE *AdviseSink)(ITfSource *This, REFIID riid, IUnknown *punk, DWORD *pdwCookie);
    HRESULT(STDMETHODCALLTYPE *UnadviseSink)(ITfSource *This, DWORD dwCookie);
} ITfSourceVtbl;
struct ITfSource {
    const ITfSourceVtbl *lpVtbl;
};

typedef struct ITfInsertAtSelectionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfInsertAtSelection *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfInsertAtSelection *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfInsertAtSelection *This);
    HRESULT(STDMETHODCALLTYPE *InsertTextAtSelection)(ITfInsertAtSelection *This, TfEditCookie ec, DWORD dwFlags, const WCHAR *pchText, LONG cch, ITfRange **ppRange);
    HRESULT(STDMETHODCALLTYPE *InsertEmbeddedAtSelection)(ITfInsertAtSelection *This, TfEditCookie ec, DWORD dwFlags, IDataObject *pDataObject, ITfRange **ppRange);
} ITfInsertAtSelectionVtbl;
struct ITfInsertAtSelection {
    const ITfInsertAtSelectionVtbl *lpVtbl;
};

typedef struct ITfContextCompositionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContextComposition *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContextComposition *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContextComposition *This);
    HRESULT(STDMETHODCALLTYPE *StartComposition)(ITfContextComposition *This, TfEditCookie ecWrite, ITfRange *pCompositionRange, ITfCompositionSink *pSink, ITfComposition **ppComposition);
    HRESULT(STDMETHODCALLTYPE *EnumCompositions)(ITfContextComposition *This, IEnumITfCompositionView **ppEnum);
    HRESULT(STDMETHODCALLTYPE *FindComposition)(ITfContextComposition *This, TfEditCookie ecRead, ITfRange *pTestRange, IEnumITfCompositionView **ppEnum);
    HRESULT(STDMETHODCALLTYPE *TakeOwnership)(ITfContextComposition *This, TfEditCookie ecWrite, ITfCompositionView *pComposition, ITfCompositionSink *pSink, ITfComposition **ppComposition);
} ITfContextCompositionVtbl;
struct ITfContextComposition {
    const ITfContextCompositionVtbl *lpVtbl;
};

typedef struct ITfCompositionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfComposition *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfComposition *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfComposition *This);
    HRESULT(STDMETHODCALLTYPE *GetRange)(ITfComposition *This, ITfRange **ppRange);
    HRESULT(STDMETHODCALLTYPE *ShiftStart)(ITfComposition *This, TfEditCookie ecWrite, ITfRange *pNewStart);
    HRESULT(STDMETHODCALLTYPE *ShiftEnd)(ITfComposition *This, TfEditCookie ecWrite, ITfRange *pNewEnd);
    HRESULT(STDMETHODCALLTYPE *EndComposition)(ITfComposition *This, TfEditCookie ecWrite);
} ITfCompositionVtbl;
struct ITfComposition {
    const ITfCompositionVtbl *lpVtbl;
};

typedef struct ITfCompositionViewVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfCompositionView *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfCompositionView *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfCompositionView *This);
    HRESULT(STDMETHODCALLTYPE *GetOwnerClsid)(ITfCompositionView *This, CLSID *pclsid);
    HRESULT(STDMETHODCALLTYPE *GetRange)(ITfCompositionView *This, ITfRange **ppRange);
} ITfCompositionViewVtbl;
struct ITfCompositionView {
    const ITfCompositionViewVtbl *lpVtbl;
};

typedef struct ITfRangeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfRange *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfRange *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfRange *This);
    HRESULT(STDMETHODCALLTYPE *GetText)(ITfRange *This, TfEditCookie ec, DWORD dwFlags, WCHAR *pchText, ULONG cchMax, ULONG *pcch);
    HRESULT(STDMETHODCALLTYPE *SetText)(ITfRange *This, TfEditCookie ec, DWORD dwFlags, const WCHAR *pchText, LONG cch);
    HRESULT(STDMETHODCALLTYPE *GetFormattedText)(ITfRange *This, TfEditCookie ec, IDataObject **ppDataObject);
    HRESULT(STDMETHODCALLTYPE *GetEmbedded)(ITfRange *This, TfEditCookie ec, REFGUID rguidService, REFIID riid, IUnknown **ppunk);
    HRESULT(STDMETHODCALLTYPE *InsertEmbedded)(ITfRange *This, TfEditCookie ec, DWORD dwFlags, IDataObject *pDataObject);
    HRESULT(STDMETHODCALLTYPE *ShiftStart)(ITfRange *This, TfEditCookie ec, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt);
    HRESULT(STDMETHODCALLTYPE *ShiftEnd)(ITfRange *This, TfEditCookie ec, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt);
    HRESULT(STDMETHODCALLTYPE *ShiftStartToRange)(ITfRange *This, TfEditCookie ec, ITfRange *pRange, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *ShiftEndToRange)(ITfRange *This, TfEditCookie ec, ITfRange *pRange, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *ShiftStartRegion)(ITfRange *This, TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion);
    HRESULT(STDMETHODCALLTYPE *ShiftEndRegion)(ITfRange *This, TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion);
    HRESULT(STDMETHODCALLTYPE *IsEmpty)(ITfRange *This, TfEditCookie ec, BOOL *pfEmpty);
    HRESULT(STDMETHODCALLTYPE *Collapse)(ITfRange *This, TfEditCookie ec, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *IsEqualStart)(ITfRange *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual);
    HRESULT(STDMETHODCALLTYPE *IsEqualEnd)(ITfRange *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual);
    HRESULT(STDMETHODCALLTYPE *CompareStart)(ITfRange *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *plResult);
    HRESULT(STDMETHODCALLTYPE *CompareEnd)(ITfRange *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *plResult);
    HRESULT(STDMETHODCALLTYPE *AdjustForInsert)(ITfRange *This, TfEditCookie ec, ULONG cchInsert, BOOL *pfInsertOk);
    HRESULT(STDMETHODCALLTYPE *GetGravity)(ITfRange *This, TfGravity *pgStart, TfGravity *pgEnd);
    HRESULT(STDMETHODCALLTYPE *SetGravity)(ITfRange *This, TfEditCookie ec, TfGravity gStart, TfGravity gEnd);
    HRESULT(STDMETHODCALLTYPE *Clone)(ITfRange *This, ITfRange **ppClone);
    HRESULT(STDMETHODCALLTYPE *GetContext)(ITfRange *This, ITfContext **ppContext);
} ITfRangeVtbl;
struct ITfRange {
    const ITfRangeVtbl *lpVtbl;
};

typedef struct ITfRangeACPVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfRangeACP *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfRangeACP *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfRangeACP *This);
    HRESULT(STDMETHODCALLTYPE *GetText)(ITfRangeACP *This, TfEditCookie ec, DWORD dwFlags, WCHAR *pchText, ULONG cchMax, ULONG *pcch);
    HRESULT(STDMETHODCALLTYPE *SetText)(ITfRangeACP *This, TfEditCookie ec, DWORD dwFlags, const WCHAR *pchText, LONG cch);
    HRESULT(STDMETHODCALLTYPE *GetFormattedText)(ITfRangeACP *This, TfEditCookie ec, IDataObject **ppDataObject);
    HRESULT(STDMETHODCALLTYPE *GetEmbedded)(ITfRangeACP *This, TfEditCookie ec, REFGUID rguidService, REFIID riid, IUnknown **ppunk);
    HRESULT(STDMETHODCALLTYPE *InsertEmbedded)(ITfRangeACP *This, TfEditCookie ec, DWORD dwFlags, IDataObject *pDataObject);
    HRESULT(STDMETHODCALLTYPE *ShiftStart)(ITfRangeACP *This, TfEditCookie ec, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt);
    HRESULT(STDMETHODCALLTYPE *ShiftEnd)(ITfRangeACP *This, TfEditCookie ec, LONG cchReq, LONG *pcch, const TF_HALTCOND *pHalt);
    HRESULT(STDMETHODCALLTYPE *ShiftStartToRange)(ITfRangeACP *This, TfEditCookie ec, ITfRange *pRange, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *ShiftEndToRange)(ITfRangeACP *This, TfEditCookie ec, ITfRange *pRange, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *ShiftStartRegion)(ITfRangeACP *This, TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion);
    HRESULT(STDMETHODCALLTYPE *ShiftEndRegion)(ITfRangeACP *This, TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion);
    HRESULT(STDMETHODCALLTYPE *IsEmpty)(ITfRangeACP *This, TfEditCookie ec, BOOL *pfEmpty);
    HRESULT(STDMETHODCALLTYPE *Collapse)(ITfRangeACP *This, TfEditCookie ec, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *IsEqualStart)(ITfRangeACP *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual);
    HRESULT(STDMETHODCALLTYPE *IsEqualEnd)(ITfRangeACP *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, BOOL *pfEqual);
    HRESULT(STDMETHODCALLTYPE *CompareStart)(ITfRangeACP *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *plResult);
    HRESULT(STDMETHODCALLTYPE *CompareEnd)(ITfRangeACP *This, TfEditCookie ec, ITfRange *pWith, TfAnchor aPos, LONG *plResult);
    HRESULT(STDMETHODCALLTYPE *AdjustForInsert)(ITfRangeACP *This, TfEditCookie ec, ULONG cchInsert, BOOL *pfInsertOk);
    HRESULT(STDMETHODCALLTYPE *GetGravity)(ITfRangeACP *This, TfGravity *pgStart, TfGravity *pgEnd);
    HRESULT(STDMETHODCALLTYPE *SetGravity)(ITfRangeACP *This, TfEditCookie ec, TfGravity gStart, TfGravity gEnd);
    HRESULT(STDMETHODCALLTYPE *Clone)(ITfRangeACP *This, ITfRange **ppClone);
    HRESULT(STDMETHODCALLTYPE *GetContext)(ITfRangeACP *This, ITfContext **ppContext);
    HRESULT(STDMETHODCALLTYPE *GetExtent)(ITfRangeACP *This, LONG *pacpAnchor, LONG *pcch);
    HRESULT(STDMETHODCALLTYPE *SetExtent)(ITfRangeACP *This, LONG acpAnchor, LONG cch);
} ITfRangeACPVtbl;
struct ITfRangeACP {
    const ITfRangeACPVtbl *lpVtbl;
};

typedef struct ITfEditRecordVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfEditRecord *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfEditRecord *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfEditRecord *This);
    HRESULT(STDMETHODCALLTYPE *GetSelectionStatus)(ITfEditRecord *This, BOOL *changed);
    HRESULT(STDMETHODCALLTYPE *GetTextAndPropertyUpdates)(ITfEditRecord *This, DWORD flags, const GUID **props, ULONG count, IEnumTfRanges **ret);
} ITfEditRecordVtbl;
struct ITfEditRecord {
    const ITfEditRecordVtbl *lpVtbl;
};

typedef struct ITfEditSessionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfEditSession *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfEditSession *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfEditSession *This);
    HRESULT(STDMETHODCALLTYPE *DoEditSession)(ITfEditSession *This, TfEditCookie ec);
} ITfEditSessionVtbl;
struct ITfEditSession {
    const ITfEditSessionVtbl *lpVtbl;
};

typedef struct ITfContextOwnerCompositionSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContextOwnerCompositionSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContextOwnerCompositionSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContextOwnerCompositionSink *This);
    HRESULT(STDMETHODCALLTYPE *OnStartComposition)(ITfContextOwnerCompositionSink *This, ITfCompositionView *pComposition, BOOL *pfOk);
    HRESULT(STDMETHODCALLTYPE *OnUpdateComposition)(ITfContextOwnerCompositionSink *This, ITfCompositionView *pComposition, ITfRange *pRangeNew);
    HRESULT(STDMETHODCALLTYPE *OnEndComposition)(ITfContextOwnerCompositionSink *This, ITfCompositionView *pComposition);
} ITfContextOwnerCompositionSinkVtbl;
struct ITfContextOwnerCompositionSink {
    const ITfContextOwnerCompositionSinkVtbl *lpVtbl;
};

typedef struct ITfContextOwnerCompositionServicesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContextOwnerCompositionServices *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContextOwnerCompositionServices *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContextOwnerCompositionServices *This);
    HRESULT(STDMETHODCALLTYPE *StartComposition)(ITfContextOwnerCompositionServices *This, TfEditCookie ecWrite, ITfRange *pCompositionRange, ITfCompositionSink *pSink, ITfComposition **ppComposition);
    HRESULT(STDMETHODCALLTYPE *EnumCompositions)(ITfContextOwnerCompositionServices *This, IEnumITfCompositionView **ppEnum);
    HRESULT(STDMETHODCALLTYPE *FindComposition)(ITfContextOwnerCompositionServices *This, TfEditCookie ecRead, ITfRange *pTestRange, IEnumITfCompositionView **ppEnum);
    HRESULT(STDMETHODCALLTYPE *TakeOwnership)(ITfContextOwnerCompositionServices *This, TfEditCookie ecWrite, ITfCompositionView *pComposition, ITfCompositionSink *pSink, ITfComposition **ppComposition);
    HRESULT(STDMETHODCALLTYPE *TerminateComposition)(ITfContextOwnerCompositionServices *This, ITfCompositionView *pComposition);
} ITfContextOwnerCompositionServicesVtbl;
struct ITfContextOwnerCompositionServices {
    const ITfContextOwnerCompositionServicesVtbl *lpVtbl;
};

typedef struct ITfTextEditSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfTextEditSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfTextEditSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfTextEditSink *This);
    HRESULT(STDMETHODCALLTYPE *OnEndEdit)(ITfTextEditSink *This, ITfContext *pic, TfEditCookie ecReadOnly, ITfEditRecord *pEditRecord);
} ITfTextEditSinkVtbl;
struct ITfTextEditSink {
    const ITfTextEditSinkVtbl *lpVtbl;
};

typedef struct ITfCompositionSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfCompositionSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfCompositionSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfCompositionSink *This);
    HRESULT(STDMETHODCALLTYPE *OnCompositionTerminated)(ITfCompositionSink *This, TfEditCookie ecWrite, ITfComposition *pComposition);
} ITfCompositionSinkVtbl;
struct ITfCompositionSink {
    const ITfCompositionSinkVtbl *lpVtbl;
};

typedef struct ITfReadOnlyPropertyVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfReadOnlyProperty *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfReadOnlyProperty *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfReadOnlyProperty *This);
    HRESULT(STDMETHODCALLTYPE *GetType)(ITfReadOnlyProperty *This, GUID *pguid);
    HRESULT(STDMETHODCALLTYPE *EnumRanges)(ITfReadOnlyProperty *This, TfEditCookie ec, IEnumTfRanges **ppEnum, ITfRange *pTargetRange);
    HRESULT(STDMETHODCALLTYPE *GetValue)(ITfReadOnlyProperty *This, TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue);
    HRESULT(STDMETHODCALLTYPE *GetContext)(ITfReadOnlyProperty *This, ITfContext **ppContext);
} ITfReadOnlyPropertyVtbl;
struct ITfReadOnlyProperty {
    const ITfReadOnlyPropertyVtbl *lpVtbl;
};

typedef struct ITfPropertyVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfProperty *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfProperty *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfProperty *This);
    HRESULT(STDMETHODCALLTYPE *GetType)(ITfProperty *This, GUID *pguid);
    HRESULT(STDMETHODCALLTYPE *EnumRanges)(ITfProperty *This, TfEditCookie ec, IEnumTfRanges **ppEnum, ITfRange *pTargetRange);
    HRESULT(STDMETHODCALLTYPE *GetValue)(ITfProperty *This, TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue);
    HRESULT(STDMETHODCALLTYPE *GetContext)(ITfProperty *This, ITfContext **ppContext);
    HRESULT(STDMETHODCALLTYPE *FindRange)(ITfProperty *This, TfEditCookie ec, ITfRange *pRange, ITfRange **ppRange, TfAnchor aPos);
    HRESULT(STDMETHODCALLTYPE *SetValueStore)(ITfProperty *This, TfEditCookie ec, ITfRange *pRange, ITfPropertyStore *pPropStore);
    HRESULT(STDMETHODCALLTYPE *SetValue)(ITfProperty *This, TfEditCookie ec, ITfRange *pRange, const VARIANT *pvarValue);
    HRESULT(STDMETHODCALLTYPE *Clear)(ITfProperty *This, TfEditCookie ec, ITfRange *pRange);
} ITfPropertyVtbl;
struct ITfProperty {
    const ITfPropertyVtbl *lpVtbl;
};

typedef struct IEnumTfRangesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfRanges *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfRanges *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfRanges *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfRanges *This, IEnumTfRanges **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfRanges *This, ULONG ulCount, ITfRange **ppRange, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfRanges *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfRanges *This, ULONG ulCount);
} IEnumTfRangesVtbl;
struct IEnumTfRanges {
    const IEnumTfRangesVtbl *lpVtbl;
};

typedef struct ITfCompartmentMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfCompartmentMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfCompartmentMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfCompartmentMgr *This);
    HRESULT(STDMETHODCALLTYPE *GetCompartment)(ITfCompartmentMgr *This, REFGUID rguid, ITfCompartment **ppcomp);
    HRESULT(STDMETHODCALLTYPE *ClearCompartment)(ITfCompartmentMgr *This, TfClientId tid, REFGUID rguid);
    HRESULT(STDMETHODCALLTYPE *EnumCompartments)(ITfCompartmentMgr *This, IEnumGUID **ppEnum);
} ITfCompartmentMgrVtbl;
struct ITfCompartmentMgr {
    const ITfCompartmentMgrVtbl *lpVtbl;
};

typedef struct ITfCompartmentVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfCompartment *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfCompartment *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfCompartment *This);
    HRESULT(STDMETHODCALLTYPE *SetValue)(ITfCompartment *This, TfClientId tid, const VARIANT *pvarValue);
    HRESULT(STDMETHODCALLTYPE *GetValue)(ITfCompartment *This, VARIANT *pvarValue);
} ITfCompartmentVtbl;
struct ITfCompartment {
    const ITfCompartmentVtbl *lpVtbl;
};

typedef struct ITfCompartmentEventSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfCompartmentEventSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfCompartmentEventSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfCompartmentEventSink *This);
    HRESULT(STDMETHODCALLTYPE *OnChange)(ITfCompartmentEventSink *This, REFGUID rguid);
} ITfCompartmentEventSinkVtbl;
struct ITfCompartmentEventSink {
    const ITfCompartmentEventSinkVtbl *lpVtbl;
};

typedef struct IEnumTfContextsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfContexts *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfContexts *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfContexts *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfContexts *This, IEnumTfContexts **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfContexts *This, ULONG ulCount, ITfContext **rgContext, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfContexts *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfContexts *This, ULONG ulCount);
} IEnumTfContextsVtbl;
struct IEnumTfContexts {
    const IEnumTfContextsVtbl *lpVtbl;
};

typedef struct IEnumTfDocumentMgrsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfDocumentMgrs *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfDocumentMgrs *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfDocumentMgrs *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfDocumentMgrs *This, IEnumTfDocumentMgrs **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfDocumentMgrs *This, ULONG ulCount, ITfDocumentMgr **rgDocumentMgr, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfDocumentMgrs *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfDocumentMgrs *This, ULONG ulCount);
} IEnumTfDocumentMgrsVtbl;
struct IEnumTfDocumentMgrs {
    const IEnumTfDocumentMgrsVtbl *lpVtbl;
};

typedef struct ITfKeystrokeMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfKeystrokeMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfKeystrokeMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfKeystrokeMgr *This);
    HRESULT(STDMETHODCALLTYPE *AdviseKeyEventSink)(ITfKeystrokeMgr *This, TfClientId tid, ITfKeyEventSink *pSink, BOOL fForeground);
    HRESULT(STDMETHODCALLTYPE *UnadviseKeyEventSink)(ITfKeystrokeMgr *This, TfClientId tid);
    HRESULT(STDMETHODCALLTYPE *GetForeground)(ITfKeystrokeMgr *This, CLSID *pclsid);
    HRESULT(STDMETHODCALLTYPE *TestKeyDown)(ITfKeystrokeMgr *This, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *TestKeyUp)(ITfKeystrokeMgr *This, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *KeyDown)(ITfKeystrokeMgr *This, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *KeyUp)(ITfKeystrokeMgr *This, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *GetPreservedKey)(ITfKeystrokeMgr *This, ITfContext *pic, const TF_PRESERVEDKEY *pprekey, GUID *pguid);
    HRESULT(STDMETHODCALLTYPE *IsPreservedKey)(ITfKeystrokeMgr *This, REFGUID rguid, const TF_PRESERVEDKEY *pprekey, BOOL *pfRegistered);
    HRESULT(STDMETHODCALLTYPE *PreserveKey)(ITfKeystrokeMgr *This, TfClientId tid, REFGUID rguid, const TF_PRESERVEDKEY *prekey, const WCHAR *pchDesc, ULONG cchDesc);
    HRESULT(STDMETHODCALLTYPE *UnpreserveKey)(ITfKeystrokeMgr *This, REFGUID rguid, const TF_PRESERVEDKEY *pprekey);
    HRESULT(STDMETHODCALLTYPE *SetPreservedKeyDescription)(ITfKeystrokeMgr *This, REFGUID rguid, const WCHAR *pchDesc, ULONG cchDesc);
    HRESULT(STDMETHODCALLTYPE *GetPreservedKeyDescription)(ITfKeystrokeMgr *This, REFGUID rguid, BSTR *pbstrDesc);
    HRESULT(STDMETHODCALLTYPE *SimulatePreservedKey)(ITfKeystrokeMgr *This, ITfContext *pic, REFGUID rguid, BOOL *pfEaten);
} ITfKeystrokeMgrVtbl;
struct ITfKeystrokeMgr {
    const ITfKeystrokeMgrVtbl *lpVtbl;
};

typedef struct ITfMessagePumpVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfMessagePump *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfMessagePump *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfMessagePump *This);
    HRESULT(STDMETHODCALLTYPE *PeekMessageA)(ITfMessagePump *This, LPMSG pMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg, BOOL *pfResult);
    HRESULT(STDMETHODCALLTYPE *GetMessageA)(ITfMessagePump *This, LPMSG pMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax, BOOL *pfResult);
    HRESULT(STDMETHODCALLTYPE *PeekMessageW)(ITfMessagePump *This, LPMSG pMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg, BOOL *pfResult);
    HRESULT(STDMETHODCALLTYPE *GetMessageW)(ITfMessagePump *This, LPMSG pMsg, HWND hwnd, UINT wMsgFilterMin, UINT wMsgFilterMax, BOOL *pfResult);
} ITfMessagePumpVtbl;
struct ITfMessagePump {
    const ITfMessagePumpVtbl *lpVtbl;
};

typedef struct ITfClientIdVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfClientId *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfClientId *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfClientId *This);
    HRESULT(STDMETHODCALLTYPE *GetClientId)(ITfClientId *This, REFCLSID rclsid, TfClientId *ptid);
} ITfClientIdVtbl;
struct ITfClientId {
    const ITfClientIdVtbl *lpVtbl;
};

typedef struct ITfUIElementMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfUIElementMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfUIElementMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfUIElementMgr *This);
    HRESULT(STDMETHODCALLTYPE *BeginUIElement)(ITfUIElementMgr *This, ITfUIElement *element, BOOL *show, DWORD *id);
    HRESULT(STDMETHODCALLTYPE *UpdateUIElement)(ITfUIElementMgr *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *EndUIElement)(ITfUIElementMgr *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *GetUIElement)(ITfUIElementMgr *This, DWORD id, ITfUIElement **element);
    HRESULT(STDMETHODCALLTYPE *EnumUIElements)(ITfUIElementMgr *This, IEnumTfUIElements **enum_elements);
} ITfUIElementMgrVtbl;
struct ITfUIElementMgr {
    const ITfUIElementMgrVtbl *lpVtbl;
};

typedef struct ITfUIElementVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfUIElement *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfUIElement *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfUIElement *This);
    HRESULT(STDMETHODCALLTYPE *GetDescription)(ITfUIElement *This, BSTR *description);
    HRESULT(STDMETHODCALLTYPE *GetGUID)(ITfUIElement *This, GUID *guid);
    HRESULT(STDMETHODCALLTYPE *Show)(ITfUIElement *This, BOOL show);
    HRESULT(STDMETHODCALLTYPE *IsShown)(ITfUIElement *This, BOOL *show);
} ITfUIElementVtbl;
struct ITfUIElement {
    const ITfUIElementVtbl *lpVtbl;
};

typedef struct IEnumTfUIElementsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfUIElements *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfUIElements *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfUIElements *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfUIElements *This, IEnumTfUIElements **enum_elements);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfUIElements *This, ULONG count, ITfUIElement **element, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfUIElements *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfUIElements *This, ULONG count);
} IEnumTfUIElementsVtbl;
struct IEnumTfUIElements {
    const IEnumTfUIElementsVtbl *lpVtbl;
};

typedef struct ITfThreadMgrEventSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfThreadMgrEventSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfThreadMgrEventSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfThreadMgrEventSink *This);
    HRESULT(STDMETHODCALLTYPE *OnInitDocumentMgr)(ITfThreadMgrEventSink *This, ITfDocumentMgr *pdim);
    HRESULT(STDMETHODCALLTYPE *OnUninitDocumentMgr)(ITfThreadMgrEventSink *This, ITfDocumentMgr *pdim);
    HRESULT(STDMETHODCALLTYPE *OnSetFocus)(ITfThreadMgrEventSink *This, ITfDocumentMgr *pdimFocus, ITfDocumentMgr *pdimPrevFocus);
    HRESULT(STDMETHODCALLTYPE *OnPushContext)(ITfThreadMgrEventSink *This, ITfContext *pic);
    HRESULT(STDMETHODCALLTYPE *OnPopContext)(ITfThreadMgrEventSink *This, ITfContext *pic);
} ITfThreadMgrEventSinkVtbl;
struct ITfThreadMgrEventSink {
    const ITfThreadMgrEventSinkVtbl *lpVtbl;
};

typedef struct ITfUIElementSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfUIElementSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfUIElementSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfUIElementSink *This);
    HRESULT(STDMETHODCALLTYPE *BeginUIElement)(ITfUIElementSink *This, DWORD id, BOOL *show);
    HRESULT(STDMETHODCALLTYPE *UpdateUIElement)(ITfUIElementSink *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *EndUIElement)(ITfUIElementSink *This, DWORD id);
} ITfUIElementSinkVtbl;
struct ITfUIElementSink {
    const ITfUIElementSinkVtbl *lpVtbl;
};

typedef struct ITfCategoryMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfCategoryMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfCategoryMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfCategoryMgr *This);
    HRESULT(STDMETHODCALLTYPE *RegisterCategory)(ITfCategoryMgr *This, REFCLSID rclsid, REFGUID rcatid, REFGUID rguid);
    HRESULT(STDMETHODCALLTYPE *UnregisterCategory)(ITfCategoryMgr *This, REFCLSID rclsid, REFGUID rcatid, REFGUID rguid);
    HRESULT(STDMETHODCALLTYPE *EnumCategoriesInItem)(ITfCategoryMgr *This, REFGUID rguid, IEnumGUID **ppEnum);
    HRESULT(STDMETHODCALLTYPE *EnumItemsInCategory)(ITfCategoryMgr *This, REFGUID rcatid, IEnumGUID **ppEnum);
    HRESULT(STDMETHODCALLTYPE *FindClosestCategory)(ITfCategoryMgr *This, REFGUID rguid, GUID *pcatid, const GUID **ppcatidList, ULONG ulCount);
    HRESULT(STDMETHODCALLTYPE *RegisterGUIDDescription)(ITfCategoryMgr *This, REFCLSID rclsid, REFGUID rguid, const WCHAR *pchDesc, ULONG cch);
    HRESULT(STDMETHODCALLTYPE *UnregisterGUIDDescription)(ITfCategoryMgr *This, REFCLSID rclsid, REFGUID rguid);
    HRESULT(STDMETHODCALLTYPE *GetGUIDDescription)(ITfCategoryMgr *This, REFGUID rguid, BSTR *pbstrDesc);
    HRESULT(STDMETHODCALLTYPE *RegisterGUIDDWORD)(ITfCategoryMgr *This, REFCLSID rclsid, REFGUID rguid, DWORD dw);
    HRESULT(STDMETHODCALLTYPE *UnregisterGUIDDWORD)(ITfCategoryMgr *This, REFCLSID rclsid, REFGUID rguid);
    HRESULT(STDMETHODCALLTYPE *GetGUIDDWORD)(ITfCategoryMgr *This, REFGUID rguid, DWORD *pdw);
    HRESULT(STDMETHODCALLTYPE *RegisterGUID)(ITfCategoryMgr *This, REFGUID rguid, TfGuidAtom *pguidatom);
    HRESULT(STDMETHODCALLTYPE *GetGUID)(ITfCategoryMgr *This, TfGuidAtom guidatom, GUID *pguid);
    HRESULT(STDMETHODCALLTYPE *IsEqualTfGuidAtom)(ITfCategoryMgr *This, TfGuidAtom guidatom, REFGUID rguid, BOOL *pfEqual);
} ITfCategoryMgrVtbl;
struct ITfCategoryMgr {
    const ITfCategoryMgrVtbl *lpVtbl;
};

typedef struct ITfDisplayAttributeMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfDisplayAttributeMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfDisplayAttributeMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfDisplayAttributeMgr *This);
    HRESULT(STDMETHODCALLTYPE *OnUpdateInfo)(ITfDisplayAttributeMgr *This);
    HRESULT(STDMETHODCALLTYPE *EnumDisplayAttributeInfo)(ITfDisplayAttributeMgr *This, IEnumTfDisplayAttributeInfo **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetDisplayAttributeInfo)(ITfDisplayAttributeMgr *This, REFGUID guid, ITfDisplayAttributeInfo **ppInfo, CLSID *pclsidOwner);
} ITfDisplayAttributeMgrVtbl;
struct ITfDisplayAttributeMgr {
    const ITfDisplayAttributeMgrVtbl *lpVtbl;
};

typedef struct ITfDisplayAttributeInfoVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfDisplayAttributeInfo *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfDisplayAttributeInfo *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfDisplayAttributeInfo *This);
    HRESULT(STDMETHODCALLTYPE *GetGUID)(ITfDisplayAttributeInfo *This, GUID *pguid);
    HRESULT(STDMETHODCALLTYPE *GetDescription)(ITfDisplayAttributeInfo *This, BSTR *pbstrDesc);
    HRESULT(STDMETHODCALLTYPE *GetAttributeInfo)(ITfDisplayAttributeInfo *This, TF_DISPLAYATTRIBUTE *pda);
    HRESULT(STDMETHODCALLTYPE *SetAttributeInfo)(ITfDisplayAttributeInfo *This, const TF_DISPLAYATTRIBUTE *pda);
    HRESULT(STDMETHODCALLTYPE *Reset)(ITfDisplayAttributeInfo *This);
} ITfDisplayAttributeInfoVtbl;
struct ITfDisplayAttributeInfo {
    const ITfDisplayAttributeInfoVtbl *lpVtbl;
};

typedef struct IEnumTfDisplayAttributeInfoVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfDisplayAttributeInfo *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfDisplayAttributeInfo *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfDisplayAttributeInfo *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfDisplayAttributeInfo *This, IEnumTfDisplayAttributeInfo **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfDisplayAttributeInfo *This, ULONG ulCount, ITfDisplayAttributeInfo **rgInfo, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfDisplayAttributeInfo *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfDisplayAttributeInfo *This, ULONG ulCount);
} IEnumTfDisplayAttributeInfoVtbl;
struct IEnumTfDisplayAttributeInfo {
    const IEnumTfDisplayAttributeInfoVtbl *lpVtbl;
};

typedef struct ITfContextViewVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContextView *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContextView *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContextView *This);
    HRESULT(STDMETHODCALLTYPE *GetRangeFromPoint)(ITfContextView *This, TfEditCookie cookie, const POINT *pt, DWORD flags, ITfRange **range);
    HRESULT(STDMETHODCALLTYPE *GetTextExt)(ITfContextView *This, TfEditCookie cookie, ITfRange *range, RECT *rect, BOOL *clipped);
    HRESULT(STDMETHODCALLTYPE *GetScreenExt)(ITfContextView *This, RECT *rect);
    HRESULT(STDMETHODCALLTYPE *GetWnd)(ITfContextView *This, HWND *hwnd);
} ITfContextViewVtbl;
struct ITfContextView {
    const ITfContextViewVtbl *lpVtbl;
};

typedef struct IEnumTfContextViewsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfContextViews *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfContextViews *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfContextViews *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfContextViews *This, IEnumTfContextViews **ret);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfContextViews *This, ULONG count, ITfContextView **views, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfContextViews *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfContextViews *This, ULONG count);
} IEnumTfContextViewsVtbl;
struct IEnumTfContextViews {
    const IEnumTfContextViewsVtbl *lpVtbl;
};

typedef struct IEnumITfCompositionViewVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumITfCompositionView *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumITfCompositionView *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumITfCompositionView *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumITfCompositionView *This, IEnumITfCompositionView **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumITfCompositionView *This, ULONG ulCount, ITfCompositionView **rgCompositionView, ULONG *pcFetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumITfCompositionView *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumITfCompositionView *This, ULONG ulCount);
} IEnumITfCompositionViewVtbl;
struct IEnumITfCompositionView {
    const IEnumITfCompositionViewVtbl *lpVtbl;
};

typedef struct ITfThreadFocusSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfThreadFocusSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfThreadFocusSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfThreadFocusSink *This);
    HRESULT(STDMETHODCALLTYPE *OnSetThreadFocus)(ITfThreadFocusSink *This);
    HRESULT(STDMETHODCALLTYPE *OnKillThreadFocus)(ITfThreadFocusSink *This);
} ITfThreadFocusSinkVtbl;
struct ITfThreadFocusSink {
    const ITfThreadFocusSinkVtbl *lpVtbl;
};

typedef struct ITfInputProcessorProfileActivationSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfInputProcessorProfileActivationSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfInputProcessorProfileActivationSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfInputProcessorProfileActivationSink *This);
    HRESULT(STDMETHODCALLTYPE *OnActivated)(ITfInputProcessorProfileActivationSink *This, DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID catid, REFGUID guidProfile, HKL hkl, DWORD dwFlags);
} ITfInputProcessorProfileActivationSinkVtbl;
struct ITfInputProcessorProfileActivationSink {
    const ITfInputProcessorProfileActivationSinkVtbl *lpVtbl;
};

typedef struct ITfContextOwnerServicesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContextOwnerServices *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContextOwnerServices *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContextOwnerServices *This);
    HRESULT(STDMETHODCALLTYPE *OnLayoutChange)(ITfContextOwnerServices *This);
    HRESULT(STDMETHODCALLTYPE *OnStatusChange)(ITfContextOwnerServices *This, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *OnAttributeChange)(ITfContextOwnerServices *This, REFGUID rguidAttribute);
    HRESULT(STDMETHODCALLTYPE *Serialize)(ITfContextOwnerServices *This, ITfProperty *pProp, ITfRange *pRange, TF_PERSISTENT_PROPERTY_HEADER_ACP *pHdr, IStream *pStream);
    HRESULT(STDMETHODCALLTYPE *Unserialize)(ITfContextOwnerServices *This, ITfProperty *pProp, const TF_PERSISTENT_PROPERTY_HEADER_ACP *pHdr, IStream *pStream, ITfPersistentPropertyLoaderACP *pLoader);
    HRESULT(STDMETHODCALLTYPE *ForceLoadProperty)(ITfContextOwnerServices *This, ITfProperty *pProp);
    HRESULT(STDMETHODCALLTYPE *CreateRange)(ITfContextOwnerServices *This, LONG acpStart, LONG acpEnd, ITfRangeACP **ppRange);
} ITfContextOwnerServicesVtbl;
struct ITfContextOwnerServices {
    const ITfContextOwnerServicesVtbl *lpVtbl;
};

typedef struct ITfInputProcessorProfilesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfInputProcessorProfiles *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfInputProcessorProfiles *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfInputProcessorProfiles *This);
    HRESULT(STDMETHODCALLTYPE *Register)(ITfInputProcessorProfiles *This, REFCLSID rclsid);
    HRESULT(STDMETHODCALLTYPE *Unregister)(ITfInputProcessorProfiles *This, REFCLSID rclsid);
    HRESULT(STDMETHODCALLTYPE *AddLanguageProfile)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, const WCHAR *pchDesc, ULONG cchDesc, const WCHAR *pchIconFile, ULONG cchFile, ULONG uIconIndex);
    HRESULT(STDMETHODCALLTYPE *RemoveLanguageProfile)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile);
    HRESULT(STDMETHODCALLTYPE *EnumInputProcessorInfo)(ITfInputProcessorProfiles *This, IEnumGUID **ppEnum);
    HRESULT(STDMETHODCALLTYPE *GetDefaultLanguageProfile)(ITfInputProcessorProfiles *This, LANGID langid, REFGUID catid, CLSID *pclsid, GUID *pguidProfile);
    HRESULT(STDMETHODCALLTYPE *SetDefaultLanguageProfile)(ITfInputProcessorProfiles *This, LANGID langid, REFCLSID rclsid, REFGUID guidProfiles);
    HRESULT(STDMETHODCALLTYPE *ActivateLanguageProfile)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfiles);
    HRESULT(STDMETHODCALLTYPE *GetActiveLanguageProfile)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID *plangid, GUID *pguidProfile);
    HRESULT(STDMETHODCALLTYPE *GetLanguageProfileDescription)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, BSTR *pbstrProfile);
    HRESULT(STDMETHODCALLTYPE *GetCurrentLanguage)(ITfInputProcessorProfiles *This, LANGID *plangid);
    HRESULT(STDMETHODCALLTYPE *ChangeCurrentLanguage)(ITfInputProcessorProfiles *This, LANGID langid);
    HRESULT(STDMETHODCALLTYPE *GetLanguageList)(ITfInputProcessorProfiles *This, LANGID **ppLangId, ULONG *pulCount);
    HRESULT(STDMETHODCALLTYPE *EnumLanguageProfiles)(ITfInputProcessorProfiles *This, LANGID langid, IEnumTfLanguageProfiles **ppEnum);
    HRESULT(STDMETHODCALLTYPE *EnableLanguageProfile)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, BOOL fEnable);
    HRESULT(STDMETHODCALLTYPE *IsEnabledLanguageProfile)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, BOOL *pfEnable);
    HRESULT(STDMETHODCALLTYPE *EnableLanguageProfileByDefault)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, BOOL fEnable);
    HRESULT(STDMETHODCALLTYPE *SubstituteKeyboardLayout)(ITfInputProcessorProfiles *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, HKL hKL);
} ITfInputProcessorProfilesVtbl;
struct ITfInputProcessorProfiles {
    const ITfInputProcessorProfilesVtbl *lpVtbl;
};

typedef struct ITfInputProcessorProfileMgrVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfInputProcessorProfileMgr *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfInputProcessorProfileMgr *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfInputProcessorProfileMgr *This);
    HRESULT(STDMETHODCALLTYPE *ActivateProfile)(ITfInputProcessorProfileMgr *This, DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID guidProfile, HKL hkl, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *DeactivateProfile)(ITfInputProcessorProfileMgr *This, DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID guidProfile, HKL hkl, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *GetProfile)(ITfInputProcessorProfileMgr *This, DWORD dwProfileType, LANGID langid, REFCLSID clsid, REFGUID guidProfile, HKL hkl, TF_INPUTPROCESSORPROFILE *pProfile);
    HRESULT(STDMETHODCALLTYPE *EnumProfiles)(ITfInputProcessorProfileMgr *This, LANGID langid, IEnumTfInputProcessorProfiles **ppEnum);
    HRESULT(STDMETHODCALLTYPE *ReleaseInputProcessor)(ITfInputProcessorProfileMgr *This, REFCLSID rclsid, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *RegisterProfile)(ITfInputProcessorProfileMgr *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, const WCHAR *pchDesc, ULONG cchDesc, const WCHAR *pchIconFile, ULONG cchFile, ULONG uIconIndex, HKL hklsubstitute, DWORD dwPreferredLayout, BOOL bEnabledByDefault, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *UnregisterProfile)(ITfInputProcessorProfileMgr *This, REFCLSID rclsid, LANGID langid, REFGUID guidProfile, DWORD dwFlags);
    HRESULT(STDMETHODCALLTYPE *GetActiveProfile)(ITfInputProcessorProfileMgr *This, REFGUID catid, TF_INPUTPROCESSORPROFILE *pProfile);
} ITfInputProcessorProfileMgrVtbl;
struct ITfInputProcessorProfileMgr {
    const ITfInputProcessorProfileMgrVtbl *lpVtbl;
};

typedef struct ITfRangeBackupVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfRangeBackup *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfRangeBackup *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfRangeBackup *This);
    HRESULT(STDMETHODCALLTYPE *Restore)(ITfRangeBackup *This, TfEditCookie cookie, ITfRange *range);
} ITfRangeBackupVtbl;
struct ITfRangeBackup {
    const ITfRangeBackupVtbl *lpVtbl;
};

typedef struct ITfFunctionProviderVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfFunctionProvider *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfFunctionProvider *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfFunctionProvider *This);
    HRESULT(STDMETHODCALLTYPE *GetType)(ITfFunctionProvider *This, GUID *guid);
    HRESULT(STDMETHODCALLTYPE *GetDescription)(ITfFunctionProvider *This, BSTR *desc);
    HRESULT(STDMETHODCALLTYPE *GetFunction)(ITfFunctionProvider *This, REFGUID guid, REFIID riid, IUnknown **func);
} ITfFunctionProviderVtbl;
struct ITfFunctionProvider {
    const ITfFunctionProviderVtbl *lpVtbl;
};

typedef struct IEnumTfFunctionProvidersVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfFunctionProviders *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfFunctionProviders *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfFunctionProviders *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfFunctionProviders *This, IEnumTfFunctionProviders **ret);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfFunctionProviders *This, ULONG count, ITfFunctionProvider **prov, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfFunctionProviders *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfFunctionProviders *This, ULONG count);
} IEnumTfFunctionProvidersVtbl;
struct IEnumTfFunctionProviders {
    const IEnumTfFunctionProvidersVtbl *lpVtbl;
};

typedef struct IEnumTfPropertiesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfProperties *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfProperties *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfProperties *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfProperties *This, IEnumTfProperties **ret);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfProperties *This, ULONG count, ITfProperty **props, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfProperties *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfProperties *This, ULONG count);
} IEnumTfPropertiesVtbl;
struct IEnumTfProperties {
    const IEnumTfPropertiesVtbl *lpVtbl;
};

typedef struct ITfTextInputProcessorVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfTextInputProcessor *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfTextInputProcessor *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfTextInputProcessor *This);
    HRESULT(STDMETHODCALLTYPE *Activate)(ITfTextInputProcessor *This, ITfThreadMgr *ptim, TfClientId tid);
    HRESULT(STDMETHODCALLTYPE *Deactivate)(ITfTextInputProcessor *This);
} ITfTextInputProcessorVtbl;
struct ITfTextInputProcessor {
    const ITfTextInputProcessorVtbl *lpVtbl;
};

typedef struct ITfKeyEventSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfKeyEventSink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfKeyEventSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfKeyEventSink *This);
    HRESULT(STDMETHODCALLTYPE *OnSetFocus)(ITfKeyEventSink *This, BOOL fForeground);
    HRESULT(STDMETHODCALLTYPE *OnTestKeyDown)(ITfKeyEventSink *This, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *OnTestKeyUp)(ITfKeyEventSink *This, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *OnKeyDown)(ITfKeyEventSink *This, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *OnKeyUp)(ITfKeyEventSink *This, ITfContext *pic, WPARAM wParam, LPARAM lParam, BOOL *pfEaten);
    HRESULT(STDMETHODCALLTYPE *OnPreservedKey)(ITfKeyEventSink *This, ITfContext *pic, REFGUID rguid, BOOL *pfEaten);
} ITfKeyEventSinkVtbl;
struct ITfKeyEventSink {
    const ITfKeyEventSinkVtbl *lpVtbl;
};

typedef struct IEnumTfLanguageProfilesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfLanguageProfiles *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfLanguageProfiles *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfLanguageProfiles *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfLanguageProfiles *This, IEnumTfLanguageProfiles **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfLanguageProfiles *This, ULONG ulCount, TF_LANGUAGEPROFILE *pProfile, ULONG *pcFetch);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfLanguageProfiles *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfLanguageProfiles *This, ULONG ulCount);
} IEnumTfLanguageProfilesVtbl;
struct IEnumTfLanguageProfiles {
    const IEnumTfLanguageProfilesVtbl *lpVtbl;
};

typedef struct IEnumTfInputProcessorProfilesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumTfInputProcessorProfiles *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumTfInputProcessorProfiles *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumTfInputProcessorProfiles *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumTfInputProcessorProfiles *This, IEnumTfInputProcessorProfiles **ppEnum);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumTfInputProcessorProfiles *This, ULONG ulCount, TF_INPUTPROCESSORPROFILE *pProfile, ULONG *pcFetch);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumTfInputProcessorProfiles *This);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumTfInputProcessorProfiles *This, ULONG ulCount);
} IEnumTfInputProcessorProfilesVtbl;
struct IEnumTfInputProcessorProfiles {
    const IEnumTfInputProcessorProfilesVtbl *lpVtbl;
};

typedef struct ITfActiveLanguageProfileNotifySinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfActiveLanguageProfileNotifySink *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfActiveLanguageProfileNotifySink *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfActiveLanguageProfileNotifySink *This);
    HRESULT(STDMETHODCALLTYPE *OnActivated)(ITfActiveLanguageProfileNotifySink *This, REFCLSID clsid, REFGUID guidProfile, BOOL fActivated);
} ITfActiveLanguageProfileNotifySinkVtbl;
struct ITfActiveLanguageProfileNotifySink {
    const ITfActiveLanguageProfileNotifySinkVtbl *lpVtbl;
};

typedef struct ITfPropertyStoreVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfPropertyStore *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfPropertyStore *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfPropertyStore *This);
    HRESULT(STDMETHODCALLTYPE *GetType)(ITfPropertyStore *This, GUID *pguid);
    HRESULT(STDMETHODCALLTYPE *GetDataType)(ITfPropertyStore *This, DWORD *pdwReserved);
    HRESULT(STDMETHODCALLTYPE *GetData)(ITfPropertyStore *This, VARIANT *pvarValue);
    HRESULT(STDMETHODCALLTYPE *OnTextUpdated)(ITfPropertyStore *This, DWORD dwFlags, ITfRange *pRangeNew, BOOL *pfAccept);
    HRESULT(STDMETHODCALLTYPE *Shrink)(ITfPropertyStore *This, ITfRange *pRangeNew, BOOL *pfFree);
    HRESULT(STDMETHODCALLTYPE *Divide)(ITfPropertyStore *This, ITfRange *pRangeThis, ITfRange *pRangeNew, ITfPropertyStore **ppPropStore);
    HRESULT(STDMETHODCALLTYPE *Clone)(ITfPropertyStore *This, ITfPropertyStore **pPropStore);
    HRESULT(STDMETHODCALLTYPE *GetPropertyRangeCreator)(ITfPropertyStore *This, CLSID *pclsid);
    HRESULT(STDMETHODCALLTYPE *Serialize)(ITfPropertyStore *This, IStream *pStream, ULONG *pcb);
} ITfPropertyStoreVtbl;
struct ITfPropertyStore {
    const ITfPropertyStoreVtbl *lpVtbl;
};
/* the owner of a context that has no text store of its own, which the thread manager asks about its window. */
typedef struct ITfContextOwner ITfContextOwner;
typedef struct ITfContextOwnerVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITfContextOwner *This, REFIID riid, void **ppvObject);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITfContextOwner *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITfContextOwner *This);
    HRESULT(STDMETHODCALLTYPE *GetACPFromPoint)(ITfContextOwner *This, const POINT *ptScreen, DWORD dwFlags, LONG *pacp);
    HRESULT(STDMETHODCALLTYPE *GetTextExt)(ITfContextOwner *This, LONG acpStart, LONG acpEnd, RECT *prc, BOOL *pfClipped);
    HRESULT(STDMETHODCALLTYPE *GetScreenExt)(ITfContextOwner *This, RECT *prc);
    HRESULT(STDMETHODCALLTYPE *GetStatus)(ITfContextOwner *This, TF_STATUS *pdcs);
    HRESULT(STDMETHODCALLTYPE *GetWnd)(ITfContextOwner *This, HWND *phwnd);
    HRESULT(STDMETHODCALLTYPE *GetAttribute)(ITfContextOwner *This, REFGUID rguidAttribute, VARIANT *pvarValue);
} ITfContextOwnerVtbl;
struct ITfContextOwner {
    const ITfContextOwnerVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ entry points of msctf.dll */

TFAPI HRESULT WINAPI TF_CreateThreadMgr(ITfThreadMgr **thread_mgr);
TFAPI HRESULT WINAPI TF_GetThreadMgr(ITfThreadMgr **thread_mgr);
TFAPI HRESULT WINAPI TF_CreateInputProcessorProfiles(ITfInputProcessorProfiles **profiles);
TFAPI HRESULT WINAPI TF_CreateCategoryMgr(ITfCategoryMgr **category_mgr);
TFAPI HRESULT WINAPI TF_CreateDisplayAttributeMgr(ITfDisplayAttributeMgr **display_attribute_mgr);

#endif
