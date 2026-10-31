/* wbemcli.h: the windows management instrumentation interfaces that clients and providers use. */
#ifndef MIME_WIN32_WBEMCLI_H
#define MIME_WIN32_WBEMCLI_H

#include <mime/win32/oleauto.h>

/* ------------------------------------------------------------------ status codes */

#define WBEM_S_NO_ERROR 0
#define WBEM_S_FALSE 1
#define WBEM_S_ALREADY_EXISTS 0x40001
#define WBEM_S_RESET_TO_DEFAULT 0x40002
#define WBEM_S_DIFFERENT 0x40003
#define WBEM_S_TIMEDOUT 0x40004
#define WBEM_S_NO_MORE_DATA 0x40005
#define WBEM_S_OPERATION_CANCELLED 0x40006
#define WBEM_S_PENDING 0x40007
#define WBEM_S_DUPLICATE_OBJECTS 0x40008
#define WBEM_S_ACCESS_DENIED 0x40009
#define WBEM_S_PARTIAL_RESULTS 0x40010
#define WBEM_S_SOURCE_NOT_AVAILABLE 0x40017

#define WBEM_E_FAILED ((HRESULT)0x80041001L)
#define WBEM_E_NOT_FOUND ((HRESULT)0x80041002L)
#define WBEM_E_ACCESS_DENIED ((HRESULT)0x80041003L)
#define WBEM_E_PROVIDER_FAILURE ((HRESULT)0x80041004L)
#define WBEM_E_TYPE_MISMATCH ((HRESULT)0x80041005L)
#define WBEM_E_OUT_OF_MEMORY ((HRESULT)0x80041006L)
#define WBEM_E_INVALID_CONTEXT ((HRESULT)0x80041007L)
#define WBEM_E_INVALID_PARAMETER ((HRESULT)0x80041008L)
#define WBEM_E_NOT_AVAILABLE ((HRESULT)0x80041009L)
#define WBEM_E_CRITICAL_ERROR ((HRESULT)0x8004100AL)
#define WBEM_E_INVALID_STREAM ((HRESULT)0x8004100BL)
#define WBEM_E_NOT_SUPPORTED ((HRESULT)0x8004100CL)
#define WBEM_E_INVALID_SUPERCLASS ((HRESULT)0x8004100DL)
#define WBEM_E_INVALID_NAMESPACE ((HRESULT)0x8004100EL)
#define WBEM_E_INVALID_OBJECT ((HRESULT)0x8004100FL)
#define WBEM_E_INVALID_CLASS ((HRESULT)0x80041010L)
#define WBEM_E_PROVIDER_NOT_FOUND ((HRESULT)0x80041011L)
#define WBEM_E_INVALID_PROVIDER_REGISTRATION ((HRESULT)0x80041012L)
#define WBEM_E_PROVIDER_LOAD_FAILURE ((HRESULT)0x80041013L)
#define WBEM_E_INITIALIZATION_FAILURE ((HRESULT)0x80041014L)
#define WBEM_E_TRANSPORT_FAILURE ((HRESULT)0x80041015L)
#define WBEM_E_INVALID_OPERATION ((HRESULT)0x80041016L)
#define WBEM_E_INVALID_QUERY ((HRESULT)0x80041017L)
#define WBEM_E_INVALID_QUERY_TYPE ((HRESULT)0x80041018L)
#define WBEM_E_ALREADY_EXISTS ((HRESULT)0x80041019L)
#define WBEM_E_OVERRIDE_NOT_ALLOWED ((HRESULT)0x8004101AL)
#define WBEM_E_PROPAGATED_QUALIFIER ((HRESULT)0x8004101BL)
#define WBEM_E_PROPAGATED_PROPERTY ((HRESULT)0x8004101CL)
#define WBEM_E_UNEXPECTED ((HRESULT)0x8004101DL)
#define WBEM_E_ILLEGAL_OPERATION ((HRESULT)0x8004101EL)
#define WBEM_E_CANNOT_BE_KEY ((HRESULT)0x8004101FL)
#define WBEM_E_INCOMPLETE_CLASS ((HRESULT)0x80041020L)
#define WBEM_E_INVALID_SYNTAX ((HRESULT)0x80041021L)
#define WBEM_E_NONDECORATED_OBJECT ((HRESULT)0x80041022L)
#define WBEM_E_READ_ONLY ((HRESULT)0x80041023L)
#define WBEM_E_PROVIDER_NOT_CAPABLE ((HRESULT)0x80041024L)
#define WBEM_E_CLASS_HAS_CHILDREN ((HRESULT)0x80041025L)
#define WBEM_E_CLASS_HAS_INSTANCES ((HRESULT)0x80041026L)
#define WBEM_E_QUERY_NOT_IMPLEMENTED ((HRESULT)0x80041027L)
#define WBEM_E_ILLEGAL_NULL ((HRESULT)0x80041028L)
#define WBEM_E_INVALID_QUALIFIER_TYPE ((HRESULT)0x80041029L)
#define WBEM_E_INVALID_PROPERTY_TYPE ((HRESULT)0x8004102AL)
#define WBEM_E_VALUE_OUT_OF_RANGE ((HRESULT)0x8004102BL)
#define WBEM_E_CANNOT_BE_SINGLETON ((HRESULT)0x8004102CL)
#define WBEM_E_INVALID_CIM_TYPE ((HRESULT)0x8004102DL)
#define WBEM_E_INVALID_METHOD ((HRESULT)0x8004102EL)
#define WBEM_E_INVALID_METHOD_PARAMETERS ((HRESULT)0x8004102FL)
#define WBEM_E_SYSTEM_PROPERTY ((HRESULT)0x80041030L)
#define WBEM_E_INVALID_PROPERTY ((HRESULT)0x80041031L)
#define WBEM_E_CALL_CANCELLED ((HRESULT)0x80041032L)
#define WBEM_E_SHUTTING_DOWN ((HRESULT)0x80041033L)
#define WBEM_E_PROPAGATED_METHOD ((HRESULT)0x80041034L)
#define WBEM_E_UNSUPPORTED_PARAMETER ((HRESULT)0x80041035L)
#define WBEM_E_MISSING_PARAMETER_ID ((HRESULT)0x80041036L)
#define WBEM_E_INVALID_PARAMETER_ID ((HRESULT)0x80041037L)
#define WBEM_E_NONCONSECUTIVE_PARAMETER_IDS ((HRESULT)0x80041038L)
#define WBEM_E_PARAMETER_ID_ON_RETVAL ((HRESULT)0x80041039L)
#define WBEM_E_INVALID_OBJECT_PATH ((HRESULT)0x8004103AL)
#define WBEM_E_OUT_OF_DISK_SPACE ((HRESULT)0x8004103BL)
#define WBEM_E_BUFFER_TOO_SMALL ((HRESULT)0x8004103CL)
#define WBEM_E_UNSUPPORTED_PUT_EXTENSION ((HRESULT)0x8004103DL)
#define WBEM_E_UNKNOWN_OBJECT_TYPE ((HRESULT)0x8004103EL)
#define WBEM_E_UNKNOWN_PACKET_TYPE ((HRESULT)0x8004103FL)
#define WBEM_E_MARSHAL_VERSION_MISMATCH ((HRESULT)0x80041040L)
#define WBEM_E_MARSHAL_INVALID_SIGNATURE ((HRESULT)0x80041041L)
#define WBEM_E_INVALID_QUALIFIER ((HRESULT)0x80041042L)
#define WBEM_E_INVALID_DUPLICATE_PARAMETER ((HRESULT)0x80041043L)
#define WBEM_E_TOO_MUCH_DATA ((HRESULT)0x80041044L)
#define WBEM_E_SERVER_TOO_BUSY ((HRESULT)0x80041045L)
#define WBEM_E_INVALID_FLAVOR ((HRESULT)0x80041046L)
#define WBEM_E_CIRCULAR_REFERENCE ((HRESULT)0x80041047L)
#define WBEM_E_UNSUPPORTED_CLASS_UPDATE ((HRESULT)0x80041048L)
#define WBEM_E_CANNOT_CHANGE_KEY_INHERITANCE ((HRESULT)0x80041049L)
#define WBEM_E_CANNOT_CHANGE_INDEX_INHERITANCE ((HRESULT)0x80041050L)
#define WBEM_E_TOO_MANY_PROPERTIES ((HRESULT)0x80041051L)
#define WBEM_E_UPDATE_TYPE_MISMATCH ((HRESULT)0x80041052L)
#define WBEM_E_UPDATE_OVERRIDE_NOT_ALLOWED ((HRESULT)0x80041053L)
#define WBEM_E_UPDATE_PROPAGATED_METHOD ((HRESULT)0x80041054L)
#define WBEM_E_METHOD_NOT_IMPLEMENTED ((HRESULT)0x80041055L)
#define WBEM_E_METHOD_DISABLED ((HRESULT)0x80041056L)
#define WBEM_E_REFRESHER_BUSY ((HRESULT)0x80041057L)
#define WBEM_E_UNPARSABLE_QUERY ((HRESULT)0x80041058L)
#define WBEM_E_NOT_EVENT_CLASS ((HRESULT)0x80041059L)
#define WBEM_E_MISSING_GROUP_WITHIN ((HRESULT)0x8004105AL)
#define WBEM_E_MISSING_AGGREGATION_LIST ((HRESULT)0x8004105BL)
#define WBEM_E_PROPERTY_NOT_AN_OBJECT ((HRESULT)0x8004105CL)
#define WBEM_E_AGGREGATING_BY_OBJECT ((HRESULT)0x8004105DL)
#define WBEM_E_UNINTERPRETABLE_PROVIDER_QUERY ((HRESULT)0x8004105FL)
#define WBEM_E_BACKUP_RESTORE_WINMGMT_RUNNING ((HRESULT)0x80041060L)
#define WBEM_E_QUEUE_OVERFLOW ((HRESULT)0x80041061L)
#define WBEM_E_PRIVILEGE_NOT_HELD ((HRESULT)0x80041062L)
#define WBEM_E_INVALID_OPERATOR ((HRESULT)0x80041063L)
#define WBEM_E_LOCAL_CREDENTIALS ((HRESULT)0x80041064L)
#define WBEM_E_CANNOT_BE_ABSTRACT ((HRESULT)0x80041065L)
#define WBEM_E_AMENDED_OBJECT ((HRESULT)0x80041066L)
#define WBEM_E_CLIENT_TOO_SLOW ((HRESULT)0x80041067L)
#define WBEM_E_NULL_SECURITY_DESCRIPTOR ((HRESULT)0x80041068L)
#define WBEM_E_TIMED_OUT ((HRESULT)0x80041069L)
#define WBEM_E_INVALID_ASSOCIATION ((HRESULT)0x8004106AL)
#define WBEM_E_AMBIGUOUS_OPERATION ((HRESULT)0x8004106BL)
#define WBEM_E_QUOTA_VIOLATION ((HRESULT)0x8004106CL)
#define WBEM_E_RESERVED_001 ((HRESULT)0x8004106DL)
#define WBEM_E_RESERVED_002 ((HRESULT)0x8004106EL)
#define WBEM_E_UNSUPPORTED_LOCALE ((HRESULT)0x8004106FL)
#define WBEM_E_HANDLE_OUT_OF_DATE ((HRESULT)0x80041070L)
#define WBEM_E_CONNECTION_FAILED ((HRESULT)0x80041071L)
#define WBEM_E_INVALID_HANDLE_REQUEST ((HRESULT)0x80041072L)
#define WBEM_E_PROPERTY_NAME_TOO_WIDE ((HRESULT)0x80041073L)
#define WBEM_E_CLASS_NAME_TOO_WIDE ((HRESULT)0x80041074L)
#define WBEM_E_METHOD_NAME_TOO_WIDE ((HRESULT)0x80041075L)
#define WBEM_E_QUALIFIER_NAME_TOO_WIDE ((HRESULT)0x80041076L)
#define WBEM_E_RERUN_COMMAND ((HRESULT)0x80041077L)
#define WBEM_E_DATABASE_VER_MISMATCH ((HRESULT)0x80041078L)
#define WBEM_E_VETO_DELETE ((HRESULT)0x80041079L)
#define WBEM_E_VETO_PUT ((HRESULT)0x8004107AL)
#define WBEM_E_INVALID_LOCALE ((HRESULT)0x80041080L)
#define WBEM_E_PROVIDER_SUSPENDED ((HRESULT)0x80041081L)
#define WBEM_E_SYNCHRONIZATION_REQUIRED ((HRESULT)0x80041082L)
#define WBEM_E_NO_SCHEMA ((HRESULT)0x80041083L)
#define WBEM_E_PROVIDER_ALREADY_REGISTERED ((HRESULT)0x80041084L)
#define WBEM_E_PROVIDER_NOT_REGISTERED ((HRESULT)0x80041085L)
#define WBEM_E_FATAL_TRANSPORT_ERROR ((HRESULT)0x80041086L)
#define WBEM_E_ENCRYPTED_CONNECTION_REQUIRED ((HRESULT)0x80041087L)

