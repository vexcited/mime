/* enhanced metafile internals shared by the recorder and the player. */
#pragma once
#include "gdi32_private.h"

#define EMF_HEADER_SIZE 108u
#define EMF_CAPS 128

/* the device bounds of a record in progress, in floating point. */
typedef struct EMF_BOUNDS {
    double Left, Top, Right, Bottom;
    BOOL Empty;
} EMF_BOUNDS;

typedef struct EMF_DC {
    HDC Dc;
    BYTE *Data;
    DWORD Size;
    DWORD Capacity;
    DWORD Records;
    BOOL Failed;
    HANDLE File;
    WCHAR Path[MAX_PATH];
    /* the handle table by index, index 0 being the metafile itself. */
    HGDIOBJ *Objects;
    DWORD ObjectCapacity;
    WORD Handles; /* one more than the highest index used. */
    int Caps[EMF_CAPS];
    RECTL Bounds; /* union of the record bounds, inclusive. */
    BOOL HasBounds;
    BOOL ClipKnown;
    BOOL HasClip;
    RECT Clip; /* device bounds of the clip and meta regions. */
    int ArcDirection; /* as recorded, 0 before the first. */
    int Saves;        /* saved states, for the relative levels of RestoreDC. */
    BOOL Printer; /* the reference device is a printer, which answers the escapes. */
    BOOL DcBrush;
    BOOL DcPen;
    DWORD AppBounds; /* SetBoundsRect flags. */
    RECT AppRect;
} EMF_DC;

ENHMETAHEADER *EmfHeader(EMF_DC *emf);
void EmfBoundsInit(EMF_BOUNDS *b);
void EmfBoundsPoint(EMF_BOUNDS *b, double x, double y);
void EmfBoundsInflate(EMF_BOUNDS *b, double by);
/* clips the bounds, stores them inclusive in the record and adds them to the header. */
void EmfBoundsCommit(EMF_DC *emf, RECTL *field, const EMF_BOUNDS *b);
void EmfClipChanged(EMF_DC *emf);
BOOL EmfWorldToDevice(EMF_DC *emf, XFORM *m);
void EmfTransformPoint(const XFORM *m, double x, double y, double *dx, double *dy);
DWORD EmfObjectIndex(EMF_DC *emf, HGDIOBJ object, BOOL *created);
DWORD EmfStockIndex(HGDIOBJ object);
