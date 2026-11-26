/*
 * what windows 11 26300 answers to each process and thread information class at each length, measured on the caller's
 * own process and thread. a class has runs of lengths that got the same status and the same returned length: one exact
 * length, a few of them, every length from one on, or one status whatever the length.
 */
#ifndef MIME_PS_CLASSREF_H
#define MIME_PS_CLASSREF_H

#define PS_CLASS_NO_LENGTH 0xFFFFFFFFu

typedef struct PS_CLASS_RUN {
    UCHAR Klass;
    ULONG First, Last; /* the lengths asked that the run spans */
    ULONG Status;
    ULONG Returned;    /* PS_CLASS_NO_LENGTH when the returned length is left alone */
} PS_CLASS_RUN;

typedef struct PS_CLASS_ANSWER {
    NTSTATUS Status;
    ULONG Returned;
    BOOLEAN Decided;  /* the length or the class is refused: nothing of the process or thread is looked at */
    BOOLEAN Variable; /* the length depends on what is asked about: the class decides by itself when it is served */
} PS_CLASS_ANSWER;

#include "classref.inc"

/* a status that tells the length was not enough. */
static inline BOOLEAN PspClassShort(ULONG status)
{
    return status == (ULONG)STATUS_INFO_LENGTH_MISMATCH || status == (ULONG)STATUS_BUFFER_TOO_SMALL ||
           status == (ULONG)STATUS_BUFFER_OVERFLOW;
}

/*
 * the answer of a class at a length, FALSE for a class windows was not asked about. a length between two that were
 * asked takes the run of the one below it, unless that run is a single exact length: then it takes the next.
 */
static inline BOOLEAN PspClassAnswer(const PS_CLASS_RUN *runs, ULONG count, ULONG klass, ULONG length,
                                     PS_CLASS_ANSWER *answer)
{
    const PS_CLASS_RUN *first = nullptr, *last = nullptr, *run = nullptr;
    for (ULONG i = 0; i < count; i++) {
        if (runs[i].Klass != klass)
            continue;
        if (!first)
            first = &runs[i];
        last = &runs[i];
    }
    if (!first || klass > 0xFF)
        return FALSE;
    for (const PS_CLASS_RUN *r = first; r <= last; r++) {
        if (length > r->Last && r != last && length < r[1].First) {
            run = r->First == r->Last ? r + 1 : r;
            break;
        }
        if (length <= r->Last || r == last) {
            run = r;
            break;
        }
    }
    BOOLEAN short_with_length = FALSE;
    for (const PS_CLASS_RUN *r = first; r <= last; r++)
        if (PspClassShort(r->Status) && r->Returned != PS_CLASS_NO_LENGTH)
            short_with_length = TRUE;
    answer->Status = (NTSTATUS)run->Status;
    answer->Returned = run->Returned;
    answer->Variable = short_with_length && NT_SUCCESS((NTSTATUS)last->Status);
    /* refused: the one status of the class when it is not a success, or a length it does not take. */
    answer->Decided = !answer->Variable && ((first == last && !NT_SUCCESS(answer->Status)) || PspClassShort(run->Status));
    return TRUE;
}

/* the answer where this kernel does not serve the class: the status, the length, and zeroes for the data. */
static inline NTSTATUS PspClassDefault(const PS_CLASS_ANSWER *answer, PVOID info, ULONG length, PULONG returned)
{
    static const UCHAR zero[256];
    if (returned && answer->Returned != PS_CLASS_NO_LENGTH)
        KiCopyToUser(returned, &answer->Returned, sizeof(ULONG));
    if (NT_SUCCESS(answer->Status) && answer->Returned != PS_CLASS_NO_LENGTH) {
        ULONG bytes = answer->Returned < length ? answer->Returned : length;
        for (ULONG at = 0; at < bytes; at += sizeof(zero)) {
            NTSTATUS status = KiCopyToUser((PUCHAR)info + at, zero, bytes - at < sizeof(zero) ? bytes - at : sizeof(zero));
            if (!NT_SUCCESS(status))
                return status;
        }
    }
    return answer->Status;
}

#endif