/* ------------------------------------------------------------------ flags and enumerations */

#define WBEM_FLAG_RETURN_IMMEDIATELY 0x10
#define WBEM_FLAG_RETURN_WBEM_COMPLETE 0
#define WBEM_FLAG_BIDIRECTIONAL 0
#define WBEM_FLAG_FORWARD_ONLY 0x20
#define WBEM_FLAG_NO_ERROR_OBJECT 0x40
#define WBEM_FLAG_RETURN_ERROR_OBJECT 0
#define WBEM_FLAG_SEND_STATUS 0x80
#define WBEM_FLAG_DONT_SEND_STATUS 0
#define WBEM_FLAG_ENSURE_LOCATABLE 0x100
#define WBEM_FLAG_DIRECT_READ 0x200
#define WBEM_FLAG_SEND_ONLY_SELECTED 0
#define WBEM_RETURN_WHEN_COMPLETE 0
#define WBEM_RETURN_IMMEDIATELY 0x10
#define WBEM_MASK_RESERVED_FLAGS 0x1F000
#define WBEM_FLAG_USE_AMENDED_QUALIFIERS 0x20000
#define WBEM_FLAG_STRONG_VALIDATION 0x100000

#define WBEM_FLAG_DEEP 0
#define WBEM_FLAG_SHALLOW 1
#define WBEM_FLAG_PROTOTYPE 2

