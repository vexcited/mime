/*
 * what the c++ files of msvcrt share: the classes of the c runtime's
 * c++ abi, the records compilers emit for run time type information and
 * exceptions, and the per thread state of exception handling.
 */
#ifndef MIME_MSVCRT_CXX_H
#define MIME_MSVCRT_CXX_H

/* the headers are c ones, without linkage of their own. */
extern "C" {
#include <mime/win32/winbase.h>
void *__cdecl malloc(size_t size);
void __cdecl free(void *p);
__declspec(noreturn) void __cdecl abort(void);
int __cdecl _callnewh(size_t size);
int(__cdecl *MsvcrtSetNewHandler(int(__cdecl *handler)(size_t)))(size_t);
int(__cdecl *MsvcrtQueryNewHandler(void))(size_t);
int MsvcrtSetNewMode(int mode);
int MsvcrtQueryNewMode(void);
char *MsvcrtUndecorateType(const char *decorated);
}

inline void *operator new(size_t, void *p) { return p; }

__declspec(noreturn) void __cdecl terminate(void);

#if defined(__i386__)
#define __thiscall_member __thiscall
#define MSVCRT_C_PREFIX "_"
#else
#define __thiscall_member
#define MSVCRT_C_PREFIX ""
#endif

/* the names of members the compiler would not emit under their own, by architecture. */
#define MSVCRT_NAME(name) __asm__(MSVCRT_NAME_##name)
#if defined(__i386__)
#define MSVCRT_NAME_BAD_CAST_DEFAULT "??_Fbad_cast@@QAEXXZ"
#define MSVCRT_NAME_BAD_TYPEID_DEFAULT "??_Fbad_typeid@@QAEXXZ"
#define MSVCRT_NAME_EXCEPTION_VECTOR_DELETING "??_Eexception@@UAEPAXI@Z"
#define MSVCRT_NAME_BAD_CAST_VECTOR_DELETING "??_Ebad_cast@@UAEPAXI@Z"
#define MSVCRT_NAME_BAD_TYPEID_VECTOR_DELETING "??_Ebad_typeid@@UAEPAXI@Z"
#define MSVCRT_NAME_NON_RTTI_VECTOR_DELETING "??_E__non_rtti_object@@UAEPAXI@Z"
#define MSVCRT_NAME_TYPE_INFO_DELETING "??_Gtype_info@@UAEPAXI@Z"
#else
#define MSVCRT_NAME_BAD_CAST_DEFAULT "??_Fbad_cast@@QEAAXXZ"
#define MSVCRT_NAME_BAD_TYPEID_DEFAULT "??_Fbad_typeid@@QEAAXXZ"
#define MSVCRT_NAME_EXCEPTION_VECTOR_DELETING "??_Eexception@@UEAAPEAXI@Z"
#define MSVCRT_NAME_BAD_CAST_VECTOR_DELETING "??_Ebad_cast@@UEAAPEAXI@Z"
#define MSVCRT_NAME_BAD_TYPEID_VECTOR_DELETING "??_Ebad_typeid@@UEAAPEAXI@Z"
#define MSVCRT_NAME_NON_RTTI_VECTOR_DELETING "??_E__non_rtti_object@@UEAAPEAXI@Z"
#define MSVCRT_NAME_TYPE_INFO_DELETING "??_Gtype_info@@UEAAPEAXI@Z"
#endif

/* ------------------------------------------------------------------ classes */

struct __type_info_node;

/* the type descriptor compilers emit: this vtable, a cache for the undecorated name, the decorated one. */
class type_info {
public:
    virtual ~type_info();
    const char *name(__type_info_node *node) const;
    const char *name() const;
    const char *raw_name() const;
    int before(const type_info &other) const;
    int operator==(const type_info &other) const;
    int operator!=(const type_info &other) const;

private:
    type_info(const type_info &);
    type_info &operator=(const type_info &);
    void *_m_data;
    char _m_d_name[1];
};

class exception {
public:
    exception();
    exception(const char *const &what);
    exception(const char *const &what, int);
    exception(const exception &other);
    exception &operator=(const exception &other);
    virtual ~exception();
    virtual const char *what() const;

private:
    const char *_m_what;
    int _m_doFree;
};

class bad_cast : public exception {
private:
    bad_cast(const char *const *what);

public:
    bad_cast(const char *const &what);
    bad_cast(const char *what);
    bad_cast(const bad_cast &other);
    bad_cast &operator=(const bad_cast &other);
    virtual ~bad_cast();
    /* string literals match two constructors: the runtime throws through this. */
    static bad_cast create(const char *what) { return bad_cast(&what); }

protected:
    virtual void _Doraise() const;
};

class bad_typeid : public exception {
public:
    bad_typeid(const char *what);
    bad_typeid(const bad_typeid &other);
    bad_typeid &operator=(const bad_typeid &other);
    virtual ~bad_typeid();

protected:
    virtual void _Doraise() const;
};

class __non_rtti_object : public bad_typeid {
public:
    __non_rtti_object(const char *what);
    __non_rtti_object(const __non_rtti_object &other);
    __non_rtti_object &operator=(const __non_rtti_object &other);
    virtual ~__non_rtti_object();
};

/* ------------------------------------------------------------------ run time type information */

/* where a base class sits: at mdisp, or through the virtual base table at pdisp, entry vdisp. */
struct RTTI_PMD {
    int mdisp;
    int pdisp;
    int vdisp;
};

/* pointers of these records are rvas on 64-bit windows and addresses on x86, an int either way. */
struct RTTI_LOCATOR {
    ULONG signature;
    ULONG offset;
    ULONG cd_offset;
    int type;
    int hierarchy;
#if defined(_WIN64)
    int self;
#endif
};

struct RTTI_HIERARCHY {
    ULONG signature;
    ULONG attributes;
    ULONG count;
    int bases;
};

struct RTTI_BASE {
    int type;
    ULONG contained;
    RTTI_PMD where;
    ULONG attributes;
    int hierarchy;
};

#define RTTI_HIERARCHY_MULTIPLE 1
#define RTTI_HIERARCHY_VIRTUAL 2
#define RTTI_BASE_NOT_VISIBLE 0x01
#define RTTI_BASE_AMBIGUOUS 0x02
#define RTTI_BASE_PRIVATE 0x04

/* ------------------------------------------------------------------ exceptions */

/* the thread's state: its handlers, the catch blocks running, newest first, and throws not yet caught. */
struct MSVCRT_CATCH;
struct MSVCRT_EH_THREAD {
    void *terminate;
    void *unexpected;
    void *translator;
    MSVCRT_CATCH *catches;
    int throws;
    /* the object of the exception a catch is unwinding to, which the catches it leaves must not destroy. */
    void *caught_object;
};

extern "C" MSVCRT_EH_THREAD *MsvcrtEhThread(void);
extern "C" int MsvcrtIsExceptionTypeof(const type_info *type, const EXCEPTION_RECORD *record);

#endif
