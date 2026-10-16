/*
 * the c++ part of msvcrt: type_info, whose objects are the type
 * descriptors compilers emit, the exception classes programs derive
 * from, operator new and delete, terminate and unexpected, and the
 * casts of run time type information. written in c++ so the compiler's
 * microsoft abi gives the exported names, vtables and type information
 * programs were built against.
 */
#include "cxx.h"

/* ------------------------------------------------------------------ operator new and delete */

/* msvcrt's operator new returns null when the new handler gives up, it never throws. */
void *__cdecl operator new(size_t size)
{
    for (;;) {
        void *p = malloc(size);
        if (p || !_callnewh(size))
            return p;
    }
}

void *__cdecl operator new(size_t size, int, const char *, int) { return operator new(size); }
void *__cdecl operator new[](size_t size) { return operator new(size); }
void *__cdecl operator new[](size_t size, int, const char *, int) { return operator new(size); }
void __cdecl operator delete(void *p) noexcept { free(p); }
void __cdecl operator delete[](void *p) noexcept { free(p); }
void __cdecl operator delete(void *p, size_t) noexcept { free(p); }
void __cdecl operator delete[](void *p, size_t) noexcept { free(p); }

typedef int(__cdecl *new_handler_t)(size_t);
typedef void(__cdecl *std_new_handler_t)(void);

new_handler_t __cdecl _set_new_handler(new_handler_t handler) { return MsvcrtSetNewHandler(handler); }
new_handler_t __cdecl _query_new_handler(void) { return MsvcrtQueryNewHandler(); }
int __cdecl _set_new_mode(int mode) { return MsvcrtSetNewMode(mode); }
int __cdecl _query_new_mode(void) { return MsvcrtQueryNewMode(); }

/* the standard handler is not kept: only null may be set, as in msvcrt. */
std_new_handler_t __cdecl set_new_handler(std_new_handler_t handler) { return nullptr; }

/* ------------------------------------------------------------------ type_info */

type_info::~type_info() {}

/* the undecorated name, made the first time and kept in the descriptor's spare field. */
const char *type_info::name(__type_info_node *) const
{
    if (!_m_data) {
        char *undecorated = MsvcrtUndecorateType(_m_d_name);
        if (undecorated && InterlockedCompareExchangePointer(const_cast<void **>(&_m_data), undecorated, nullptr))
            free(undecorated);
    }
    return _m_data ? static_cast<const char *>(_m_data) : _m_d_name;
}

const char *type_info::name() const { return name(nullptr); }
const char *type_info::raw_name() const { return _m_d_name; }
int type_info::before(const type_info &other) const { return strcmp(_m_d_name + 1, other._m_d_name + 1) < 0; }
int type_info::operator==(const type_info &other) const { return this == &other || !strcmp(_m_d_name + 1, other._m_d_name + 1); }
int type_info::operator!=(const type_info &other) const { return !(*this == other); }

/*
 * type_info's vtable, which the compiler does not emit for the class
 * itself: no locator before it, then the scalar deleting destructor.
 */
extern "C" void *__thiscall_member MsvcrtTypeInfoDeleting(type_info *self, unsigned flags) MSVCRT_NAME(TYPE_INFO_DELETING);
extern "C" void *__thiscall_member MsvcrtTypeInfoDeleting(type_info *self, unsigned flags)
{
    self->~type_info();
    if (flags & 1)
        operator delete(self);
    return self;
}

#if defined(_WIN64)
__asm__(".section .rdata,\"dr\"\n.p2align 3\n.quad 0\n.globl \"??_7type_info@@6B@\"\n\"??_7type_info@@6B@\":\n"
        ".quad \"" MSVCRT_NAME_TYPE_INFO_DELETING "\"\n.text\n");
#else
__asm__(".section .rdata,\"dr\"\n.p2align 2\n.long 0\n.globl \"??_7type_info@@6B@\"\n\"??_7type_info@@6B@\":\n"
        ".long \"" MSVCRT_NAME_TYPE_INFO_DELETING "\"\n.text\n");
#endif

/* ------------------------------------------------------------------ exception classes */

exception::exception() : _m_what(nullptr), _m_doFree(0) {}

exception::exception(const char *const &what) : _m_what(nullptr), _m_doFree(0)
{
    if (what) {
        size_t n = strlen(what) + 1;
        char *copy = static_cast<char *>(malloc(n));
        if (copy) {
            memcpy(copy, what, n);
            _m_what = copy;
            _m_doFree = 1;
        }
    }
}

/* the message kept as it is, not copied. */
exception::exception(const char *const &what, int) : _m_what(what), _m_doFree(0) {}

exception::exception(const exception &other) : _m_what(nullptr), _m_doFree(0) { *this = other; }