#define WBEM_FLAG_CREATE_OR_UPDATE 0
#define WBEM_FLAG_UPDATE_ONLY 1
#define WBEM_FLAG_CREATE_ONLY 2
#define WBEM_FLAG_UPDATE_COMPATIBLE 0
#define WBEM_FLAG_UPDATE_SAFE_MODE 0x20
#define WBEM_FLAG_UPDATE_FORCE_MODE 0x40
#define WBEM_MASK_UPDATE_MODE 0x60
#define WBEM_FLAG_ADVISORY 0x10000

#define WBEM_FLAG_ALWAYS 0
#define WBEM_FLAG_ONLY_IF_TRUE 1
#define WBEM_FLAG_ONLY_IF_FALSE 2
#define WBEM_FLAG_ONLY_IF_IDENTICAL 3
#define WBEM_MASK_PRIMARY_CONDITION 3
#define WBEM_FLAG_KEYS_ONLY 4
#define WBEM_FLAG_REFS_ONLY 8
#define WBEM_FLAG_LOCAL_ONLY 0x10
#define WBEM_FLAG_PROPAGATED_ONLY 0x20
#define WBEM_FLAG_SYSTEM_ONLY 0x30
#define WBEM_FLAG_NONSYSTEM_ONLY 0x40
#define WBEM_MASK_CONDITION_ORIGIN 0x70
#define WBEM_FLAG_CLASS_OVERRIDES_ONLY 0x100
#define WBEM_FLAG_CLASS_LOCAL_AND_OVERRIDES 0x200
#define WBEM_MASK_CLASS_CONDITION 0x300

#define WBEM_FLAVOR_DONT_PROPAGATE 0
#define WBEM_FLAVOR_FLAG_PROPAGATE_TO_INSTANCE 1
#define WBEM_FLAVOR_FLAG_PROPAGATE_TO_DERIVED_CLASS 2
#define WBEM_FLAVOR_MASK_PROPAGATION 0xF
#define WBEM_FLAVOR_OVERRIDABLE 0
#define WBEM_FLAVOR_NOT_OVERRIDABLE 0x10
#define WBEM_FLAVOR_MASK_PERMISSIONS 0x10
#define WBEM_FLAVOR_ORIGIN_LOCAL 0
#define WBEM_FLAVOR_ORIGIN_PROPAGATED 0x20
#define WBEM_FLAVOR_ORIGIN_SYSTEM 0x40
#define WBEM_FLAVOR_MASK_ORIGIN 0x60
#define WBEM_FLAVOR_NOT_AMENDED 0
#define WBEM_FLAVOR_AMENDED 0x80
#define WBEM_FLAVOR_MASK_AMENDED 0x80

#define WBEM_INFINITE 0xFFFFFFFF
#define WBEM_STATUS_COMPLETE 0
#define WBEM_STATUS_REQUIREMENTS 1
#define WBEM_STATUS_PROGRESS 2

#define WBEM_GENUS_CLASS 1
#define WBEM_GENUS_INSTANCE 2

#define WBEM_COMPARISON_INCLUDE_ALL 0
#define WBEM_FLAG_IGNORE_QUALIFIERS 0x1
#define WBEM_FLAG_IGNORE_OBJECT_SOURCE 0x2
#define WBEM_FLAG_IGNORE_DEFAULT_VALUES 0x4
#define WBEM_FLAG_IGNORE_CLASS 0x8
#define WBEM_FLAG_IGNORE_CASE 0x10
#define WBEM_FLAG_IGNORE_FLAVOR 0x20

#define WBEM_FLAG_NO_FLAVORS 1

#define WBEM_FLAG_CONNECT_REPOSITORY_ONLY 0x40
#define WBEM_FLAG_CONNECT_USE_MAX_WAIT 0x80
#define WBEM_FLAG_CONNECT_PROVIDERS 0x100

