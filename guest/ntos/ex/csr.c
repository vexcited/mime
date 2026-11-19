/*
 * the side of csrss a process of windows 11 26300 talks to while it starts, measured there with csrconn.c, csrdbg.c
 * and csrsd.c of scratch/dlls/probe in the research repository. mime has no csrss: the kernel names the section
 * and the port of each session and answers the port itself, on the thread of the caller.
 *
 * a process opens \Sessions\n\Windows\SharedSection for SECTION_MAP_READ, makes a reserved section of 64 KB, and
 * connects to \Sessions\n\Windows\ApiPort with that section as its view and 0x30 bytes of connection information.
 * the connection maps the view in the client, tells where the server sees it, and fills the first three fields of the
 * information: the base of the shared heap in the server, the array of static data there, and the process id of the
 * server. messages are 0x3b8 bytes at most. a second connection of the same process is refused.
 *
 * a program that loads kernel32 alone sends nothing more before its entry point. user32 sends one request, the
 * connection to server dll 3 with 0x248 bytes in the view, which win32k answers: see guest/win32k/usershared.c.
 */
#include "../ntos.h"

#define STATUS_PORT_CONNECTION_REFUSED ((NTSTATUS)0xC0000041L)
#define STATUS_PORT_MESSAGE_TOO_LONG ((NTSTATUS)0xC000002FL)
#define PORT_ALL_ACCESS 0x1F0001
#define CSR_MESSAGE_MAXIMUM 0x3B8
#define CSR_VIEW_SIZE 0x10000
#define LPC_REPLY 2

typedef struct CSR_PORT_VIEW {
    ULONG Length;
    HANDLE SectionHandle;
    ULONG SectionOffset;
    SIZE_T ViewSize;
    PVOID ViewBase, ViewRemoteBase;
} CSR_PORT_VIEW;

typedef struct CSR_CONNECT_INFO {
    ULONG64 SharedBase, StaticData, ServerProcess, Reserved;
    ULONG ServerDll; /* 1 when the 8 bytes that follow the status are for the base server. */
    NTSTATUS Status;
    ULONG64 Data;
} CSR_CONNECT_INFO;

typedef struct CSR_MESSAGE {
    USHORT DataLength, TotalLength;
    USHORT Type, DataInfoOffset;
    ULONG64 Process, Thread;
    ULONG MessageId, Pad;
    ULONG64 CallbackId;
    ULONG64 CaptureBuffer;
    ULONG Api;
    NTSTATUS Status;
    ULONG64 Arguments[(CSR_MESSAGE_MAXIMUM - 0x38) / 8];
} CSR_MESSAGE;
MIME_STATIC_ASSERT(sizeof(CSR_MESSAGE) == CSR_MESSAGE_MAXIMUM, "csr message");

typedef struct CSR_PORT {
    ULONG Kind; /* ALPC_KIND_CSR of alpc.c, whose port type this is. */
    BOOLEAN Client; /* the end a process holds, and not the name it connects to. */
    ULONG Session;
    ULONG64 ViewBase, RemoteBase;
} CSR_PORT;

#define ALPC_KIND_CSR 1

extern POBJECT_TYPE ExAlpcPortType;
void ExInitializeAlpc(void);

static POBJECT_TYPE g_port_type;
static ULONG64 g_remote_next;
static volatile LONG g_sequence;
static HANDLE g_server_thread;
/* the server dlls drivers stand for, by their number: win32k is 3, as winsrv is on windows. */
static CSR_SERVER_CONNECT g_server_dll[4];

void NTAPI ExRegisterCsrServerDll(ULONG index, CSR_SERVER_CONNECT connect)
{
    if (index < RTL_NUMBER_OF(g_server_dll))
        g_server_dll[index] = connect;
}

/* the descriptor of the shared section of both sessions: the administrators own it, and everyone, the restricted and
 * the two groups of packages may query it, map it to read or to execute and read the descriptor. */
static const UCHAR g_section_sd[] = {
    0x01, 0x00, 0x04, 0x80, 0x14, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00,
    0x20, 0x02, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x60, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x0d, 0x00, 0x02, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
    0x0d, 0x00, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0x0d, 0x00, 0x02, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f,
    0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x0d, 0x00, 0x02, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
};

static NTSTATUS insert(PVOID object, PCWSTR directory, PCWSTR leaf, ACCESS_MASK access)
{
    WCHAR path[64];
    _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", directory, leaf);
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_PERMANENT | OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE handle;
    return ObInsertObjectNamed(object, &oa, access, KernelMode, &handle);
}