exception &exception::operator=(const exception &other)
{
    if (this == &other)
        return *this;
    if (_m_doFree)
        free(const_cast<char *>(_m_what));
    _m_what = nullptr;
    _m_doFree = 0;
    if (other._m_doFree)
        new (this) exception(other._m_what);
    else
        _m_what = other._m_what;
    return *this;
}

exception::~exception()
{
    if (_m_doFree)
        free(const_cast<char *>(_m_what));
}

const char *exception::what() const { return _m_what ? _m_what : "Unknown exception"; }

bad_cast::bad_cast(const char *const *what) : exception(*what, 1) {}
bad_cast::bad_cast(const char *const &what) : exception(what) {}
bad_cast::bad_cast(const char *what) : exception(what, 1) {}
bad_cast::bad_cast(const bad_cast &other) : exception(other) {}
bad_cast &bad_cast::operator=(const bad_cast &other)
{
    exception::operator=(other);
    return *this;
}
bad_cast::~bad_cast() {}
void bad_cast::_Doraise() const { throw *this; }

bad_typeid::bad_typeid(const char *what) : exception(what, 1) {}
bad_typeid::bad_typeid(const bad_typeid &other) : exception(other) {}
bad_typeid &bad_typeid::operator=(const bad_typeid &other)
{
    exception::operator=(other);
    return *this;
}
bad_typeid::~bad_typeid() {}
void bad_typeid::_Doraise() const { throw *this; }

__non_rtti_object::__non_rtti_object(const char *what) : bad_typeid(what) {}
__non_rtti_object::__non_rtti_object(const __non_rtti_object &other) : bad_typeid(other) {}
__non_rtti_object &__non_rtti_object::operator=(const __non_rtti_object &other)
{
    bad_typeid::operator=(other);
    return *this;
}
__non_rtti_object::~__non_rtti_object() {}

/* the default constructors msvcrt exports as closures, with the standard messages. */
extern "C" void __thiscall_member MsvcrtBadCastDefault(bad_cast *self) MSVCRT_NAME(BAD_CAST_DEFAULT);
extern "C" void __thiscall_member MsvcrtBadCastDefault(bad_cast *self) { new (self) bad_cast(bad_cast::create("bad cast")); }
extern "C" void __thiscall_member MsvcrtBadTypeidDefault(bad_typeid *self) MSVCRT_NAME(BAD_TYPEID_DEFAULT);
extern "C" void __thiscall_member MsvcrtBadTypeidDefault(bad_typeid *self) { new (self) bad_typeid("bad typeid"); }

/*
 * the vector deleting destructors, which vtables of the compiler's own
 * have in place of the scalar ones: flags bit 2 destroys an array whose
 * count precedes it, bit 1 frees the memory.
 */
template <class T> static void *vector_deleting(T *self, unsigned flags)
{
    if (flags & 2) {
        size_t *count = reinterpret_cast<size_t *>(self) - 1;
        for (size_t i = *count; i > 0; i--)
            self[i - 1].~T();
        if (flags & 1)
            operator delete[](count);
        return count;
    }
    self->~T();
    if (flags & 1)
        operator delete(self);
    return self;
}

#define VECTOR_DELETING(T, NAME)                                                                                       \
    extern "C" void *__thiscall_member MsvcrtVectorDeleting_##T(T *self, unsigned flags) MSVCRT_NAME(NAME);             \
    extern "C" void *__thiscall_member MsvcrtVectorDeleting_##T(T *self, unsigned flags)                                \
    {                                                                                                                  \
        return vector_deleting(self, flags);                                                                           \
    }
