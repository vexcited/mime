/* the Windows Runtime streams over IStream, which are not here yet. */
#include <mime/stubexport.h>

MIME_STUB_RETURN("shcore", HRESULT, CreateRandomAccessStreamOverStream, 4, E_NOTIMPL)
MIME_STUB_RETURN("shcore", HRESULT, CreateStreamOverRandomAccessStream, 3, E_NOTIMPL)
