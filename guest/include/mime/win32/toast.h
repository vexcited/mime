/*
 * the windows runtime interfaces behind toast notifications: Windows.Data.Xml.Dom, the boxed values of
 * Windows.Foundation, and Windows.UI.Notifications. every method list is a macro, so the implementations and the
 * programs that call them share the layouts.
 */
#ifndef MIME_WIN32_TOAST_H
#define MIME_WIN32_TOAST_H

#include <mime/win32/wgc.h>

typedef struct DateTime {
    INT64 UniversalTime; /* 100 nanosecond units since 1601 */
} DateTime;

/* ------------------------------------------------------------------ Windows.Foundation */

typedef enum PropertyType {
    PropertyType_Empty = 0,
    PropertyType_UInt8 = 1,
    PropertyType_Int16 = 2,
    PropertyType_UInt16 = 3,
    PropertyType_Int32 = 4,
    PropertyType_UInt32 = 5,
    PropertyType_Int64 = 6,
    PropertyType_UInt64 = 7,
    PropertyType_Single = 8,
    PropertyType_Double = 9,
    PropertyType_Char16 = 10,
    PropertyType_Boolean = 11,
    PropertyType_String = 12,
    PropertyType_Inspectable = 13,
    PropertyType_DateTime = 14,
    PropertyType_TimeSpan = 15,
    PropertyType_Guid = 16,
} PropertyType;

typedef struct IPropertyValue IPropertyValue;
#define IPropertyValue_METHODS(T)                                                                                      \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, PropertyType * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_IsNumericScalar)(T * This, BOOLEAN * value);                                        \
    HRESULT(STDMETHODCALLTYPE *GetUInt8)(T * This, BYTE * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetInt16)(T * This, INT16 * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetUInt16)(T * This, UINT16 * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetInt32)(T * This, INT32 * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetUInt32)(T * This, UINT32 * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetInt64)(T * This, INT64 * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *GetUInt64)(T * This, UINT64 * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetSingle)(T * This, float * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetDouble)(T * This, double * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetChar16)(T * This, WCHAR * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetBoolean)(T * This, BOOLEAN * value);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetString)(T * This, HSTRING * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetGuid)(T * This, GUID * value);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetDateTime)(T * This, DateTime * value);                                               \
    HRESULT(STDMETHODCALLTYPE *GetTimeSpan)(T * This, TimeSpan * value);                                               \
    HRESULT(STDMETHODCALLTYPE *GetPoint)(T * This, void *value);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetSize)(T * This, void *value);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetRect)(T * This, void *value);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetUInt8Array)(T * This, UINT32 * size, void *value);                                   \
    HRESULT(STDMETHODCALLTYPE *GetInt16Array)(T * This, UINT32 * size, void *value);                                   \
    HRESULT(STDMETHODCALLTYPE *GetUInt16Array)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetInt32Array)(T * This, UINT32 * size, void *value);                                   \
    HRESULT(STDMETHODCALLTYPE *GetUInt32Array)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetInt64Array)(T * This, UINT32 * size, void *value);                                   \
    HRESULT(STDMETHODCALLTYPE *GetUInt64Array)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetSingleArray)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetDoubleArray)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetChar16Array)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetBooleanArray)(T * This, UINT32 * size, void *value);                                 \
    HRESULT(STDMETHODCALLTYPE *GetStringArray)(T * This, UINT32 * size, void *value);                                  \
    HRESULT(STDMETHODCALLTYPE *GetInspectableArray)(T * This, UINT32 * size, void *value);                             \
    HRESULT(STDMETHODCALLTYPE *GetGuidArray)(T * This, UINT32 * size, void *value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetDateTimeArray)(T * This, UINT32 * size, void *value);                                \
    HRESULT(STDMETHODCALLTYPE *GetTimeSpanArray)(T * This, UINT32 * size, void *value);                                \
    HRESULT(STDMETHODCALLTYPE *GetPointArray)(T * This, UINT32 * size, void *value);                                   \
    HRESULT(STDMETHODCALLTYPE *GetSizeArray)(T * This, UINT32 * size, void *value);                                    \
    HRESULT(STDMETHODCALLTYPE *GetRectArray)(T * This, UINT32 * size, void *value);
MIME_COM_INTERFACE(IPropertyValue)

/* IReference<T> of a value type: the value follows the inspectable methods. */
typedef struct IReferenceDateTime IReferenceDateTime;
#define IReferenceDateTime_METHODS(T)                                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Value)(T * This, DateTime * value);
MIME_COM_INTERFACE(IReferenceDateTime)

