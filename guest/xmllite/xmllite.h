/*
 * the interfaces of xmllite.dll, the pull parser and the writer of xml, with the calls between its files.
 */
#ifndef XMLLITE_H
#define XMLLITE_H

#include <mime/win32/winbase.h>
#include <mime/win32/objbase.h>
#include <mime/win32/objidl.h>
#include <mime/win32/winerror.h>

#define MX_E_INPUTEND ((HRESULT)0xC00CEE01)
#define MX_E_ENCODING ((HRESULT)0xC00CEE02)
#define MX_E_ENCODINGSWITCH ((HRESULT)0xC00CEE03)
#define MX_E_ENCODINGSIGNATURE ((HRESULT)0xC00CEE04)
#define WC_E_WHITESPACE ((HRESULT)0xC00CEE21)
#define WC_E_SEMICOLON ((HRESULT)0xC00CEE22)
#define WC_E_GREATERTHAN ((HRESULT)0xC00CEE23)
#define WC_E_QUOTE ((HRESULT)0xC00CEE24)
#define WC_E_EQUAL ((HRESULT)0xC00CEE25)
#define WC_E_LESSTHAN ((HRESULT)0xC00CEE26)
#define WC_E_HEXDIGIT ((HRESULT)0xC00CEE27)
#define WC_E_DIGIT ((HRESULT)0xC00CEE28)
#define WC_E_XMLCHARACTER ((HRESULT)0xC00CEE2B)
#define WC_E_NAMECHARACTER ((HRESULT)0xC00CEE2C)
#define WC_E_SYNTAX ((HRESULT)0xC00CEE2D)
#define WC_E_CDSECT ((HRESULT)0xC00CEE2E)
#define WC_E_COMMENT ((HRESULT)0xC00CEE2F)
#define WC_E_DECLDOCTYPE ((HRESULT)0xC00CEE32)
#define WC_E_NAME ((HRESULT)0xC00CEE39)
#define WC_E_ROOTELEMENT ((HRESULT)0xC00CEE3A)
#define WC_E_ELEMENTMATCH ((HRESULT)0xC00CEE3B)
#define WC_E_UNIQUEATTRIBUTE ((HRESULT)0xC00CEE3C)
#define WC_E_LEADINGXML ((HRESULT)0xC00CEE3E)
#define WC_E_XMLDECL ((HRESULT)0xC00CEE40)
#define WC_E_ENCNAME ((HRESULT)0xC00CEE41)
#define WC_E_UNDECLAREDENTITY ((HRESULT)0xC00CEE47)
#define WC_E_PI ((HRESULT)0xC00CEE4A)
#define WC_E_CDSECTEND ((HRESULT)0xC00CEE4D)
#define WC_E_DTDPROHIBITED ((HRESULT)0xC00CEE4F)
#define WC_E_INVALIDXMLSPACE ((HRESULT)0xC00CEE50)
#define NC_E_QNAMECOLON ((HRESULT)0xC00CEE62)
#define NC_E_NAMECOLON ((HRESULT)0xC00CEE63)
#define NC_E_UNDECLAREDPREFIX ((HRESULT)0xC00CEE65)
#define NC_E_EMPTYURI ((HRESULT)0xC00CEE66)
#define SC_E_MAXELEMENTDEPTH ((HRESULT)0xC00CEE81)
#define WR_E_NONWHITESPACE ((HRESULT)0xC00CEF01)
#define WR_E_NSPREFIXDECLARED ((HRESULT)0xC00CEF02)
#define WR_E_NSPREFIXWITHEMPTYNSURI ((HRESULT)0xC00CEF03)
#define WR_E_DUPLICATEATTRIBUTE ((HRESULT)0xC00CEF04)
#define WR_E_NAMESPACEUNDECLARED ((HRESULT)0xC00CEF09)
#define WR_E_INVALIDACTION ((HRESULT)0xC00CEF0B)
#define WR_E_INVALIDSURROGATEPAIR ((HRESULT)0xC00CEF0C)

typedef enum {
    XmlNodeType_None = 0,
    XmlNodeType_Element = 1,
    XmlNodeType_Attribute = 2,
    XmlNodeType_Text = 3,
    XmlNodeType_CDATA = 4,
    XmlNodeType_ProcessingInstruction = 7,
    XmlNodeType_Comment = 8,
    XmlNodeType_DocumentType = 10,
    XmlNodeType_Whitespace = 13,
    XmlNodeType_EndElement = 15,
    XmlNodeType_XmlDeclaration = 17,
} XmlNodeType;

