/*
 * Windows.Data.Xml.Dom, shared with the toast code that reads its content from it.
 */
#ifndef MIME_COMBASE_XMLDOM_H
#define MIME_COMBASE_XMLDOM_H

#include "combase_private.h"
#include <mime/win32/toast.h>
#include <mime/winrt_object.h>

typedef struct XCTX XCTX;

/*
 * a document's nodes live as long as a program holds any of them. each node counts program references, and its first
 * one takes a reference on the document's context, which frees every node when released. links between nodes take
 * no references.
 */
typedef struct XNODE {
    WRT_OBJECT Base;
    XCTX *Ctx;
    struct XNODE *CtxNext;
    NodeType Type;
    WCHAR *Name;  /* tag, attribute name or instruction target. */
    WCHAR *Value; /* text of a text, comment or section, attribute value, or instruction data. */
    struct XNODE *Parent, *Prev, *Next, *First, *Last;
    struct XNODE *Attrs;    /* an element's attributes, linked by AttrNext. */
    struct XNODE *AttrNext;
    WCHAR *Uri; /* a document's address. */
} XNODE;

/* null for another implementation's interfaces. */
XNODE *CbXmlFromInterface(IUnknown *object);

/* a new empty document with one reference. */
HRESULT CbXmlNewDocument(IXmlDocument **document);

/* the document must be empty; its content is replaced. */
HRESULT CbXmlLoadText(XNODE *document, const WCHAR *text, SIZE_T length);

XNODE *CbXmlRootElement(XNODE *document);

XNODE *CbXmlAttribute(XNODE *element, const WCHAR *name);

/* the text of a node and its descendants, freed with CbFree. */
WCHAR *CbXmlInnerText(XNODE *node);

/* returns the length in characters. */
WCHAR *CbXmlSerialize(XNODE *node, SIZE_T *length);

/* PropertyValue boxes holding a string. */
HRESULT CbBoxString(const WCHAR *text, IInspectable **box);
HRESULT CbUnboxString(IInspectable *box, WCHAR **text);

/* answers IReferenceDateTime and IPropertyValue. */
HRESULT CbBoxDateTime(DateTime time, IReferenceDateTime **box);

/* program references on a node. */
ULONG CbXmlAddRef(XNODE *node);
ULONG CbXmlRelease(XNODE *node);

/* Windows.Data.Xml.Dom.XmlDocument and Windows.Foundation.PropertyValue. */
extern const WCHAR CbXmlDocumentClass[], CbPropertyValueClass[];
HRESULT CbXmlDocumentFactory(REFIID riid, void **factory);
HRESULT CbPropertyValueFactory(REFIID riid, void **factory);

/* toast.c. the manager only answers where the host can show notifications. */
extern const WCHAR CbToastManagerClass[], CbToastClass[];
HRESULT CbToastManagerFactory(REFIID riid, void **factory);
HRESULT CbToastFactory(REFIID riid, void **factory);

#endif
