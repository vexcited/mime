/*
 * pseudo consoles: a console whose screen is carried to a terminal as the
 * bytes it speaks, and whose keys are carried back as the bytes it sends.
 * the console is a file of the console driver (\Device\ConDrv\Pty); two
 * threads of the process that made it move the bytes between the driver
 * and the pipes of the terminal. the processes made with the pseudo console
 * attribute are attached to the console of that file.
 */
#include "kernelbase_private.h"
#include <mime/condrv.h>

#define PSEUDOCONSOLE_INHERIT_CURSOR 0x1
#define PTY_BUFFER 4096

typedef struct KB_PTY {
    HANDLE Console;  /* the file of the driver that is the console */
    HANDLE Input;    /* the read end of the pipe the terminal types into, a copy */
    HANDLE Output;   /* the write end of the pipe the terminal reads from, a copy */
    HANDLE Reader;   /* the thread that takes the keys to the console */
    HANDLE Writer;   /* the thread that takes the screen to the terminal */
    volatile LONG Owners; /* the caller, the reader and the writer: the last to let go frees it */
} KB_PTY;

static void release(KB_PTY *pty)
{
    if (!InterlockedDecrement(&pty->Owners))
        KbFree(pty);
}

HANDLE KbPseudoConsoleHandle(HPCON console)
{
    return console ? ((KB_PTY *)console)->Console : nullptr;
}

/* the screen of the console, until it is closed and has nothing left to say. */
static DWORD WINAPI pty_writer(LPVOID parameter)
{
    KB_PTY *pty = parameter;
    UCHAR *buffer = KbAlloc(PTY_BUFFER);
    if (buffer) {
        for (;;) {
            IO_STATUS_BLOCK io;
            NTSTATUS status = NtDeviceIoControlFile(pty->Console, nullptr, nullptr, nullptr, &io, IOCTL_CONDRV_PTY_OUTPUT,
                                                    nullptr, 0, buffer, PTY_BUFFER);
            if (!NT_SUCCESS(status) || !io.Information)
                break;
            DWORD done;
            const UCHAR *p = buffer;
            ULONG left = (ULONG)io.Information;
            BOOL ok = TRUE;
            while (left && ok) {
                ok = WriteFile(pty->Output, p, left, &done, nullptr) && done;
                p += done;
                left -= done;
            }
            if (!ok)
                break;
        }
        KbFree(buffer);
    }
    CloseHandle(pty->Output);
    NtClose(pty->Console);
    release(pty);
    return 0;
}

/* the keys of the terminal, until its pipe ends. */
static DWORD WINAPI pty_reader(LPVOID parameter)
{
    KB_PTY *pty = parameter;
    UCHAR *buffer = KbAlloc(PTY_BUFFER);
    if (buffer) {
        for (;;) {
            DWORD got;
            if (!ReadFile(pty->Input, buffer, PTY_BUFFER, &got, nullptr) || !got)
                break;
            IO_STATUS_BLOCK io;
            NTSTATUS status = NtDeviceIoControlFile(pty->Console, nullptr, nullptr, nullptr, &io, IOCTL_CONDRV_PTY_INPUT,
                                                    buffer, got, nullptr, 0);
            if (!NT_SUCCESS(status))
                break;
        }
        KbFree(buffer);
    }
    CloseHandle(pty->Input);
    release(pty);
    return 0;
}

static HANDLE open_pty(NTSTATUS *status)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_CONDRV_DEVICE L"\\" MIME_CONDRV_PTY);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    HANDLE h = nullptr;
    *status = NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    return NT_SUCCESS(*status) ? h : nullptr;
}

static HRESULT hresult(DWORD error)
{
    return error ? (HRESULT)(0x80070000 | (error & 0xFFFF)) : 0;
}