typedef LONG CIMTYPE;
#define CIM_ILLEGAL 0xFFF
#define CIM_EMPTY 0
#define CIM_SINT8 16
#define CIM_UINT8 17
#define CIM_SINT16 2
#define CIM_UINT16 18
#define CIM_SINT32 3
#define CIM_UINT32 19
#define CIM_SINT64 20
#define CIM_UINT64 21
#define CIM_REAL32 4
#define CIM_REAL64 5
#define CIM_BOOLEAN 11
#define CIM_STRING 8
#define CIM_DATETIME 101
#define CIM_REFERENCE 102
#define CIM_CHAR16 103
#define CIM_OBJECT 13
#define CIM_FLAG_ARRAY 0x2000

#define WBEM_MAX_IDENTIFIER 4096

/* ------------------------------------------------------------------ guids */

MIME_DEFINE_GUID(CLSID_WbemLocator, 0x4590F811, 0x1D3A, 0x11D0, 0x89, 0x1F, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24);
MIME_DEFINE_GUID(CLSID_WbemAdministrativeLocator, 0xCB8555CC, 0x9128, 0x11D1, 0xAD, 0x9B, 0x00, 0xC0, 0x4F, 0xD8, 0xFD, 0xFF);
MIME_DEFINE_GUID(CLSID_WbemAuthenticatedLocator, 0xCD184336, 0x9128, 0x11D1, 0xAD, 0x9B, 0x00, 0xC0, 0x4F, 0xD8, 0xFD, 0xFF);
MIME_DEFINE_GUID(CLSID_WbemUnauthenticatedLocator, 0x4590F812, 0x1D3A, 0x11D0, 0x89, 0x1F, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24);
MIME_DEFINE_GUID(CLSID_WbemClassObject, 0x9A653086, 0x174F, 0x11D2, 0xB5, 0xF9, 0x00, 0x10, 0x4B, 0x70, 0x3E, 0xFD);
MIME_DEFINE_GUID(CLSID_WbemContext, 0x674B6698, 0xEE92, 0x11D0, 0xAD, 0x71, 0x00, 0xC0, 0x4F, 0xD8, 0xFD, 0xFF);
MIME_DEFINE_GUID(CLSID_WbemDefPath, 0xCF4CC405, 0xE2C5, 0x4DDD, 0xB3, 0xCE, 0x5E, 0x75, 0x82, 0xD8, 0xC9, 0xFA);
MIME_DEFINE_GUID(CLSID_WbemStatusCodeText, 0xEB87E1BD, 0x3233, 0x11D2, 0xAE, 0xC9, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(CLSID_WbemQuery, 0xEAC8A024, 0x21E2, 0x4523, 0xAD, 0x73, 0xA7, 0x1A, 0x0A, 0xA2, 0xF5, 0x6A);
MIME_DEFINE_GUID(CLSID_WbemObjectTextSrc, 0x8D1C559D, 0x84F0, 0x4BB3, 0xA7, 0xD5, 0x56, 0xA7, 0x43, 0x5A, 0x9B, 0xA6);
MIME_DEFINE_GUID(CLSID_MofCompiler, 0x6DAF9757, 0x2E37, 0x11D2, 0xAE, 0xC9, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);

MIME_DEFINE_GUID(IID_IWbemLocator, 0xDC12A687, 0x737F, 0x11CF, 0x88, 0x4D, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24);
MIME_DEFINE_GUID(IID_IWbemServices, 0x9556DC99, 0x828C, 0x11CF, 0xA3, 0x7E, 0x00, 0xAA, 0x00, 0x32, 0x40, 0xC7);
MIME_DEFINE_GUID(IID_IWbemClassObject, 0xDC12A681, 0x737F, 0x11CF, 0x88, 0x4D, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24);
MIME_DEFINE_GUID(IID_IEnumWbemClassObject, 0x027947E1, 0xD731, 0x11CE, 0xA3, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01);
MIME_DEFINE_GUID(IID_IWbemCallResult, 0x44ACA675, 0xE8FC, 0x11D0, 0xA0, 0x7C, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IWbemQualifierSet, 0xDC12A680, 0x737F, 0x11CF, 0x88, 0x4D, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24);
MIME_DEFINE_GUID(IID_IWbemContext, 0x44ACA674, 0xE8FC, 0x11D0, 0xA0, 0x7C, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IWbemObjectSink, 0x7C857801, 0x7381, 0x11CF, 0x88, 0x4D, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24);
MIME_DEFINE_GUID(IID_IWbemObjectAccess, 0x49353C9A, 0x516B, 0x11D1, 0xAE, 0xA6, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IWbemObjectTextSrc, 0xBFBF883A, 0xCAD7, 0x11D3, 0xA1, 0x1B, 0x00, 0x10, 0x5A, 0x1F, 0x51, 0x5A);
MIME_DEFINE_GUID(IID_IWbemPath, 0x3BC15AF2, 0x736C, 0x477E, 0x9E, 0x51, 0x23, 0x8A, 0xF8, 0x66, 0x7D, 0xCC);
MIME_DEFINE_GUID(IID_IWbemPathKeyList, 0x9AE62877, 0x7544, 0x4BB0, 0xAA, 0x26, 0xA1, 0x38, 0x24, 0x65, 0x9E, 0xD6);
MIME_DEFINE_GUID(IID_IWbemStatusCodeText, 0xEB87E1BC, 0x3233, 0x11D2, 0xAE, 0xC9, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IWbemProviderInit, 0x1BE41572, 0x91DD, 0x11D1, 0xAE, 0xB2, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IWbemLevel1Login, 0xF309AD18, 0xD86A, 0x11D0, 0xA0, 0x75, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IMofCompiler, 0x6DAF974E, 0x2E37, 0x11D2, 0xAE, 0xC9, 0x00, 0xC0, 0x4F, 0xB6, 0x88, 0x20);
MIME_DEFINE_GUID(IID_IWbemQuery, 0x81166F58, 0xDD98, 0x11D3, 0xA1, 0x20, 0x00, 0x10, 0x5A, 0x1F, 0x51, 0x5A);

/* ------------------------------------------------------------------ interfaces */

typedef struct IWbemClassObject IWbemClassObject;
typedef struct IWbemQualifierSet IWbemQualifierSet;
typedef struct IWbemServices IWbemServices;
typedef struct IEnumWbemClassObject IEnumWbemClassObject;
typedef struct IWbemContext IWbemContext;
typedef struct IWbemCallResult IWbemCallResult;
typedef struct IWbemObjectSink IWbemObjectSink;
typedef struct IWbemLocator IWbemLocator;
typedef struct IWbemObjectAccess IWbemObjectAccess;

#define IWbemQualifierSet_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Get)(T * This, LPCWSTR name, LONG flags, VARIANT *value, LONG *flavor);                 \
    HRESULT(STDMETHODCALLTYPE *Put)(T * This, LPCWSTR name, VARIANT *value, LONG flavor);                              \
    HRESULT(STDMETHODCALLTYPE *Delete)(T * This, LPCWSTR name);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetNames)(T * This, LONG flags, SAFEARRAY **names);                                     \
    HRESULT(STDMETHODCALLTYPE *BeginEnumeration)(T * This, LONG flags);                                                \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, LONG flags, BSTR *name, VARIANT *value, LONG *flavor);                  \
    HRESULT(STDMETHODCALLTYPE *EndEnumeration)(T * This);