/* only the scalar creators are named, the arrays and the structures are one failing function. */
typedef struct IPropertyValueStatics IPropertyValueStatics;
#define IPropertyValueStatics_METHODS(T)                                                                               \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateEmpty)(T * This, IInspectable * *value);                                          \
    HRESULT(STDMETHODCALLTYPE *CreateUInt8)(T * This, BYTE v, IInspectable * *value);                                  \
    HRESULT(STDMETHODCALLTYPE *CreateInt16)(T * This, INT16 v, IInspectable * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateUInt16)(T * This, UINT16 v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateInt32)(T * This, INT32 v, IInspectable * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateUInt32)(T * This, UINT32 v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateInt64)(T * This, INT64 v, IInspectable * *value);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateUInt64)(T * This, UINT64 v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateSingle)(T * This, float v, IInspectable * *value);                                \
    HRESULT(STDMETHODCALLTYPE *CreateDouble)(T * This, double v, IInspectable * *value);                               \
    HRESULT(STDMETHODCALLTYPE *CreateChar16)(T * This, WCHAR v, IInspectable * *value);                                \
    HRESULT(STDMETHODCALLTYPE *CreateBoolean)(T * This, BOOLEAN v, IInspectable * *value);                             \
    HRESULT(STDMETHODCALLTYPE *CreateString)(T * This, HSTRING v, IInspectable * *value);                              \
    HRESULT(STDMETHODCALLTYPE *CreateInspectable)(T * This, IInspectable * v, IInspectable * *value);                  \
    HRESULT(STDMETHODCALLTYPE *CreateGuid)(T * This, GUID v, IInspectable * *value);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateDateTime)(T * This, DateTime v, IInspectable * *value);                           \
    HRESULT(STDMETHODCALLTYPE *CreateTimeSpan)(T * This, TimeSpan v, IInspectable * *value);                           \
    HRESULT(STDMETHODCALLTYPE *CreatePoint)(T * This, double x, double y, IInspectable * *value);                      \
    HRESULT(STDMETHODCALLTYPE *CreateSize)(T * This, double w, double h, IInspectable * *value);                       \
    HRESULT(STDMETHODCALLTYPE *CreateRect)(T * This, double x, double y, double w, double h, IInspectable * *value);   \
    HRESULT(STDMETHODCALLTYPE *CreateUInt8Array)(T * This, UINT32 n, void *v, IInspectable * *value);                  \
    HRESULT(STDMETHODCALLTYPE *CreateInt16Array)(T * This, UINT32 n, void *v, IInspectable * *value);                  \
    HRESULT(STDMETHODCALLTYPE *CreateUInt16Array)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateInt32Array)(T * This, UINT32 n, void *v, IInspectable * *value);                  \
    HRESULT(STDMETHODCALLTYPE *CreateUInt32Array)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateInt64Array)(T * This, UINT32 n, void *v, IInspectable * *value);                  \
    HRESULT(STDMETHODCALLTYPE *CreateUInt64Array)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateSingleArray)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateDoubleArray)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateChar16Array)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateBooleanArray)(T * This, UINT32 n, void *v, IInspectable * *value);                \
    HRESULT(STDMETHODCALLTYPE *CreateStringArray)(T * This, UINT32 n, void *v, IInspectable * *value);                 \
    HRESULT(STDMETHODCALLTYPE *CreateInspectableArray)(T * This, UINT32 n, void *v, IInspectable * *value);            \
    HRESULT(STDMETHODCALLTYPE *CreateGuidArray)(T * This, UINT32 n, void *v, IInspectable * *value);                   \
    HRESULT(STDMETHODCALLTYPE *CreateDateTimeArray)(T * This, UINT32 n, void *v, IInspectable * *value);               \
    HRESULT(STDMETHODCALLTYPE *CreateTimeSpanArray)(T * This, UINT32 n, void *v, IInspectable * *value);               \
    HRESULT(STDMETHODCALLTYPE *CreatePointArray)(T * This, UINT32 n, void *v, IInspectable * *value);                  \
    HRESULT(STDMETHODCALLTYPE *CreateSizeArray)(T * This, UINT32 n, void *v, IInspectable * *value);                   \
    HRESULT(STDMETHODCALLTYPE *CreateRectArray)(T * This, UINT32 n, void *v, IInspectable * *value);
MIME_COM_INTERFACE(IPropertyValueStatics)

/* IVectorView<T> of pointers. */
typedef struct IVectorViewPtr IVectorViewPtr;
#define IVectorViewPtr_METHODS(T)                                                                                      \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetAt)(T * This, UINT32 index, void **item);                                            \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, UINT32 * size);                                                     \
    HRESULT(STDMETHODCALLTYPE *IndexOf)(T * This, void *item, UINT32 *index, BOOLEAN *found);                          \
    HRESULT(STDMETHODCALLTYPE *GetMany)(T * This, UINT32 start, UINT32 capacity, void **items, UINT32 *actual);
MIME_COM_INTERFACE(IVectorViewPtr)

MIME_DEFINE_GUID(IID_IPropertyValue, 0x4BD682DD, 0x7554, 0x40E9, 0x9A, 0x9B, 0x82, 0x65, 0x4E, 0xDE, 0x7E, 0x62);
MIME_DEFINE_GUID(IID_IPropertyValueStatics, 0x629BDBC8, 0xD932, 0x4FF4, 0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58);
MIME_DEFINE_GUID(IID_IReferenceDateTime, 0x5541D8A7, 0x497C, 0x5AA4, 0x86, 0xFC, 0x77, 0x13, 0xAD, 0xBF, 0x2A, 0x2C);


/* IMap<HSTRING, IInspectable>, the interface of a property set, which holds what a person typed into a toast. */
typedef struct IInspectableMap IInspectableMap;
#define IInspectableMap_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Lookup)(T * This, HSTRING key, IInspectable * *value);                                  \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, UINT32 * size);                                                     \
    HRESULT(STDMETHODCALLTYPE *HasKey)(T * This, HSTRING key, BOOLEAN * found);                                        \
    HRESULT(STDMETHODCALLTYPE *GetView)(T * This, IInspectable * *view);                                               \
    HRESULT(STDMETHODCALLTYPE *Insert)(T * This, HSTRING key, IInspectable * value, BOOLEAN * replaced);              \
    HRESULT(STDMETHODCALLTYPE *Remove)(T * This, HSTRING key);                                                         \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This);
MIME_COM_INTERFACE(IInspectableMap)

/* the interface a toast activator, the COM server of an application, is called through. */
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

