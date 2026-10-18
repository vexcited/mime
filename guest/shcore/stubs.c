/* the exports mime does not implement, failing as windows does without the feature. */
#include <mime/stubexport.h>

MIME_STUB_RETURN("shcore", HRESULT, CreateRandomAccessStreamOnFile, 4, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, CreateRandomAccessStreamOverStream, 4, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, CreateStreamOverRandomAccessStream, 3, E_NOTIMPL)
MIME_STUB_VOID("shcore", RecordFeatureError, 2)
MIME_STUB_VOID("shcore", RecordFeatureUsage, 4)
MIME_STUB_RETURN("shcore", HRESULT, SHTaskPoolAllowThreadReuse, 0, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, SHTaskPoolDoNotWaitForMoreTasks, 0, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, SHTaskPoolGetCurrentThreadLifetime, 1, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, SHTaskPoolGetUniqueContext, 0, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, SHTaskPoolQueueTask, 6, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, SHTaskPoolSetThreadReuseAllowed, 1, E_NOTIMPL)
MIME_STUB_VOID("shcore", UnsubscribeFeatureStateChangeNotification, 1)
