/* what the kernel gives the generator of activation contexts, in types both sides of it have. */
#ifndef MIME_NTOS_SXS_SXSK_H
#define MIME_NTOS_SXS_SXSK_H

PVOID SxsKAlloc(SIZE_T size, ULONG flags);
void SxsKFree(PVOID p);
NTSTATUS SxsKReadFile(PCWSTR path, PVOID *data, SIZE_T *size);
SIZE_T SxsKFileSize(const void *data);
NTSTATUS SxsKRootResource(ULONG_PTR name, const void **data, SIZE_T *size);
PCWSTR SxsKSystemDirectory(void);
PCWSTR SxsKProcessArch(void);
NTSTATUS SxsKDataAlloc(PVOID *base, SIZE_T *size);
void SxsKDataFree(PVOID base);
NTSTATUS NTAPI SxsKOpenKey(PHANDLE key, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTSTATUS NTAPI SxsKQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length, PULONG returned);
NTSTATUS NTAPI SxsKClose(HANDLE handle);

/* an image as its file holds it, or as it is mapped. */
typedef struct SXS_IMAGE {
    const UCHAR *Base;
    SIZE_T Size;
    BOOLEAN File;
} SXS_IMAGE;

NTSTATUS SxsKFindManifest(const SXS_IMAGE *image, ULONG_PTR name, const void **data, SIZE_T *size);

#endif