MIME_DEFINE_GUID(IID_IInspectableMap, 0x1B0D3570, 0x0877, 0x5EC2, 0x8A, 0x2C, 0x3B, 0x95, 0x39, 0x50, 0x6A, 0xCA);
MIME_DEFINE_GUID(IID_IPropertySet, 0x8A43ED9F, 0xF4E6, 0x4421, 0xAC, 0xF9, 0x1D, 0xAB, 0x29, 0x86, 0x82, 0x0C);
MIME_DEFINE_GUID(IID_IToastActivatedEventArgs2, 0xAB7DA512, 0xCC61, 0x568E, 0x81, 0xBE, 0x30, 0x4A, 0xC3, 0x10, 0x38,
                 0xFA);
MIME_DEFINE_GUID(IID_INotificationActivationCallback, 0x53E31837, 0x6600, 0x4A81, 0x93, 0x95, 0x75, 0xCF, 0xFE, 0x74,
                 0x6F, 0x94);

/* ------------------------------------------------------------------ Windows.Data.Xml.Dom */

typedef enum NodeType {
    NodeType_Invalid = 0,
    NodeType_ElementNode = 1,
    NodeType_AttributeNode = 2,
    NodeType_TextNode = 3,
    NodeType_DataSectionNode = 4,
    NodeType_EntityReferenceNode = 5,
    NodeType_EntityNode = 6,
    NodeType_ProcessingInstructionNode = 7,
    NodeType_CommentNode = 8,
    NodeType_DocumentNode = 9,
    NodeType_DocumentTypeNode = 10,
    NodeType_DocumentFragmentNode = 11,
    NodeType_NotationNode = 12,
} NodeType;

typedef struct IXmlNode IXmlNode;
typedef struct IXmlNodeList IXmlNodeList;
typedef struct IXmlNamedNodeMap IXmlNamedNodeMap;
typedef struct IXmlDocument IXmlDocument;
typedef struct IXmlElement IXmlElement;
typedef struct IXmlAttribute IXmlAttribute;
typedef struct IXmlText IXmlText;
typedef struct IXmlComment IXmlComment;
typedef struct IXmlCDataSection IXmlCDataSection;
typedef struct IXmlProcessingInstruction IXmlProcessingInstruction;
typedef struct IXmlEntityReference IXmlEntityReference;
typedef struct IXmlDocumentFragment IXmlDocumentFragment;
typedef struct IXmlDocumentType IXmlDocumentType;
typedef struct IXmlDomImplementation IXmlDomImplementation;

#define IXmlNode_METHODS(T)                                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_NodeValue)(T * This, IInspectable * *value);                                        \
    HRESULT(STDMETHODCALLTYPE *put_NodeValue)(T * This, IInspectable * value);                                         \
    HRESULT(STDMETHODCALLTYPE *get_NodeType)(T * This, NodeType * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_NodeName)(T * This, HSTRING * value);                                               \
    HRESULT(STDMETHODCALLTYPE *get_ParentNode)(T * This, IXmlNode * *value);                                           \
    HRESULT(STDMETHODCALLTYPE *get_ChildNodes)(T * This, IXmlNodeList * *value);                                       \
    HRESULT(STDMETHODCALLTYPE *get_FirstChild)(T * This, IXmlNode * *value);                                           \
    HRESULT(STDMETHODCALLTYPE *get_LastChild)(T * This, IXmlNode * *value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_PreviousSibling)(T * This, IXmlNode * *value);                                      \
    HRESULT(STDMETHODCALLTYPE *get_NextSibling)(T * This, IXmlNode * *value);                                          \
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(T * This, IXmlNamedNodeMap * *value);                                   \
    HRESULT(STDMETHODCALLTYPE *HasChildNodes)(T * This, BOOLEAN * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_OwnerDocument)(T * This, IXmlDocument * *value);                                    \
    HRESULT(STDMETHODCALLTYPE *InsertBefore)(T * This, IXmlNode * child, IXmlNode * reference, IXmlNode * *inserted);  \
    HRESULT(STDMETHODCALLTYPE *ReplaceChild)(T * This, IXmlNode * child, IXmlNode * reference, IXmlNode * *previous);  \
    HRESULT(STDMETHODCALLTYPE *RemoveChild)(T * This, IXmlNode * child, IXmlNode * *removed);                          \
    HRESULT(STDMETHODCALLTYPE *AppendChild)(T * This, IXmlNode * child, IXmlNode * *appended);                         \
    HRESULT(STDMETHODCALLTYPE *CloneNode)(T * This, BOOLEAN deep, IXmlNode * *clone);                                  \
    HRESULT(STDMETHODCALLTYPE *get_NamespaceUri)(T * This, IInspectable * *value);                                     \
    HRESULT(STDMETHODCALLTYPE *get_LocalName)(T * This, IInspectable * *value);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Prefix)(T * This, IInspectable * *value);                                           \
    HRESULT(STDMETHODCALLTYPE *Normalize)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *put_Prefix)(T * This, IInspectable * value);
MIME_COM_INTERFACE(IXmlNode)

typedef struct IXmlNodeSerializer IXmlNodeSerializer;
#define IXmlNodeSerializer_METHODS(T)                                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetXml)(T * This, HSTRING * xml);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_InnerText)(T * This, HSTRING * value);                                              \
    HRESULT(STDMETHODCALLTYPE *put_InnerText)(T * This, HSTRING value);
MIME_COM_INTERFACE(IXmlNodeSerializer)

