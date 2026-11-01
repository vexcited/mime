/*
 * Windows.Networking.Connectivity: the network information statics, connection profiles, costs and adapters. method
 * lists are macros so implementations and callers share the layouts.
 */
#ifndef MIME_WIN32_CONNECTIVITY_H
#define MIME_WIN32_CONNECTIVITY_H

#include <mime/win32/winrt_async.h>

typedef enum NetworkConnectivityLevel {
    NetworkConnectivityLevel_None = 0,
    NetworkConnectivityLevel_LocalAccess = 1,
    NetworkConnectivityLevel_ConstrainedInternetAccess = 2,
    NetworkConnectivityLevel_InternetAccess = 3,
} NetworkConnectivityLevel;

typedef enum NetworkCostType {
    NetworkCostType_Unknown = 0,
    NetworkCostType_Unrestricted = 1,
    NetworkCostType_Fixed = 2,
    NetworkCostType_Variable = 3,
} NetworkCostType;

typedef enum NetworkTypes {
    NetworkTypes_None = 0,
    NetworkTypes_Internet = 1,
    NetworkTypes_PrivateNetwork = 2,
} NetworkTypes;

typedef enum DomainConnectivityLevel {
    DomainConnectivityLevel_None = 0,
    DomainConnectivityLevel_Unauthenticated = 1,
    DomainConnectivityLevel_Authenticated = 2,
} DomainConnectivityLevel;

typedef struct NetworkUsageStates {
    INT32 Roaming, Shared;
} NetworkUsageStates;

typedef struct DataUsage {
    UINT64 BytesSent, BytesReceived;
} DataUsage;

MIME_DEFINE_GUID(IID_INetworkInformationStatics, 0x5074F851, 0x950D, 0x4165, 0x9C, 0x15, 0x36, 0x56, 0x19, 0x48, 0x1E,
                 0xEA);
MIME_DEFINE_GUID(IID_INetworkInformationStatics2, 0x459CED14, 0x2832, 0x49B6, 0xBA, 0x6E, 0xE2, 0x65, 0xF0, 0x47, 0x86,
                 0xA8);
MIME_DEFINE_GUID(IID_IConnectionProfile, 0x71BA143C, 0x598E, 0x49D0, 0x84, 0xEB, 0x8F, 0xEB, 0xAE, 0xDC, 0xC1, 0x95);
MIME_DEFINE_GUID(IID_IConnectionProfile2, 0xE2045145, 0x4C9F, 0x400C, 0x91, 0x50, 0x7E, 0xC7, 0xD6, 0xE2, 0x88, 0x8A);
MIME_DEFINE_GUID(IID_IConnectionProfile3, 0x578C2528, 0x4CD9, 0x4161, 0x80, 0x45, 0x20, 0x1C, 0xFD, 0x5B, 0x11, 0x5C);
MIME_DEFINE_GUID(IID_IConnectionProfile4, 0x7A2D42CD, 0x81E0, 0x4AE6, 0xAB, 0xED, 0xAB, 0x9C, 0xA1, 0x3E, 0xB7, 0x14);
MIME_DEFINE_GUID(IID_IConnectionProfile5, 0x85361EC7, 0x9C73, 0x4BE0, 0x8F, 0x14, 0x57, 0x8E, 0xEC, 0x71, 0xEE, 0x0E);
MIME_DEFINE_GUID(IID_IConnectionProfile6, 0xDC27DFE2, 0x7A6F, 0x5D0E, 0x95, 0x89, 0x2F, 0xE2, 0xE5, 0xB6, 0xF9, 0xAA);
MIME_DEFINE_GUID(IID_IConnectionCost, 0xBAD7D829, 0x3416, 0x4B10, 0xA2, 0x02, 0xBA, 0xC0, 0xB0, 0x75, 0xBD, 0xAE);
MIME_DEFINE_GUID(IID_IConnectionCost2, 0x8E113A05, 0xE209, 0x4549, 0xBB, 0x25, 0x5E, 0x0D, 0xB6, 0x91, 0xCB, 0x05);
MIME_DEFINE_GUID(IID_INetworkAdapter, 0x3B542E03, 0x5388, 0x496C, 0xA8, 0xA3, 0xAF, 0xFD, 0x39, 0xAE, 0xC2, 0xE6);
MIME_DEFINE_GUID(IID_INetworkItem, 0x01BC4D39, 0xF5E0, 0x4567, 0xA2, 0x8C, 0x42, 0x08, 0x0C, 0x83, 0x1B, 0x2B);
MIME_DEFINE_GUID(IID_INetworkStatusChangedEventHandler, 0x71BA143F, 0x598E, 0x49D0, 0x84, 0xEB, 0x8F, 0xEB, 0xAE, 0xDC,
                 0xC1, 0x95);