MIME_COM_INTERFACE(IWbemQualifierSet)

#define IWbemClassObject_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetQualifierSet)(T * This, IWbemQualifierSet **set);                                    \
    HRESULT(STDMETHODCALLTYPE *Get)(T * This, LPCWSTR name, LONG flags, VARIANT *value, CIMTYPE *type, LONG *flavor);  \
    HRESULT(STDMETHODCALLTYPE *Put)(T * This, LPCWSTR name, LONG flags, VARIANT *value, CIMTYPE type);                 \
    HRESULT(STDMETHODCALLTYPE *Delete)(T * This, LPCWSTR name);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetNames)(T * This, LPCWSTR qualifier, LONG flags, VARIANT *qualifier_value,            \
                                         SAFEARRAY **names);                                                           \
    HRESULT(STDMETHODCALLTYPE *BeginEnumeration)(T * This, LONG flags);                                                \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, LONG flags, BSTR *name, VARIANT *value, CIMTYPE *type, LONG *flavor);   \
    HRESULT(STDMETHODCALLTYPE *EndEnumeration)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *GetPropertyQualifierSet)(T * This, LPCWSTR property, IWbemQualifierSet **set);          \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IWbemClassObject **copy);                                              \
    HRESULT(STDMETHODCALLTYPE *GetObjectText)(T * This, LONG flags, BSTR *text);                                       \
    HRESULT(STDMETHODCALLTYPE *SpawnDerivedClass)(T * This, LONG flags, IWbemClassObject **derived);                   \
    HRESULT(STDMETHODCALLTYPE *SpawnInstance)(T * This, LONG flags, IWbemClassObject **instance);                      \
    HRESULT(STDMETHODCALLTYPE *CompareTo)(T * This, LONG flags, IWbemClassObject *other);                              \
    HRESULT(STDMETHODCALLTYPE *GetPropertyOrigin)(T * This, LPCWSTR name, BSTR *class_name);                           \
    HRESULT(STDMETHODCALLTYPE *InheritsFrom)(T * This, LPCWSTR ancestor);                                              \
    HRESULT(STDMETHODCALLTYPE *GetMethod)(T * This, LPCWSTR name, LONG flags, IWbemClassObject **in,                   \
                                          IWbemClassObject **out);                                                     \
    HRESULT(STDMETHODCALLTYPE *PutMethod)(T * This, LPCWSTR name, LONG flags, IWbemClassObject *in,                    \
                                          IWbemClassObject *out);                                                      \
    HRESULT(STDMETHODCALLTYPE *DeleteMethod)(T * This, LPCWSTR name);                                                  \
    HRESULT(STDMETHODCALLTYPE *BeginMethodEnumeration)(T * This, LONG flags);                                          \
    HRESULT(STDMETHODCALLTYPE *NextMethod)(T * This, LONG flags, BSTR *name, IWbemClassObject **in,                    \
                                           IWbemClassObject **out);                                                    \
    HRESULT(STDMETHODCALLTYPE *EndMethodEnumeration)(T * This);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetMethodQualifierSet)(T * This, LPCWSTR method, IWbemQualifierSet **set);              \
    HRESULT(STDMETHODCALLTYPE *GetMethodOrigin)(T * This, LPCWSTR method, BSTR *class_name);
MIME_COM_INTERFACE(IWbemClassObject)

#define IWbemObjectAccess_METHODS(T)                                                                                   \
    IWbemClassObject_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetPropertyType)(T * This, LPCWSTR name, CIMTYPE *type, LONG *flavor);                  \
    HRESULT(STDMETHODCALLTYPE *GetPropertyHandle)(T * This, LPCWSTR name, CIMTYPE *type, LONG *handle);                \
    HRESULT(STDMETHODCALLTYPE *WritePropertyValue)(T * This, LONG handle, LONG size, const BYTE *data);                \
    HRESULT(STDMETHODCALLTYPE *ReadPropertyValue)(T * This, LONG handle, LONG size, LONG *needed, BYTE *data);         \
    HRESULT(STDMETHODCALLTYPE *ReadDWORD)(T * This, LONG handle, DWORD *value);                                        \
    HRESULT(STDMETHODCALLTYPE *WriteDWORD)(T * This, LONG handle, DWORD value);                                        \
    HRESULT(STDMETHODCALLTYPE *ReadQWORD)(T * This, LONG handle, ULONGLONG *value);                                    \
    HRESULT(STDMETHODCALLTYPE *WriteQWORD)(T * This, LONG handle, ULONGLONG value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetPropertyInfoByHandle)(T * This, LONG handle, BSTR *name, CIMTYPE *type);             \
    HRESULT(STDMETHODCALLTYPE *Lock)(T * This, LONG flags);                                                            \
    HRESULT(STDMETHODCALLTYPE *Unlock)(T * This, LONG flags);