typedef struct IXmlNodeSelector IXmlNodeSelector;
#define IXmlNodeSelector_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *SelectSingleNode)(T * This, HSTRING xpath, IXmlNode * *node);                           \
    HRESULT(STDMETHODCALLTYPE *SelectNodes)(T * This, HSTRING xpath, IXmlNodeList * *nodes);                           \
    HRESULT(STDMETHODCALLTYPE *SelectSingleNodeNS)(T * This, HSTRING xpath, IInspectable * ns, IXmlNode * *node);      \
    HRESULT(STDMETHODCALLTYPE *SelectNodesNS)(T * This, HSTRING xpath, IInspectable * ns, IXmlNodeList * *nodes);
MIME_COM_INTERFACE(IXmlNodeSelector)

#define IXmlNodeList_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Length)(T * This, UINT32 * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *Item)(T * This, UINT32 index, IXmlNode * *node);
MIME_COM_INTERFACE(IXmlNodeList)

#define IXmlNamedNodeMap_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Length)(T * This, UINT32 * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *Item)(T * This, UINT32 index, IXmlNode * *node);                                        \
    HRESULT(STDMETHODCALLTYPE *GetNamedItem)(T * This, HSTRING name, IXmlNode * *node);                                \
    HRESULT(STDMETHODCALLTYPE *SetNamedItem)(T * This, IXmlNode * node, IXmlNode * *previous);                         \
    HRESULT(STDMETHODCALLTYPE *RemoveNamedItem)(T * This, HSTRING name, IXmlNode * *previous);                         \
    HRESULT(STDMETHODCALLTYPE *GetNamedItemNS)(T * This, IInspectable * ns, HSTRING name, IXmlNode * *node);           \
    HRESULT(STDMETHODCALLTYPE *RemoveNamedItemNS)(T * This, IInspectable * ns, HSTRING name, IXmlNode * *previous);    \
    HRESULT(STDMETHODCALLTYPE *SetNamedItemNS)(T * This, IXmlNode * node, IXmlNode * *previous);
MIME_COM_INTERFACE(IXmlNamedNodeMap)

#define IXmlDocument_METHODS(T)                                                                                        \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Doctype)(T * This, IXmlDocumentType * *value);                                      \
    HRESULT(STDMETHODCALLTYPE *get_Implementation)(T * This, IXmlDomImplementation * *value);                          \
    HRESULT(STDMETHODCALLTYPE *get_DocumentElement)(T * This, IXmlElement * *value);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateElement)(T * This, HSTRING name, IXmlElement * *element);                         \
    HRESULT(STDMETHODCALLTYPE *CreateDocumentFragment)(T * This, IXmlDocumentFragment * *fragment);                    \
    HRESULT(STDMETHODCALLTYPE *CreateTextNode)(T * This, HSTRING data, IXmlText * *text);                              \
    HRESULT(STDMETHODCALLTYPE *CreateComment)(T * This, HSTRING data, IXmlComment * *comment);                         \
    HRESULT(STDMETHODCALLTYPE *CreateProcessingInstruction)(T * This, HSTRING target, HSTRING data,                    \
                                                            IXmlProcessingInstruction * *pi);                          \
    HRESULT(STDMETHODCALLTYPE *CreateAttribute)(T * This, HSTRING name, IXmlAttribute * *attribute);                   \
    HRESULT(STDMETHODCALLTYPE *CreateEntityReference)(T * This, HSTRING name, IXmlEntityReference * *reference);       \
    HRESULT(STDMETHODCALLTYPE *GetElementsByTagName)(T * This, HSTRING name, IXmlNodeList * *elements);                \
    HRESULT(STDMETHODCALLTYPE *CreateCDataSection)(T * This, HSTRING data, IXmlCDataSection * *section);               \
    HRESULT(STDMETHODCALLTYPE *get_DocumentUri)(T * This, HSTRING * value);                                            \
    HRESULT(STDMETHODCALLTYPE *CreateAttributeNS)(T * This, IInspectable * ns, HSTRING name,                           \
                                                  IXmlAttribute * *attribute);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateElementNS)(T * This, IInspectable * ns, HSTRING name, IXmlElement * *element);     \
    HRESULT(STDMETHODCALLTYPE *GetElementById)(T * This, HSTRING id, IXmlElement * *element);                          \
    HRESULT(STDMETHODCALLTYPE *ImportNode)(T * This, IXmlNode * node, BOOLEAN deep, IXmlNode * *imported);
MIME_COM_INTERFACE(IXmlDocument)

typedef struct IXmlDocumentIO IXmlDocumentIO;
#define IXmlDocumentIO_METHODS(T)                                                                                      \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *LoadXml)(T * This, HSTRING xml);                                                        \
    HRESULT(STDMETHODCALLTYPE *LoadXmlWithSettings)(T * This, HSTRING xml, IInspectable * settings);                   \
    HRESULT(STDMETHODCALLTYPE *SaveToFileAsync)(T * This, IInspectable * file, IInspectable * *operation);
MIME_COM_INTERFACE(IXmlDocumentIO)

typedef struct IXmlDocumentIO2 IXmlDocumentIO2;
#define IXmlDocumentIO2_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *LoadXmlFromBuffer)(T * This, IInspectable * buffer);                                    \
    HRESULT(STDMETHODCALLTYPE *LoadXmlFromBufferWithSettings)(T * This, IInspectable * buffer,                         \
                                                              IInspectable * settings);
MIME_COM_INTERFACE(IXmlDocumentIO2)