typedef struct INetworkStatusChangedEventHandler INetworkStatusChangedEventHandler;
#define INetworkStatusChangedEventHandler_METHODS(T)                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, IInspectable * sender);
MIME_COM_INTERFACE(INetworkStatusChangedEventHandler)

typedef struct IConnectionCost IConnectionCost;
#define IConnectionCost_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_NetworkCostType)(T * This, NetworkCostType * value);                                \
    HRESULT(STDMETHODCALLTYPE *get_Roaming)(T * This, BOOLEAN * value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_OverDataLimit)(T * This, BOOLEAN * value);                                          \
    HRESULT(STDMETHODCALLTYPE *get_ApproachingDataLimit)(T * This, BOOLEAN * value);
MIME_COM_INTERFACE(IConnectionCost)

typedef struct IConnectionCost2 IConnectionCost2;
#define IConnectionCost2_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_BackgroundDataUsageRestricted)(T * This, BOOLEAN * value);
MIME_COM_INTERFACE(IConnectionCost2)

typedef struct INetworkItem INetworkItem;
#define INetworkItem_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_NetworkId)(T * This, GUID * value);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetNetworkTypes)(T * This, NetworkTypes * value);
MIME_COM_INTERFACE(INetworkItem)

typedef struct INetworkAdapter INetworkAdapter;
#define INetworkAdapter_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_OutboundMaxBitsPerSecond)(T * This, UINT64 * value);                                \
    HRESULT(STDMETHODCALLTYPE *get_InboundMaxBitsPerSecond)(T * This, UINT64 * value);                                 \
    HRESULT(STDMETHODCALLTYPE *get_IanaInterfaceType)(T * This, UINT32 * value);                                       \
    HRESULT(STDMETHODCALLTYPE *get_NetworkItem)(T * This, INetworkItem * *value);                                      \
    HRESULT(STDMETHODCALLTYPE *get_NetworkAdapterId)(T * This, GUID * value);                                          \
    HRESULT(STDMETHODCALLTYPE *GetConnectedProfileAsync)(T * This, IAsyncOperation * *operation);
MIME_COM_INTERFACE(INetworkAdapter)

typedef struct IConnectionProfile IConnectionProfile;
#define IConnectionProfile_METHODS(T)                                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_ProfileName)(T * This, HSTRING * value);                                            \
    HRESULT(STDMETHODCALLTYPE *GetNetworkConnectivityLevel)(T * This, NetworkConnectivityLevel * value);               \
    HRESULT(STDMETHODCALLTYPE *GetNetworkNames)(T * This, IVectorViewPtr * *value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetConnectionCost)(T * This, IConnectionCost * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *GetDataPlanStatus)(T * This, IInspectable * *value);                                    \
    HRESULT(STDMETHODCALLTYPE *get_NetworkAdapter)(T * This, INetworkAdapter * *value);                                \
    HRESULT(STDMETHODCALLTYPE *GetLocalUsage)(T * This, DateTime start, DateTime end, DataUsage * value);              \
    HRESULT(STDMETHODCALLTYPE *GetLocalUsagePerRoamingStates)(T * This, DateTime start, DateTime end,                  \
                                                              NetworkUsageStates states, DataUsage * value);           \
    HRESULT(STDMETHODCALLTYPE *get_NetworkSecuritySettings)(T * This, IInspectable * *value);
MIME_COM_INTERFACE(IConnectionProfile)

