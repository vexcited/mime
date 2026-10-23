/*
 * the type library model filled by both the msft reader and the creator, and shared by the objects on top. A read
 * library is decoded lazily into TL_TYPE records of functions, variables and implemented types; a created library is
 * built straight into the same records and written as an msft file.
 */
#ifndef MIME_OLEAUT32_TYPELIB_H
#define MIME_OLEAUT32_TYPELIB_H

#include "oleaut32_private.h"

typedef struct TL_LIB TL_LIB;
typedef struct TL_TYPE TL_TYPE;
typedef struct TYPEINFO TYPEINFO;

struct TYPEINFO {
    const ITypeInfo2Vtbl *lpVtbl;
    const ITypeCompVtbl *lpCompVtbl;
    const ICreateTypeInfo2Vtbl *lpCreateVtbl;
    LONG Refs;
    TL_TYPE *Type;
    BOOL VtableView;
    TYPEINFO *Twin;
};

extern const ITypeInfo2Vtbl g_typeinfo_vtbl;
extern const ITypeCompVtbl g_typecomp_vtbl;
extern const ICreateTypeInfo2Vtbl g_createinfo_vtbl;

typedef struct TL_CUST {
    struct TL_CUST *Next;
    GUID Guid;
    VARIANT Value;
} TL_CUST;

typedef struct {
    WCHAR *Name;
    TYPEDESC Type;
    USHORT Flags;
    /* valid when Flags has PARAMFLAG_FHASDEFAULT. */
    VARIANT Default;
    TL_CUST *Cust;
} TL_PARAM;

typedef struct {
    MEMBERID Id;
    FUNCKIND Kind;
    INVOKEKIND Invoke;
    CALLCONV Conv;
    USHORT Flags;
    /* in bytes of the library's own pointer size. */
    SHORT VtOffset;
    BOOL VtOffsetSet;
    SHORT OptCount;
    USHORT ParamCount;
    SHORT ScodeCount;
    SCODE *Scodes;
    TYPEDESC Ret;
    WCHAR *Name;
    TL_PARAM *Params;
    DWORD HelpContext, HelpStringContext;
    WCHAR *Doc;
    WCHAR *HelpFile;
    /* module functions: the dll and the entry, by name or ordinal. */
    WCHAR *DllName;
    WCHAR *EntryName;
    USHORT Ordinal;
    BOOL HasOrdinal;
    TL_CUST *Cust;
} TL_FUNC;

typedef struct {
    MEMBERID Id;
    WCHAR *Name;
    VARKIND Kind;
    ULONG Offset;
    VARIANT Value;
    TYPEDESC Type;
    USHORT Flags;
    DWORD HelpContext, HelpStringContext;
    WCHAR *Doc;
    TL_CUST *Cust;
} TL_VAR;

typedef struct {
    HREFTYPE Ref;
    INT Flags;
    TL_CUST *Cust;
} TL_IMPL;

/* a type of another library, by guid or by index. */
typedef struct {
    GUID Lib;
    LCID Lcid;
    WORD Major, Minor;
    WCHAR *File;
    GUID Type;
    BOOL ByGuid;
    ULONG Index;
    TYPEKIND Kind;
} TL_IMPORT;

struct TL_TYPE {
    TL_LIB *Lib;
    ULONG Index;
    TYPEKIND Kind;
    BOOL HasGuid;
    GUID Guid;
    WORD Major, Minor;
    USHORT Flags;
    USHORT Alignment;
    ULONG SizeInstance;
    USHORT SizeVft;
    WCHAR *Name;
    WCHAR *Doc;
    WCHAR *Schema;
    WCHAR *DllName;
    DWORD HelpContext, HelpStringContext;
    TYPEDESC Alias;
    USHORT IdlFlags;
    USHORT FuncCount, VarCount, ImplCount;
    TL_FUNC *Funcs;
    TL_VAR *Vars;
    TL_IMPL *Impls;
    TL_CUST *Cust;
    MEMBERID Constructor, Destructor;
    /* functions and interfaces contributed by the bases of an interface or dispinterface. */
    USHORT InheritedFuncs, InheritedImpls;
    BOOL Loaded;
    BOOL Layouted;
    /* member alignment of a struct, when set. */
    BOOL PackSet;
    USHORT Pack;
    /* record offset in the file, for a type that was read. */
    ULONG FileOffset;
    /* raw file offsets, until decoded. */
    ULONG NameOff, DocOff, GuidOff, CustOff;
    /* the dispatch or plain view, and the vtable view of a dual interface. */
    TYPEINFO *Info, *Dual;
};

typedef struct {
    ULONG Offset, Length;
} TL_SEGMENT;