#define IXmlElement_METHODS(T)                                                                                         \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_TagName)(T * This, HSTRING * value);                                                \
    HRESULT(STDMETHODCALLTYPE *GetAttribute)(T * This, HSTRING name, HSTRING * value);                                 \
    HRESULT(STDMETHODCALLTYPE *SetAttribute)(T * This, HSTRING name, HSTRING value);                                   \
    HRESULT(STDMETHODCALLTYPE *RemoveAttribute)(T * This, HSTRING name);                                               \
    HRESULT(STDMETHODCALLTYPE *GetAttributeNode)(T * This, HSTRING name, IXmlAttribute * *attribute);                  \
    HRESULT(STDMETHODCALLTYPE *SetAttributeNode)(T * This, IXmlAttribute * attribute, IXmlAttribute * *previous);      \
    HRESULT(STDMETHODCALLTYPE *RemoveAttributeNode)(T * This, IXmlAttribute * attribute, IXmlAttribute * *removed);    \
    HRESULT(STDMETHODCALLTYPE *GetElementsByTagName)(T * This, HSTRING name, IXmlNodeList * *elements);                \
    HRESULT(STDMETHODCALLTYPE *SetAttributeNS)(T * This, IInspectable * ns, HSTRING name, HSTRING value);              \
    HRESULT(STDMETHODCALLTYPE *GetAttributeNS)(T * This, IInspectable * ns, HSTRING name, HSTRING * value);            \
    HRESULT(STDMETHODCALLTYPE *RemoveAttributeNS)(T * This, IInspectable * ns, HSTRING name);                          \
    HRESULT(STDMETHODCALLTYPE *SetAttributeNodeNS)(T * This, IXmlAttribute * attribute, IXmlAttribute * *previous);    \
    HRESULT(STDMETHODCALLTYPE *GetAttributeNodeNS)(T * This, IInspectable * ns, HSTRING name,                          \
                                                   IXmlAttribute * *attribute);
MIME_COM_INTERFACE(IXmlElement)

#define IXmlAttribute_METHODS(T)                                                                                       \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, HSTRING * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_Specified)(T * This, BOOLEAN * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_Value)(T * This, HSTRING * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *put_Value)(T * This, HSTRING value);
MIME_COM_INTERFACE(IXmlAttribute)

typedef struct IXmlCharacterData IXmlCharacterData;
#define IXmlCharacterData_METHODS(T)                                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Data)(T * This, HSTRING * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *put_Data)(T * This, HSTRING value);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_Length)(T * This, UINT32 * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *SubstringData)(T * This, UINT32 offset, UINT32 count, HSTRING * data);                  \
    HRESULT(STDMETHODCALLTYPE *AppendData)(T * This, HSTRING data);                                                    \
    HRESULT(STDMETHODCALLTYPE *InsertData)(T * This, UINT32 offset, HSTRING data);                                     \
    HRESULT(STDMETHODCALLTYPE *DeleteData)(T * This, UINT32 offset, UINT32 count);                                     \
    HRESULT(STDMETHODCALLTYPE *ReplaceData)(T * This, UINT32 offset, UINT32 count, HSTRING data);
MIME_COM_INTERFACE(IXmlCharacterData)

#define IXmlText_METHODS(T)                                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *SplitText)(T * This, UINT32 offset, IXmlText * *second);
MIME_COM_INTERFACE(IXmlText)

/* interfaces that add no method to their parent. */
#define IXmlComment_METHODS(T) MIME_INSPECTABLE_METHODS(T)
MIME_COM_INTERFACE(IXmlComment)
#define IXmlCDataSection_METHODS(T) MIME_INSPECTABLE_METHODS(T)
MIME_COM_INTERFACE(IXmlCDataSection)
#define IXmlDocumentFragment_METHODS(T) MIME_INSPECTABLE_METHODS(T)
MIME_COM_INTERFACE(IXmlDocumentFragment)

MIME_DEFINE_GUID(IID_IXmlNode, 0x1C741D59, 0x2122, 0x47D5, 0xA8, 0x56, 0x83, 0xF3, 0xD4, 0x21, 0x48, 0x75);
MIME_DEFINE_GUID(IID_IXmlNodeSerializer, 0x5CC5B382, 0xE6DD, 0x4991, 0xAB, 0xEF, 0x06, 0xD8, 0xD2, 0xE7, 0xBD, 0x0C);
MIME_DEFINE_GUID(IID_IXmlNodeSelector, 0x63DBBA8B, 0xD0DB, 0x4FE1, 0xB7, 0x45, 0xF9, 0x43, 0x3A, 0xFD, 0xC2, 0x5B);
MIME_DEFINE_GUID(IID_IXmlNodeList, 0x8C60AD77, 0x83A4, 0x4EC1, 0x9C, 0x54, 0x7B, 0xA4, 0x29, 0xE1, 0x3D, 0xA6);
MIME_DEFINE_GUID(IID_IXmlNamedNodeMap, 0xB3A69EB0, 0xAAB0, 0x4B82, 0xA6, 0xFA, 0xB1, 0x45, 0x3F, 0x7C, 0x02, 0x1B);
MIME_DEFINE_GUID(IID_IXmlDocument, 0xF7F3A506, 0x1E87, 0x42D6, 0xBC, 0xFB, 0xB8, 0xC8, 0x09, 0xFA, 0x54, 0x94);
MIME_DEFINE_GUID(IID_IXmlDocumentIO, 0x6CD0E74E, 0xEE65, 0x4489, 0x9E, 0xBF, 0xCA, 0x43, 0xE8, 0x7B, 0xA6, 0x37);
MIME_DEFINE_GUID(IID_IXmlDocumentIO2, 0x5D034661, 0x7BD8, 0x4AD5, 0x9E, 0xBF, 0x81, 0xE6, 0x34, 0x72, 0x63, 0xB1);
MIME_DEFINE_GUID(IID_IXmlElement, 0x2DFB8A1F, 0x6B10, 0x4EF8, 0x9F, 0x83, 0xEF, 0xCC, 0xE8, 0xFA, 0xEC, 0x37);
MIME_DEFINE_GUID(IID_IXmlAttribute, 0xAC144AA4, 0xB4F1, 0x4DB6, 0xB2, 0x06, 0x8A, 0x22, 0xC3, 0x08, 0xDB, 0x0A);
MIME_DEFINE_GUID(IID_IXmlCharacterData, 0x132E42AB, 0x4E36, 0x4DF6, 0xB1, 0xC8, 0x0C, 0xE6, 0x2F, 0xD8, 0x8B, 0x26);
MIME_DEFINE_GUID(IID_IXmlText, 0xF931A4CB, 0x308D, 0x4760, 0xA1, 0xD5, 0x43, 0xB6, 0x74, 0x50, 0xAC, 0x7E);
MIME_DEFINE_GUID(IID_IXmlComment, 0xBCA474D5, 0xB61F, 0x4611, 0x9C, 0xAC, 0x2E, 0x92, 0xE3, 0x47, 0x6D, 0x47);
MIME_DEFINE_GUID(IID_IXmlCDataSection, 0x4D04B46F, 0xC8BD, 0x45B4, 0x88, 0x99, 0x04, 0x00, 0xD7, 0xC2, 0xC6, 0x0F);
MIME_DEFINE_GUID(IID_IXmlDocumentFragment, 0xE2EA6A96, 0x0C21, 0x44A5, 0x8B, 0xC9, 0x9E, 0x4A, 0x26, 0x27, 0x08, 0xEC);