typedef struct IConnectionProfile2 IConnectionProfile2;
#define IConnectionProfile2_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_IsWwanConnectionProfile)(T * This, BOOLEAN * value);                                \
    HRESULT(STDMETHODCALLTYPE *get_IsWlanConnectionProfile)(T * This, BOOLEAN * value);                                \
    HRESULT(STDMETHODCALLTYPE *get_WwanConnectionProfileDetails)(T * This, IInspectable * *value);                     \
    HRESULT(STDMETHODCALLTYPE *get_WlanConnectionProfileDetails)(T * This, IInspectable * *value);                     \
    HRESULT(STDMETHODCALLTYPE *get_ServiceProviderGuid)(T * This, IInspectable * *value);                              \
    HRESULT(STDMETHODCALLTYPE *GetSignalBars)(T * This, IInspectable * *value);                                        \
    HRESULT(STDMETHODCALLTYPE *GetDomainConnectivityLevel)(T * This, DomainConnectivityLevel * value);                 \
    HRESULT(STDMETHODCALLTYPE *GetNetworkUsageAsync)(T * This, DateTime start, DateTime end, INT32 granularity,        \
                                                     NetworkUsageStates states, IAsyncOperation * *operation);         \
    HRESULT(STDMETHODCALLTYPE *GetConnectivityIntervalsAsync)(T * This, DateTime start, DateTime end,                  \
                                                              NetworkUsageStates states, IAsyncOperation * *operation);
MIME_COM_INTERFACE(IConnectionProfile2)

typedef struct IConnectionProfile3 IConnectionProfile3;
#define IConnectionProfile3_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetAttributedNetworkUsageAsync)(T * This, DateTime start, DateTime end,                 \
                                                               NetworkUsageStates states,                              \
                                                               IAsyncOperation * *operation);
MIME_COM_INTERFACE(IConnectionProfile3)

typedef struct IConnectionProfile4 IConnectionProfile4;
#define IConnectionProfile4_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetProviderNetworkUsageAsync)(T * This, DateTime start, DateTime end,                   \
                                                             NetworkUsageStates states, IAsyncOperation * *operation);
MIME_COM_INTERFACE(IConnectionProfile4)

typedef struct IConnectionProfile5 IConnectionProfile5;
#define IConnectionProfile5_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_CanDelete)(T * This, BOOLEAN * value);                                              \
    HRESULT(STDMETHODCALLTYPE *TryDeleteAsync)(T * This, IAsyncOperation * *operation);
MIME_COM_INTERFACE(IConnectionProfile5)

typedef struct IConnectionProfile6 IConnectionProfile6;
#define IConnectionProfile6_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *IsDomainAuthenticatedBy)(T * This, INT32 kind, BOOLEAN * value);
MIME_COM_INTERFACE(IConnectionProfile6)

typedef struct INetworkInformationStatics INetworkInformationStatics;
#define INetworkInformationStatics_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetConnectionProfiles)(T * This, IVectorViewPtr * *value);                              \
    HRESULT(STDMETHODCALLTYPE *GetInternetConnectionProfile)(T * This, IConnectionProfile * *value);                   \
    HRESULT(STDMETHODCALLTYPE *GetLanIdentifiers)(T * This, IVectorViewPtr * *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetHostNames)(T * This, IVectorViewPtr * *value);                                       \
    HRESULT(STDMETHODCALLTYPE *GetProxyConfigurationAsync)(T * This, IInspectable * uri,                               \
                                                           IAsyncOperation * *operation);                              \
    HRESULT(STDMETHODCALLTYPE *GetSortedEndpointPairs)(T * This, IInspectable * pairs, INT32 options,                  \
                                                       IVectorViewPtr * *value);                                       \
    HRESULT(STDMETHODCALLTYPE *add_NetworkStatusChanged)(T * This, INetworkStatusChangedEventHandler * handler,        \
                                                         EventRegistrationToken * token);                              \
    HRESULT(STDMETHODCALLTYPE *remove_NetworkStatusChanged)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(INetworkInformationStatics)

typedef struct INetworkInformationStatics2 INetworkInformationStatics2;
#define INetworkInformationStatics2_METHODS(T)                                                                         \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *FindConnectionProfilesAsync)(T * This, IInspectable * filter,                           \
                                                            IAsyncOperation * *operation);
MIME_COM_INTERFACE(INetworkInformationStatics2)

#endif