DECLSPEC_EXPORT HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE input, HANDLE output, DWORD flags, HPCON *console)
{
    if (!console)
        return E_INVALIDARG;
    *console = nullptr;
    /* a terminal needs both its ends. */
    if ((input == nullptr || input == INVALID_HANDLE_VALUE) && (output == nullptr || output == INVALID_HANDLE_VALUE))
        return E_INVALIDARG;
    if (size.X <= 0 || size.Y <= 0)
        return E_INVALIDARG;
    KB_PTY *pty = KbAlloc(sizeof(*pty));
    if (!pty)
        return E_OUTOFMEMORY;
    memset(pty, 0, sizeof(*pty));
    HANDLE self = NtCurrentProcess();
    if (!DuplicateHandle(self, input, self, &pty->Input, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(self, output, self, &pty->Output, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        HRESULT hr = hresult(GetLastError());
        if (pty->Input)
            CloseHandle(pty->Input);
        KbFree(pty);
        return hr;
    }
    NTSTATUS status;
    pty->Console = open_pty(&status);
    if (!pty->Console) {
        CloseHandle(pty->Input);
        CloseHandle(pty->Output);
        KbFree(pty);
        return hresult(RtlNtStatusToDosError(status));
    }
    MIME_CONDRV_PTY_CREATE rq = {{size.X, size.Y}, flags};
    IO_STATUS_BLOCK io;
    status = NtDeviceIoControlFile(pty->Console, nullptr, nullptr, nullptr, &io, IOCTL_CONDRV_PTY_CREATE, &rq, sizeof(rq),
                                   nullptr, 0);
    if (!NT_SUCCESS(status)) {
        NtClose(pty->Console);
        CloseHandle(pty->Input);
        CloseHandle(pty->Output);
        KbFree(pty);
        return E_INVALIDARG;
    }
    pty->Owners = 3;
    pty->Writer = CreateThread(nullptr, 0, pty_writer, pty, 0, nullptr);
    pty->Reader = CreateThread(nullptr, 0, pty_reader, pty, 0, nullptr);
    if (!pty->Writer || !pty->Reader) {
        HRESULT hr = hresult(GetLastError());
        NtDeviceIoControlFile(pty->Console, nullptr, nullptr, nullptr, &io, IOCTL_CONDRV_PTY_CLOSE, nullptr, 0, nullptr, 0);
        return hr ? hr : E_FAIL;
    }
    *console = (HPCON)pty;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI ResizePseudoConsole(HPCON console, COORD size)
{
    if (!console)
        return E_INVALIDARG;
    if (size.X < 0 || size.Y < 0)
        return E_INVALIDARG;
    KB_PTY *pty = (KB_PTY *)console;
    MIME_CONDRV_COORD c = {size.X, size.Y};
    IO_STATUS_BLOCK io;
    NTSTATUS status = NtDeviceIoControlFile(pty->Console, nullptr, nullptr, nullptr, &io, IOCTL_CONDRV_PTY_RESIZE, &c, sizeof(c),
                                            nullptr, 0);
    if (status == STATUS_INVALID_PARAMETER)
        return E_INVALIDARG;
    return NT_SUCCESS(status) ? S_OK : hresult(RtlNtStatusToDosError(status));
}

/*
 * closing hangs the console up: its processes are told, and what the console still has to say is carried to
 * the terminal by the writer, which ends when the last process has gone. nothing is waited for.
 */
DECLSPEC_EXPORT VOID WINAPI ClosePseudoConsole(HPCON console)
{
    if (!console)
        return;
    KB_PTY *pty = (KB_PTY *)console;
    IO_STATUS_BLOCK io;
    NtDeviceIoControlFile(pty->Console, nullptr, nullptr, nullptr, &io, IOCTL_CONDRV_PTY_CLOSE, nullptr, 0, nullptr, 0);
    if (pty->Reader)
        CloseHandle(pty->Reader);
    if (pty->Writer)
        CloseHandle(pty->Writer);
    release(pty);
}

DECLSPEC_EXPORT HRESULT WINAPI ReleasePseudoConsole(HPCON console)
{
    return console ? S_OK : E_INVALIDARG;
}