/* ------------------------------------------------------------------ Windows.UI.Notifications */

typedef enum ToastTemplateType {
    ToastTemplateType_ToastImageAndText01 = 0,
    ToastTemplateType_ToastImageAndText02 = 1,
    ToastTemplateType_ToastImageAndText03 = 2,
    ToastTemplateType_ToastImageAndText04 = 3,
    ToastTemplateType_ToastText01 = 4,
    ToastTemplateType_ToastText02 = 5,
    ToastTemplateType_ToastText03 = 6,
    ToastTemplateType_ToastText04 = 7,
} ToastTemplateType;

typedef enum NotificationSetting {
    NotificationSetting_Enabled = 0,
    NotificationSetting_DisabledForApplication = 1,
    NotificationSetting_DisabledForUser = 2,
    NotificationSetting_DisabledByGroupPolicy = 3,
    NotificationSetting_DisabledByManifest = 4,
} NotificationSetting;

typedef enum ToastDismissalReason {
    ToastDismissalReason_UserCanceled = 0,
    ToastDismissalReason_ApplicationHidden = 1,
    ToastDismissalReason_TimedOut = 2,
} ToastDismissalReason;

typedef enum NotificationUpdateResult {
    NotificationUpdateResult_Succeeded = 0,
    NotificationUpdateResult_Failed = 1,
    NotificationUpdateResult_NotFound = 2,
} NotificationUpdateResult;

typedef struct IToastNotifier IToastNotifier;
typedef struct IToastNotification IToastNotification;
typedef struct IToastNotificationHistory IToastNotificationHistory;
typedef struct IToastNotificationManagerForUser IToastNotificationManagerForUser;

#define IToastNotificationManagerStatics_METHODS(T)                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateToastNotifier)(T * This, IToastNotifier * *notifier);                             \
    HRESULT(STDMETHODCALLTYPE *CreateToastNotifierWithId)(T * This, HSTRING id, IToastNotifier * *notifier);           \
    HRESULT(STDMETHODCALLTYPE *GetTemplateContent)(T * This, ToastTemplateType type, IXmlDocument * *content);
typedef struct IToastNotificationManagerStatics IToastNotificationManagerStatics;
MIME_COM_INTERFACE(IToastNotificationManagerStatics)

#define IToastNotificationManagerStatics2_METHODS(T)                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_History)(T * This, IToastNotificationHistory * *value);
typedef struct IToastNotificationManagerStatics2 IToastNotificationManagerStatics2;
MIME_COM_INTERFACE(IToastNotificationManagerStatics2)

#define IToastNotificationManagerStatics4_METHODS(T)                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetForUser)(T * This, IInspectable * user, IToastNotificationManagerForUser * *value);  \
    HRESULT(STDMETHODCALLTYPE *ConfigureNotificationMirroring)(T * This, INT32 value);
typedef struct IToastNotificationManagerStatics4 IToastNotificationManagerStatics4;
MIME_COM_INTERFACE(IToastNotificationManagerStatics4)

#define IToastNotificationManagerStatics5_METHODS(T)                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetDefault)(T * This, IToastNotificationManagerForUser * *value);
typedef struct IToastNotificationManagerStatics5 IToastNotificationManagerStatics5;
MIME_COM_INTERFACE(IToastNotificationManagerStatics5)

#define IToastNotificationManagerForUser_METHODS(T)                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateToastNotifier)(T * This, IToastNotifier * *notifier);                             \
    HRESULT(STDMETHODCALLTYPE *CreateToastNotifierWithId)(T * This, HSTRING id, IToastNotifier * *notifier);           \
    HRESULT(STDMETHODCALLTYPE *get_History)(T * This, IToastNotificationHistory * *value);                             \
    HRESULT(STDMETHODCALLTYPE *get_User)(T * This, IInspectable * *value);
MIME_COM_INTERFACE(IToastNotificationManagerForUser)

