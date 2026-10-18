/*
 * the security support provider interface of the schannel package: the
 * credential forms, InitializeSecurityContext with schannel's token and
 * extra buffers, the stream buffers of EncryptMessage and DecryptMessage,
 * the context attributes and the control tokens. tls.c does the protocol.
 */
#include "schannel.h"

/* ------------------------------------------------------------------ credentials */

void SchParseCredentials(const void *auth_data, SCH_CONFIG *config)
{
    ULONG enabled = 0, disabled = 0;
    memset(config, 0, sizeof(*config));
    if (auth_data) {
        ULONG version = *(const ULONG *)auth_data;
        if (version == SCH_CREDENTIALS_VERSION) {
            const SCH_CRED_V5 *c = auth_data;
            config->Flags = c->dwFlags;
            /* parameters tied to alpn ids only apply to those protocols, which mime does not track. */
            for (DWORD i = 0; c->pTlsParameters && i < c->cTlsParameters; i++)
                if (!c->pTlsParameters[i].cAlpnIds)
                    disabled |= c->pTlsParameters[i].grbitDisabledProtocols;
        } else if (version == SCHANNEL_CRED_VERSION || version == SCH_CRED_V3) {
            const SCH_CRED_V4 *c = auth_data;
            config->Flags = c->dwFlags;
            enabled = c->grbitEnabledProtocols;
            config->MinStrength = c->dwMinimumCipherStrength;
            config->MaxStrength = c->dwMaximumCipherStrength;
        }
    }
    /* no protocols means the system's defaults, tls 1.2 and 1.3 on windows 11. */
    ULONG supported = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    config->Protocols = (enabled ? enabled : supported) & supported & ~disabled;
}

/* ------------------------------------------------------------------ buffers */

static SecBuffer *find_buffer(PSecBufferDesc desc, ULONG type)
{
    if (!desc || !desc->pBuffers)
        return nullptr;
    for (ULONG i = 0; i < desc->cBuffers; i++)
        if ((desc->pBuffers[i].BufferType & ~SECBUFFER_ATTRMASK) == type)
            return &desc->pBuffers[i];
    return nullptr;
}

/*
 * the alpn names of a SEC_APPLICATION_PROTOCOLS buffer: ProtocolListsSize,
 * then lists of a 32-bit extension, a 16-bit size and the names in their
 * wire form.
 */
static SECURITY_STATUS read_alpn(PSecBufferDesc input, BYTE *out, SIZE_T capacity, SIZE_T *size)
{
    *size = 0;
    SecBuffer *b = find_buffer(input, SECBUFFER_APPLICATION_PROTOCOLS);
    if (!b || !b->pvBuffer)
        return SEC_E_OK;
    const BYTE *p = b->pvBuffer;
    ULONG lists;
    if (b->cbBuffer < 4)
        return SEC_E_INVALID_TOKEN;
    memcpy(&lists, p, 4);
    if (lists > b->cbBuffer - 4)
        return SEC_E_INVALID_TOKEN;
    p += 4;
    while (lists >= 6) {
        ULONG extension;
        USHORT n;
        memcpy(&extension, p, 4);
        memcpy(&n, p + 4, 2);
        if (n > lists - 6)
            return SEC_E_INVALID_TOKEN;
        if (extension == 2) {
            const BYTE *names = p + 6;
            for (USHORT at = 0; at < n;) {
                if (!names[at] || names[at] > n - at - 1)
                    return SEC_E_INVALID_TOKEN;
                at += 1 + names[at];
            }
            if (n > capacity)
                return SEC_E_INVALID_TOKEN;
            memcpy(out, names, n);
            *size = n;
        }
        p += 6 + n;
        lists -= 6 + n;
    }
    return SEC_E_OK;
}

/* hands the queued records to the output token, in LocalAlloc memory with ISC_REQ_ALLOCATE_MEMORY. */
static SECURITY_STATUS emit(TLS *t, PSecBufferDesc output, ULONG flags)
{
    TLS_BUF *out = TlsOutput(t);
    SecBuffer *token = find_buffer(output, SECBUFFER_TOKEN);
    SECURITY_STATUS status = SEC_E_OK;
    if (out->Failed) {
        status = SEC_E_INSUFFICIENT_MEMORY;
    } else if (!token) {
        if (out->Size)
            status = SEC_E_INVALID_TOKEN;
    } else if (flags & ISC_REQ_ALLOCATE_MEMORY) {
        token->pvBuffer = nullptr;
        token->cbBuffer = 0;
        if (out->Size) {
            void *p = LocalAlloc(LMEM_FIXED, out->Size);
            if (!p) {
                status = SEC_E_INSUFFICIENT_MEMORY;
            } else {
                memcpy(p, out->Data, out->Size);
                token->pvBuffer = p;
                token->cbBuffer = (ULONG)out->Size;
            }
        }
    } else if (out->Size > token->cbBuffer || (out->Size && !token->pvBuffer)) {
        status = SEC_E_INSUFFICIENT_MEMORY;
    } else {
        if (out->Size)
            memcpy(token->pvBuffer, out->Data, out->Size);
        token->cbBuffer = (ULONG)out->Size;
    }
    out->Size = 0;
    out->Failed = false;
    return status;
}