enum { XmlReaderProperty_MultiLanguage, XmlReaderProperty_ConformanceLevel, XmlReaderProperty_RandomAccess,
       XmlReaderProperty_XmlResolver, XmlReaderProperty_DtdProcessing, XmlReaderProperty_ReadState,
       XmlReaderProperty_MaxElementDepth, XmlReaderProperty_MaxEntityExpansion };
enum { XmlWriterProperty_MultiLanguage, XmlWriterProperty_Indent, XmlWriterProperty_ByteOrderMark,
       XmlWriterProperty_OmitXmlDeclaration, XmlWriterProperty_ConformanceLevel };
enum { XmlConformanceLevel_Auto, XmlConformanceLevel_Fragment, XmlConformanceLevel_Document };
enum { DtdProcessing_Prohibit, DtdProcessing_Parse };
enum { XmlReadState_Initial, XmlReadState_Interactive, XmlReadState_Error, XmlReadState_EndOfFile, XmlReadState_Closed };

typedef struct IXmlReader IXmlReader;
#define IXmlReader_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetInput)(T * This, IUnknown * input);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, UINT property, LONG_PTR * value);                                \
    HRESULT(STDMETHODCALLTYPE *SetProperty)(T * This, UINT property, LONG_PTR value);                                  \
    HRESULT(STDMETHODCALLTYPE *Read)(T * This, XmlNodeType * type);                                                    \
    HRESULT(STDMETHODCALLTYPE *GetNodeType)(T * This, XmlNodeType * type);                                             \
    HRESULT(STDMETHODCALLTYPE *MoveToFirstAttribute)(T * This);                                                        \
    HRESULT(STDMETHODCALLTYPE *MoveToNextAttribute)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *MoveToAttributeByName)(T * This, LPCWSTR local_name, LPCWSTR namespace_uri);            \
    HRESULT(STDMETHODCALLTYPE *MoveToElement)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *GetQualifiedName)(T * This, LPCWSTR * name, UINT * length);                             \
    HRESULT(STDMETHODCALLTYPE *GetNamespaceUri)(T * This, LPCWSTR * uri, UINT * length);                               \
    HRESULT(STDMETHODCALLTYPE *GetLocalName)(T * This, LPCWSTR * name, UINT * length);                                 \
    HRESULT(STDMETHODCALLTYPE *GetPrefix)(T * This, LPCWSTR * prefix, UINT * length);                                  \
    HRESULT(STDMETHODCALLTYPE *GetValue)(T * This, LPCWSTR * value, UINT * length);                                    \
    HRESULT(STDMETHODCALLTYPE *ReadValueChunk)(T * This, WCHAR * buffer, UINT size, UINT * read);                      \
    HRESULT(STDMETHODCALLTYPE *GetBaseUri)(T * This, LPCWSTR * uri, UINT * length);                                    \
    BOOL(STDMETHODCALLTYPE *IsDefault)(T * This);                                                                      \
    BOOL(STDMETHODCALLTYPE *IsEmptyElement)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *GetLineNumber)(T * This, UINT * line);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetLinePosition)(T * This, UINT * position);                                            \
    HRESULT(STDMETHODCALLTYPE *GetAttributeCount)(T * This, UINT * count);                                             \
    HRESULT(STDMETHODCALLTYPE *GetDepth)(T * This, UINT * depth);                                                      \
    BOOL(STDMETHODCALLTYPE *IsEOF)(T * This);
MIME_COM_INTERFACE(IXmlReader)