/* the scalar deleting destructors msvcrt exports, under names of their own for the linker. */
#define SCALAR_DELETING(T)                                                                                             \
    extern "C" void *__thiscall_member MsvcrtScalarDeleting_##T(T *self, unsigned flags)                                \
        __asm__(MSVCRT_C_PREFIX "MsvcrtScalarDeleting_" #T);                                                                           \
    extern "C" void *__thiscall_member MsvcrtScalarDeleting_##T(T *self, unsigned flags)                                \
    {                                                                                                                  \
        return vector_deleting(self, flags & 1);                                                                       \
    }
SCALAR_DELETING(exception)
SCALAR_DELETING(bad_cast)
SCALAR_DELETING(bad_typeid)
SCALAR_DELETING(__non_rtti_object)

VECTOR_DELETING(exception, EXCEPTION_VECTOR_DELETING)
VECTOR_DELETING(bad_cast, BAD_CAST_VECTOR_DELETING)
VECTOR_DELETING(bad_typeid, BAD_TYPEID_VECTOR_DELETING)
VECTOR_DELETING(__non_rtti_object, NON_RTTI_VECTOR_DELETING)

/* objects of each class, so the compiler emits their vtables and scalar deleting destructors. */
void MsvcrtEmitVtables()
{
    delete new exception();
    delete new bad_cast(bad_cast::create(""));
    delete new bad_typeid("");
    delete new __non_rtti_object("");
}

/* ------------------------------------------------------------------ terminate and unexpected */

typedef void(__cdecl *terminate_function)(void);

terminate_function __cdecl set_terminate(terminate_function handler)
{
    MSVCRT_EH_THREAD *t = MsvcrtEhThread();
    terminate_function old = (terminate_function)t->terminate;
    t->terminate = (void *)handler;
    return old;
}

terminate_function __cdecl set_unexpected(terminate_function handler)
{
    MSVCRT_EH_THREAD *t = MsvcrtEhThread();
    terminate_function old = (terminate_function)t->unexpected;
    t->unexpected = (void *)handler;
    return old;
}

extern "C" terminate_function __cdecl _get_terminate(void) { return (terminate_function)MsvcrtEhThread()->terminate; }
extern "C" terminate_function __cdecl _get_unexpected(void) { return (terminate_function)MsvcrtEhThread()->unexpected; }

/* the handler set, then abort, as terminate does when the handler returns or there is none. */
void __cdecl terminate(void)
{
    terminate_function handler = (terminate_function)MsvcrtEhThread()->terminate;
    if (handler)
        handler();
    abort();
}

void __cdecl unexpected(void)
{
    terminate_function handler = (terminate_function)MsvcrtEhThread()->unexpected;
    if (handler)
        handler();
    terminate();
}

void __cdecl _inconsistency(void) { terminate(); }

extern "C" void __cdecl __std_terminate(void) { terminate(); }

typedef void(__cdecl *se_translator_t)(unsigned, EXCEPTION_POINTERS *);

se_translator_t __cdecl _set_se_translator(se_translator_t translator)
{
    MSVCRT_EH_THREAD *t = MsvcrtEhThread();
    se_translator_t old = (se_translator_t)t->translator;
    t->translator = (void *)translator;
    return old;
}

extern "C" int __cdecl __uncaught_exception(void) { return MsvcrtEhThread()->throws > 0; }

/* whether an exception is a c++ one of the type given or a class derived from it. */
int __cdecl _is_exception_typeof(const type_info &type, EXCEPTION_POINTERS *pointers)
{
    return MsvcrtIsExceptionTypeof(&type, pointers->ExceptionRecord);
}

/* ------------------------------------------------------------------ run time type information */

/* the complete object locator before an object's vtable. */
static const RTTI_LOCATOR *locator_of(const void *object)
{
    const void *const *vtable = *static_cast<const void *const *const *>(object);
    return static_cast<const RTTI_LOCATOR *>(vtable[-1]);
}

/* the image whose rvas a locator's fields count from: on x86 they are addresses. */
static ULONG_PTR locator_base(const RTTI_LOCATOR *locator)
{
#if defined(_WIN64)
    if (locator->signature == 1)
        return (ULONG_PTR)locator - locator->self;
    void *base = nullptr;
    RtlPcToFileHeader(const_cast<RTTI_LOCATOR *>(locator), &base);
    return (ULONG_PTR)base;
#else
    return 0;
#endif
}

#define RTTI_PTR(base, field) ((const void *)((base) + (ULONG_PTR)(field)))

/* the complete object from a pointer to a subobject, its locator's offsets undone. */
static const char *complete_object(const void *object, const RTTI_LOCATOR *locator)
{
    const char *p = static_cast<const char *>(object) - locator->offset;
    if (locator->cd_offset)
        p += *(const int *)(static_cast<const char *>(object) - locator->cd_offset);
    return p;
}

extern "C" void *__cdecl __RTCastToVoid(void *object)
{
    if (!object)
        return nullptr;
    const RTTI_LOCATOR *locator = locator_of(object);
    return const_cast<char *>(complete_object(object, locator));
}

extern "C" const type_info *__cdecl __RTtypeid(void *object)
{
    if (!object)
        throw bad_typeid("Attempted a typeid of NULL pointer!");
    const RTTI_LOCATOR *locator = locator_of(object);
    if (!locator)
        throw __non_rtti_object("Bad read pointer - no RTTI data!");
    return static_cast<const type_info *>(RTTI_PTR(locator_base(locator), locator->type));
}

/* the offset of a base class subobject within the object at p. */
static ptrdiff_t base_offset(const char *p, const RTTI_PMD &pmd)
{
    ptrdiff_t offset = pmd.mdisp;
    if (pmd.pdisp >= 0) {
        const char *vbtable = *(const char *const *)(p + pmd.pdisp);
        offset += *(const int *)(vbtable + pmd.vdisp) + pmd.pdisp;
    }
    return offset;
}

static const RTTI_BASE *base_at(ULONG_PTR base, const RTTI_HIERARCHY *hierarchy, unsigned i)
{
    const int *array = static_cast<const int *>(RTTI_PTR(base, hierarchy->bases));
#if defined(_WIN64)
    return static_cast<const RTTI_BASE *>(RTTI_PTR(base, array[i]));
#else
    return reinterpret_cast<const RTTI_BASE *const *>(array)[i];
#endif
}

static bool same_type(ULONG_PTR base, int a, const type_info *b)
{
    const type_info *t = static_cast<const type_info *>(RTTI_PTR(base, a));
    return t == b || !strcmp(t->raw_name(), b->raw_name());
}

/*
 * dynamic_cast: the complete object, then, in its class hierarchy, the
 * target as a public base found once (or the subobject the source is
 * part of, for a cast across), its offset from the complete object.
 */
extern "C" void *__cdecl __RTDynamicCast(void *object, LONG vfdelta, const type_info *source, const type_info *target,
                                         int is_reference)
{
    if (!object)
        return nullptr;
    const char *vfobject = static_cast<const char *>(object) + vfdelta;
    const RTTI_LOCATOR *locator = locator_of(vfobject);
    ULONG_PTR base = locator_base(locator);
    const char *complete = complete_object(vfobject, locator);
    const RTTI_HIERARCHY *hierarchy = static_cast<const RTTI_HIERARCHY *>(RTTI_PTR(base, locator->hierarchy));
    void *result = nullptr;
    unsigned found = 0;
    for (unsigned i = 0; i < hierarchy->count; i++) {
        const RTTI_BASE *b = base_at(base, hierarchy, i);
        if (!same_type(base, b->type, target))
            continue;
        /* bases that are not public or are ambiguous are no answer. */
        if (b->attributes & (RTTI_BASE_NOT_VISIBLE | RTTI_BASE_AMBIGUOUS))
            continue;
        result = const_cast<char *>(complete + base_offset(complete, b->where));
        found++;
        if (!(hierarchy->attributes & RTTI_HIERARCHY_MULTIPLE))
            break;
    }
    if (found != 1)
        result = nullptr;
    if (!result && is_reference)
        throw bad_cast::create("Bad dynamic_cast!");
    return result;
}

/* ------------------------------------------------------------------ vcruntime's helpers */

/* std::exception of current compilers keeps its message in this, copied or freed through these. */
struct __std_exception_data {
    const char *what;
    bool do_free;
};

extern "C" void __cdecl __std_exception_copy(const __std_exception_data *from, __std_exception_data *to)
{
    to->what = from->what;
    to->do_free = false;
    if (from->do_free && from->what) {
        size_t n = strlen(from->what) + 1;
        char *copy = static_cast<char *>(malloc(n));
        if (copy) {
            memcpy(copy, from->what, n);
            to->what = copy;
            to->do_free = true;
        }
    }
}

extern "C" void __cdecl __std_exception_destroy(__std_exception_data *data)
{
    if (data->do_free)
        free(const_cast<char *>(data->what));
    data->what = nullptr;
    data->do_free = false;
}

/* std::type_info of current compilers, after its vtable: the undecorated name, made when asked, and the decorated one. */
struct __std_type_info_data {
    const char *undecorated;
    const char decorated[1];
};

extern "C" int __cdecl __std_type_info_compare(const __std_type_info_data *a, const __std_type_info_data *b)
{
    return a == b ? 0 : strcmp(a->decorated + 1, b->decorated + 1);
}

extern "C" size_t __cdecl __std_type_info_hash(const __std_type_info_data *data)
{
#if defined(_WIN64)
    size_t hash = 14695981039346656037ull, prime = 1099511628211ull;
#else
    size_t hash = 2166136261u, prime = 16777619u;
#endif
    for (const char *p = data->decorated + 1; *p; p++)
        hash = (hash ^ (unsigned char)*p) * prime;
    return hash;
}

extern "C" const char *__cdecl __std_type_info_name(__std_type_info_data *data, __type_info_node *root)
{
    if (!data->undecorated) {
        char *undecorated = MsvcrtUndecorateType(data->decorated);
        if (undecorated && InterlockedCompareExchangePointer((void **)&data->undecorated, undecorated, nullptr))
            free(undecorated);
    }
    return data->undecorated ? data->undecorated : data->decorated;
}

/* the names are kept in the descriptors themselves: nothing to free. */
extern "C" void __cdecl __std_type_info_destroy_list(__type_info_node *root) {}