static void session(ULONG id, PCWSTR directory)
{
    PVOID section;
    ULONG64 server, static_data;
    NTSTATUS status = PspCsrSharedSection(id, &section, &server, &static_data);
    if (NT_SUCCESS(status)) {
        ObReferenceObject(section);
        status = insert(section, directory, L"SharedSection", SECTION_ALL_ACCESS);
    }
    SD_BLOB *sd;
    if (NT_SUCCESS(status) && NT_SUCCESS(SeSdFromBytes(g_section_sd, sizeof(g_section_sd), &sd))) {
        ObStoreSecurity(section, sd);
        SeSdDereference(sd);
    }
    CSR_PORT *port = nullptr;
    if (NT_SUCCESS(status))
        status = ObCreateObject(g_port_type, nullptr, KernelMode, sizeof(*port), (PVOID *)&port);
    if (NT_SUCCESS(status)) {
        memset(port, 0, sizeof(*port));
        port->Kind = ALPC_KIND_CSR;
        port->Session = id;
        status = insert(port, directory, L"ApiPort", PORT_ALL_ACCESS);
    }
    if (!NT_SUCCESS(status))
        KiLogPrintf("csr: session %u has no api port: %08x", id, status);
}

void ExInitializeCsr(void)
{
    ExInitializeAlpc();
    g_port_type = ExAlpcPortType;
    /* the server has no address space here, so the addresses it tells of its views are only numbers. */
    g_remote_next = 0x20000000000ull + ((MiRandom() & 0xffffff) << 16);
    g_server_thread = PsCurrentThread()->Cid.UniqueThread;
    session(0, L"\\Windows");
    session(1, L"\\Sessions\\1\\Windows");
}

NTSTATUS NTAPI NtConnectPort(PHANDLE handle, PUNICODE_STRING name, PVOID qos, CSR_PORT_VIEW *client_view, PVOID server_view,
                             PULONG maximum, PVOID information, PULONG information_length)
{
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    PEPROCESS process = PsCurrentProcess();
    UNICODE_STRING n;
    NTSTATUS status = KiCaptureUnicodeString(&n, name);
    if (!NT_SUCCESS(status))
        return status;
    CSR_PORT *port;
    status = ObReferenceObjectByName(&n, OBJ_CASE_INSENSITIVE, nullptr, 0, g_port_type, KernelMode, nullptr, (PVOID *)&port);
    KiFreeCapturedString(&n);
    if (!NT_SUCCESS(status))
        return status;
    CSR_PORT_VIEW view = {};
    CSR_CONNECT_INFO info = {};
    ULONG length = 0;
    if (port->Kind != ALPC_KIND_CSR || port->Client)
        status = STATUS_OBJECT_TYPE_MISMATCH;
    else if (!client_view || !information || !information_length)
        status = STATUS_PORT_CONNECTION_REFUSED;
    else if (!NT_SUCCESS(KiCopyFromUser(&view, client_view, sizeof(view))) ||
             !NT_SUCCESS(KiCopyFromUser(&length, information_length, sizeof(length))))
        status = STATUS_ACCESS_VIOLATION;
    /* csrss was only seen with the information ntdll sends, and refuses the process it already serves. */
    else if (view.Length != sizeof(view) || length != sizeof(info) || process->CsrConnected)
        status = STATUS_PORT_CONNECTION_REFUSED;
    else if (!NT_SUCCESS(KiCopyFromUser(&info, information, sizeof(info))))
        status = STATUS_ACCESS_VIOLATION;
    PSECTION section = nullptr;
    if (NT_SUCCESS(status))
        status = ObReferenceHandleFor(view.SectionHandle, SECTION_MAP_READ | SECTION_MAP_WRITE, MmSectionType, mode, &section);
    PVOID base = nullptr;
    if (NT_SUCCESS(status)) {
        SIZE_T size = view.ViewSize;
        status = MiMapViewOfSection(section, process, &base, view.SectionOffset, &size, PAGE_READWRITE, 0);
        view.ViewSize = size;
        ObDereferenceObject(section);
    }
    CSR_PORT *end = nullptr;
    if (NT_SUCCESS(status))
        status = ObCreateObject(g_port_type, nullptr, KernelMode, sizeof(*end), (PVOID *)&end);
    if (NT_SUCCESS(status)) {
        ULONG64 server, static_data;
        PVOID shared;
        PspCsrSharedSection(port->Session, &shared, &server, &static_data);
        memset(end, 0, sizeof(*end));
        end->Kind = ALPC_KIND_CSR;
        end->Client = TRUE;
        end->Session = port->Session;
        end->ViewBase = (ULONG64)base;
        end->RemoteBase = __atomic_fetch_add(&g_remote_next, CSR_VIEW_SIZE, __ATOMIC_RELAXED);
        view.ViewBase = base;
        view.ViewRemoteBase = (PVOID)end->RemoteBase;
        info.SharedBase = server;
        info.StaticData = static_data;
        info.ServerProcess = (ULONG64)PsInitialSystemProcess->UniqueProcessId;
        ULONG longest = CSR_MESSAGE_MAXIMUM;
        HANDLE h = nullptr;
        if (!NT_SUCCESS(KiCopyToUser(client_view, &view, sizeof(view))) ||
            !NT_SUCCESS(KiCopyToUser(information, &info, sizeof(info))) ||
            (maximum && !NT_SUCCESS(KiCopyToUser(maximum, &longest, sizeof(longest))))) {
            ObDereferenceObject(end);
            status = STATUS_ACCESS_VIOLATION;
        } else {
            status = ObInsertObject(end, PORT_ALL_ACCESS, 0, &h);
        }
        if (NT_SUCCESS(status))
            status = ObStoreHandle(handle, h);
        if (NT_SUCCESS(status))
            process->CsrConnected = TRUE;
    }
    if (!NT_SUCCESS(status) && base)
        MiUnmapViewOfSection(process, base);
    ObDereferenceObject(port);
    return status;
}

