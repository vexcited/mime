/*
 * the wire forms of the types an interface passes that are not plain data: handles, global memory, bitmaps and pointers
 * to interfaces. A stub holds a routine for each of them to size, write, read and free it, as a type with the
 * user_marshal attribute asks, and these are the routines every standard proxy file of windows names. Each has a 64 bit
 * form for the second transfer syntax, which the same routines of the first one differ from only in alignment and in the
 * width of the values that stand for pointers.
 *
 * The handles do not travel: the value is sent and means something to a peer on the same machine only, as windows has it.
 */
#include "orpc.h"
#include <mime/win32/rpcndr.h>
#include <mime/win32/wingdi.h>

#define WDT_INPROC_CALL 0x48746457 /* "WdtH" */
#define WDT_REMOTE_CALL 0x52746457 /* "WdtR" */
#define WDT_INPROC64_CALL 0x50746457 /* "WdtP" */

#define RPC_BAD_STUB_DATA 1783
#define RPC_WRONG_STUB_VERSION 1733
#define RPC_NULL_REF 1780
#define ARITH_OVERFLOW ((RPC_STATUS)0x80010010)

/* the context of a call is the low byte of the flags a routine gets. */
static BYTE context_of(const ULONG *flags)
{
    return (BYTE)*flags;
}

static USER_MARSHAL_CB *callback_of(ULONG *flags)
{
    return (USER_MARSHAL_CB *)flags;
}

static ULONG add_size(ULONG a, ULONG b)
{
    ULONG sum = a + b;
    if (sum < a)
        RpcRaiseException(ARITH_OVERFLOW);
    return sum;
}

static ULONG align_size(ULONG offset, ULONG mask)
{
    ULONG aligned = (offset + mask) & ~mask;
    if (aligned < offset)
        RpcRaiseException(ARITH_OVERFLOW);
    return aligned;
}

/* the gap that alignment leaves is written as zeros, as the buffer may hold anything. */
static BYTE *align_zero(BYTE *p, ULONG mask)
{
    BYTE *aligned = (BYTE *)(((ULONG_PTR)p + mask) & ~(ULONG_PTR)mask);
    for (; p < aligned; p++)
        *p = 0;
    return aligned;
}

/* the buffer the call has and how far a routine may read: from the start of the message to its end. */
typedef struct {
    const BYTE *Start;
    const BYTE *End;
} READER;

static void reader_open(READER *r, ULONG *flags, const BYTE *position)
{
    NDR_USER_MARSHAL_INFO info;
    memset(&info, 0, sizeof(info));
    RPC_STATUS status = NdrGetUserMarshalInfo(flags, 1, &info);
    if (status || !info.Level1.BufferSize) {
        /* a call outside a stub has no buffer to be held to. */
        r->Start = nullptr;
        r->End = nullptr;
        return;
    }
    r->Start = info.Level1.Buffer;
    r->End = (const BYTE *)info.Level1.Buffer + info.Level1.BufferSize;
    if (position < r->Start || position > r->End)
        RpcRaiseException(1784);
}

static SIZE_T reader_left(const READER *r, const BYTE *position)
{
    if (!r->End)
        return (SIZE_T)0x7FFFFFFF;
    return position <= r->End ? (SIZE_T)(r->End - position) : 0;
}

static const BYTE *reader_need(const READER *r, const BYTE *position, SIZE_T bytes)
{
    if (reader_left(r, position) < bytes)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    return position;
}

static const BYTE *align_read(const BYTE *p, ULONG mask)
{
    return (const BYTE *)(((ULONG_PTR)p + mask) & ~(ULONG_PTR)mask);
}

/* ------------------------------------------------------------------ pointers to interfaces */

/*
 * A stream over the part of the message an interface goes to or comes from. The reference to the object is written by
 * CoMarshalInterface into the buffer itself, so the size that was asked for with CoGetMarshalSizeMax is the room.
 */
typedef struct {
    IStream Stream;
    LONG Refs;
    BYTE *Data;
    ULONG Size;
    ULONG Position;
} BUFFERSTREAM;

