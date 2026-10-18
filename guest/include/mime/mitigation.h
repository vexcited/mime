/*
 * process mitigation options, shared by the kernel and kernelbase.
 *
 * a program gives CreateProcess two words of PROCESS_CREATION_MITIGATION_POLICY flags, each field two bits
 * wide at a multiple of four. kernelbase repacks them into the three words the kernel takes as the
 * mitigation options attribute of NtCreateUserProcess, and the kernel unpacks them again into the flag
 * word of each PROCESS_MITIGATION_POLICY class.
 */
#ifndef MIME_MITIGATION_H
#define MIME_MITIGATION_H

#include <mime/nt.h>

/* a field of a word of creation flags: where it is in the program's two words and in the kernel's three. */
typedef struct MIME_MITIGATION_FIELD {
    UCHAR Word;        /* 0 or 1 */
    UCHAR Shift;
    UCHAR KernelWord;  /* 0 to 2 */
    UCHAR KernelShift;
    UCHAR AllowThree;  /* the field takes the value 3 */
} MIME_MITIGATION_FIELD;

/* the fields from bit 8 of the first word on, DEP and SEHOP aside. */
#define MIME_MITIGATION_FIELDS \
    {0, 8, 0, 8, 1},   /* force relocate images, 3 is always on requiring relocations */ \
    {0, 12, 0, 12, 0}, /* heap terminate on corruption */ \
    {0, 16, 0, 16, 0}, /* bottom up aslr */ \
    {0, 20, 0, 20, 0}, /* high entropy aslr */ \
    {0, 24, 0, 24, 0}, /* strict handle checks */ \
    {0, 28, 0, 28, 0}, /* win32k system call disable */ \
    {0, 32, 0, 32, 0}, /* extension point disable */ \
    {0, 36, 0, 36, 1}, /* prohibit dynamic code, 3 is with opt out */ \
    {0, 40, 0, 40, 1}, /* control flow guard */ \
    {0, 44, 0, 44, 1}, /* block non microsoft binaries */ \
    {0, 48, 0, 48, 0}, /* font disable */ \
    {0, 52, 0, 52, 0}, /* image load no remote */ \
    {0, 56, 0, 56, 0}, /* image load no low label */ \
    {0, 60, 0, 60, 0}, /* image load prefer system32 */ \
    {1, 4, 1, 4, 1},   /* loader integrity continuity */ \
    {1, 8, 1, 8, 0},   /* strict control flow guard */ \
    {1, 12, 1, 44, 1}, /* module tampering protection */ \
    {1, 16, 1, 48, 0}, /* restrict indirect branch prediction */ \
    {1, 20, 1, 56, 0}, /* allow downgrade of dynamic code policy */ \
    {1, 24, 1, 52, 0}, /* speculative store bypass disable */ \
    {1, 28, 1, 60, 1}, /* cet user shadow stacks */ \
    {1, 32, 2, 0, 1},  /* cet set context ip validation */ \
    {1, 36, 2, 4, 1},  /* block non cet binaries */ \
    {1, 44, 2, 12, 0}, \
    {1, 48, 2, 16, 0}, /* cet dynamic apis out of proc only */ \
    {1, 52, 2, 24, 0}, \
    {1, 56, 2, 28, 0}  /* fsctl system call disable */

/* the bits no flag may use: the second bit of a field's pair and the gaps. */
#define MIME_MITIGATION_INVALID0 0xCCCCCCCCCCCCCCF8ull
#define MIME_MITIGATION_INVALID1 0xFCCCCCCCCCCCCCCFull

/* the policy classes of PROCESS_MITIGATION_POLICY. */
enum {
    MIME_POLICY_DEP,
    MIME_POLICY_ASLR,
    MIME_POLICY_DYNAMIC_CODE,
    MIME_POLICY_STRICT_HANDLE_CHECK,
    MIME_POLICY_SYSTEM_CALL_DISABLE,
    MIME_POLICY_OPTIONS_MASK,
    MIME_POLICY_EXTENSION_POINT_DISABLE,
    MIME_POLICY_CONTROL_FLOW_GUARD,
    MIME_POLICY_SIGNATURE,
    MIME_POLICY_FONT_DISABLE,
    MIME_POLICY_IMAGE_LOAD,
    MIME_POLICY_SYSTEM_CALL_FILTER,
    MIME_POLICY_PAYLOAD_RESTRICTION,
    MIME_POLICY_CHILD_PROCESS,
    MIME_POLICY_SIDE_CHANNEL_ISOLATION,
    MIME_POLICY_USER_SHADOW_STACK,
    MIME_POLICY_REDIRECTION_TRUST,
    MIME_POLICY_USER_POINTER_AUTH,
    MIME_POLICY_SEHOP,
    MIME_POLICY_COUNT
};

/* the values of PROCESS_CREATION_CHILD_PROCESS_POLICY. */
#define MIME_CHILD_PROCESS_RESTRICTED 0x1
#define MIME_CHILD_PROCESS_OVERRIDE 0x2
#define MIME_CHILD_PROCESS_RESTRICTED_UNLESS_SECURE 0x4

/* the flag words of the policies. */
#define MIME_ASLR_BOTTOM_UP 0x1
#define MIME_ASLR_FORCE_RELOCATE 0x2
#define MIME_ASLR_HIGH_ENTROPY 0x4
#define MIME_ASLR_DISALLOW_STRIPPED 0x8
#define MIME_DYNAMIC_CODE_PROHIBIT 0x1
#define MIME_DYNAMIC_CODE_THREAD_OPT_OUT 0x2
#define MIME_DYNAMIC_CODE_REMOTE_DOWNGRADE 0x4
#define MIME_DYNAMIC_CODE_AUDIT 0x8
#define MIME_STRICT_HANDLE_RAISE 0x1
#define MIME_STRICT_HANDLE_PERMANENT 0x2
#define MIME_SYSTEM_CALL_DISABLE_WIN32K 0x1
#define MIME_SYSTEM_CALL_DISABLE_AUDIT 0x2
#define MIME_EXTENSION_POINT_DISABLE 0x1
#define MIME_CFG_ENABLE 0x1
#define MIME_CFG_EXPORT_SUPPRESSION 0x2
#define MIME_CFG_STRICT 0x4
#define MIME_SIGNATURE_MICROSOFT_ONLY 0x1
#define MIME_SIGNATURE_STORE_ONLY 0x2
#define MIME_SIGNATURE_OPT_IN 0x4
#define MIME_FONT_DISABLE_NONSYSTEM 0x1
#define MIME_FONT_AUDIT 0x2
#define MIME_IMAGE_LOAD_NO_REMOTE 0x1
#define MIME_IMAGE_LOAD_NO_LOW_LABEL 0x2
#define MIME_IMAGE_LOAD_PREFER_SYSTEM32 0x4
#define MIME_CHILD_PROCESS_NO_CREATION 0x1
#define MIME_CHILD_PROCESS_AUDIT 0x2
#define MIME_CHILD_PROCESS_ALLOW_SECURE 0x4
#define MIME_SIDE_CHANNEL_SMT_BRANCH 0x1
#define MIME_SIDE_CHANNEL_SPECULATIVE_STORE 0x8

/* a thread's own dynamic code policy, ThreadDynamicCodePolicyInfo. */
#define MIME_THREAD_DYNAMIC_CODE_ALLOW 1

#endif