typedef struct IXmlWriter IXmlWriter;
#define IXmlWriter_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetOutput)(T * This, IUnknown * output);                                                \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, UINT property, LONG_PTR * value);                                \
    HRESULT(STDMETHODCALLTYPE *SetProperty)(T * This, UINT property, LONG_PTR value);                                  \
    HRESULT(STDMETHODCALLTYPE *WriteAttributes)(T * This, IXmlReader * reader, BOOL defaults);                         \
    HRESULT(STDMETHODCALLTYPE *WriteAttributeString)(T * This, LPCWSTR prefix, LPCWSTR local_name, LPCWSTR uri,        \
                                                     LPCWSTR value);                                                   \
    HRESULT(STDMETHODCALLTYPE *WriteCData)(T * This, LPCWSTR text);                                                    \
    HRESULT(STDMETHODCALLTYPE *WriteCharEntity)(T * This, WCHAR character);                                            \
    HRESULT(STDMETHODCALLTYPE *WriteChars)(T * This, const WCHAR *chars, UINT count);                                  \
    HRESULT(STDMETHODCALLTYPE *WriteComment)(T * This, LPCWSTR text);                                                  \
    HRESULT(STDMETHODCALLTYPE *WriteDocType)(T * This, LPCWSTR name, LPCWSTR public_id, LPCWSTR system_id,             \
                                             LPCWSTR subset);                                                          \
    HRESULT(STDMETHODCALLTYPE *WriteElementString)(T * This, LPCWSTR prefix, LPCWSTR local_name, LPCWSTR uri,          \
                                                   LPCWSTR value);                                                     \
    HRESULT(STDMETHODCALLTYPE *WriteEndDocument)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *WriteEndElement)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *WriteEntityRef)(T * This, LPCWSTR name);                                                \
    HRESULT(STDMETHODCALLTYPE *WriteFullEndElement)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *WriteName)(T * This, LPCWSTR name);                                                     \
    HRESULT(STDMETHODCALLTYPE *WriteNmToken)(T * This, LPCWSTR token);                                                 \
    HRESULT(STDMETHODCALLTYPE *WriteNode)(T * This, IXmlReader * reader, BOOL defaults);                               \
    HRESULT(STDMETHODCALLTYPE *WriteNodeShallow)(T * This, IXmlReader * reader, BOOL defaults);                        \
    HRESULT(STDMETHODCALLTYPE *WriteProcessingInstruction)(T * This, LPCWSTR name, LPCWSTR text);                      \
    HRESULT(STDMETHODCALLTYPE *WriteQualifiedName)(T * This, LPCWSTR local_name, LPCWSTR uri);                         \
    HRESULT(STDMETHODCALLTYPE *WriteRaw)(T * This, LPCWSTR text);                                                      \
    HRESULT(STDMETHODCALLTYPE *WriteRawChars)(T * This, const WCHAR *chars, UINT count);                               \
    HRESULT(STDMETHODCALLTYPE *WriteStartDocument)(T * This, UINT standalone);                                         \
    HRESULT(STDMETHODCALLTYPE *WriteStartElement)(T * This, LPCWSTR prefix, LPCWSTR local_name, LPCWSTR uri);          \
    HRESULT(STDMETHODCALLTYPE *WriteString)(T * This, LPCWSTR text);                                                   \
    HRESULT(STDMETHODCALLTYPE *WriteSurrogateCharEntity)(T * This, WCHAR low, WCHAR high);                             \
    HRESULT(STDMETHODCALLTYPE *WriteWhitespace)(T * This, LPCWSTR whitespace);                                         \
    HRESULT(STDMETHODCALLTYPE *Flush)(T * This);
MIME_COM_INTERFACE(IXmlWriter)

MIME_DEFINE_GUID(IID_IXmlReader, 0x7279FC81, 0x709D, 0x4095, 0xB6, 0x3D, 0x69, 0xFE, 0x4B, 0x0D, 0x90, 0x30);
MIME_DEFINE_GUID(IID_IXmlWriter, 0x7279FC88, 0x709D, 0x4095, 0xB6, 0x3D, 0x69, 0xFE, 0x4B, 0x0D, 0x90, 0x30);

/* what the input and the output objects hold, so the reader and the writer can take them. */
typedef struct XML_STREAM_OBJECT {
    const IUnknownVtbl *lpVtbl;
    LONG references;
    IUnknown *stream;
    UINT code_page;
    BOOL hint;
    WCHAR *base_uri;
} XML_STREAM_OBJECT;

extern const IUnknownVtbl XmlInputVtbl;
extern const IUnknownVtbl XmlOutputVtbl;

/* the code page a name of an encoding stands for, or 0 for a name that is not known. */
UINT XmlCodePageOfName(const WCHAR *name, SIZE_T length);

HRESULT XmlCreateReader(REFIID iid, void **object);
HRESULT XmlCreateWriter(REFIID iid, void **object);

void *XmlAlloc(SIZE_T size);
void *XmlRealloc(void *block, SIZE_T size);
void XmlFree(void *block);
WCHAR *XmlDuplicate(const WCHAR *text, SIZE_T length);

#endif
