/* RFC 6455 frames and close handshake over upgraded requests; client frames are masked. */
#include "winhttp_private.h"

enum { OP_CONTINUATION = 0, OP_TEXT = 1, OP_BINARY = 2, OP_CLOSE = 8, OP_PING = 9, OP_PONG = 10 };

void WhWebSocketReleased(WH_OBJECT *socket)
{
    WhConnClose(nullptr, socket->Conn);
    socket->Conn = nullptr;
}

HINTERNET WINAPI WinHttpWebSocketCompleteUpgrade(HINTERNET handle, DWORD_PTR context)
{
    WH_OBJECT *request = WhReference(handle);
    if (!request)
        return nullptr;
    DWORD error = ERROR_SUCCESS;
    if (request->Type != WH_REQUEST)
        error = ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
    else if (!request->UpgradeRequested || request->State != 2 || request->StatusCode != 101 || !request->Conn)
        error = ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    if (error) {
        WhRelease(request);
        SetLastError(error);
        return nullptr;
    }
    WH_OBJECT *socket = WhCreate(WH_WEBSOCKET, request);
    if (!socket) {
        WhRelease(request);
        return nullptr;
    }
    EnterCriticalSection(&request->Lock);
    socket->Conn = request->Conn;
    request->Conn = nullptr;
    LeaveCriticalSection(&request->Lock);
    socket->Context = context;
    HINTERNET result = socket->Handle;
    WhNotify(socket, WINHTTP_CALLBACK_STATUS_HANDLE_CREATED, &result, sizeof(result));
    WhRelease(socket);
    WhRelease(request);
    SetLastError(ERROR_SUCCESS);
    return result;
}

