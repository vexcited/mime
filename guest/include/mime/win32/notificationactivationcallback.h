/* windows calls an app's COM server through INotificationActivationCallback when the user acts on its toasts. */
#ifndef MIME_WIN32_NOTIFICATIONACTIVATIONCALLBACK_H
#define MIME_WIN32_NOTIFICATIONACTIVATIONCALLBACK_H

#include <mime/win32/objbase.h>

typedef struct NOTIFICATION_USER_INPUT_DATA {
    LPCWSTR Key;
    LPCWSTR Value;
} NOTIFICATION_USER_INPUT_DATA;

typedef struct INotificationActivationCallback INotificationActivationCallback;
typedef struct INotificationActivationCallbackVtbl {
    MIME_IUNKNOWN_METHODS(INotificationActivationCallback)
    HRESULT(STDMETHODCALLTYPE *Activate)(INotificationActivationCallback *This, LPCWSTR app_user_model_id,
                                         LPCWSTR invoked_arguments, const NOTIFICATION_USER_INPUT_DATA *data,
                                         ULONG count);
} INotificationActivationCallbackVtbl;
struct INotificationActivationCallback {
    const INotificationActivationCallbackVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_INotificationActivationCallback, 0x53E31837, 0x6600, 0x4A81, 0x93, 0x95, 0x75, 0xCF, 0xFE, 0x74,
                 0x6F, 0x94);

#endif
