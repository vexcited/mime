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
    LONG last_dll_error;
    BSTR err_description;
    BSTR err_source;
    VARIANT scratch[4];
    BOOL in_handler;
} VbThread;

VbThread *VbThreadState(void);
/* raises the runtime error. it does not return. */
__attribute__((noreturn)) void VbRaise(LONG number);
/* the runtime error that stands for a failed hresult. */
LONG VbErrorOfHresult(HRESULT hr);
/* raises the error of a failed hresult, and returns for a success. */
void VbCheck(HRESULT hr);
LCID VbLocale(void);

void VbOutputDebug(const char *text);

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

#endif
