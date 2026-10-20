/*
 * metafiles as images: not written yet. The functions the other modules call report that the format is not
 * supported.
 */
#include "gp_private.h"

GpStatus gp_metafile_from_bytes(BYTE *data, SIZE_T size, GImage **image) { return UnknownImageFormat; }
GpStatus gp_metafile_to_bitmap(GImage *image, GBitmap **bitmap) { return NotImplemented; }
GpStatus gp_metafile_clone(GpImage *image, GpImage **clone) { return NotImplemented; }
GpStatus gp_metafile_dispose(GpImage *image) { return InvalidParameter; }
GpStatus gp_metafile_dimension(GpImage *image, float *width, float *height) { return NotImplemented; }
GpStatus gp_metafile_bounds(GpImage *image, GpRectF *bounds, GpUnit *unit) { return NotImplemented; }
GpStatus gp_metafile_raw_format(GpImage *image, GUID *format) { return NotImplemented; }
GpStatus gp_metafile_resolution(GpImage *image, float *dpi_x, float *dpi_y) { return NotImplemented; }
GpStatus gp_metafile_draw(GGraphics *g, GImage *image, const GpPointF *dst, float sx, float sy, float sw, float sh,
                          GpUnit src_unit, struct GAttrs *attrs)
{
    return NotImplemented;
}