#define IToastNotifier_METHODS(T)                                                                                      \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Show)(T * This, IToastNotification * toast);                                            \
    HRESULT(STDMETHODCALLTYPE *Hide)(T * This, IToastNotification * toast);                                            \
    HRESULT(STDMETHODCALLTYPE *get_Setting)(T * This, INT32 * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *AddToSchedule)(T * This, IInspectable * toast);                                         \
    HRESULT(STDMETHODCALLTYPE *RemoveFromSchedule)(T * This, IInspectable * toast);                                    \
    HRESULT(STDMETHODCALLTYPE *GetScheduledToastNotifications)(T * This, IVectorViewPtr * *value);
MIME_COM_INTERFACE(IToastNotifier)

typedef struct IToastNotifier2 IToastNotifier2;
#define IToastNotifier2_METHODS(T)                                                                                     \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *UpdateWithTagAndGroup)(T * This, IInspectable * data, HSTRING tag, HSTRING group,       \
                                                      INT32 * result);                                                 \
    HRESULT(STDMETHODCALLTYPE *UpdateWithTag)(T * This, IInspectable * data, HSTRING tag, INT32 * result);
MIME_COM_INTERFACE(IToastNotifier2)

#define IToastNotification_METHODS(T)                                                                                  \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Content)(T * This, IXmlDocument * *value);                                          \
    HRESULT(STDMETHODCALLTYPE *put_ExpirationTime)(T * This, IReferenceDateTime * value);                              \
    HRESULT(STDMETHODCALLTYPE *get_ExpirationTime)(T * This, IReferenceDateTime * *value);                             \
    HRESULT(STDMETHODCALLTYPE *add_Dismissed)(T * This, ITypedEventHandler * handler, EventRegistrationToken * token); \
    HRESULT(STDMETHODCALLTYPE *remove_Dismissed)(T * This, EventRegistrationToken token);                              \
    HRESULT(STDMETHODCALLTYPE *add_Activated)(T * This, ITypedEventHandler * handler, EventRegistrationToken * token); \
    HRESULT(STDMETHODCALLTYPE *remove_Activated)(T * This, EventRegistrationToken token);                              \
    HRESULT(STDMETHODCALLTYPE *add_Failed)(T * This, ITypedEventHandler * handler, EventRegistrationToken * token);    \
    HRESULT(STDMETHODCALLTYPE *remove_Failed)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IToastNotification)

typedef struct IToastNotification2 IToastNotification2;
#define IToastNotification2_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *put_Tag)(T * This, HSTRING value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Tag)(T * This, HSTRING * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *put_Group)(T * This, HSTRING value);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_Group)(T * This, HSTRING * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *put_SuppressPopup)(T * This, BOOLEAN value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_SuppressPopup)(T * This, BOOLEAN * value);
MIME_COM_INTERFACE(IToastNotification2)

typedef struct IToastNotification3 IToastNotification3;
#define IToastNotification3_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_NotificationMirroring)(T * This, INT32 * value);                                    \
    HRESULT(STDMETHODCALLTYPE *put_NotificationMirroring)(T * This, INT32 value);                                      \
    HRESULT(STDMETHODCALLTYPE *get_RemoteId)(T * This, HSTRING * value);                                               \
    HRESULT(STDMETHODCALLTYPE *put_RemoteId)(T * This, HSTRING value);
MIME_COM_INTERFACE(IToastNotification3)

typedef struct IToastNotification4 IToastNotification4;
#define IToastNotification4_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Data)(T * This, IInspectable * *value);                                             \
    HRESULT(STDMETHODCALLTYPE *put_Data)(T * This, IInspectable * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_Priority)(T * This, INT32 * value);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_Priority)(T * This, INT32 value);
MIME_COM_INTERFACE(IToastNotification4)

typedef struct IToastNotification6 IToastNotification6;
#define IToastNotification6_METHODS(T)                                                                                 \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_ExpiresOnReboot)(T * This, BOOLEAN * value);                                        \
    HRESULT(STDMETHODCALLTYPE *put_ExpiresOnReboot)(T * This, BOOLEAN value);
MIME_COM_INTERFACE(IToastNotification6)

typedef struct IToastNotificationFactory IToastNotificationFactory;
#define IToastNotificationFactory_METHODS(T)                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateToastNotification)(T * This, IXmlDocument * content, IToastNotification * *value);
MIME_COM_INTERFACE(IToastNotificationFactory)

#define IToastNotificationHistory_METHODS(T)                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *RemoveGroup)(T * This, HSTRING group);                                                  \
    HRESULT(STDMETHODCALLTYPE *RemoveGroupWithId)(T * This, HSTRING group, HSTRING id);                                \
    HRESULT(STDMETHODCALLTYPE *RemoveGroupedTagWithId)(T * This, HSTRING tag, HSTRING group, HSTRING id);              \
    HRESULT(STDMETHODCALLTYPE *RemoveGroupedTag)(T * This, HSTRING tag, HSTRING group);                                \
    HRESULT(STDMETHODCALLTYPE *Remove)(T * This, HSTRING tag);                                                         \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *ClearWithId)(T * This, HSTRING id);
MIME_COM_INTERFACE(IToastNotificationHistory)

typedef struct IToastNotificationHistory2 IToastNotificationHistory2;
#define IToastNotificationHistory2_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetHistory)(T * This, IVectorViewPtr * *value);                                         \
    HRESULT(STDMETHODCALLTYPE *GetHistoryWithId)(T * This, HSTRING id, IVectorViewPtr * *value);
MIME_COM_INTERFACE(IToastNotificationHistory2)

typedef struct IToastDismissedEventArgs IToastDismissedEventArgs;
#define IToastDismissedEventArgs_METHODS(T)                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Reason)(T * This, ToastDismissalReason * value);
MIME_COM_INTERFACE(IToastDismissedEventArgs)

