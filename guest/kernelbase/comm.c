/*
 * serial ports: each function is one or a few IOCTL_SERIAL requests to the port driver, waited on an event. mime has
 * no serial driver, so the requests fail as they would on a file that is not a port.
 */
#include "kernelbase_private.h"

#define SERIAL_IOCTL(function) (0x1B0000 | (function) << 2)
#define IOCTL_SERIAL_SET_BAUD_RATE SERIAL_IOCTL(1)
#define IOCTL_SERIAL_SET_QUEUE_SIZE SERIAL_IOCTL(2)
#define IOCTL_SERIAL_SET_LINE_CONTROL SERIAL_IOCTL(3)
#define IOCTL_SERIAL_SET_BREAK_ON SERIAL_IOCTL(4)
#define IOCTL_SERIAL_SET_BREAK_OFF SERIAL_IOCTL(5)
#define IOCTL_SERIAL_IMMEDIATE_CHAR SERIAL_IOCTL(6)
#define IOCTL_SERIAL_SET_TIMEOUTS SERIAL_IOCTL(7)
#define IOCTL_SERIAL_GET_TIMEOUTS SERIAL_IOCTL(8)
#define IOCTL_SERIAL_SET_DTR SERIAL_IOCTL(9)
#define IOCTL_SERIAL_CLR_DTR SERIAL_IOCTL(10)
#define IOCTL_SERIAL_RESET_DEVICE SERIAL_IOCTL(11)
#define IOCTL_SERIAL_SET_RTS SERIAL_IOCTL(12)
#define IOCTL_SERIAL_CLR_RTS SERIAL_IOCTL(13)
#define IOCTL_SERIAL_SET_XOFF SERIAL_IOCTL(14)
#define IOCTL_SERIAL_SET_XON SERIAL_IOCTL(15)
#define IOCTL_SERIAL_GET_WAIT_MASK SERIAL_IOCTL(16)
#define IOCTL_SERIAL_SET_WAIT_MASK SERIAL_IOCTL(17)
#define IOCTL_SERIAL_WAIT_ON_MASK SERIAL_IOCTL(18)
#define IOCTL_SERIAL_PURGE SERIAL_IOCTL(19)
#define IOCTL_SERIAL_GET_BAUD_RATE SERIAL_IOCTL(20)
#define IOCTL_SERIAL_GET_LINE_CONTROL SERIAL_IOCTL(21)
#define IOCTL_SERIAL_GET_CHARS SERIAL_IOCTL(22)
#define IOCTL_SERIAL_SET_CHARS SERIAL_IOCTL(23)
#define IOCTL_SERIAL_GET_HANDFLOW SERIAL_IOCTL(24)
#define IOCTL_SERIAL_SET_HANDFLOW SERIAL_IOCTL(25)
#define IOCTL_SERIAL_GET_MODEMSTATUS SERIAL_IOCTL(26)
#define IOCTL_SERIAL_GET_COMMSTATUS SERIAL_IOCTL(27)
#define IOCTL_SERIAL_XOFF_COUNTER SERIAL_IOCTL(28)
#define IOCTL_SERIAL_GET_PROPERTIES SERIAL_IOCTL(29)
#define IOCTL_SERIAL_GET_DTRRTS SERIAL_IOCTL(30)
#define IOCTL_SERIAL_SET_BREAK_ON_ALIAS IOCTL_SERIAL_SET_BREAK_ON

/* the handflow bits of ntddser.h. */
#define SERIAL_DTR_CONTROL 0x01
#define SERIAL_DTR_HANDSHAKE 0x02
#define SERIAL_CTS_HANDSHAKE 0x08
#define SERIAL_DSR_HANDSHAKE 0x10
#define SERIAL_DSR_SENSITIVITY 0x40
#define SERIAL_ERROR_ABORT 0x80000000u
#define SERIAL_AUTO_TRANSMIT 0x01
#define SERIAL_AUTO_RECEIVE 0x02
#define SERIAL_ERROR_CHAR 0x04
#define SERIAL_NULL_STRIPPING 0x08
#define SERIAL_RTS_CONTROL 0x40
#define SERIAL_RTS_HANDSHAKE 0x80
#define SERIAL_TRANSMIT_TOGGLE 0xC0
#define SERIAL_XOFF_CONTINUE 0x80000000u

typedef struct SERIAL_HANDFLOW {
    ULONG ControlHandShake;
    ULONG FlowReplace;
    LONG XonLimit;
    LONG XoffLimit;
} SERIAL_HANDFLOW;

