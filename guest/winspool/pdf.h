/*
 * the PDF writer of the print processor.
 */
#ifndef MIME_WINSPOOL_PDF_H
#define MIME_WINSPOOL_PDF_H

typedef struct PdfWriter PdfWriter;

/* starts a document in a file that is open for writing; the file stays the caller's to close. */
PdfWriter *PdfCreate(HANDLE file, LPCWSTR title);
/* adds a page of 32 bit pixels, a row after the other, top first; gray writes a gray scale page. */
BOOL PdfAddImagePage(PdfWriter *pdf, const BYTE *bgra, int width, int height, int stride, int xdpi, int ydpi,
                     BOOL gray);
UINT PdfPageCount(const PdfWriter *pdf);
/* ends the document and frees the writer. */
BOOL PdfFinish(PdfWriter *pdf);
/* frees the writer without ending the document. */
void PdfAbort(PdfWriter *pdf);

#endif
