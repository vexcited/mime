/* unimplemented exports, failing as Windows does without the feature. */
#include <mime/stubexport.h>

MIME_STUB_VOID("ndfapi", NdfRunDllDiagnoseIncident, 4)
MIME_STUB_VOID("ndfapi", NdfRunDllDiagnoseNetConnectionIncident, 4)
MIME_STUB_VOID("ndfapi", NdfRunDllDiagnoseWithAnswerFile, 4)
MIME_STUB_VOID("ndfapi", NdfRunDllDuplicateIPDefendingSystem, 4)
MIME_STUB_VOID("ndfapi", NdfRunDllDuplicateIPOffendingSystem, 4)
MIME_STUB_VOID("ndfapi", NdfRunDllHelpTopic, 4)
MIME_STUB_RETURN("ndfapi", HRESULT, DllCanUnloadNow, 0, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, DllGetClassObject, 3, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, DllRegisterServer, 0, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, DllUnregisterServer, 0, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, NdfCreateGroupingIncident, 7, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, NdfCreateInboundIncident, 7, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, NdfCreatePnrpIncident, 5, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, NdfCreateWinSockIncident, 6, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, NdfRepairIncident, 3, E_NOTIMPL)
MIME_STUB_RETURN("ndfapi", HRESULT, NdfRepairIncidentEx, 4, E_NOTIMPL)