static HRESULT WINAPI bs_query(IStream *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_ISequentialStream) || IsEqualGUID(iid, &IID_IStream)) {
        *out = self;
        InterlockedIncrement(&((BUFFERSTREAM *)self)->Refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI bs_addref(IStream *self)
{
    return InterlockedIncrement(&((BUFFERSTREAM *)self)->Refs);
}

static ULONG WINAPI bs_release(IStream *self)
{
    BUFFERSTREAM *s = (BUFFERSTREAM *)self;
    LONG refs = InterlockedDecrement(&s->Refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, s);
    return refs;
}

static HRESULT WINAPI bs_read(IStream *self, void *buffer, ULONG bytes, ULONG *read)
{
    BUFFERSTREAM *s = (BUFFERSTREAM *)self;
    ULONG n = s->Size - s->Position < bytes ? s->Size - s->Position : bytes;
    memcpy(buffer, s->Data + s->Position, n);
    s->Position += n;
    if (read)
        *read = n;
    return n == bytes ? S_OK : S_FALSE;
}

static HRESULT WINAPI bs_write(IStream *self, const void *buffer, ULONG bytes, ULONG *written)
{
    BUFFERSTREAM *s = (BUFFERSTREAM *)self;
    if (s->Size - s->Position < bytes) {
        if (written)
            *written = 0;
        return STG_E_MEDIUMFULL;
    }
    memcpy(s->Data + s->Position, buffer, bytes);
    s->Position += bytes;
    if (written)
        *written = bytes;
    return S_OK;
}

static HRESULT WINAPI bs_seek(IStream *self, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    BUFFERSTREAM *s = (BUFFERSTREAM *)self;
    LONGLONG base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? s->Position : s->Size;
    LONGLONG target = base + move.QuadPart;
    if (target < 0 || target > s->Size)
        return STG_E_INVALIDFUNCTION;
    s->Position = (ULONG)target;
    if (position)
        position->QuadPart = s->Position;
    return S_OK;
}

static HRESULT WINAPI bs_set_size(IStream *self, ULARGE_INTEGER size)
{
    return size.QuadPart <= ((BUFFERSTREAM *)self)->Size ? S_OK : STG_E_MEDIUMFULL;
}

static HRESULT WINAPI bs_copy_to(IStream *self, IStream *to, ULARGE_INTEGER count, ULARGE_INTEGER *read,
                                 ULARGE_INTEGER *written)
{
    BUFFERSTREAM *s = (BUFFERSTREAM *)self;
    ULONG n = s->Size - s->Position;
    if (count.QuadPart < n)
        n = (ULONG)count.QuadPart;
    ULONG done = 0;
    HRESULT hr = to->lpVtbl->Write(to, s->Data + s->Position, n, &done);
    s->Position += done;
    if (read)
        read->QuadPart = done;
    if (written)
        written->QuadPart = done;
    return hr;
}

static HRESULT WINAPI bs_commit(IStream *self, DWORD flags)
{
    return S_OK;
}

static HRESULT WINAPI bs_revert(IStream *self)
{
    return S_OK;
}

static HRESULT WINAPI bs_lock(IStream *self, ULARGE_INTEGER offset, ULARGE_INTEGER count, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI bs_stat(IStream *self, STATSTG *stat, DWORD flags)
{
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    stat->type = STGTY_STREAM;
    stat->cbSize.QuadPart = ((BUFFERSTREAM *)self)->Size;
    return S_OK;
}

static HRESULT WINAPI bs_clone(IStream *self, IStream **clone)
{
    return E_NOTIMPL;
}

static const IStreamVtbl g_bufferstream_vtbl = {
    bs_query, bs_addref, bs_release, bs_read, bs_write, bs_seek, bs_set_size, bs_copy_to, bs_commit, bs_revert, bs_lock,
    bs_lock, bs_stat, bs_clone};

static BUFFERSTREAM *bufferstream_new(BYTE *data, ULONG size)
{
    BUFFERSTREAM *s = HeapAlloc(GetProcessHeap(), 0, sizeof(*s));
    if (!s)
        RpcRaiseException(E_OUTOFMEMORY);
    s->Stream.lpVtbl = &g_bufferstream_vtbl;
    s->Refs = 1;
    s->Data = data;
    s->Size = size;
    s->Position = 0;
    return s;
}

/* The wire form of a pointer to an interface: the count of bytes twice (the first of a conformant array, the second of
   its variance), then the bytes of the object reference. The 64 bit form has a count of 8 bytes before the second. */
static ULONG interface_size(ULONG *flags, ULONG context, ULONG start, IUnknown *punk, REFIID iid, BOOL w64)
{
    if (!punk)
        return start;
    ULONG pos = align_size(start, w64 ? 7 : 3);
    pos = add_size(pos, w64 ? 12 : 8);
    ULONG size = 0;
    HRESULT hr = CoGetMarshalSizeMax(&size, iid, punk, (BYTE)context, callback_of(flags)->pStubMsg->pvDestContext,
                                     MSHLFLAGS_NORMAL);
    if (FAILED(hr))
        RpcRaiseException((RPC_STATUS)hr);
    return add_size(pos, size);
}

static BYTE *interface_marshal(ULONG *flags, ULONG context, BYTE *buffer, IUnknown *punk, REFIID iid, BOOL w64)
{
    if (!punk)
        return buffer;
    READER r;
    reader_open(&r, flags, buffer);
    BYTE *head = align_zero(buffer, w64 ? 7 : 3);
    BYTE *data = head + (w64 ? 12 : 8);
    ULONG room = r.End ? (ULONG)(r.End > data ? r.End - data : 0) : 0x7FFFFFFF;
    BUFFERSTREAM *s = bufferstream_new(data, room);
    HRESULT hr = CoMarshalInterface(&s->Stream, iid, punk, (BYTE)context, callback_of(flags)->pStubMsg->pvDestContext,
                                    MSHLFLAGS_NORMAL);
    ULONG used = s->Position;
    s->Stream.lpVtbl->Release(&s->Stream);
    if (FAILED(hr))
        RpcRaiseException((RPC_STATUS)hr);
    if (w64) {
        *(ULONG *)head = used;
        *(ULONG *)(head + 4) = 0;
        *(ULONG *)(head + 8) = used;
    } else {
        *(ULONG *)head = used;
        *(ULONG *)(head + 4) = used;
    }
    return data + used;
}

/* The object reference is checked before it is handed on: the signature, a known kind of reference and the interface asked
   for. A peer that sends anything else is not following the form. */
static BYTE *interface_unmarshal(ULONG *flags, BYTE *buffer, IUnknown **pp, REFIID iid, BOOL w64)
{
    READER r;
    reader_open(&r, flags, buffer);
    const BYTE *head = align_read(buffer, w64 ? 7 : 3);
    ULONG header = w64 ? 12 : 8;
    reader_need(&r, head, header + 24);
    ULONG conformance = *(const ULONG *)head;
    ULONG size = *(const ULONG *)(head + (w64 ? 8 : 4));
    const BYTE *data = head + header;
    if (conformance != size || reader_left(&r, data) < size)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    if (*(const ULONG *)data != OBJREF_SIGNATURE)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    ULONG kind = *(const ULONG *)(data + 4);
    if ((kind & ~(ULONG)(0x1F)) != 0 || !kind)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    if (!IsEqualGUID((const GUID *)(data + 8), iid))
        RpcRaiseException(RPC_BAD_STUB_DATA);
    BUFFERSTREAM *s = bufferstream_new((BYTE *)data, size);
    IUnknown *object = nullptr;
    HRESULT hr = CoUnmarshalInterface(&s->Stream, iid, (void **)&object);
    ULONG used = s->Position;
    s->Stream.lpVtbl->Release(&s->Stream);
    if (FAILED(hr))
        RpcRaiseException((RPC_STATUS)hr);
    if (used != size) {
        object->lpVtbl->Release(object);
        RpcRaiseException(RPC_BAD_STUB_DATA);
    }
    if (*pp)
        (*pp)->lpVtbl->Release(*pp);
    *pp = object;
    return (BYTE *)data + size;
}

WINOLEAPI ULONG WINAPI WdtpInterfacePointer_UserSize(ULONG *flags, ULONG context, ULONG start, IUnknown *punk,
                                                     REFIID iid)
{
    return interface_size(flags, context, start, punk, iid, FALSE);
}

WINOLEAPI ULONG WINAPI WdtpInterfacePointer_UserSize64(ULONG *flags, ULONG context, ULONG start, IUnknown *punk,
                                                       REFIID iid)
{
    return interface_size(flags, context, start, punk, iid, TRUE);
}

WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserMarshal(ULONG *flags, ULONG context, BYTE *buffer, IUnknown *punk,
                                                        REFIID iid)
{
    return interface_marshal(flags, context, buffer, punk, iid, FALSE);
}

WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserMarshal64(ULONG *flags, ULONG context, BYTE *buffer, IUnknown *punk,
                                                          REFIID iid)
{
    return interface_marshal(flags, context, buffer, punk, iid, TRUE);
}

WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserUnmarshal(ULONG *flags, BYTE *buffer, IUnknown **pp, REFIID iid)
{
    return interface_unmarshal(flags, buffer, pp, iid, FALSE);
}

WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserUnmarshal64(ULONG *flags, BYTE *buffer, IUnknown **pp, REFIID iid)
{
    return interface_unmarshal(flags, buffer, pp, iid, TRUE);
}

WINOLEAPI void WINAPI WdtpInterfacePointer_UserFree(IUnknown *punk)
{
    if (punk)
        punk->lpVtbl->Release(punk);
}

WINOLEAPI void WINAPI WdtpInterfacePointer_UserFree64(IUnknown *punk)
{
    if (punk)
        punk->lpVtbl->Release(punk);
}

/* ------------------------------------------------------------------ handles of windows and objects */

/* A handle of the kinds that belong to the machine, sent as its value and the tag that says it is one. */
static ULONG handle_size(const ULONG *flags, ULONG start, const void *handle)
{
    if (!handle)
        return start;
    if (context_of(flags) == MSHCTX_DIFFERENTMACHINE)
        RpcRaiseException(RPC_WRONG_STUB_VERSION);
    return add_size(align_size(start, 3), 8);
}

static BYTE *handle_marshal(const ULONG *flags, BYTE *buffer, const HANDLE *handle)
{
    if (!handle)
        return buffer;
    if (context_of(flags) == MSHCTX_DIFFERENTMACHINE)
        RpcRaiseException(RPC_WRONG_STUB_VERSION);
    BYTE *p = align_zero(buffer, 3);
    *(ULONG *)p = WDT_INPROC_CALL;
    *(LONG *)(p + 4) = (LONG)(LONG_PTR)*handle;
    return p + 8;
}

static BYTE *handle_unmarshal(ULONG *flags, BYTE *buffer, HANDLE *handle)
{
    if (context_of(flags) == MSHCTX_DIFFERENTMACHINE)
        RpcRaiseException(RPC_WRONG_STUB_VERSION);
    READER r;
    reader_open(&r, flags, buffer);
    const BYTE *p = align_read(buffer, 3);
    reader_need(&r, p, 8);
    if (*(const ULONG *)p != WDT_INPROC_CALL)
        RpcRaiseException(RPC_WRONG_STUB_VERSION);
    *handle = (HANDLE)(LONG_PTR) * (const LONG *)(p + 4);
    return (BYTE *)p + 8;
}

#define HANDLE_ROUTINES(prefix, type)                                                                                  \
    WINOLEAPI ULONG WINAPI prefix##_UserSize(ULONG *flags, ULONG start, type *handle)                                 \
    {                                                                                                                  \
        return handle_size(flags, start, handle);                                                                      \
    }                                                                                                                  \
    WINOLEAPI BYTE *WINAPI prefix##_UserMarshal(ULONG *flags, BYTE *buffer, type *handle)                              \
    {                                                                                                                  \
        return handle_marshal(flags, buffer, (const HANDLE *)handle);                                                  \
    }                                                                                                                  \
    WINOLEAPI BYTE *WINAPI prefix##_UserUnmarshal(ULONG *flags, BYTE *buffer, type *handle)                            \
    {                                                                                                                  \
        return handle_unmarshal(flags, buffer, (HANDLE *)handle);                                                      \
    }                                                                                                                  \
    WINOLEAPI void WINAPI prefix##_UserFree(ULONG *flags, type *handle)                                                \
    {                                                                                                                  \
    }                                                                                                                  \
    WINOLEAPI ULONG WINAPI prefix##_UserSize64(ULONG *flags, ULONG start, type *handle)                               \
    {                                                                                                                  \
        return handle_size(flags, start, handle);                                                                      \
    }                                                                                                                  \
    WINOLEAPI BYTE *WINAPI prefix##_UserMarshal64(ULONG *flags, BYTE *buffer, type *handle)                            \
    {                                                                                                                  \
        return handle_marshal(flags, buffer, (const HANDLE *)handle);                                                  \
    }                                                                                                                  \
    WINOLEAPI BYTE *WINAPI prefix##_UserUnmarshal64(ULONG *flags, BYTE *buffer, type *handle)                          \
    {                                                                                                                  \
        return handle_unmarshal(flags, buffer, (HANDLE *)handle);                                                      \
    }                                                                                                                  \
    WINOLEAPI void WINAPI prefix##_UserFree64(ULONG *flags, type *handle)                                              \
    {                                                                                                                  \
    }

HANDLE_ROUTINES(HWND, HWND)
HANDLE_ROUTINES(HICON, HICON)
HANDLE_ROUTINES(HMENU, HMENU)
HANDLE_ROUTINES(HACCEL, HANDLE)
HANDLE_ROUTINES(HBRUSH, HBRUSH)
HANDLE_ROUTINES(HDC, HDC)
HANDLE_ROUTINES(HMONITOR, HMONITOR)

/* ------------------------------------------------------------------ global memory */

/*
 * Memory in a call is sent whole when the peer is another process: a tag, the value of the handle, and for a handle that
 * is not null the size twice with the value again and the bytes. A peer in the process takes the handle as it is.
 */
static ULONG global_size(const ULONG *flags, ULONG start, const HGLOBAL *global, BOOL w64)
{
    if (!global)
        return start;
    BOOL inproc = context_of(flags) == MSHCTX_INPROC;
    ULONG pos = add_size(align_size(start, w64 ? 7 : 3), 4);
    if (inproc || w64)
        pos = align_size(pos, 7);
    pos = add_size(pos, inproc || w64 ? 8 : 4);
    if (inproc || !*global)
        return pos;
    SIZE_T bytes = GlobalSize(*global);
    if (bytes > 0xFFFFFFFF)
        RpcRaiseException(ARITH_OVERFLOW);
    return add_size(add_size(pos, w64 ? 16 : 12), (ULONG)bytes);
}

static BYTE *global_marshal(const ULONG *flags, BYTE *buffer, const HGLOBAL *global, BOOL w64)
{
    if (!global)
        return buffer;
    BYTE *p = align_zero(buffer, w64 ? 7 : 3);
    if (context_of(flags) == MSHCTX_INPROC) {
        *(ULONG *)p = WDT_INPROC64_CALL;
        p = align_zero(p + 4, 7);
        *(ULONGLONG *)p = (ULONGLONG)(ULONG_PTR)*global;
        return p + 8;
    }
    *(ULONG *)p = WDT_REMOTE_CALL;
    p += 4;
    if (w64) {
        p = align_zero(p, 7);
        *(ULONGLONG *)p = (ULONGLONG)(ULONG_PTR)*global;
        p += 8;
    } else {
        *(LONG *)p = (LONG)(LONG_PTR)*global;
        p += 4;
    }
    if (!*global)
        return p;
    ULONG bytes = (ULONG)GlobalSize(*global);
    if (w64) {
        *(ULONGLONG *)p = bytes;
        *(ULONG *)(p + 8) = (ULONG)(ULONG_PTR)*global;
        *(ULONG *)(p + 12) = bytes;
        p += 16;
    } else {
        *(ULONG *)p = bytes;
        *(ULONG *)(p + 4) = (ULONG)(ULONG_PTR)*global;
        *(ULONG *)(p + 8) = bytes;
        p += 12;
    }
    if (bytes) {
        const void *data = GlobalLock(*global);
        memcpy(p, data, bytes);
        GlobalUnlock(*global);
    }
    return p + bytes;
}

static BYTE *global_unmarshal(ULONG *flags, BYTE *buffer, HGLOBAL *global, BOOL w64)
{
    READER r;
    reader_open(&r, flags, buffer);
    const BYTE *p = align_read(buffer, w64 ? 7 : 3);
    reader_need(&r, p, 8);
    ULONG tag = *(const ULONG *)p;
    p += 4;
    BOOL inproc = context_of(flags) == MSHCTX_INPROC;
    if (tag != WDT_REMOTE_CALL) {
        if (!inproc)
            RpcRaiseException(RPC_BAD_STUB_DATA);
        HGLOBAL value;
        if (tag == WDT_INPROC_CALL) {
            reader_need(&r, p, 4);
            value = (HGLOBAL)(LONG_PTR) * (const LONG *)p;
            p += 4;
        } else if (tag == WDT_INPROC64_CALL) {
            p = align_read(p, 7);
            reader_need(&r, p, 8);
            value = (HGLOBAL)(ULONG_PTR) * (const ULONGLONG *)p;
            p += 8;
        } else {
            RpcRaiseException(RPC_WRONG_STUB_VERSION);
        }
        if (*global && *global != value)
            GlobalFree(*global);
        *global = value;
        return (BYTE *)p;
    }
    if (inproc)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    ULONGLONG handle;
    if (w64) {
        p = align_read(p, 7);
        reader_need(&r, p, 8);
        handle = *(const ULONGLONG *)p;
        p += 8;
    } else {
        reader_need(&r, p, 4);
        handle = (ULONGLONG)(LONGLONG) * (const LONG *)p;
        p += 4;
    }
    if (!handle) {
        if (*global)
            GlobalFree(*global);
        *global = nullptr;
        return (BYTE *)p;
    }
    ULONG head = w64 ? 16 : 12;
    reader_need(&r, p, head);
    ULONG size = *(const ULONG *)p;
    ULONG again = *(const ULONG *)(p + (w64 ? 12 : 8));
    ULONG value = *(const ULONG *)(p + (w64 ? 8 : 4));
    if (value != (ULONG)handle || size != again)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    p += head;
    if (reader_left(&r, p) < size)
        RpcRaiseException(RPC_BAD_STUB_DATA);
    HGLOBAL memory = *global;
    if (memory && GlobalSize(memory) != size) {
        GlobalFree(memory);
        memory = nullptr;
    }
    if (!memory)
        memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        RpcRaiseException(E_OUTOFMEMORY);
    void *data = GlobalLock(memory);
    memcpy(data, p, size);
    GlobalUnlock(memory);
    *global = memory;
    return (BYTE *)p + size;
}

WINOLEAPI ULONG WINAPI HGLOBAL_UserSize(ULONG *flags, ULONG start, HGLOBAL *global)
{
    return global_size(flags, start, global, FALSE);
}

WINOLEAPI BYTE *WINAPI HGLOBAL_UserMarshal(ULONG *flags, BYTE *buffer, HGLOBAL *global)
{
    return global_marshal(flags, buffer, global, FALSE);
}

WINOLEAPI BYTE *WINAPI HGLOBAL_UserUnmarshal(ULONG *flags, BYTE *buffer, HGLOBAL *global)
{
    return global_unmarshal(flags, buffer, global, FALSE);
}

WINOLEAPI void WINAPI HGLOBAL_UserFree(ULONG *flags, HGLOBAL *global)
{
    if (global && *global && context_of(flags) != MSHCTX_INPROC)
        GlobalFree(*global);
}

WINOLEAPI ULONG WINAPI HGLOBAL_UserSize64(ULONG *flags, ULONG start, HGLOBAL *global)
{
    return global_size(flags, start, global, TRUE);
}

WINOLEAPI BYTE *WINAPI HGLOBAL_UserMarshal64(ULONG *flags, BYTE *buffer, HGLOBAL *global)
{
    return global_marshal(flags, buffer, global, TRUE);
}

WINOLEAPI BYTE *WINAPI HGLOBAL_UserUnmarshal64(ULONG *flags, BYTE *buffer, HGLOBAL *global)
{
    return global_unmarshal(flags, buffer, global, TRUE);
}

WINOLEAPI void WINAPI HGLOBAL_UserFree64(ULONG *flags, HGLOBAL *global)
{
    HGLOBAL_UserFree(flags, global);
}

/* ------------------------------------------------------------------ bitmaps */

/*
 * A bitmap of the device is sent as its bits: a tag, the value of the handle, then for a handle that is not null the
 * size of the bits, the structure that describes them and the bits. The device independent bitmaps are sent as these
 * too, which is what the format has, so a section's pixels arrive but not its sharing.
 */
static ULONG bitmap_bytes(HBITMAP bitmap, BITMAP *bm)
{
    memset(bm, 0, sizeof(*bm));
    if (!GetObjectW(bitmap, sizeof(*bm), bm)) {
        DWORD error = GetLastError();
        RpcRaiseException(error > 0 ? (RPC_STATUS)((error & 0xFFFF) | 0x80070000) : (RPC_STATUS)error);
    }
    ULONGLONG rows = (ULONGLONG)bm->bmHeight * bm->bmPlanes;
    ULONGLONG total = rows * (ULONGLONG)bm->bmWidthBytes;
    if (rows > 0xFFFFFFFF || total > 0xFFFFFFFF)
        RpcRaiseException(ARITH_OVERFLOW);
    return (ULONG)total;
}

static ULONG bitmap_size(const ULONG *flags, ULONG start, const HBITMAP *bitmap, BOOL w64)
{
    if (!bitmap)
        return start;
    ULONG pos = add_size(align_size(start, w64 ? 7 : 3), w64 ? 16 : 8);
    if (!*bitmap || context_of(flags) == MSHCTX_INPROC)
        return pos;
    BITMAP bm;
    ULONG bits = bitmap_bytes(*bitmap, &bm);
    return add_size(add_size(pos, w64 ? 32 : 24), bits);
}

static BYTE *bitmap_marshal(const ULONG *flags, BYTE *buffer, const HBITMAP *bitmap, BOOL w64)
{
    if (!bitmap)
        return buffer;
    BYTE *p = align_zero(buffer, w64 ? 7 : 3);
    BOOL inproc = context_of(flags) == MSHCTX_INPROC;
    *(ULONG *)p = inproc ? (w64 ? WDT_INPROC64_CALL : WDT_INPROC_CALL) : WDT_REMOTE_CALL;
    p += 4;
    if (w64) {
        p = align_zero(p, 7);
        *(ULONGLONG *)p = (ULONGLONG)(ULONG_PTR)*bitmap;
        p += 8;
    } else {
        *(LONG *)p = (LONG)(LONG_PTR)*bitmap;
        p += 4;
    }
    if (!*bitmap || inproc)
        return p;
    BITMAP bm;
    ULONG bits = bitmap_bytes(*bitmap, &bm);
    if (w64) {
        *(ULONGLONG *)p = bits;
        memcpy(p + 8, &bm.bmType, 16);
        *(ULONG *)(p + 24) = *(const ULONG *)&bm.bmPlanes;
        *(ULONG *)(p + 28) = bits;
        p += 32;
    } else {
        *(ULONG *)p = bits;
        memcpy(p + 4, &bm.bmType, 16);
        *(ULONG *)(p + 20) = *(const ULONG *)&bm.bmPlanes;
        p += 24;
    }
    if (!GetBitmapBits(*bitmap, bits, p) && bits) {
        DWORD error = GetLastError();
        RpcRaiseException(error > 0 ? (RPC_STATUS)((error & 0xFFFF) | 0x80070000) : (RPC_STATUS)error);
    }
    return p + bits;
}

static BYTE *bitmap_unmarshal(ULONG *flags, BYTE *buffer, HBITMAP *bitmap, BOOL w64)
{
    READER r;
    reader_open(&r, flags, buffer);
    const BYTE *p = align_read(buffer, w64 ? 7 : 3);
    reader_need(&r, p, 8);
    ULONG tag = *(const ULONG *)p;
    p += 4;
    HBITMAP result;
    if (w64) {
        p = align_read(p, 7);
        reader_need(&r, p, 8);
        result = (HBITMAP)(ULONG_PTR) * (const ULONGLONG *)p;
        p += 8;
    } else {
        reader_need(&r, p, 4);
        result = (HBITMAP)(LONG_PTR) * (const LONG *)p;
        p += 4;
    }
    BOOL inproc = context_of(flags) == MSHCTX_INPROC;
    if (tag == WDT_REMOTE_CALL) {
        if (inproc)
            RpcRaiseException(RPC_BAD_STUB_DATA);
        if (result) {
            ULONG head = w64 ? 32 : 24;
            reader_need(&r, p, head);
            ULONG size = *(const ULONG *)p;
            const BYTE *bm = p + (w64 ? 8 : 4);
            LONG width = *(const LONG *)(bm + 4), height = *(const LONG *)(bm + 8), width_bytes = *(const LONG *)(bm + 12);
            UINT planes = *(const USHORT *)(bm + 16), bits_pixel = *(const USHORT *)(bm + 18);
            ULONGLONG rows = (ULONGLONG)height * planes;
            if (width < 0 || height < 0 || width_bytes < 0 || rows > 0xFFFFFFFF ||
                rows * (ULONGLONG)width_bytes != size)
                RpcRaiseException(RPC_BAD_STUB_DATA);
            /* the rows of a bitmap of the device are padded to words. */
            ULONGLONG row_bits = (ULONGLONG)width * planes * bits_pixel;
            if (row_bits > 0xFFFFFFFF || (ULONG)(2 * ((row_bits + 15) >> 4)) != (ULONG)width_bytes)
                RpcRaiseException(RPC_BAD_STUB_DATA);
            p += head;
            if (reader_left(&r, p) < size)
                RpcRaiseException(RPC_BAD_STUB_DATA);
            result = CreateBitmap(width, height, planes, bits_pixel, p);
            if (!result) {
                DWORD error = GetLastError();
                RpcRaiseException(error > 0 ? (RPC_STATUS)((error & 0xFFFF) | 0x80070000) : (RPC_STATUS)error);
            }
            p += size;
        }
    } else if (tag == WDT_INPROC_CALL || tag == WDT_INPROC64_CALL) {
        if (!inproc)
            RpcRaiseException(RPC_BAD_STUB_DATA);
    } else {
        RpcRaiseException(RPC_WRONG_STUB_VERSION);
    }
    if (*bitmap)
        DeleteObject(*bitmap);
    *bitmap = result;
    return (BYTE *)p;
}

WINOLEAPI ULONG WINAPI HBITMAP_UserSize(ULONG *flags, ULONG start, HBITMAP *bitmap)
{
    return bitmap_size(flags, start, bitmap, FALSE);
}

WINOLEAPI BYTE *WINAPI HBITMAP_UserMarshal(ULONG *flags, BYTE *buffer, HBITMAP *bitmap)
{
    return bitmap_marshal(flags, buffer, bitmap, FALSE);
}

WINOLEAPI BYTE *WINAPI HBITMAP_UserUnmarshal(ULONG *flags, BYTE *buffer, HBITMAP *bitmap)
{
    return bitmap_unmarshal(flags, buffer, bitmap, FALSE);
}

WINOLEAPI void WINAPI HBITMAP_UserFree(ULONG *flags, HBITMAP *bitmap)
{
    if (bitmap && *bitmap && context_of(flags) != MSHCTX_INPROC)
        DeleteObject(*bitmap);
}

WINOLEAPI ULONG WINAPI HBITMAP_UserSize64(ULONG *flags, ULONG start, HBITMAP *bitmap)
{
    return bitmap_size(flags, start, bitmap, TRUE);
}

WINOLEAPI BYTE *WINAPI HBITMAP_UserMarshal64(ULONG *flags, BYTE *buffer, HBITMAP *bitmap)
{
    return bitmap_marshal(flags, buffer, bitmap, TRUE);
}

WINOLEAPI BYTE *WINAPI HBITMAP_UserUnmarshal64(ULONG *flags, BYTE *buffer, HBITMAP *bitmap)
{
    return bitmap_unmarshal(flags, buffer, bitmap, TRUE);
}

WINOLEAPI void WINAPI HBITMAP_UserFree64(ULONG *flags, HBITMAP *bitmap)
{
    HBITMAP_UserFree(flags, bitmap);
}
