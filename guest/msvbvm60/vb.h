/*
 * msvbvm60.dll: the runtime of Visual Basic 6. a program compiled to native code calls the helpers of this library for
 * strings, variants, conversions, arrays, objects and errors, and starts through ThunRTMain.
 */
#ifndef VB_H
#define VB_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>

/* the helpers take their arguments the way the compiler of Visual Basic passes them. */
#define VB_STD __attribute__((stdcall))
#define VB_FAST __attribute__((fastcall))
#define VB_THIS __attribute__((thiscall))
#define VB_C __attribute__((cdecl))

/* the reference forms of the variant members. */
#define V_ARRAYREF(v) ((v)->pparray)
#define V_UNKNOWNREF(v) ((v)->ppunkVal)
#define V_DISPATCHREF(v) ((IDispatch **)(v)->ppunkVal)
#define V_BSTRREF(v) ((v)->pbstrVal)

/* the numbers of the runtime errors. */
enum {
    VBERR_RETURN_WITHOUT_GOSUB = 3,
    VBERR_INVALID_CALL = 5,
    VBERR_OVERFLOW = 6,
    VBERR_OUT_OF_MEMORY = 7,
    VBERR_SUBSCRIPT = 9,
    VBERR_DIVISION_BY_ZERO = 11,
    VBERR_TYPE_MISMATCH = 13,
    VBERR_OUT_OF_STRING_SPACE = 14,
    VBERR_NO_RESUME = 20,
    VBERR_BAD_FILE_NAME = 52,
    VBERR_BAD_FILE_NUMBER = 52,
    VBERR_FILE_NOT_FOUND = 53,
    VBERR_FILE_ALREADY_OPEN = 55,
    VBERR_IO = 57,
    VBERR_END_OF_FILE = 62,
    VBERR_PATH_NOT_FOUND = 76,
    VBERR_INVALID_USE_OF_NULL = 94,
    VBERR_OBJECT_REQUIRED = 424,
    VBERR_NO_SUCH_FILE_OR_CLASS = 432,
    VBERR_CANNOT_CREATE_OBJECT = 429,
    VBERR_NO_SUCH_MEMBER = 438,
    VBERR_APPLICATION_DEFINED = 440,
    VBERR_END = 40040
};

/* the exception code that carries a runtime error, with the number in the first parameter. */
#define VB_EXCEPTION 0xE0564200u

/* the state a thread of a program keeps. */
typedef struct VbThread {
    LONG err_number;
    LONG erl;
    LONG hresult;
    LONG last_dll_error;
    BSTR err_description;
    BSTR err_source;
    BSTR err_helpfile;
    LONG err_helpcontext;
    VARIANT scratch[4];
    BOOL in_handler;
} VbThread;

VbThread *VbThreadState(void);
/* raises the runtime error. it does not return. */
__attribute__((noreturn)) void VbRaise(LONG number);
/* the same, for an error whose description and source the caller set already. */
__attribute__((noreturn)) void VbRaiseKeep(LONG number);
/* raises the error that stands for a failed hresult, which is the hresult itself when no runtime error stands for it. */
__attribute__((noreturn)) void VbRaiseHr(HRESULT hr);
/* clears the error of the thread. */
void VbClearError(void);
/* the name of the project, which is the source of the errors the runtime raises. */
const WCHAR *VbProjectName(void);
/* the runtime error that stands for a failed hresult. */
LONG VbErrorOfHresult(HRESULT hr);
/* raises the error of a failed hresult, and returns for a success. */
void VbCheck(HRESULT hr);
LCID VbLocale(void);
/* the runtime error of a failed system call. */
LONG VbErrorOfWin32(DWORD error);
/* arguments of the library functions, in vbfn.c. */
BOOL VbIsMissing(const VARIANT *v);
VB_STD double vb___vbaDateVar(VARIANT *v);
void VbValue(VARIANT *out, const VARIANT *in);
LONG VbLong(const VARIANT *v);
/* the text of an argument, false for Null. the string belongs to the caller. */
BOOL VbText(const VARIANT *v, BSTR *out);

void VbOutputDebug(const char *text);

/* the Collection of Visual Basic, in vbcoll.c. */
extern const GUID CLSID_VbCollection;
void *VbNewCollection(void);

/* the text of a runtime error and the dialog an error that nothing handles ends the program with. */
const WCHAR *VbErrorText(LONG number);
void VbTerminateWithError(LONG number);
/* the title of the project, for dialogs. */
void VbProjectTitle(WCHAR *out, int count);

/* the compiled structures of an executable that main.c reads. */
#pragma pack(push, 1)
typedef struct VbHeader {
    char signature[4];
    UINT16 runtime_build;
    char language_dll[14];
    char backup_language_dll[14];
    UINT16 runtime_revision;
    UINT32 language_id;
    UINT32 backup_language_id;
    UINT32 sub_main;
    UINT32 project_info;
    UINT32 mdl_intrinsic_flags;
    UINT32 mdl_intrinsic_flags2;
    UINT32 thread_flags;
    UINT32 thread_count;
    UINT16 form_count;
    UINT16 external_count;
    UINT32 thunk_count;
    UINT32 gui_table;
    UINT32 external_table;
    UINT32 com_register_data;
    UINT32 project_exename;
    UINT32 project_title;
    UINT32 help_file;
    UINT32 project_name;
} VbHeader;
#pragma pack(pop)


/* the members of an object of the program, in vbmember.c. */
HRESULT VbMemberIds(const BYTE *descriptor, const BYTE *priv, LPOLESTR *names, UINT count, DISPID *ids);
HRESULT VbMemberInvoke(void *object, void **vtable, const BYTE *descriptor, const BYTE *priv, DISPID id, WORD flags, DISPPARAMS *params,
                       VARIANT *result, EXCEPINFO *info, UINT *bad);
/* calls a procedure of the compiled code with the words as its stack arguments. */
LONG VbCallMember(void *fn, DWORD *argv, int argc);

/* forms and controls, in vbform.c. */
typedef struct VbForm VbForm;
void VbLoadProject(const VbHeader *header);
BOOL VbStartForms(HINSTANCE module);
BOOL VbHasForms(void);
int VbFormCount(void);
/* the object of New on a class of the project, or null when the class is not one that can be made. */
void *VbNewProjectObject(const void *info);
/* the object of New on a class description of any kind. */
void VbNewObject(const void *info, void **out);
/* the global object of the library, which holds App, Screen and the loading of forms. */
extern const GUID CLSID_VbGlobal;
void *VbNewGlobal(void);

#endif