struct TL_LIB {
    const ITypeLib2Vtbl *lpVtbl;
    const ITypeCompVtbl *lpCompVtbl;
    const ICreateTypeLib2Vtbl *lpCreateVtbl;
    LONG Refs;
    CRITICAL_SECTION Lock;
    GUID Guid;
    BOOL HasGuid;
    LCID Lcid;
    WORD Major, Minor;
    WORD Flags;
    SYSKIND Syskind;
    WCHAR *Name, *Doc, *HelpFile, *HelpStringDll;
    DWORD HelpContext, HelpStringContext;
    TL_CUST *Cust;
    ULONG TypeCount, TypeCapacity;
    TL_TYPE **Types;
    /* imported types, and the libraries they were found in. */
    ULONG ImportCount;
    TL_IMPORT *Imports;
    ITypeLib **ImportedLibs;
    /* reference to IDispatch. */
    HREFTYPE DispatchRef;
    BOOL Writable;
    /* the file image of a library that was read. */
    const BYTE *Image;
    SIZE_T ImageSize;
    void (*ReleaseImage)(void *context, const BYTE *image);
    void *ImageContext;
    SYSKIND FileSyskind;
    UINT CodePage;
    TL_SEGMENT Seg[15];
    ULONG TypeOffsets, ImpOffset;
    /* loaded from, or to be saved to. */
    WCHAR *Path;
    /* strings and descriptions live as long as the library. */
    struct TL_CHUNK *Chunks;
    /* type name hash for FindName and GetTypeInfoOfName. */
    BOOL NamesIndexed;
};

#define SEG_TYPEINFO 0
#define SEG_IMPINFO 1
#define SEG_IMPFILES 2
#define SEG_REFTAB 3
#define SEG_GUIDHASH 4
#define SEG_GUID 5
#define SEG_NAMEHASH 6
#define SEG_NAME 7
#define SEG_STRING 8
#define SEG_TYPDESC 9
#define SEG_ARRAYDESC 10
#define SEG_CUSTDATA 11
#define SEG_CDGUID 12

/* library heap. */
void *TlAlloc(TL_LIB *lib, SIZE_T size);
void TlHeapFree(TL_LIB *lib);
WCHAR *TlTypeName(TL_TYPE *t);
ITypeInfo *TlInfoVtableHalf(ITypeInfo *info);
HRESULT TlCustAll(TL_CUST *list, CUSTDATA *data);
WCHAR *TlStrDup(TL_LIB *lib, const WCHAR *text);
WCHAR *TlStrDupLen(TL_LIB *lib, const WCHAR *text, SIZE_T length);

/* vtable offsets and sizes converted between the library's and this processor's pointer size. */
SHORT TlNativeVtOffset(const TL_LIB *lib, SHORT offset);
USHORT TlNativeVtSize(const TL_LIB *lib, USHORT size);
BOOL TlNativeSyskind(SYSKIND kind);

/* reading. */
HRESULT TlReadImage(const BYTE *image, SIZE_T size, SYSKIND target, void (*release)(void *, const BYTE *),
                    void *context, TL_LIB **lib);
HRESULT TlLoadMembers(TL_TYPE *type);
HRESULT TlEnsureLayout(TL_TYPE *type);
/* strings by file offset. */
WCHAR *TlNameAt(TL_LIB *lib, ULONG offset);
WCHAR *TlStringAt(TL_LIB *lib, ULONG offset);
BOOL TlGuidAt(TL_LIB *lib, ULONG offset, GUID *guid);

/* type references. */
#define HREF_DEPTH(h) ((h) >> 24)
#define HREF_LOCAL_STEP 128
HREFTYPE TlHrefOfIndex(ULONG index);
BOOL TlIndexOfHref(HREFTYPE ref, ULONG *index);
HRESULT TlResolveHref(TL_TYPE *from, HREFTYPE ref, ITypeInfo **info);

/* objects. */
HRESULT TlGetTypeInfo(TL_LIB *lib, ULONG index, BOOL vtable_view, ITypeInfo **info);
HRESULT TlTypeInfoCreate(TL_TYPE *type, BOOL vtable_view, TYPEINFO **info);
void TlLibAddRef(TL_LIB *lib);
void TlLibRelease(TL_LIB *lib);
extern const ITypeLib2Vtbl g_typelib_vtbl;
extern const ITypeCompVtbl g_typelib_comp_vtbl;
extern const ICreateTypeLib2Vtbl g_createlib_vtbl;
TL_LIB *TlLibCreate(void);
HRESULT TlLibQuery(TL_LIB *lib, REFIID iid, void **object);
TL_TYPE *TlTypeAdd(TL_LIB *lib);
void TlLibDestroy(TL_LIB *lib);
TL_LIB *TlLibOfInfo(TYPEINFO *info);
TL_TYPE *TlTypeOfInfo(TYPEINFO *info);

/* FUNCDESC and VARDESC from the model. */
HRESULT TlBuildFuncDesc(TL_TYPE *type, TL_FUNC *func, BOOL dispatch_view, USHORT depth, FUNCDESC **out);
HRESULT TlBuildVarDesc(TL_TYPE *type, TL_VAR *var, VARDESC **out);
void TlCopyTypeDesc(TL_LIB *lib, TYPEDESC *to, const TYPEDESC *from);
HRESULT TlVariantCopy(TL_LIB *lib, VARIANT *to, const VARIANT *from);

/* loading and registration. */
HRESULT TlLoadFile(LPCOLESTR path, SYSKIND kind, TL_LIB **lib);
HRESULT TlSaveMsft(TL_LIB *lib, LPCOLESTR path);
HRESULT TlInvokeOnType(ITypeInfo *self, TL_TYPE *type, BOOL dual_dispatch_view, PVOID instance, MEMBERID id, WORD flags,
                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_err);

/* dispatch. */
HRESULT AutDispatchInvoke(IUnknown *instance, ITypeInfo *info, MEMBERID id, WORD flags, DISPPARAMS *params,
                          VARIANT *result, EXCEPINFO *excep, UINT *arg_err);

#endif