typedef struct SERIAL_CHARS {
    UCHAR EofChar, ErrorChar, BreakChar, EventChar, XonChar, XoffChar;
} SERIAL_CHARS;

typedef struct SERIAL_LINE_CONTROL {
    UCHAR StopBits, Parity, WordLength;
} SERIAL_LINE_CONTROL;

typedef struct SERIAL_STATUS {
    ULONG Errors;
    ULONG HoldReasons;
    ULONG AmountInInQueue;
    ULONG AmountInOutQueue;
    BOOLEAN EofReceived;
    BOOLEAN WaitForImmediate;
} SERIAL_STATUS;

static NTSTATUS serial(HANDLE file, HANDLE event, ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len)
{
    IO_STATUS_BLOCK iosb = {};
    NTSTATUS status = NtDeviceIoControlFile(file, event, nullptr, nullptr, &iosb, code, in, in_len, out, out_len);
    if (status == STATUS_PENDING) {
        status = NtWaitForSingleObject(event, FALSE, nullptr);
        if (NT_SUCCESS(status))
            status = iosb.Status;
    }
    return status;
}

static BOOL is_error(NTSTATUS status)
{
    return ((ULONG)status & 0xC0000000) == 0xC0000000;
}

/* creates the event, makes the request and sets the last error. */
static BOOL serial_call(HANDLE file, ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len)
{
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event)
        return FALSE;
    NTSTATUS status = serial(file, event, code, in, in_len, out, out_len);
    CloseHandle(event);
    if (is_error(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetCommState(HANDLE file, LPDCB dcb)
{
    memset(dcb, 0, sizeof(*dcb));
    dcb->DCBlength = sizeof(*dcb);
    dcb->fBinary = TRUE;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event)
        return FALSE;
    ULONG baud = 0;
    SERIAL_LINE_CONTROL line = {};
    SERIAL_CHARS chars = {};
    SERIAL_HANDFLOW flow = {};
    NTSTATUS status = serial(file, event, IOCTL_SERIAL_GET_BAUD_RATE, nullptr, 0, &baud, sizeof(baud));
    if (!is_error(status))
        status = serial(file, event, IOCTL_SERIAL_GET_LINE_CONTROL, nullptr, 0, &line, sizeof(line));
    if (!is_error(status))
        status = serial(file, event, IOCTL_SERIAL_GET_CHARS, nullptr, 0, &chars, sizeof(chars));
    if (!is_error(status))
        status = serial(file, event, IOCTL_SERIAL_GET_HANDFLOW, nullptr, 0, &flow, sizeof(flow));
    CloseHandle(event);
    if (is_error(status))
        return KbSetStatus(status);
    dcb->BaudRate = baud;
    dcb->StopBits = line.StopBits;
    dcb->Parity = line.Parity;
    dcb->ByteSize = line.WordLength;
    dcb->EofChar = (char)chars.EofChar;
    dcb->ErrorChar = (char)chars.ErrorChar;
    dcb->EvtChar = (char)chars.EventChar;
    dcb->XonChar = (char)chars.XonChar;
    dcb->XoffChar = (char)chars.XoffChar;
    dcb->fOutxCtsFlow = !!(flow.ControlHandShake & SERIAL_CTS_HANDSHAKE);
    dcb->fOutxDsrFlow = !!(flow.ControlHandShake & SERIAL_DSR_HANDSHAKE);
    dcb->fDsrSensitivity = !!(flow.ControlHandShake & SERIAL_DSR_SENSITIVITY);
    dcb->fAbortOnError = !!(flow.ControlHandShake & SERIAL_ERROR_ABORT);
    dcb->fDtrControl = (flow.ControlHandShake & SERIAL_DTR_HANDSHAKE)   ? DTR_CONTROL_HANDSHAKE
                       : (flow.ControlHandShake & SERIAL_DTR_CONTROL) ? DTR_CONTROL_ENABLE
                                                                      : DTR_CONTROL_DISABLE;
    dcb->fOutX = !!(flow.FlowReplace & SERIAL_AUTO_TRANSMIT);
    dcb->fInX = !!(flow.FlowReplace & SERIAL_AUTO_RECEIVE);
    dcb->fErrorChar = !!(flow.FlowReplace & SERIAL_ERROR_CHAR);
    dcb->fNull = !!(flow.FlowReplace & SERIAL_NULL_STRIPPING);
    dcb->fTXContinueOnXoff = !!(flow.FlowReplace & SERIAL_XOFF_CONTINUE);
    switch (flow.FlowReplace & SERIAL_TRANSMIT_TOGGLE) {
    case SERIAL_RTS_CONTROL:
        dcb->fRtsControl = RTS_CONTROL_ENABLE;
        break;
    case SERIAL_RTS_HANDSHAKE:
        dcb->fRtsControl = RTS_CONTROL_HANDSHAKE;
        break;
    case SERIAL_TRANSMIT_TOGGLE:
        dcb->fRtsControl = RTS_CONTROL_TOGGLE;
        break;
    default:
        dcb->fRtsControl = RTS_CONTROL_DISABLE;
        break;
    }
    dcb->XonLim = (WORD)flow.XonLimit;
    dcb->XoffLim = (WORD)flow.XoffLimit;
    return TRUE;
}

BOOL WINAPI SetCommState(HANDLE file, LPDCB dcb)
{
    ULONG baud = dcb->BaudRate;
    SERIAL_LINE_CONTROL line = {dcb->StopBits, dcb->Parity, dcb->ByteSize};
    SERIAL_CHARS chars = {(UCHAR)dcb->EofChar, (UCHAR)dcb->ErrorChar, 0, (UCHAR)dcb->EvtChar, (UCHAR)dcb->XonChar,
                          (UCHAR)dcb->XoffChar};
    SERIAL_HANDFLOW flow = {};
    if (dcb->fOutxCtsFlow)
        flow.ControlHandShake |= SERIAL_CTS_HANDSHAKE;
    if (dcb->fOutxDsrFlow)
        flow.ControlHandShake |= SERIAL_DSR_HANDSHAKE;
    if (dcb->fDsrSensitivity)
        flow.ControlHandShake |= SERIAL_DSR_SENSITIVITY;
    if (dcb->fAbortOnError)
        flow.ControlHandShake |= SERIAL_ERROR_ABORT;
    if (dcb->fDtrControl == DTR_CONTROL_ENABLE)
        flow.ControlHandShake |= SERIAL_DTR_CONTROL;
    else if (dcb->fDtrControl == DTR_CONTROL_HANDSHAKE)
        flow.ControlHandShake |= SERIAL_DTR_HANDSHAKE;
    if (dcb->fOutX)
        flow.FlowReplace |= SERIAL_AUTO_TRANSMIT;
    if (dcb->fInX)
        flow.FlowReplace |= SERIAL_AUTO_RECEIVE;
    if (dcb->fErrorChar)
        flow.FlowReplace |= SERIAL_ERROR_CHAR;
    if (dcb->fNull)
        flow.FlowReplace |= SERIAL_NULL_STRIPPING;
    if (dcb->fTXContinueOnXoff)
        flow.FlowReplace |= SERIAL_XOFF_CONTINUE;
    static const ULONG rts[] = {0, SERIAL_RTS_CONTROL, SERIAL_RTS_HANDSHAKE, SERIAL_TRANSMIT_TOGGLE};
    flow.FlowReplace |= rts[dcb->fRtsControl];
    flow.XonLimit = dcb->XonLim;
    flow.XoffLimit = dcb->XoffLim;
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event)
        return FALSE;
    NTSTATUS status = serial(file, event, IOCTL_SERIAL_SET_BAUD_RATE, &baud, sizeof(baud), nullptr, 0);
    if (!is_error(status))
        status = serial(file, event, IOCTL_SERIAL_SET_LINE_CONTROL, &line, sizeof(line), nullptr, 0);
    if (!is_error(status))
        status = serial(file, event, IOCTL_SERIAL_SET_CHARS, &chars, sizeof(chars), nullptr, 0);
    if (!is_error(status))
        status = serial(file, event, IOCTL_SERIAL_SET_HANDFLOW, &flow, sizeof(flow), nullptr, 0);
    CloseHandle(event);
    if (is_error(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI GetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts)
{
    return serial_call(file, IOCTL_SERIAL_GET_TIMEOUTS, nullptr, 0, timeouts, sizeof(*timeouts));
}

BOOL WINAPI SetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts)
{
    COMMTIMEOUTS copy = *timeouts;
    return serial_call(file, IOCTL_SERIAL_SET_TIMEOUTS, &copy, sizeof(copy), nullptr, 0);
}

/* the driver's error bits map onto the CE_ flags in windows' order. */
BOOL WINAPI ClearCommError(HANDLE file, LPDWORD errors, LPCOMSTAT stat)
{
    SERIAL_STATUS st = {};
    if (!serial_call(file, IOCTL_SERIAL_GET_COMMSTATUS, nullptr, 0, &st, sizeof(st)))
        return FALSE;
    if (stat) {
        memset(stat, 0, sizeof(*stat));
        stat->fCtsHold = !!(st.HoldReasons & 1);
        stat->fDsrHold = !!(st.HoldReasons & 2);
        stat->fRlsdHold = !!(st.HoldReasons & 4);
        stat->fXoffHold = !!(st.HoldReasons & 8);
        stat->fXoffSent = !!(st.HoldReasons & 0x10);
        stat->fEof = st.EofReceived;
        stat->fTxim = st.WaitForImmediate;
        stat->cbInQue = st.AmountInInQueue;
        stat->cbOutQue = st.AmountInOutQueue;
    }
    if (errors) {
        DWORD e = 0;
        if (st.Errors & 1)
            e |= CE_BREAK;
        if (st.Errors & 2)
            e |= CE_FRAME;
        if (st.Errors & 4)
            e |= CE_OVERRUN;
        if (st.Errors & 8)
            e |= CE_RXOVER;
        if (st.Errors & 0x10)
            e |= CE_RXPARITY;
        *errors = e;
    }
    return TRUE;
}

BOOL WINAPI EscapeCommFunction(HANDLE file, DWORD function)
{
    static const ULONG codes[] = {0, IOCTL_SERIAL_SET_XOFF, IOCTL_SERIAL_SET_XON, IOCTL_SERIAL_SET_RTS,
                                  IOCTL_SERIAL_CLR_RTS, IOCTL_SERIAL_SET_DTR, IOCTL_SERIAL_CLR_DTR,
                                  IOCTL_SERIAL_RESET_DEVICE, IOCTL_SERIAL_SET_BREAK_ON, IOCTL_SERIAL_SET_BREAK_OFF};
    if (!function || function >= RTL_NUMBER_OF(codes)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return serial_call(file, codes[function], nullptr, 0, nullptr, 0);
}

BOOL WINAPI SetCommBreak(HANDLE file)
{
    return serial_call(file, IOCTL_SERIAL_SET_BREAK_ON, nullptr, 0, nullptr, 0);
}

BOOL WINAPI ClearCommBreak(HANDLE file)
{
    return serial_call(file, IOCTL_SERIAL_SET_BREAK_OFF, nullptr, 0, nullptr, 0);
}

BOOL WINAPI GetCommModemStatus(HANDLE file, LPDWORD status)
{
    return serial_call(file, IOCTL_SERIAL_GET_MODEMSTATUS, nullptr, 0, status, sizeof(*status));
}

BOOL WINAPI PurgeComm(HANDLE file, DWORD flags)
{
    return serial_call(file, IOCTL_SERIAL_PURGE, &flags, sizeof(flags), nullptr, 0);
}

BOOL WINAPI SetupComm(HANDLE file, DWORD in_queue, DWORD out_queue)
{
    ULONG sizes[2] = {in_queue, out_queue};
    return serial_call(file, IOCTL_SERIAL_SET_QUEUE_SIZE, sizes, sizeof(sizes), nullptr, 0);
}

BOOL WINAPI GetCommMask(HANDLE file, LPDWORD mask)
{
    return serial_call(file, IOCTL_SERIAL_GET_WAIT_MASK, nullptr, 0, mask, sizeof(*mask));
}

BOOL WINAPI SetCommMask(HANDLE file, DWORD mask)
{
    return serial_call(file, IOCTL_SERIAL_SET_WAIT_MASK, &mask, sizeof(mask), nullptr, 0);
}

BOOL WINAPI TransmitCommChar(HANDLE file, char c)
{
    return serial_call(file, IOCTL_SERIAL_IMMEDIATE_CHAR, &c, sizeof(c), nullptr, 0);
}

BOOL WINAPI GetCommProperties(HANDLE file, LPCOMMPROP prop)
{
    return serial_call(file, IOCTL_SERIAL_GET_PROPERTIES, nullptr, 0, prop, sizeof(*prop));
}

BOOL WINAPI WaitCommEvent(HANDLE file, LPDWORD mask, LPOVERLAPPED overlapped)
{
    if (!overlapped)
        return serial_call(file, IOCTL_SERIAL_WAIT_ON_MASK, nullptr, 0, mask, sizeof(*mask));
    overlapped->Internal = STATUS_PENDING;
    NTSTATUS status = NtDeviceIoControlFile(file, overlapped->hEvent, nullptr, overlapped,
                                            (PIO_STATUS_BLOCK)overlapped, IOCTL_SERIAL_WAIT_ON_MASK, nullptr, 0, mask,
                                            sizeof(*mask));
    if (is_error(status) || status == STATUS_PENDING)
        return KbSetStatus(status);
    return TRUE;
}