MIME_COM_INTERFACE(IWbemObjectAccess)

#define IWbemContext_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IWbemContext **copy);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetNames)(T * This, LONG flags, SAFEARRAY **names);                                     \
    HRESULT(STDMETHODCALLTYPE *BeginEnumeration)(T * This, LONG flags);                                                \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, LONG flags, BSTR *name, VARIANT *value);                                \
    HRESULT(STDMETHODCALLTYPE *EndEnumeration)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *SetValue)(T * This, LPCWSTR name, LONG flags, VARIANT *value);                          \
    HRESULT(STDMETHODCALLTYPE *GetValue)(T * This, LPCWSTR name, LONG flags, VARIANT *value);                          \
    HRESULT(STDMETHODCALLTYPE *DeleteValue)(T * This, LPCWSTR name, LONG flags);                                       \
    HRESULT(STDMETHODCALLTYPE *DeleteAll)(T * This);
MIME_COM_INTERFACE(IWbemContext)

#define IWbemCallResult_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetResultObject)(T * This, LONG timeout, IWbemClassObject **result);                    \
    HRESULT(STDMETHODCALLTYPE *GetResultString)(T * This, LONG timeout, BSTR *result);                                 \
    HRESULT(STDMETHODCALLTYPE *GetResultServices)(T * This, LONG timeout, IWbemServices **services);                   \
    HRESULT(STDMETHODCALLTYPE *GetCallStatus)(T * This, LONG timeout, LONG *status);
MIME_COM_INTERFACE(IWbemCallResult)

#define IWbemObjectSink_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Indicate)(T * This, LONG count, IWbemClassObject **objects);                            \
    HRESULT(STDMETHODCALLTYPE *SetStatus)(T * This, LONG flags, HRESULT result, BSTR param, IWbemClassObject *object);
MIME_COM_INTERFACE(IWbemObjectSink)

#define IEnumWbemClassObject_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, LONG timeout, ULONG count, IWbemClassObject **objects, ULONG *returned);\
    HRESULT(STDMETHODCALLTYPE *NextAsync)(T * This, ULONG count, IWbemObjectSink *sink);                               \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IEnumWbemClassObject **copy);                                          \
    HRESULT(STDMETHODCALLTYPE *Skip)(T * This, LONG timeout, ULONG count);
MIME_COM_INTERFACE(IEnumWbemClassObject)

#define IWbemServices_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *OpenNamespace)(T * This, const BSTR name, LONG flags, IWbemContext *context,            \
                                              IWbemServices **services, IWbemCallResult **result);                     \
    HRESULT(STDMETHODCALLTYPE *CancelAsyncCall)(T * This, IWbemObjectSink *sink);                                      \
    HRESULT(STDMETHODCALLTYPE *QueryObjectSink)(T * This, LONG flags, IWbemObjectSink **sink);                         \
    HRESULT(STDMETHODCALLTYPE *GetObject)(T * This, const BSTR path, LONG flags, IWbemContext *context,                \
                                          IWbemClassObject **object, IWbemCallResult **result);                        \
    HRESULT(STDMETHODCALLTYPE *GetObjectAsync)(T * This, const BSTR path, LONG flags, IWbemContext *context,           \
                                               IWbemObjectSink *sink);                                                 \
    HRESULT(STDMETHODCALLTYPE *PutClass)(T * This, IWbemClassObject *object, LONG flags, IWbemContext *context,        \
                                         IWbemCallResult **result);                                                    \
    HRESULT(STDMETHODCALLTYPE *PutClassAsync)(T * This, IWbemClassObject *object, LONG flags, IWbemContext *context,   \
                                              IWbemObjectSink *sink);                                                  \
    HRESULT(STDMETHODCALLTYPE *DeleteClass)(T * This, const BSTR name, LONG flags, IWbemContext *context,              \
                                            IWbemCallResult **result);                                                 \
    HRESULT(STDMETHODCALLTYPE *DeleteClassAsync)(T * This, const BSTR name, LONG flags, IWbemContext *context,         \
                                                 IWbemObjectSink *sink);                                               \
    HRESULT(STDMETHODCALLTYPE *CreateClassEnum)(T * This, const BSTR superclass, LONG flags, IWbemContext *context,    \
                                                IEnumWbemClassObject **enumerator);                                    \
    HRESULT(STDMETHODCALLTYPE *CreateClassEnumAsync)(T * This, const BSTR superclass, LONG flags,                      \
                                                     IWbemContext *context, IWbemObjectSink *sink);                    \
    HRESULT(STDMETHODCALLTYPE *PutInstance)(T * This, IWbemClassObject *instance, LONG flags, IWbemContext *context,   \
                                            IWbemCallResult **result);                                                 \
    HRESULT(STDMETHODCALLTYPE *PutInstanceAsync)(T * This, IWbemClassObject *instance, LONG flags,                     \
                                                 IWbemContext *context, IWbemObjectSink *sink);                        \
    HRESULT(STDMETHODCALLTYPE *DeleteInstance)(T * This, const BSTR path, LONG flags, IWbemContext *context,           \
                                               IWbemCallResult **result);                                              \
    HRESULT(STDMETHODCALLTYPE *DeleteInstanceAsync)(T * This, const BSTR path, LONG flags, IWbemContext *context,      \
                                                    IWbemObjectSink *sink);                                            \
    HRESULT(STDMETHODCALLTYPE *CreateInstanceEnum)(T * This, const BSTR class_name, LONG flags,                        \
                                                   IWbemContext *context, IEnumWbemClassObject **enumerator);          \
    HRESULT(STDMETHODCALLTYPE *CreateInstanceEnumAsync)(T * This, const BSTR class_name, LONG flags,                   \
                                                        IWbemContext *context, IWbemObjectSink *sink);                 \
    HRESULT(STDMETHODCALLTYPE *ExecQuery)(T * This, const BSTR language, const BSTR query, LONG flags,                 \
                                          IWbemContext *context, IEnumWbemClassObject **enumerator);                   \
    HRESULT(STDMETHODCALLTYPE *ExecQueryAsync)(T * This, const BSTR language, const BSTR query, LONG flags,            \
                                               IWbemContext *context, IWbemObjectSink *sink);                          \
    HRESULT(STDMETHODCALLTYPE *ExecNotificationQuery)(T * This, const BSTR language, const BSTR query, LONG flags,     \
                                                      IWbemContext *context, IEnumWbemClassObject **enumerator);       \
    HRESULT(STDMETHODCALLTYPE *ExecNotificationQueryAsync)(T * This, const BSTR language, const BSTR query,            \
                                                           LONG flags, IWbemContext *context,                          \
                                                           IWbemObjectSink *sink);                                     \
    HRESULT(STDMETHODCALLTYPE *ExecMethod)(T * This, const BSTR object, const BSTR method, LONG flags,                 \
                                           IWbemContext *context, IWbemClassObject *in, IWbemClassObject **out,        \
                                           IWbemCallResult **result);                                                  \
    HRESULT(STDMETHODCALLTYPE *ExecMethodAsync)(T * This, const BSTR object, const BSTR method, LONG flags,            \
                                                IWbemContext *context, IWbemClassObject *in,                           \
                                                IWbemObjectSink *sink);
