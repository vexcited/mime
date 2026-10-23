#ifndef MIME_WINSPOOL_PDF_H
#define MIME_WINSPOOL_PDF_H

typedef struct PdfWriter PdfWriter;

/* starts a document on an open file; the caller remains responsible for closing the file. */
PdfWriter *PdfCreate(HANDLE file, LPCWSTR title);
/* adds top-down rows of 32-bit pixels; gray requests grayscale output. */
BOOL PdfAddImagePage(PdfWriter *pdf, const BYTE *bgra, int width, int height, int stride, int xdpi, int ydpi,
                     BOOL gray);
UINT PdfPageCount(const PdfWriter *pdf);
/* ends the document and frees the writer. */
BOOL PdfFinish(PdfWriter *pdf);
/* frees the writer without ending the document. */
void PdfAbort(PdfWriter *pdf);

#endif
