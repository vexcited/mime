/* the primitive tables of the cng providers, which mime does not have: unknown algorithms everywhere. */
#include <mime/stubexport.h>

MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetAsymmetricEncryptionInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetCipherInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetHashInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetKeyDerivationInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetKeyEncapsulationInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetRngInterface, 3, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetSecretAgreementInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, GetSignatureInterface, 4, STATUS_NOT_SUPPORTED)
MIME_STUB_RETURN("bcryptprimitives", NTSTATUS, MSCryptConvertRsaPrivateBlobToFullRsaBlob, 5, STATUS_NOT_SUPPORTED)