MIME_COM_INTERFACE(IWbemServices)

#define IWbemLocator_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ConnectServer)(T * This, const BSTR network_resource, const BSTR user,                  \
                                              const BSTR password, const BSTR locale, LONG security_flags,             \
                                              const BSTR authority, IWbemContext *context, IWbemServices **services);
MIME_COM_INTERFACE(IWbemLocator)

#define IWbemProviderInit_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, LPWSTR user, LONG flags, LPWSTR ns, LPWSTR locale,                \
                                           IWbemServices *services, IWbemContext *context, void *sink);
typedef struct IWbemProviderInit IWbemProviderInit;
MIME_COM_INTERFACE(IWbemProviderInit)

#define IWbemLevel1Login_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *EstablishPosition)(T * This, LPWSTR locale_list, DWORD flags, DWORD *max_version);      \
    HRESULT(STDMETHODCALLTYPE *RequestChallenge)(T * This, LPWSTR network_resource, LPWSTR user, UCHAR *nonce);        \
    HRESULT(STDMETHODCALLTYPE *WBEMLogin)(T * This, LPWSTR preferred_locale, UCHAR *access_token, LONG flags,          \
                                          IWbemContext *context, IWbemServices **services);                            \
    HRESULT(STDMETHODCALLTYPE *NTLMLogin)(T * This, LPWSTR network_resource, LPWSTR preferred_locale, LONG flags,      \
                                          IWbemContext *context, IWbemServices **services);
typedef struct IWbemLevel1Login IWbemLevel1Login;
MIME_COM_INTERFACE(IWbemLevel1Login)

/* ------------------------------------------------------------------ the path parser and status text */

#define WBEMPATH_CREATE_ACCEPT_RELATIVE 0x1
#define WBEMPATH_CREATE_ACCEPT_ABSOLUTE 0x2
#define WBEMPATH_CREATE_ACCEPT_ALL 0x4
#define WBEMPATH_TREAT_SINGLE_IDENT_AS_NS 0x8

#define WBEMPATH_INFO_ANON_LOCAL_MACHINE 0x1
#define WBEMPATH_INFO_HAS_MACHINE_NAME 0x2
#define WBEMPATH_INFO_IS_CLASS_REF 0x4
#define WBEMPATH_INFO_IS_INST_REF 0x8
#define WBEMPATH_INFO_HAS_SUBSCOPES 0x10
#define WBEMPATH_INFO_IS_COMPOUND 0x20
#define WBEMPATH_INFO_HAS_V2_REF_PATHS 0x40
#define WBEMPATH_INFO_HAS_IMPLIED_KEY 0x80
#define WBEMPATH_INFO_CONTAINS_SINGLETON 0x100
#define WBEMPATH_INFO_V1_COMPLIANT 0x200
#define WBEMPATH_INFO_V2_COMPLIANT 0x400
#define WBEMPATH_INFO_CIM_COMPLIANT 0x800
#define WBEMPATH_INFO_IS_SINGLE_TOKEN 0x1000
#define WBEMPATH_INFO_IS_PARENT 0x2000
#define WBEMPATH_INFO_SERVER_NAMESPACE_ONLY 0x4000
#define WBEMPATH_INFO_NATIVE_PATH 0x8000
#define WBEMPATH_INFO_WMI_PATH 0x10000
#define WBEMPATH_INFO_PATH_HAD_SERVER 0x20000

#define WBEMPATH_COMPRESSED 0x1
#define WBEMPATH_GET_RELATIVE_ONLY 0x2
#define WBEMPATH_GET_SERVER_TOO 0x4
#define WBEMPATH_GET_SERVER_AND_NAMESPACE_ONLY 0x8
#define WBEMPATH_GET_NAMESPACE_ONLY 0x10
#define WBEMPATH_GET_ORIGINAL 0x20

#define WBEMPATH_QUOTEDTEXT 0x1
#define WBEMPATH_TEXT 0x2

#define WBEMPATH_KEY_PROP_NAME 0
#define WBEM_FLAG_PATH_KEYS_ARE_INSTANCE 0

typedef struct IWbemPathKeyList IWbemPathKeyList;
typedef struct IWbemPath IWbemPath;
typedef struct IWbemStatusCodeText IWbemStatusCodeText;

