/* the Windows Runtime stream calls, which need Windows.Storage.Streams that is not here. */
#include <mime/stubexport.h>

MIME_STUB_RETURN("shcore", HRESULT, CreateRandomAccessStreamOnFile, 4, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, CreateRandomAccessStreamOverStream, 4, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, CreateStreamOverRandomAccessStream, 3, E_NOTIMPL)
