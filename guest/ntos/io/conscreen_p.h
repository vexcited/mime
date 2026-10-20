/* the parts of the screen buffer module that its source files share. */
#ifndef MIME_CONSCREEN_P_H
#define MIME_CONSCREEN_P_H

#include "conscreen.h"

#define CS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CS_MAX(a, b) ((a) > (b) ? (a) : (b))

/* the attribute with the terminal's colors and no flags, which the table of attributes starts with. */
#define CS_EXT_DEFAULT 0

/* the most a window or a buffer is wide or high. */
#define CS_MAX_DIMENSION 32767
#define CS_MAX_CELLS (4u * 1024 * 1024)

/* ---- cells and grids */

CsCell CsBlank(const CsBuffer *buffer);
CsCell CsBlankOf(CsAttr attr);
CsCell CsEraseBlank(const CsBuffer *buffer);
bool CsCellEqual(const CsCell *a, const CsCell *b);

bool CsGridInit(CsGrid *grid, int width, int height, CsCell fill);
void CsGridFree(CsGrid *grid);
void CsRowFill(CsRow *row, int from, int to, CsCell cell);
void CsGridScroll(CsGrid *grid, int top, int bottom, int count, bool up, CsCell fill);

/* ---- attributes */

uint16_t CsExtIntern(CsTerminal *term, const CsExt *ext);
const CsExt *CsExtGet(const CsTerminal *term, unsigned index);
CsAttr CsAttrOf(CsTerminal *term, const CsExt *ext);
CsAttr CsAttrFromLegacy(CsTerminal *term, uint16_t attributes);
CsAttr CsEraseAttr(CsTerminal *term, CsAttr attr);
uint32_t CsLegacyToAnsi(uint32_t index);
uint32_t CsAnsiToLegacy(uint32_t index);

/* ---- the terminal */

void CsOutBytes(CsTerminal *term, const void *data, size_t length);
void CsOutString(CsTerminal *term, const char *text);
void CsOutNumber(CsTerminal *term, unsigned value);
void CsOutUtf8(CsTerminal *term, uint32_t codepoint);
void CsReplyString(CsTerminal *term, const char *text);

bool CsGeometryFits(const CsBuffer *buffer);
bool CsForwarding(const CsBuffer *buffer);
CsBuffer *CsDisplayed(const CsTerminal *term);

void CsHostGoto(CsTerminal *term, int x, int y);
void CsHostSetAttr(CsTerminal *term, const CsBuffer *buffer, CsAttr attr);
void CsHostWrap(CsTerminal *term, bool wrap);
void CsHostPrintCell(CsTerminal *term, const CsBuffer *buffer, const CsCell *lead, const CsCell *trail);
void CsHostCursor(CsTerminal *term, const CsBuffer *buffer);

/* marks cells that were changed without the terminal being told. */
void CsMark(CsBuffer *buffer, int y, int x0, int x1, bool force);
void CsMarkRows(CsBuffer *buffer, int y0, int y1, bool force);

/* the shadow: what the host terminal is believed to show. */
void CsShadowFill(CsTerminal *term, int y0, int y1, int x0, int x1, CsCell cell);

/* the viewport row of an absolute row, or -1 when it is outside. */
static inline int CsViewRow(const CsBuffer *b, int y)
{
    return y >= b->Window.Top && y <= b->Window.Bottom ? y - b->Window.Top : -1;
}

/* ---- escape sequences and text, convt.c */

void CsPutText(CsBuffer *buffer, const uint16_t *text, size_t count);
void CsParserReset(CsBuffer *buffer);

/* ---- operations shared by the sequences and the calls */

void CsNewline(CsBuffer *buffer, bool carriage);
void CsEraseCells(CsBuffer *buffer, int y, int x0, int x1);
void CsEraseRect(CsBuffer *buffer, int y0, int y1, int x0, int x1);
void CsScrollRegion(CsBuffer *buffer, int top, int bottom, int count, bool up, CsCell fill);
void CsMoveCursor(CsBuffer *buffer, int x, int y);
void CsAdjustWindow(CsBuffer *buffer);
void CsPlaceChar(CsBuffer *buffer, uint32_t codepoint, int width);
void CsResizeWindowFits(CsBuffer *buffer);
void CsClearTabs(CsBuffer *buffer);
void clear_cell_partner(CsBuffer *buffer, int y, int x);
void CsDefaultTabs(CsBuffer *buffer);

#endif