static DWORD SendFrame(WH_OBJECT *socket, BYTE opcode, BOOL final, const void *data, DWORD length)
{
    BYTE head[14];
    DWORD size = 0;
    head[size++] = (BYTE)((final ? 0x80 : 0) | opcode);
    if (length < 126) {
        head[size++] = (BYTE)(0x80 | length);
    } else if (length < 65536) {
        head[size++] = 0x80 | 126;
        head[size++] = (BYTE)(length >> 8);
        head[size++] = (BYTE)length;
    } else {
        head[size++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            head[size++] = i >= 4 ? 0 : (BYTE)(length >> (i * 8));
    }
    ULONG seed = GetTickCount() ^ (ULONG)(ULONG_PTR)socket ^ length;
    BYTE mask[4];
    for (DWORD i = 0; i < 4; i++)
        mask[i] = (BYTE)(RtlRandomEx(&seed) >> 8);
    memcpy(head + size, mask, 4);
    size += 4;
    BYTE *frame = WhAlloc(size + length);
    if (!frame)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(frame, head, size);
    for (DWORD i = 0; i < length; i++)
        frame[size + i] = ((const BYTE *)data)[i] ^ mask[i & 3];
    DWORD error = WhConnSend(socket->Conn, frame, size + length, socket->Timeouts.Send);
    WhFree(frame);
    return error;
}

static DWORD ReadExact(WH_OBJECT *socket, BYTE *data, DWORD length)
{
    WH_CONN *conn = socket->Conn;
    while (length) {
        DWORD buffered = conn->End - conn->Start;
        if (!buffered) {
            if (conn->Eof)
                return ERROR_WINHTTP_CONNECTION_ERROR;
            DWORD before = conn->End;
            DWORD error = conn->Tls ? WhTlsFill(conn, socket->Timeouts.Receive) : ERROR_SUCCESS;
            if (!conn->Tls) {
                if (conn->End == conn->Size) {
                    BYTE *grown = WhRealloc(conn->Buffer, conn->Size ? conn->Size * 2 : 16384);
                    if (!grown)
                        return ERROR_NOT_ENOUGH_MEMORY;
                    conn->Buffer = grown;
                    conn->Size = conn->Size ? conn->Size * 2 : 16384;
                }
                if (conn->Start == conn->End)
                    conn->Start = conn->End = before = 0;
                DWORD got;
                error = WhConnRecv(conn, conn->Buffer + conn->End, conn->Size - conn->End, &got, socket->Timeouts.Receive);
                if (!error && !got)
                    conn->Eof = TRUE;
                conn->End += got;
            }
            if (error)
                return socket->Closed ? ERROR_WINHTTP_OPERATION_CANCELLED : error;
            continue;
        }
        DWORD count = buffered < length ? buffered : length;
        memcpy(data, conn->Buffer + conn->Start, count);
        conn->Start += count;
        data += count;
        length -= count;
    }
    return ERROR_SUCCESS;
}

DWORD WhReadSome(WH_OBJECT *object, void *data, DWORD length, DWORD *got)
{
    WH_CONN *conn = object->Conn;
    *got = 0;
    if (!conn)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    if (conn->Eof && conn->Start == conn->End)
        return ERROR_SUCCESS;
    DWORD error = ReadExact(object, data, 1);
    if (error)
        return error;
    DWORD count = conn->End - conn->Start;
    if (count > length - 1)
        count = length - 1;
    memcpy((BYTE *)data + 1, conn->Buffer + conn->Start, count);
    conn->Start += count;
    *got = count + 1;
    return ERROR_SUCCESS;
}

/* read the frame header while answering pings and recording close frames. */
static DWORD NextFrame(WH_OBJECT *socket)
{
    while (TRUE) {
        BYTE head[2];
        DWORD error = ReadExact(socket, head, 2);
        if (error)
            return error;
        BYTE opcode = head[0] & 15;
        BOOL final = (head[0] & 0x80) != 0;
        ULONGLONG length = head[1] & 127;
        if (length >= 126) {
            BYTE extended[8];
            DWORD count = length == 126 ? 2 : 8;
            if ((error = ReadExact(socket, extended, count)))
                return error;
            length = 0;
            for (DWORD i = 0; i < count; i++)
                length = length << 8 | extended[i];
        }
        socket->WsMasked = (head[1] & 0x80) != 0;
        if (socket->WsMasked && (error = ReadExact(socket, socket->WsMask, 4)))
            return error;
        if (opcode >= OP_CLOSE) {
            BYTE payload[125];
            if (length > sizeof(payload))
                return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
            if ((error = ReadExact(socket, payload, (DWORD)length)))
                return error;
            if (opcode == OP_PING) {
                if ((error = SendFrame(socket, OP_PONG, TRUE, payload, (DWORD)length)))
                    return error;
                continue;
            }
            if (opcode == OP_PONG)
                continue;
            socket->WsClosedReceived = TRUE;
            socket->WsCloseStatus = length >= 2 ? (USHORT)(payload[0] << 8 | payload[1])
                                                : WINHTTP_WEB_SOCKET_EMPTY_CLOSE_STATUS;
            socket->WsCloseReasonLength = length > 2 ? (DWORD)length - 2 : 0;
            memcpy(socket->WsCloseReason, payload + 2, socket->WsCloseReasonLength);
            return ERROR_SUCCESS;
        }
        if (length > 0xFFFFFFFF)
            return ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
        if (opcode != OP_CONTINUATION)
            socket->WsFrameType = opcode;
        socket->WsFrameLeft = (DWORD)length;
        socket->WsFinal = final;
        if (length || final)
            return ERROR_SUCCESS;
    }
}

static DWORD Receive(WH_OBJECT *socket, BYTE *buffer, DWORD length, DWORD *read, WINHTTP_WEB_SOCKET_BUFFER_TYPE *type)
{
    *read = 0;
    if (socket->WsClosedReceived)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    if (!socket->WsFrameLeft) {
        DWORD error = NextFrame(socket);
        if (error)
            return error;
        if (socket->WsClosedReceived) {
            *type = WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE;
            return ERROR_SUCCESS;
        }
    }
    DWORD count = socket->WsFrameLeft < length ? socket->WsFrameLeft : length;
    DWORD error = ReadExact(socket, buffer, count);
    if (error)
        return error;
    /* servers do not mask; apply the supplied mask if present. */
    if (socket->WsMasked)
        for (DWORD i = 0; i < count; i++)
            buffer[i] ^= socket->WsMask[i & 3];
    socket->WsFrameLeft -= count;
    *read = count;
    BOOL last = !socket->WsFrameLeft && socket->WsFinal;
    BOOL text = socket->WsFrameType == OP_TEXT;
    *type = last ? (text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
                 : (text ? WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE
                         : WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE);
    return ERROR_SUCCESS;
}

static DWORD SendClose(WH_OBJECT *socket, USHORT status, const void *reason, DWORD length)
{
    if (socket->WsClosedSent)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    BYTE payload[125];
    DWORD size = 0;
    if (status != WINHTTP_WEB_SOCKET_EMPTY_CLOSE_STATUS) {
        payload[0] = (BYTE)(status >> 8);
        payload[1] = (BYTE)status;
        memcpy(payload + 2, reason, length);
        size = 2 + length;
    }
    socket->WsClosedSent = TRUE;
    return SendFrame(socket, OP_CLOSE, TRUE, payload, size);
}

static BOOL ValidClose(USHORT status, const void *reason, DWORD length)
{
    if (length > WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH || (length && !reason))
        return FALSE;
    if (status == WINHTTP_WEB_SOCKET_EMPTY_CLOSE_STATUS)
        return !length;
    return status == 1000 || (status >= 3000 && status <= 4999);
}

static WH_OBJECT *ReferenceSocket(HINTERNET handle, DWORD *error)
{
    WH_OBJECT *socket = WhReference(handle);
    if (!socket) {
        *error = ERROR_INVALID_HANDLE;
        return nullptr;
    }
    if (socket->Type != WH_WEBSOCKET) {
        WhRelease(socket);
        *error = ERROR_WINHTTP_INCORRECT_HANDLE_TYPE;
        return nullptr;
    }
    *error = ERROR_SUCCESS;
    return socket;
}

typedef struct {
    WH_OBJECT *Socket;
    WINHTTP_WEB_SOCKET_OPERATION Operation;
    BYTE *Buffer;
    DWORD Length;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE Type;
    USHORT Status;
    BYTE Reason[WINHTTP_WEB_SOCKET_MAX_CLOSE_REASON_LENGTH];
} WH_WS_JOB;

static DWORD RunJob(WH_WS_JOB *job, WINHTTP_WEB_SOCKET_STATUS *status)
{
    WH_OBJECT *socket = job->Socket;
    status->dwBytesTransferred = 0;
    status->eBufferType = job->Type;
    switch (job->Operation) {
    case WINHTTP_WEB_SOCKET_SEND_OPERATION: {
        BOOL text = job->Type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                    job->Type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE;
        BOOL final = job->Type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                     job->Type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        BYTE opcode = socket->WsSendFragment ? OP_CONTINUATION : (text ? OP_TEXT : OP_BINARY);
        DWORD error = SendFrame(socket, opcode, final, job->Buffer, job->Length);
        socket->WsSendFragment = !final;
        status->dwBytesTransferred = job->Length;
        return error;
    }
    case WINHTTP_WEB_SOCKET_RECEIVE_OPERATION:
        return Receive(socket, job->Buffer, job->Length, &status->dwBytesTransferred, &status->eBufferType);
    case WINHTTP_WEB_SOCKET_SHUTDOWN_OPERATION:
        return SendClose(socket, job->Status, job->Reason, job->Length);
    case WINHTTP_WEB_SOCKET_CLOSE_OPERATION: {
        DWORD error = socket->WsClosedSent ? ERROR_SUCCESS : SendClose(socket, job->Status, job->Reason, job->Length);
        BYTE scratch[4096];
        while (!error && !socket->WsClosedReceived) {
            DWORD read;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            error = Receive(socket, scratch, sizeof(scratch), &read, &type);
        }
        return error;
    }
    }
    return ERROR_INVALID_PARAMETER;
}

static DWORD WINAPI AsyncJob(LPVOID parameter)
{
    WH_WS_JOB *job = parameter;
    WH_OBJECT *socket = job->Socket;
    WINHTTP_WEB_SOCKET_STATUS status;
    DWORD error = RunJob(job, &status);
    EnterCriticalSection(&socket->Lock);
    socket->Busy = FALSE;
    LeaveCriticalSection(&socket->Lock);
    if (error) {
        WINHTTP_WEB_SOCKET_ASYNC_RESULT result = {{job->Operation, error}, job->Operation};
        WhNotify(socket, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, &result, sizeof(result));
    } else if (job->Operation == WINHTTP_WEB_SOCKET_SEND_OPERATION) {
        WhNotify(socket, WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE, &status, sizeof(status));
    } else if (job->Operation == WINHTTP_WEB_SOCKET_RECEIVE_OPERATION) {
        WhNotify(socket, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, &status, sizeof(status));
    } else if (job->Operation == WINHTTP_WEB_SOCKET_CLOSE_OPERATION) {
        WhNotify(socket, WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE, nullptr, 0);
    } else {
        WhNotify(socket, WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE, nullptr, 0);
    }
    WhFree(job);
    WhRelease(socket);
    return 0;
}

/* run immediately or on a pool thread for asynchronous sessions. */
static DWORD Dispatch(WH_OBJECT *socket, WH_WS_JOB *job, DWORD *read, WINHTTP_WEB_SOCKET_BUFFER_TYPE *type)
{
    if (!WhIsAsync(socket)) {
        WINHTTP_WEB_SOCKET_STATUS status;
        DWORD error = RunJob(job, &status);
        if (read)
            *read = status.dwBytesTransferred;
        if (type)
            *type = status.eBufferType;
        WhRelease(socket);
        return error;
    }
    WH_WS_JOB *copy = WhAlloc(sizeof(*copy));
    if (!copy) {
        WhRelease(socket);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    *copy = *job;
    EnterCriticalSection(&socket->Lock);
    if (socket->Busy) {
        LeaveCriticalSection(&socket->Lock);
        WhFree(copy);
        WhRelease(socket);
        return ERROR_INVALID_OPERATION;
    }
    socket->Busy = TRUE;
    LeaveCriticalSection(&socket->Lock);
    if (!QueueUserWorkItem(AsyncJob, copy, 0)) {
        socket->Busy = FALSE;
        WhFree(copy);
        WhRelease(socket);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI WinHttpWebSocketSend(HINTERNET handle, WINHTTP_WEB_SOCKET_BUFFER_TYPE type, PVOID buffer, DWORD length)
{
    if (type > WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || (length && !buffer))
        return ERROR_INVALID_PARAMETER;
    DWORD error;
    WH_OBJECT *socket = ReferenceSocket(handle, &error);
    if (!socket)
        return error;
    if (socket->WsClosedSent) {
        WhRelease(socket);
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    }
    WH_WS_JOB job = {socket, WINHTTP_WEB_SOCKET_SEND_OPERATION, buffer, length, type};
    return Dispatch(socket, &job, nullptr, nullptr);
}

DWORD WINAPI WinHttpWebSocketReceive(HINTERNET handle, PVOID buffer, DWORD length, DWORD *read,
                                     WINHTTP_WEB_SOCKET_BUFFER_TYPE *type)
{
    if (!buffer || !length)
        return ERROR_INVALID_PARAMETER;
    DWORD error;
    WH_OBJECT *socket = ReferenceSocket(handle, &error);
    if (!socket)
        return error;
    if (!WhIsAsync(socket) && (!read || !type)) {
        WhRelease(socket);
        return ERROR_INVALID_PARAMETER;
    }
    WH_WS_JOB job = {socket, WINHTTP_WEB_SOCKET_RECEIVE_OPERATION, buffer, length};
    return Dispatch(socket, &job, read, type);
}

static DWORD CloseOrShutdown(HINTERNET handle, WINHTTP_WEB_SOCKET_OPERATION operation, USHORT status, PVOID reason,
                             DWORD length)
{
    if (!ValidClose(status, reason, length))
        return ERROR_INVALID_PARAMETER;
    DWORD error;
    WH_OBJECT *socket = ReferenceSocket(handle, &error);
    if (!socket)
        return error;
    WH_WS_JOB job = {socket, operation, nullptr, length};
    job.Status = status;
    if (length)
        memcpy(job.Reason, reason, length);
    return Dispatch(socket, &job, nullptr, nullptr);
}

DWORD WINAPI WinHttpWebSocketShutdown(HINTERNET handle, USHORT status, PVOID reason, DWORD length)
{
    return CloseOrShutdown(handle, WINHTTP_WEB_SOCKET_SHUTDOWN_OPERATION, status, reason, length);
}

DWORD WINAPI WinHttpWebSocketClose(HINTERNET handle, USHORT status, PVOID reason, DWORD length)
{
    return CloseOrShutdown(handle, WINHTTP_WEB_SOCKET_CLOSE_OPERATION, status, reason, length);
}

DWORD WINAPI WinHttpWebSocketQueryCloseStatus(HINTERNET handle, USHORT *status, PVOID reason, DWORD length,
                                              DWORD *consumed)
{
    if (!status || !consumed || (length && !reason))
        return ERROR_INVALID_PARAMETER;
    DWORD error;
    WH_OBJECT *socket = ReferenceSocket(handle, &error);
    if (!socket)
        return error;
    if (!socket->WsClosedReceived) {
        error = ERROR_INVALID_OPERATION;
    } else {
        *status = socket->WsCloseStatus;
        *consumed = socket->WsCloseReasonLength;
        if (length < socket->WsCloseReasonLength)
            error = ERROR_INSUFFICIENT_BUFFER;
        else
            memcpy(reason, socket->WsCloseReason, socket->WsCloseReasonLength);
    }
    WhRelease(socket);
    return error;
}
