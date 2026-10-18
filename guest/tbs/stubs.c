/* the rest of the tpm base services, which find no tpm. */
#include <mime/stubexport.h>

MIME_STUB_RETURN("tbs", UINT, Tbsi_Create_Attestation_From_Log, 9, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsi_Get_TCG_Logs, 6, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, GetDeviceID, 4, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, GetDeviceIDString, 4, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, GetDeviceIDWithTimeout, 5, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsi_FilterLog, 6, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsi_Get_OwnerAuth, 4, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsi_ShaHash, 8, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsi_Tpm_Vendor_Maintenance_Mode, 2, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsip_TestInterruptInformation, 1, 0x8028400Fu)
MIME_STUB_RETURN("tbs", UINT, Tbsip_TestMorBit, 1, 0x8028400Fu)