#define IWbemPathKeyList_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetCount)(T * This, ULONG *count);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetKey)(T * This, LPCWSTR name, ULONG flags, ULONG cimtype, LPVOID value);              \
    HRESULT(STDMETHODCALLTYPE *SetKey2)(T * This, LPCWSTR name, ULONG flags, ULONG cimtype, VARIANT *value);           \
    HRESULT(STDMETHODCALLTYPE *GetKey)(T * This, ULONG index, ULONG flags, ULONG *name_size, LPWSTR name,              \
                                       ULONG *value_size, LPVOID value, ULONG *cimtype);                               \
    HRESULT(STDMETHODCALLTYPE *GetKey2)(T * This, ULONG index, ULONG flags, ULONG *name_size, LPWSTR name,             \
                                        VARIANT *value, ULONG *cimtype);                                               \
    HRESULT(STDMETHODCALLTYPE *RemoveKey)(T * This, LPCWSTR name, ULONG flags);                                        \
    HRESULT(STDMETHODCALLTYPE *RemoveAllKeys)(T * This, ULONG flags);                                                  \
    HRESULT(STDMETHODCALLTYPE *MakeSingleton)(T * This, BOOLEAN value);                                                \
    HRESULT(STDMETHODCALLTYPE *GetInfo)(T * This, ULONG request, ULONGLONG *response);                                 \
    HRESULT(STDMETHODCALLTYPE *GetText)(T * This, LONG flags, ULONG *buffer_length, LPWSTR text);
MIME_COM_INTERFACE(IWbemPathKeyList)

#define IWbemPath_METHODS(T)                                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetText)(T * This, ULONG mode, LPCWSTR path);                                           \
    HRESULT(STDMETHODCALLTYPE *GetText)(T * This, LONG flags, ULONG *buffer_length, LPWSTR text);                      \
    HRESULT(STDMETHODCALLTYPE *GetInfo)(T * This, ULONG request, ULONGLONG *response);                                 \
    HRESULT(STDMETHODCALLTYPE *SetServer)(T * This, LPCWSTR name);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetServer)(T * This, ULONG *name_length, LPWSTR name);                                  \
    HRESULT(STDMETHODCALLTYPE *GetNamespaceCount)(T * This, ULONG *count);                                             \
    HRESULT(STDMETHODCALLTYPE *SetNamespaceAt)(T * This, ULONG index, LPCWSTR name);                                   \
    HRESULT(STDMETHODCALLTYPE *GetNamespaceAt)(T * This, ULONG index, ULONG *name_length, LPWSTR name);                \
    HRESULT(STDMETHODCALLTYPE *RemoveNamespaceAt)(T * This, ULONG index);                                              \
    HRESULT(STDMETHODCALLTYPE *RemoveAllNamespaces)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetScopeCount)(T * This, ULONG *count);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetScope)(T * This, ULONG index, LPWSTR name);                                          \
    HRESULT(STDMETHODCALLTYPE *SetScopeFromText)(T * This, ULONG index, LPWSTR text);                                  \
    HRESULT(STDMETHODCALLTYPE *GetScope)(T * This, ULONG index, ULONG *name_length, LPWSTR name,                       \
                                         IWbemPathKeyList **keys);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetScopeAsText)(T * This, ULONG index, ULONG *text_length, LPWSTR text);                \
    HRESULT(STDMETHODCALLTYPE *RemoveScope)(T * This, ULONG index);                                                    \
    HRESULT(STDMETHODCALLTYPE *RemoveAllScopes)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *SetClassName)(T * This, LPCWSTR name);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetClassName)(T * This, ULONG *name_length, LPWSTR name);                               \
    HRESULT(STDMETHODCALLTYPE *GetKeyList)(T * This, IWbemPathKeyList **keys);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateClassPart)(T * This, LONG flags, LPCWSTR name);                                   \
    HRESULT(STDMETHODCALLTYPE *DeleteClassPart)(T * This, LONG flags);                                                 \
    BOOL(STDMETHODCALLTYPE *IsRelative)(T * This, LPWSTR machine, LPWSTR ns);                                          \
    BOOL(STDMETHODCALLTYPE *IsRelativeOrChild)(T * This, LPWSTR machine, LPWSTR ns, LONG flags);                       \
    BOOL(STDMETHODCALLTYPE *IsLocal)(T * This, LPCWSTR machine);                                                       \
    BOOL(STDMETHODCALLTYPE *IsSameClassName)(T * This, LPCWSTR name);
MIME_COM_INTERFACE(IWbemPath)

#define IWbemStatusCodeText_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetErrorCodeText)(T * This, HRESULT result, LCID locale, LONG flags, BSTR *text);       \
    HRESULT(STDMETHODCALLTYPE *GetFacilityCodeText)(T * This, HRESULT result, LCID locale, LONG flags, BSTR *text);
MIME_COM_INTERFACE(IWbemStatusCodeText)

/* ------------------------------------------------------------------ the mof compiler */

#define WBEM_FLAG_CHECK_ONLY 0x1
#define WBEM_FLAG_AUTORECOVER 0x2
#define WBEM_FLAG_WMI_CHECK 0x4
#define WBEM_FLAG_CONSOLE_PRINT 0x8
#define WBEM_FLAG_DONT_ADD_TO_LIST 0x10
#define WBEM_FLAG_SPLIT_FILES 0x20
#define WBEM_FLAG_CLASS_UPDATE_ONLY_BIT 0x40

typedef struct _WBEM_COMPILE_STATUS_INFO {
    LONG lPhaseError;
    HRESULT hRes;
    LONG ObjectNum;
    LONG FirstLine;
    LONG LastLine;
    DWORD dwOutFlags;
} WBEM_COMPILE_STATUS_INFO;

typedef struct IMofCompiler IMofCompiler;
#define IMofCompiler_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CompileFile)(T * This, LPWSTR file, LPWSTR server_and_namespace, LPWSTR user,           \
                                            LPWSTR authority, LPWSTR password, LONG options, LONG class_flags,         \
                                            LONG instance_flags, WBEM_COMPILE_STATUS_INFO *info);                      \
    HRESULT(STDMETHODCALLTYPE *CompileBuffer)(T * This, BYTE *buffer, DWORD size, LPWSTR server_and_namespace,         \
                                              LPWSTR user, LPWSTR authority, LPWSTR password, LONG options,            \
                                              LONG class_flags, LONG instance_flags,                                   \
                                              WBEM_COMPILE_STATUS_INFO *info);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateBMOF)(T * This, LPWSTR text_file, LPWSTR bmof_file, LPWSTR server_and_namespace,  \
                                           LONG options, LONG class_flags, LONG instance_flags,                        \
                                           WBEM_COMPILE_STATUS_INFO *info);
MIME_COM_INTERFACE(IMofCompiler)

#endif
