/*
 * the XML document object model of Windows.Data.Xml.Dom, shared by its implementation and the toast notifications
 * that read their content from it.
 */
#ifndef MIME_COMBASE_XMLDOM_H
#define MIME_COMBASE_XMLDOM_H

#include "combase_private.h"
#include <mime/win32/toast.h>
#include <mime/winrt_object.h>

typedef struct XCTX XCTX;

/*
 * a node. the nodes of a document live as long as any of them is held by a program: every node counts the references
 * programs hold of it, and the first one of a node takes a reference of the document's context, which frees all the
 * nodes when the last is given back. links between nodes take none.
 */
typedef struct XNODE {
    WRT_OBJECT Base;
    XCTX *Ctx;
    struct XNODE *CtxNext;
    NodeType Type;
    WCHAR *Name;  /* the tag, the attribute, the target of an instruction */
    WCHAR *Value; /* the text of a text, comment or section, the value of an attribute, the data of an instruction */
    struct XNODE *Parent, *Prev, *Next, *First, *Last;
    struct XNODE *Attrs;    /* an element's attributes, linked by AttrNext */
    struct XNODE *AttrNext;
    WCHAR *Uri; /* a document's address */
} XNODE;

/* the node behind an interface pointer of this implementation, or null for another one. */
XNODE *CbXmlFromInterface(IUnknown *object);

/* a new empty document with one reference. */
HRESULT CbXmlNewDocument(IXmlDocument **document);

/* loads text into a document that was made empty, replacing what it held. */
HRESULT CbXmlLoadText(XNODE *document, const WCHAR *text, SIZE_T length);

/* the document's root element, or null. */
XNODE *CbXmlRootElement(XNODE *document);

/* an attribute by name, or null. */
XNODE *CbXmlAttribute(XNODE *element, const WCHAR *name);

/* the text content of a node and of everything under it, in a new allocation, free it with CbFree. */
WCHAR *CbXmlInnerText(XNODE *node);

/* the markup of a node in a new allocation, with its length in characters. */
WCHAR *CbXmlSerialize(XNODE *node, SIZE_T *length);

/* a boxed string, a Windows.Foundation.PropertyValue holding it, and the string of a boxed value. */
HRESULT CbBoxString(const WCHAR *text, IInspectable **box);
HRESULT CbUnboxString(IInspectable *box, WCHAR **text);

/* a boxed date, which answers IReferenceDateTime and IPropertyValue. */
HRESULT CbBoxDateTime(DateTime time, IReferenceDateTime **box);

/* one reference taken or dropped for a node, as a program does. */
ULONG CbXmlAddRef(XNODE *node);
ULONG CbXmlRelease(XNODE *node);

/* the table of the classes: Windows.Data.Xml.Dom.XmlDocument and Windows.Foundation.PropertyValue. */
extern const WCHAR CbXmlDocumentClass[], CbPropertyValueClass[];
HRESULT CbXmlDocumentFactory(REFIID riid, void **factory);
HRESULT CbPropertyValueFactory(REFIID riid, void **factory);

/* toast.c: Windows.UI.Notifications. the manager answers only where the host can show notifications. */
extern const WCHAR CbToastManagerClass[], CbToastClass[];
HRESULT CbToastManagerFactory(REFIID riid, void **factory);
HRESULT CbToastFactory(REFIID riid, void **factory);

#endif