/* schannel reports unread input in the second buffer, the first empty one after the token. */
static void report_input(PSecBufferDesc input, SecBuffer *token, SIZE_T consumed, SIZE_T missing,
                         SECURITY_STATUS status)
{
    if (!token)
        return;
    SecBuffer *slot = nullptr;
    for (ULONG i = 0; i < input->cBuffers && !slot; i++)
        if (&input->pBuffers[i] != token && input->pBuffers[i].BufferType == SECBUFFER_EMPTY)
            slot = &input->pBuffers[i];
    if (!slot)
        return;
    if (status == SEC_E_INCOMPLETE_MESSAGE) {
        slot->BufferType = SECBUFFER_MISSING;
        slot->cbBuffer = (ULONG)missing;
    } else if ((status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) && consumed < token->cbBuffer) {
        slot->BufferType = SECBUFFER_EXTRA;
        slot->cbBuffer = (ULONG)(token->cbBuffer - consumed);
    }
}

/* ------------------------------------------------------------------ InitializeSecurityContext */

SECURITY_STATUS SchInitialize(PCredHandle credential, PCtxtHandle context, PCWSTR target, ULONG flags,
                              PSecBufferDesc input, PCtxtHandle new_context, PSecBufferDesc output,
                              ULONG *attributes, PTimeStamp expiry)
{
    SECURITY_STATUS status;
    if (!context) {
        const SEC_PACKAGE *package;
        ULONG use;
        SCH_CONFIG config;
        if (!credential || !SecLookupCredential(credential, &package, &use) ||
            !SecSchannelConfig(credential, &config))
            return SEC_E_INVALID_HANDLE;
        if (!(use & SECPKG_CRED_OUTBOUND))
            return SEC_E_NO_CREDENTIALS;
        if (!new_context)
            return SEC_E_INVALID_PARAMETER;
        BYTE alpn[256];
        SIZE_T alpn_size;
        if ((status = read_alpn(input, alpn, sizeof(alpn), &alpn_size)))
            return status;
        TLS *t = TlsCreate(&config, flags, target);
        if (!t)
            return SEC_E_INSUFFICIENT_MEMORY;
        status = TlsStart(t, alpn, alpn_size);
        if (status == SEC_I_CONTINUE_NEEDED) {
            SECURITY_STATUS emitted = emit(t, output, flags);
            if (emitted)
                status = emitted;
        }
        if (status != SEC_I_CONTINUE_NEEDED) {
            TlsDestroy(t);
            return status;
        }
        if (!SecNewSchannelContext(package, t, new_context)) {
            SecBuffer *token = find_buffer(output, SECBUFFER_TOKEN);
            if (token && (flags & ISC_REQ_ALLOCATE_MEMORY)) {
                LocalFree(token->pvBuffer);
                token->pvBuffer = nullptr;
                token->cbBuffer = 0;
            }
            TlsDestroy(t);
            return SEC_E_INSUFFICIENT_MEMORY;
        }
        if (attributes)
            *attributes = TlsReturnedFlags(t);
        if (expiry)
            TlsExpiry(t, expiry);
        return SEC_I_CONTINUE_NEEDED;
    }

    TLS *t = SecAcquireSchannelContext(context);
    if (!t)
        return SEC_E_INVALID_HANDLE;
    SecBuffer *token = find_buffer(input, SECBUFFER_TOKEN);
    BYTE *data = token ? token->pvBuffer : nullptr;
    SIZE_T size = data ? token->cbBuffer : 0, consumed = 0, missing = 0;
    if (TlsConnected(t))
        status = TlsPostHandshake(t, size, &consumed);
    else
        status = TlsHandshake(t, data, size, &consumed, &missing);
    if (input)
        report_input(input, data ? token : nullptr, consumed, missing, status);
    SECURITY_STATUS emitted = emit(t, output, flags);
    if (emitted && (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED))
        status = emitted;
    if (new_context && new_context != context)
        *new_context = *context;
    if (attributes)
        *attributes = TlsReturnedFlags(t);
    if (expiry && status == SEC_E_OK)
        TlsExpiry(t, expiry);
    SecReleaseSchannelContext(context);
    return status;
}

/* ------------------------------------------------------------------ EncryptMessage */

