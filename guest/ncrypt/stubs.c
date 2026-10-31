/* the data protection of Windows, which needs the services behind it. */
#include <mime/stubexport.h>

MIME_STUB_RETURN("ncrypt", LONG, NCryptCreateProtectionDescriptor, 3, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptGetProtectionDescriptorInfo, 4, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptOpenKeyProtector, 3, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptProtectKey, 8, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptProtectSecret, 8, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptQueryProtectionDescriptorName, 4, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptRegisterProtectionDescriptorName, 3, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptStreamOpenToProtect, 5, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptStreamOpenToUnprotect, 4, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptStreamOpenToUnprotectEx, 4, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptTranslateHandle, 6, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptUnprotectKey, 7, (LONG)0x80090029)
MIME_STUB_RETURN("ncrypt", LONG, NCryptUnprotectSecret, 8, (LONG)0x80090029)
