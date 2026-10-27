/* The words of an error for a person. */
#include "featurep.h"
#include <mime/win32/winhttp.h>
#include <mime/win32/wintrust.h>

void ErrorMessage(DWORD error, WCHAR *text, SIZE_T chars)
{
    PCWSTR own = nullptr;
    switch (error) {
    case ERROR_CANCELLED:
        own = L"The request was cancelled.";
        break;
    case FEATURE_ERROR_ACCESS_DISABLED_BY_POLICY:
        own = L"Turning this feature on is not allowed by a policy of this machine.";
        break;
    case ERROR_NOT_SUPPORTED:
        own = L"This feature cannot be installed on this machine yet.";
        break;
    case ERROR_FILE_TOO_LARGE:
        own = L"The download is larger than this feature can be.";
        break;
    case ERROR_DISK_FULL:
        own = L"There is not enough room on the drive for this feature.";
        break;
    case ERROR_ACCESS_DENIED:
        own = L"Mime could not get the files, the server or a redirect was not accepted.";
        break;
    case FEATURE_ERROR_INSTALL_FAILURE:
        own = L"The installer did not put the feature on the drive.";
        break;
    case ERROR_WINHTTP_TIMEOUT:
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
        own = L"Mime could not reach Microsoft. Check the network connection and try again.";
        break;
    case 12175: /* ERROR_WINHTTP_SECURE_FAILURE */
        own = L"The connection to Microsoft is not secure, the certificate of the server was refused.";
        break;
    case (DWORD)TRUST_E_NOSIGNATURE:
        own = L"The downloaded file has no signature, so it was not run.";
        break;
    case (DWORD)TRUST_E_BAD_DIGEST:
        own = L"The downloaded file was changed after it was signed, so it was not run.";
        break;
    case (DWORD)0x800B0004: /* TRUST_E_SUBJECT_NOT_TRUSTED */
        own = L"The downloaded file is not signed by the vendor of the feature, so it was not run.";
        break;
    case (DWORD)0x800B0109: /* CERT_E_UNTRUSTEDROOT */
        own = L"The signature of the downloaded file does not lead to a root of Microsoft, so the file was not run.";
        break;
    default:
        break;
    }
    if (own) {
        lstrcpynW(text, own, (int)chars);
        return;
    }
    TU_STR message = {};
    tu_error_text(error, &message);
    lstrcpynW(text, tu_str_text(&message), (int)chars);
    tu_str_free(&message);
}