SECURITY_STATUS SchEncrypt(TLS *t, PSecBufferDesc message)
{
    if (!message || !message->pBuffers)
        return SEC_E_INVALID_TOKEN;
    SecBuffer *header = nullptr, *data = nullptr, *trailer = nullptr;
    for (ULONG i = 0; i < message->cBuffers; i++) {
        SecBuffer *b = &message->pBuffers[i];
        switch (b->BufferType & ~SECBUFFER_ATTRMASK) {
        case SECBUFFER_STREAM_HEADER:
            header = b;
            break;
        case SECBUFFER_TOKEN:
            if (!header)
                header = b;
            break;
        case SECBUFFER_DATA:
            if (!data)
                data = b;
            break;
        case SECBUFFER_STREAM_TRAILER:
            if (!trailer)
                trailer = b;
            break;
        }
    }
    if (!header || !data || !header->pvBuffer || (data->cbBuffer && !data->pvBuffer))
        return SEC_E_INVALID_TOKEN;
    if (!TlsConnected(t))
        return SEC_E_INVALID_HANDLE;
    SCH_STREAM_SIZES sizes;
    TlsStreamSizes(t, &sizes);
    SIZE_T head = sizes.cbHeader, n = data->cbBuffer, used = 0;
    SIZE_T hcb = header->cbBuffer, tcb = trailer && trailer->pvBuffer ? trailer->cbBuffer : 0;
    BYTE *h = header->pvBuffer, *d = data->pvBuffer, *tr = tcb ? trailer->pvBuffer : nullptr;
    if (hcb + n < hcb || hcb + n + tcb < hcb + n)
        return SEC_E_INVALID_PARAMETER;
    bool contiguous = d == h + hcb && (!tr || tr == d + n);
    if (contiguous && hcb == head) {
        SECURITY_STATUS status = TlsEncrypt(t, h, d, n, tr, tcb, &used);
        if (!status && trailer)
            trailer->cbBuffer = (ULONG)used;
        return status;
    }
    /* like schannel, a record that does not fit the buffers as they are is built aside and spread over them. */
    SIZE_T capacity = hcb + n + tcb;
    if (capacity < head + n)
        return SEC_E_ENCRYPT_FAILURE;
    BYTE *temp = SecHeapAlloc(capacity);
    if (!temp)
        return SEC_E_INSUFFICIENT_MEMORY;
    if (n)
        memcpy(temp + head, d, n);
    SECURITY_STATUS status = TlsEncrypt(t, temp, temp + head, n, temp + head + n, capacity - head - n, &used);
    if (!status) {
        SIZE_T total = head + n + used;
        SIZE_T hn = total < hcb ? total : hcb;
        SIZE_T dn = total - hn < n ? total - hn : n;
        SIZE_T tn = total - hn - dn;
        memcpy(h, temp, hn);
        memcpy(d, temp + hn, dn);
        if (tn)
            memcpy(tr, temp + hn + dn, tn);
        header->cbBuffer = (ULONG)hn;
        data->cbBuffer = (ULONG)dn;
        if (trailer)
            trailer->cbBuffer = (ULONG)tn;
    }
    mime_wipe(temp, capacity);
    SecHeapFree(temp);
    return status;
}

/* ------------------------------------------------------------------ DecryptMessage */

SECURITY_STATUS SchDecrypt(TLS *t, PSecBufferDesc message)
{
    if (!message || !message->pBuffers)
        return SEC_E_INVALID_TOKEN;
    /* schannel wants the data buffer and three empty ones for the header, the data and the trailer or extra. */
    SecBuffer *data = nullptr, *empty[3] = {};
    ULONG empties = 0;
    for (ULONG i = 0; i < message->cBuffers; i++) {
        SecBuffer *b = &message->pBuffers[i];
        if (b->BufferType == SECBUFFER_DATA)
            data = b;
        else if (b->BufferType == SECBUFFER_EMPTY && empties < 3)
            empty[empties++] = b;
    }
    if (!data || empties < 3 || !data->pvBuffer)
        return SEC_E_INVALID_TOKEN;
    BYTE *base = data->pvBuffer;
    SIZE_T size = data->cbBuffer, missing;
    TLS_RECORD record;
    SECURITY_STATUS status = TlsDecrypt(t, base, size, &record, &missing);
    if (status == SEC_E_INCOMPLETE_MESSAGE) {
        empty[0]->BufferType = SECBUFFER_MISSING;
        empty[0]->cbBuffer = (ULONG)missing;
        data->BufferType = SECBUFFER_MISSING;
        data->cbBuffer = (ULONG)missing;
        return status;
    }
    /*
     * schannel lays out the buffers for data and SEC_I_RENEGOTIATE only; a
     * closing alert gets the same layout here, with no data, so that
     * callers scanning for the data buffer find none.
     */
    if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE && status != SEC_I_CONTEXT_EXPIRED &&
        status != SEC_I_NO_RENEGOTIATION)
        return status;
    if (record.Total < size) {
        empty[2]->BufferType = SECBUFFER_EXTRA;
        empty[2]->cbBuffer = (ULONG)(size - record.Total);
        empty[2]->pvBuffer = base + record.Total;
    }
    data->BufferType = SECBUFFER_STREAM_HEADER;
    data->cbBuffer = (ULONG)record.HeaderSize;
    empty[0]->BufferType = SECBUFFER_DATA;
    empty[0]->pvBuffer = record.Data ? record.Data : base + record.HeaderSize;
    empty[0]->cbBuffer = (ULONG)record.DataSize;
    empty[1]->BufferType = SECBUFFER_STREAM_TRAILER;
    empty[1]->pvBuffer = (BYTE *)empty[0]->pvBuffer + record.DataSize;
    empty[1]->cbBuffer = (ULONG)record.TrailerSize;
    return status;
}

