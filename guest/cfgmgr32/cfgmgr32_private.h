#ifndef MIME_CFGMGR32_PRIVATE_H
#define MIME_CFGMGR32_PRIVATE_H

#define ROOT_ID L"HTREE\\ROOT\\0"

DEVINST node_of(PCWSTR id);
PCWSTR id_of(DEVINST devinst);
BOOL is_root(PCWSTR id);
NTSTATUS request(MIME_PNP_CLASS klass, ULONG flags, const GUID *guid, PCWSTR in, MIME_PNP_REQUEST **out);
BOOL present(PCWSTR id);
HKEY open_enum(PCWSTR id, REGSAM access);
BOOL key_is(const DEVPROPKEY *a, const DEVPROPKEY *b);
CONFIGRET put(DEVPROPTYPE kind, const void *data, ULONG size, DEVPROPTYPE *type, PBYTE buffer, PULONG length);
BOOL parse_guid(PCWSTR text, GUID *out);

CONFIGRET InterfaceOwnProperty(PCWSTR path, const DEVPROPKEY *key, DEVPROPTYPE *type, PBYTE buffer, PULONG size,
                               BOOL *handled);

#endif