/* the address the client has for one the server was given, when it lies in the view of the port. */
static PVOID local(const CSR_PORT *port, ULONG64 remote, ULONG length)
{
    if (remote < port->RemoteBase || remote - port->RemoteBase > CSR_VIEW_SIZE - length || length > CSR_VIEW_SIZE)
        return nullptr;
    return (PVOID)(port->ViewBase + (remote - port->RemoteBase));
}

/*
 * a request whose answer was not measured is answered with success and nothing else, and said in the log. request 0
 * of server 0 connects the client to a server dll, with the bytes for that dll in the view.
 */
static NTSTATUS serve(const CSR_PORT *port, CSR_MESSAGE *message)
{
    ULONG pid = (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
    if (message->Api == 0 && message->TotalLength >= 0x58) {
        ULONG dll = (ULONG)message->Arguments[1], length = (ULONG)message->Arguments[3];
        PVOID data = local(port, message->Arguments[2], length);
        CSR_SERVER_CONNECT connect = dll < RTL_NUMBER_OF(g_server_dll) ? g_server_dll[dll] : nullptr;
        if (connect && data && length && length <= CSR_MESSAGE_MAXIMUM) {
            UCHAR bytes[CSR_MESSAGE_MAXIMUM];
            NTSTATUS status = KiCopyFromUser(bytes, data, length);
            if (NT_SUCCESS(status))
                status = connect(bytes, length);
            if (NT_SUCCESS(status))
                status = KiCopyToUser(data, bytes, length);
            if (!NT_SUCCESS(status))
                KiLogPrintf("csr: process %u connects to server dll %u with %u bytes, refused %08x", pid, dll, length, status);
            return status;
        }
        KiLogPrintf("csr: process %u connects to server dll %u with %u bytes at %p, answered empty", pid, dll, length,
                    local(port, message->Arguments[2], length));
        return STATUS_SUCCESS;
    }
    KiLogPrintf("csr: process %u asks %x in %u bytes, capture %llx, answered empty: %llx %llx %llx %llx", pid, message->Api,
                message->TotalLength, (unsigned long long)message->CaptureBuffer, (unsigned long long)message->Arguments[0],
                (unsigned long long)message->Arguments[1], (unsigned long long)message->Arguments[2],
                (unsigned long long)message->Arguments[3]);
    return STATUS_SUCCESS;
}

/*
 * only the synchronous request of a client of csrss: the message goes in and its reply comes back in one call. the
 * reply is the message itself, typed as a reply, with the ids of the server's thread and the status after the
 * request number.
 */
NTSTATUS NTAPI ExCsrSendWaitReceivePort(PVOID object, ULONG flags, PVOID send, PVOID send_attributes, PVOID receive,
                                        PSIZE_T length, PVOID receive_attributes, PLARGE_INTEGER timeout)
{
    CSR_PORT *port = object;
    NTSTATUS status = STATUS_SUCCESS;
    static CSR_MESSAGE zero;
    CSR_MESSAGE *message = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*message), KI_TAG('C', 's', 'r', 'M'));
    SIZE_T room = 0;
    if (!message)
        status = STATUS_INSUFFICIENT_RESOURCES;
    else if (!port->Client || !send || !receive || !length)
        status = STATUS_INVALID_PARAMETER;
    else if (!NT_SUCCESS(KiCopyFromUser(message, send, 0x28)) || !NT_SUCCESS(KiCopyFromUser(&room, length, sizeof(room))))
        status = STATUS_ACCESS_VIOLATION;
    else if (message->TotalLength < 0x38 || message->TotalLength > CSR_MESSAGE_MAXIMUM)
        status = STATUS_PORT_MESSAGE_TOO_LONG;
    else if (room < message->TotalLength)
        status = STATUS_BUFFER_TOO_SMALL;
    else if (!NT_SUCCESS(KiCopyFromUser(message, send, message->TotalLength)))
        status = STATUS_ACCESS_VIOLATION;
    if (NT_SUCCESS(status)) {
        ULONG total = message->TotalLength;
        if (total < sizeof(*message))
            memcpy((PUCHAR)message + total, &zero, sizeof(*message) - total);
        message->Status = serve(port, message);
        ULONG sequence = (ULONG)__atomic_add_fetch(&g_sequence, 1, __ATOMIC_RELAXED);
        message->Type = LPC_REPLY;
        message->Process = (ULONG64)PsInitialSystemProcess->UniqueProcessId;
        message->Thread = (ULONG64)g_server_thread;
        message->MessageId = (sequence & 0xfff) * 4 + 4;
        message->CallbackId = sequence;
        room = total;
        if (!NT_SUCCESS(KiCopyToUser(receive, message, total)) || !NT_SUCCESS(KiCopyToUser(length, &room, sizeof(room))))
            status = STATUS_ACCESS_VIOLATION;
    }
    ExFreePool(message);
    ObDereferenceObject(port);
    return status;
}