/* ------------------------------------------------------------------ attributes and control tokens */

SECURITY_STATUS SchQueryAttributes(TLS *t, ULONG attribute, void *buffer, bool ansi)
{
    if (!buffer)
        return SEC_E_INVALID_PARAMETER;
    if (attribute == SECPKG_ATTR_REMOTE_CERT_CONTEXT) {
        PCCERT_CONTEXT cert = TlsRemoteCertificate(t);
        if (!cert)
            return SEC_E_NO_CREDENTIALS;
        *(PCCERT_CONTEXT *)buffer = cert;
        return SEC_E_OK;
    }
    /* the rest lives in the user mode half of schannel, which exists once the handshake is over. */
    if (!TlsConnected(t))
        return SEC_E_INVALID_HANDLE;
    switch (attribute) {
    case SECPKG_ATTR_STREAM_SIZES:
        TlsStreamSizes(t, buffer);
        return SEC_E_OK;
    case SECPKG_ATTR_CONNECTION_INFO:
        TlsConnectionInfo(t, buffer);
        return SEC_E_OK;
    case SECPKG_ATTR_CIPHER_INFO:
        TlsCipherInfo(t, buffer);
        return SEC_E_OK;
    case SECPKG_ATTR_APPLICATION_PROTOCOL:
        TlsApplicationProtocol(t, buffer);
        return SEC_E_OK;
    case SECPKG_ATTR_SESSION_INFO:
        TlsSessionInfo(t, buffer);
        return SEC_E_OK;
    case SECPKG_ATTR_KEY_INFO:
        return TlsKeyInfo(t, buffer, ansi);
    case SECPKG_ATTR_LIFESPAN: {
        PCCERT_CONTEXT cert = TlsRemoteCertificate(t);
        TimeStamp *span = buffer;
        if (!cert)
            return SEC_E_NO_CREDENTIALS;
        FILETIME from = cert->pCertInfo->NotBefore, to = cert->pCertInfo->NotAfter;
        span[0].QuadPart = (LONGLONG)((ULONGLONG)from.dwHighDateTime << 32 | from.dwLowDateTime);
        span[1].QuadPart = (LONGLONG)((ULONGLONG)to.dwHighDateTime << 32 | to.dwLowDateTime);
        SchFreeCertificate(cert);
        return SEC_E_OK;
    }
    default:
        return SEC_E_UNSUPPORTED_FUNCTION;
    }
}

SECURITY_STATUS SchApplyControlToken(TLS *t, PSecBufferDesc input)
{
    SecBuffer *token = find_buffer(input, SECBUFFER_TOKEN);
    if (!token || !token->pvBuffer || token->cbBuffer < sizeof(DWORD))
        return SEC_E_INVALID_TOKEN;
    DWORD words[3] = {};
    memcpy(words, token->pvBuffer, token->cbBuffer < sizeof(words) ? token->cbBuffer : sizeof(words));
    switch (words[0]) {
    case SCHANNEL_SHUTDOWN:
        TlsQueueAlert(t, TLS1_ALERT_WARNING, 0);
        return SEC_E_OK;
    case SCHANNEL_ALERT:
        /* SCHANNEL_ALERT_TOKEN: the type, then the alert level and number. */
        if (token->cbBuffer < sizeof(words) || (words[1] != TLS1_ALERT_WARNING && words[1] != TLS1_ALERT_FATAL) ||
            words[2] > 255)
            return SEC_E_INVALID_TOKEN;
        TlsQueueAlert(t, (BYTE)words[1], (BYTE)words[2]);
        return SEC_E_OK;
    case SCHANNEL_SESSION:
        return SEC_E_OK;
    default:
        return SEC_E_UNSUPPORTED_FUNCTION;
    }
}