typedef struct IToastActivatedEventArgs IToastActivatedEventArgs;
#define IToastActivatedEventArgs_METHODS(T)                                                                            \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Arguments)(T * This, HSTRING * value);
MIME_COM_INTERFACE(IToastActivatedEventArgs)

typedef struct IToastActivatedEventArgs2 IToastActivatedEventArgs2;
#define IToastActivatedEventArgs2_METHODS(T)                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_UserInput)(T * This, IInspectable * *value);
MIME_COM_INTERFACE(IToastActivatedEventArgs2)

typedef struct IToastFailedEventArgs IToastFailedEventArgs;
#define IToastFailedEventArgs_METHODS(T)                                                                               \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_ErrorCode)(T * This, HRESULT * value);
MIME_COM_INTERFACE(IToastFailedEventArgs)

MIME_DEFINE_GUID(IID_IToastNotificationManagerStatics, 0x50AC103F, 0xD235, 0x4598, 0xBB, 0xEF, 0x98, 0xFE, 0x4D, 0x1A,
                 0x3A, 0xD4);
MIME_DEFINE_GUID(IID_IToastNotificationManagerStatics2, 0x7AB93C52, 0x0E48, 0x4750, 0xBA, 0x9D, 0x1A, 0x41, 0x13, 0x98,
                 0x18, 0x47);
MIME_DEFINE_GUID(IID_IToastNotificationManagerStatics4, 0x8F993FD3, 0xE516, 0x45FB, 0x81, 0x30, 0x39, 0x8E, 0x93, 0xFA,
                 0x52, 0xC3);
MIME_DEFINE_GUID(IID_IToastNotificationManagerStatics5, 0xD6F5F569, 0xD40D, 0x407C, 0x89, 0x89, 0x88, 0xCA, 0xB4, 0x2C,
                 0xFD, 0x14);
MIME_DEFINE_GUID(IID_IToastNotificationManagerForUser, 0x79AB57F6, 0x43FE, 0x487B, 0x8A, 0x7F, 0x99, 0x56, 0x72, 0x00,
                 0xAE, 0x94);
MIME_DEFINE_GUID(IID_IToastNotifier, 0x75927B93, 0x03F3, 0x41EC, 0x91, 0xD3, 0x6E, 0x5B, 0xAC, 0x1B, 0x38, 0xE7);
MIME_DEFINE_GUID(IID_IToastNotifier2, 0x354389C6, 0x7C01, 0x4BD5, 0x9C, 0x20, 0x60, 0x43, 0x40, 0xCD, 0x2B, 0x74);
MIME_DEFINE_GUID(IID_IToastNotification, 0x997E2675, 0x059E, 0x4E60, 0x8B, 0x06, 0x17, 0x60, 0x91, 0x7C, 0x8B, 0x80);
MIME_DEFINE_GUID(IID_IToastNotification2, 0x9DFB9FD1, 0x143A, 0x490E, 0x90, 0xBF, 0xB9, 0xFB, 0xA7, 0x13, 0x2D, 0xE7);
MIME_DEFINE_GUID(IID_IToastNotification3, 0x31E8AED8, 0x8141, 0x4F99, 0xBC, 0x0A, 0xC4, 0xED, 0x21, 0x29, 0x7D, 0x77);
MIME_DEFINE_GUID(IID_IToastNotification4, 0x15154935, 0x28EA, 0x4727, 0x88, 0xE9, 0xC5, 0x86, 0x80, 0xE2, 0xD1, 0x18);
MIME_DEFINE_GUID(IID_IToastNotification6, 0x43EBFE53, 0x89AE, 0x5C1E, 0xA2, 0x79, 0x3A, 0xEC, 0xFE, 0x9B, 0x6F, 0x54);
MIME_DEFINE_GUID(IID_IToastNotificationFactory, 0x04124B20, 0x82C6, 0x4229, 0xB1, 0x09, 0xFD, 0x9E, 0xD4, 0x66, 0x2B,
                 0x53);
MIME_DEFINE_GUID(IID_IToastNotificationHistory, 0x5CADDC63, 0x01D3, 0x4C97, 0x98, 0x6F, 0x05, 0x33, 0x48, 0x3F, 0xEE,
                 0x14);
MIME_DEFINE_GUID(IID_IToastNotificationHistory2, 0x3BC3D253, 0x2F31, 0x4092, 0x91, 0x29, 0x8A, 0xD5, 0xAB, 0xF0, 0x67,
                 0xDA);
MIME_DEFINE_GUID(IID_IToastDismissedEventArgs, 0x3F89D935, 0xD9CB, 0x4538, 0xA0, 0xF0, 0xFF, 0xE7, 0x65, 0x99, 0x38,
                 0xF8);
MIME_DEFINE_GUID(IID_IToastActivatedEventArgs, 0xE3BF92F3, 0xC197, 0x436F, 0x82, 0x65, 0x06, 0x25, 0x82, 0x4F, 0x8D,
                 0xAC);
MIME_DEFINE_GUID(IID_IToastFailedEventArgs, 0x35176862, 0xCFD4, 0x44F8, 0xAD, 0x64, 0xF5, 0x00, 0xFD, 0x89, 0x6C, 0x3B);
/* IVectorView<ToastNotification>. */
MIME_DEFINE_GUID(IID_IVectorViewToast, 0xA819F3DE, 0x60AA, 0x5159, 0x84, 0x07, 0xF0, 0xA7, 0xFB, 0x1F, 0x68, 0x32);

/* the error a program gets for a notifier of a process that has no identity: element not found. */
#define TOAST_E_NO_IDENTITY ((HRESULT)0x80070490)

#endif
