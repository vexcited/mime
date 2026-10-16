/*
 * windows visual style file parser, the .msstyles format of windows 11.
 *
 * an msstyles file is a resource-only pe dll. the library reads the pe
 * headers and the resource tree itself, so the file only needs to be in
 * memory, not loaded. the optional .mui file supplies the localized string
 * table that holds fonts, display names and a few localized values.
 *
 * the file and mui bytes are never copied: they must stay valid and unchanged
 * while the MimeMsstyles referencing them is used. every read is bounds
 * checked, malformed files make the functions fail instead of crashing.
 *
 * classes are identified by their index in the CMAP resource. parts and states
 * are the integers of vssym32.h, 0 meaning the class or part level. property
 * lookups follow the search order of uxtheme: state, part, class, then the
 * base class chain from BCMAP, then globals. see docs/msstyles.md.
 */
#ifndef MIME_LIB_MSSTYLES_H
#define MIME_LIB_MSSTYLES_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

/* primitive types, the type field of a property record. */
enum {
    MIME_TMT_ENUM = 200,
    MIME_TMT_STRING = 201,
    MIME_TMT_INT = 202,
    MIME_TMT_BOOL = 203,
    MIME_TMT_COLOR = 204,
    MIME_TMT_MARGINS = 205,
    MIME_TMT_FILENAME = 206,
    MIME_TMT_SIZE = 207,
    MIME_TMT_POSITION = 208,
    MIME_TMT_RECT = 209,
    MIME_TMT_FONT = 210,
    MIME_TMT_INTLIST = 211,
    MIME_TMT_HBITMAP = 212,
    MIME_TMT_DISKSTREAM = 213,
    MIME_TMT_STREAM = 214,
    MIME_TMT_BITMAPREF = 215,
    MIME_TMT_FLOAT = 216,
    MIME_TMT_FLOATLIST = 217,
};

/* property ids used by the renderer, values of vssym32.h. */
enum {
    MIME_TMT_NAME = 600,
    MIME_TMT_DISPLAYNAME = 601,
    MIME_TMT_TOOLTIP = 602,
    MIME_TMT_COMPANY = 603,
    MIME_TMT_AUTHOR = 604,
    MIME_TMT_COPYRIGHT = 605,
    MIME_TMT_URL = 606,
    MIME_TMT_VERSION = 607,
    MIME_TMT_DESCRIPTION = 608,
    MIME_TMT_CAPTIONFONT = 801,
    MIME_TMT_SMALLCAPTIONFONT = 802,
    MIME_TMT_MENUFONT = 803,
    MIME_TMT_STATUSFONT = 804,
    MIME_TMT_MSGBOXFONT = 805,
    MIME_TMT_ICONTITLEFONT = 806,
    MIME_TMT_HEADING1FONT = 807,
    MIME_TMT_HEADING2FONT = 808,
    MIME_TMT_BODYFONT = 809,
    MIME_TMT_FLATMENUS = 1001,
    MIME_TMT_SIZINGBORDERWIDTH = 1201,
    MIME_TMT_SCROLLBARWIDTH = 1202,
    MIME_TMT_SCROLLBARHEIGHT = 1203,
    MIME_TMT_CAPTIONBARWIDTH = 1204,
    MIME_TMT_CAPTIONBARHEIGHT = 1205,
    MIME_TMT_SMCAPTIONBARWIDTH = 1206,
    MIME_TMT_SMCAPTIONBARHEIGHT = 1207,
    MIME_TMT_MENUBARWIDTH = 1208,
    MIME_TMT_MENUBARHEIGHT = 1209,
    MIME_TMT_PADDEDBORDERWIDTH = 1210,
    MIME_TMT_MINCOLORDEPTH = 1301,
    MIME_TMT_CSSNAME = 1401,
    MIME_TMT_XMLNAME = 1402,
    MIME_TMT_LASTUPDATED = 1403,
    MIME_TMT_ALIAS = 1404,
    MIME_TMT_FIRSTCOLOR = 1601, /* TMT_SCROLLBAR, COLOR_SCROLLBAR + 1601 up to TMT_MENUBAR 1631. */
    MIME_TMT_LASTCOLOR = 1631,
    MIME_TMT_TRANSPARENT = 2201,
    MIME_TMT_AUTOSIZE = 2202,
    MIME_TMT_BORDERONLY = 2203,
    MIME_TMT_COMPOSITED = 2204,
    MIME_TMT_BGFILL = 2205,
    MIME_TMT_GLYPHTRANSPARENT = 2206,
    MIME_TMT_GLYPHONLY = 2207,
    MIME_TMT_ALWAYSSHOWSIZINGBAR = 2208,
    MIME_TMT_MIRRORIMAGE = 2209,
    MIME_TMT_UNIFORMSIZING = 2210,
    MIME_TMT_INTEGRALSIZING = 2211,
    MIME_TMT_SOURCEGROW = 2212,
    MIME_TMT_SOURCESHRINK = 2213,
    MIME_TMT_DRAWBORDERS = 2214,
    MIME_TMT_NOETCHEDEFFECT = 2215,
    MIME_TMT_TEXTAPPLYOVERLAY = 2216,
    MIME_TMT_TEXTGLOW = 2217,
    MIME_TMT_TEXTITALIC = 2218,
    MIME_TMT_COMPOSITEDOPAQUE = 2219,
    MIME_TMT_LOCALIZEDMIRRORIMAGE = 2220,
    MIME_TMT_IMAGECOUNT = 2401,
    MIME_TMT_ALPHALEVEL = 2402,
    MIME_TMT_BORDERSIZE = 2403,
    MIME_TMT_ROUNDCORNERWIDTH = 2404,
    MIME_TMT_ROUNDCORNERHEIGHT = 2405,
    MIME_TMT_GRADIENTRATIO1 = 2406,
    MIME_TMT_PROGRESSCHUNKSIZE = 2411,
    MIME_TMT_PROGRESSSPACESIZE = 2412,
    MIME_TMT_SATURATION = 2413,
    MIME_TMT_TEXTBORDERSIZE = 2414,
    MIME_TMT_ALPHATHRESHOLD = 2415,
    MIME_TMT_WIDTH = 2416,
    MIME_TMT_HEIGHT = 2417,
    MIME_TMT_GLYPHINDEX = 2418,
    MIME_TMT_TRUESIZESTRETCHMARK = 2419,
    MIME_TMT_MINDPI1 = 2420, /* MINDPI1..5 are 2420..2424, MINDPI6 2433, MINDPI7 2434. */
    MIME_TMT_MINDPI5 = 2424,
    MIME_TMT_TEXTGLOWSIZE = 2425,
    MIME_TMT_FRAMESPERSECOND = 2426,
    MIME_TMT_PIXELSPERFRAME = 2427,
    MIME_TMT_ANIMATIONDELAY = 2428,
    MIME_TMT_GLOWINTENSITY = 2429,
    MIME_TMT_OPACITY = 2430,
    MIME_TMT_COLORIZATIONCOLOR = 2431,
    MIME_TMT_COLORIZATIONOPACITY = 2432,
    MIME_TMT_MINDPI6 = 2433,
    MIME_TMT_MINDPI7 = 2434,
    MIME_TMT_GLYPHFONT = 2601,
    MIME_TMT_IMAGEFILE = 3001,
    MIME_TMT_IMAGEFILE1 = 3002, /* IMAGEFILE1..5 are 3002..3006, IMAGEFILE6 3009, IMAGEFILE7 3010. */
    MIME_TMT_IMAGEFILE5 = 3006,
    MIME_TMT_GLYPHIMAGEFILE = 3008,
    MIME_TMT_IMAGEFILE6 = 3009,
    MIME_TMT_IMAGEFILE7 = 3010,
    MIME_TMT_TEXT = 3201,
    MIME_TMT_CLASSICVALUE = 3202,
    MIME_TMT_OFFSET = 3401,
    MIME_TMT_TEXTSHADOWOFFSET = 3402,
    MIME_TMT_MINSIZE = 3403,
    MIME_TMT_MINSIZE1 = 3404, /* MINSIZE1..5 are 3404..3408, MINSIZE6 3410, MINSIZE7 3411. */
    MIME_TMT_NORMALSIZE = 3409,
    MIME_TMT_MINSIZE6 = 3410,
    MIME_TMT_MINSIZE7 = 3411,
    MIME_TMT_SIZINGMARGINS = 3601,
    MIME_TMT_CONTENTMARGINS = 3602,
    MIME_TMT_CAPTIONMARGINS = 3603,
    MIME_TMT_BORDERCOLOR = 3801,
    MIME_TMT_FILLCOLOR = 3802,
    MIME_TMT_TEXTCOLOR = 3803,
    MIME_TMT_EDGELIGHTCOLOR = 3804,
    MIME_TMT_EDGEHIGHLIGHTCOLOR = 3805,
    MIME_TMT_EDGESHADOWCOLOR = 3806,
    MIME_TMT_EDGEDKSHADOWCOLOR = 3807,
    MIME_TMT_EDGEFILLCOLOR = 3808,
    MIME_TMT_TRANSPARENTCOLOR = 3809,
    MIME_TMT_GRADIENTCOLOR1 = 3810,
    MIME_TMT_SHADOWCOLOR = 3815,
    MIME_TMT_GLOWCOLOR = 3816,
    MIME_TMT_TEXTBORDERCOLOR = 3817,
    MIME_TMT_TEXTSHADOWCOLOR = 3818,
    MIME_TMT_GLYPHTEXTCOLOR = 3819,
    MIME_TMT_GLYPHTRANSPARENTCOLOR = 3820,
    MIME_TMT_FILLCOLORHINT = 3821,
    MIME_TMT_BORDERCOLORHINT = 3822,
    MIME_TMT_ACCENTCOLORHINT = 3823,
    MIME_TMT_TEXTCOLORHINT = 3824,
    MIME_TMT_HEADING1TEXTCOLOR = 3825,
    MIME_TMT_HEADING2TEXTCOLOR = 3826,
    MIME_TMT_BODYTEXTCOLOR = 3827,
    MIME_TMT_BGTYPE = 4001,
    MIME_TMT_BORDERTYPE = 4002,
    MIME_TMT_FILLTYPE = 4003,
    MIME_TMT_SIZINGTYPE = 4004,
    MIME_TMT_HALIGN = 4005,
    MIME_TMT_CONTENTALIGNMENT = 4006,
    MIME_TMT_VALIGN = 4007,
    MIME_TMT_OFFSETTYPE = 4008,
    MIME_TMT_ICONEFFECT = 4009,
    MIME_TMT_TEXTSHADOWTYPE = 4010,
    MIME_TMT_IMAGELAYOUT = 4011,
    MIME_TMT_GLYPHTYPE = 4012,
    MIME_TMT_IMAGESELECTTYPE = 4013,
    MIME_TMT_GLYPHFONTSIZINGTYPE = 4014,
    MIME_TMT_TRUESIZESCALINGTYPE = 4015,
    MIME_TMT_USERPICTURE = 5001,
    MIME_TMT_DEFAULTPANESIZE = 5002,
    MIME_TMT_BLENDCOLOR = 5003,
    MIME_TMT_CUSTOMSPLITRECT = 5004,
    MIME_TMT_ANIMATIONBUTTONRECT = 5005,
    MIME_TMT_ANIMATIONDURATION = 5006,
    MIME_TMT_TRANSITIONDURATIONS = 6000,
    MIME_TMT_SCALEDBACKGROUND = 7001,
    MIME_TMT_ATLASIMAGE = 8000,
    MIME_TMT_ATLASINPUTIMAGE = 8001,
    MIME_TMT_ATLASRECT = 8002,
};

/* enum values. */
enum { MIME_BT_IMAGEFILE = 0, MIME_BT_BORDERFILL = 1, MIME_BT_NONE = 2 };
enum { MIME_ST_TRUESIZE = 0, MIME_ST_STRETCH = 1, MIME_ST_TILE = 2 };
enum { MIME_IL_VERTICAL = 0, MIME_IL_HORIZONTAL = 1 };
enum { MIME_IST_NONE = 0, MIME_IST_SIZE = 1, MIME_IST_DPI = 2 };
enum { MIME_TSST_NONE = 0, MIME_TSST_SIZE = 1, MIME_TSST_DPI = 2 };
enum { MIME_GT_NONE = 0, MIME_GT_IMAGEGLYPH = 1, MIME_GT_FONTGLYPH = 2 };
enum { MIME_HA_LEFT = 0, MIME_HA_CENTER = 1, MIME_HA_RIGHT = 2 };
enum { MIME_VA_TOP = 0, MIME_VA_CENTER = 1, MIME_VA_BOTTOM = 2 };
enum { MIME_BT_RECT = 0, MIME_BT_ROUNDRECT = 1, MIME_BT_ELLIPSE = 2 };
enum { MIME_FT_SOLID = 0, MIME_FT_VERTGRADIENT = 1, MIME_FT_HORZGRADIENT = 2, MIME_FT_RADIALGRADIENT = 3, MIME_FT_TILEIMAGE = 4 };

typedef struct MimeMsstylesMargins {
    int32_t left, right, top, bottom; /* MARGINS order. */
} MimeMsstylesMargins;

typedef struct MimeMsstylesRect {
    int32_t left, top, right, bottom;
} MimeMsstylesRect;

/* same layout as LOGFONTW, 92 bytes. */
typedef struct MimeMsstylesFont {
    int32_t height, width, escapement, orientation, weight;
    uint8_t italic, underline, strikeout, charset;
    uint8_t out_precision, clip_precision, quality, pitch_and_family;
    uint16_t face[32];
} MimeMsstylesFont;

/* one raw property record. */
typedef struct MimeMsstylesProp {
    int32_t prop, type;
    int32_t cls, part, state; /* where the record was found, after inheritance. */
    int32_t res_id;           /* nonzero: the value is a string in the mui, an IMAGE or a STREAM resource. */
    uint32_t size;            /* declared value size. */
    const uint8_t *data;      /* inline value, NULL when res_id is set. */
} MimeMsstylesProp;

typedef struct MimeMsstylesRec MimeMsstylesRec;

/* parsed theme. treat the fields as private. */
typedef struct MimeMsstyles {
    const uint8_t *file, *mui;
    size_t file_size, mui_size;
    const MimeAllocator *alloc;
    const uint8_t *variant, *rmap;
    uint32_t variant_size, rmap_size;
    uint32_t class_count;
    uint32_t *class_name; /* offset of each CMAP string inside the file. */
    uint16_t *class_len;  /* length in characters. */
    uint16_t *class_sep;  /* index of the character after "::", 0 without app name. */
    int32_t *base;        /* base class from BCMAP, -1 for none. */
    uint32_t *first, *end; /* record range of each class in recs. */
    MimeMsstylesRec *recs;
    uint32_t rec_count;
    int32_t globals, sysmetrics;
} MimeMsstyles;

enum { MIME_MSSTYLES_FORMAT_PNG = 1, MIME_MSSTYLES_FORMAT_BMP = 2 };

/* image selection flags. */
#define MIME_MSSTYLES_GLYPH 0x1u /* the glyph image instead of the background. */

/*
 * everything a renderer needs to draw one part state. src is the frame for
 * the state inside the decoded bitmap. sizing margins are in source pixels,
 * margin_scale is the integer factor uxtheme applies to them, which is 1
 * below 385 dpi. composed images, used by aerolite, store two sub images per
 * frame: the base at src and an overlay at overlay_src.
 */
typedef struct MimeMsstylesImage {
    const uint8_t *data; /* encoded bytes: a png, or a bmp file starting with "BM". */
    size_t size;
    uint32_t format;
    int32_t res_id;      /* IMAGE resource, or STREAM resource for an atlas. */
    int32_t prop;        /* property that referenced it. */
    int32_t width, height;
    int32_t image_count, layout, frame;
    MimeMsstylesRect src;
    MimeMsstylesRect overlay_src; /* composed images: drawn over src with source over blending. */
    int32_t frame_width, frame_height;
    int32_t min_dpi;
    int32_t min_width, min_height; /* minimum destination size for size based selection. */
    int32_t margin_scale;
    MimeMsstylesMargins sizing_margins, content_margins;
    int32_t bg_type, sizing_type, halign, valign;
    int32_t image_select_type, truesize_scaling_type, truesize_stretch_mark, glyph_type;
    int32_t draw_width, draw_height; /* final size for TRUESIZE images, else frame size. */
    uint32_t fill_color, transparent_color;
    bool atlas, composed, border_only, transparent, has_transparent_color, mirror_image;
    bool uniform_sizing, integral_sizing, source_grow, source_shrink, bg_fill;
    bool glyph_only, glyph_transparent;
} MimeMsstylesImage;

/* class matching flags. */
#define MIME_MSSTYLES_DARK 0x1u /* try the DarkMode app names first, like a dark mode window. */

/*
 * opens the theme with its first VARIANT. mui may be NULL. alloc is kept and
 * used for the indexes, released by close.
 */
bool mime_msstyles_open(MimeMsstyles *ms, const void *file, size_t size, const void *mui, size_t mui_size,
                        const MimeAllocator *alloc);

/* same, choosing the variant by color and size names, NULL matching any. */
bool mime_msstyles_open_variant(MimeMsstyles *ms, const void *file, size_t size, const void *mui, size_t mui_size,
                                const char *color, const char *size_name, const MimeAllocator *alloc);

void mime_msstyles_close(MimeMsstyles *ms);

/* finds a resource of the file, type and name being MAKEINTRESOURCE ids when name_str is NULL. */
bool mime_msstyles_resource(const MimeMsstyles *ms, const char *type_str, uint32_t type_id, const char *name_str,
                            uint32_t name_id, const uint8_t **data, uint32_t *size);

/* loads string id from the mui, then the file. returns the length, 0 if missing. */
size_t mime_msstyles_load_string(const MimeMsstyles *ms, uint32_t id, uint16_t *buf, size_t cap);

uint32_t mime_msstyles_class_count(const MimeMsstyles *ms);

/* copies the full class name as ascii, "App::Class" for subclasses. returns the length. */
size_t mime_msstyles_class_name(const MimeMsstyles *ms, int cls, char *buf, size_t cap);

/* exact match on app and class name, case insensitive. app NULL or empty means none. */
int mime_msstyles_find_class(const MimeMsstyles *ms, const char *app, const char *cls);

/*
 * OpenThemeData style matching: class_list is ';' separated, entries may carry
 * an "App::" prefix, app is the SetWindowTheme sub app name or NULL. with an
 * app name the app specific class wins, then the plain class.
 */
int mime_msstyles_match_class(const MimeMsstyles *ms, const uint16_t *class_list, const uint16_t *app, unsigned flags);
int mime_msstyles_match_class_a(const MimeMsstyles *ms, const char *class_list, const char *app, unsigned flags);

/* base class from BCMAP, -1 when the class falls back to globals. */
int mime_msstyles_base_class(const MimeMsstyles *ms, int cls);

/* raw records in file order, for dumps. */
uint32_t mime_msstyles_record_count(const MimeMsstyles *ms);
bool mime_msstyles_record(const MimeMsstyles *ms, uint32_t index, MimeMsstylesProp *out);

/* records of one class, sorted by part then state. returns false past the end. */
bool mime_msstyles_class_record(const MimeMsstyles *ms, int cls, uint32_t index, MimeMsstylesProp *out);

/* property search with inheritance, out may be NULL. */
bool mime_msstyles_find_prop(const MimeMsstyles *ms, int cls, int part, int state, int prop, MimeMsstylesProp *out);

/* only the exact class, part and state, no inheritance. */
bool mime_msstyles_find_prop_exact(const MimeMsstyles *ms, int cls, int part, int state, int prop,
                                   MimeMsstylesProp *out);

/* typed getters with inheritance. colors are COLORREF, 0x00bbggrr. */
bool mime_msstyles_get_color(const MimeMsstyles *ms, int cls, int part, int state, int prop, uint32_t *out);
bool mime_msstyles_get_int(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *out);
bool mime_msstyles_get_bool(const MimeMsstyles *ms, int cls, int part, int state, int prop, bool *out);
bool mime_msstyles_get_enum(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *out);
bool mime_msstyles_get_margins(const MimeMsstyles *ms, int cls, int part, int state, int prop,
                               MimeMsstylesMargins *out);
bool mime_msstyles_get_rect(const MimeMsstyles *ms, int cls, int part, int state, int prop, MimeMsstylesRect *out);
bool mime_msstyles_get_position(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *x,
                                int32_t *y);
bool mime_msstyles_get_intlist(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *values,
                               uint32_t cap, uint32_t *count);
/* utf-16 copy, always terminated when cap is nonzero. */
bool mime_msstyles_get_string(const MimeMsstyles *ms, int cls, int part, int state, int prop, uint16_t *buf,
                              size_t cap);
/* font height scaled from 96 to dpi like uxtheme, dpi 0 keeps 96. */
bool mime_msstyles_get_font(const MimeMsstyles *ms, int cls, int part, int state, int prop, unsigned dpi,
                            MimeMsstylesFont *out);
/* TMT_FILENAME records: the IMAGE resource bytes. */
bool mime_msstyles_get_filename(const MimeMsstyles *ms, int cls, int part, int state, int prop, int32_t *res_id,
                                const uint8_t **data, uint32_t *size);
/* TMT_DISKSTREAM records: the STREAM resource bytes. */
bool mime_msstyles_get_stream(const MimeMsstyles *ms, int cls, int part, int state, int prop, const uint8_t **data,
                              uint32_t *size);

/* decodes any record value, inheritance already applied. */
bool mime_msstyles_prop_int(const MimeMsstyles *ms, const MimeMsstylesProp *p, int32_t *out);
bool mime_msstyles_prop_ints(const MimeMsstyles *ms, const MimeMsstylesProp *p, int32_t *out, uint32_t cap,
                             uint32_t *count);
bool mime_msstyles_prop_font(const MimeMsstyles *ms, const MimeMsstylesProp *p, MimeMsstylesFont *out);
bool mime_msstyles_prop_string(const MimeMsstyles *ms, const MimeMsstylesProp *p, uint16_t *buf, size_t cap);

/* sysmetrics class: system colors by COLOR_ index, fonts, sizes and so on. */
bool mime_msstyles_sys_color(const MimeMsstyles *ms, int color_index, uint32_t *out);
bool mime_msstyles_sys_font(const MimeMsstyles *ms, int prop, unsigned dpi, MimeMsstylesFont *out);
bool mime_msstyles_sys_int(const MimeMsstyles *ms, int prop, int32_t *out);
bool mime_msstyles_sys_string(const MimeMsstyles *ms, int prop, uint16_t *buf, size_t cap);

/* documentation strings of the RMAP, TMT_DISPLAYNAME and so on. */
bool mime_msstyles_doc_string(const MimeMsstyles *ms, int prop, uint16_t *buf, size_t cap);

/*
 * picks the bitmap uxtheme would draw for a part state at dpi, following
 * IMAGESELECTTYPE, MINDPIn and MINSIZEn. dest_w and dest_h are the target
 * size, 0 when unknown. fails for parts without an image.
 */
bool mime_msstyles_get_image(const MimeMsstyles *ms, int cls, int part, int state, unsigned dpi, int dest_w,
                             int dest_h, unsigned flags, MimeMsstylesImage *out);

/*
 * parts drawn from colors instead of a bitmap, TMT 5100 as used by aerolite.
 * each frame is width by height pixels: transparent outside inset, the fill
 * color on the single pixel at inset.left + sizing.left, inset.top +
 * sizing.top, the border color everywhere else. colors are b, g, r, a bytes
 * packed little endian. stretch the result with sizing like a STRETCH image.
 */
typedef struct MimeMsstylesColorImage {
    uint32_t border, fill;
    int32_t width, height, image_count;
    MimeMsstylesMargins sizing, inset;
} MimeMsstylesColorImage;

bool mime_msstyles_get_color_image(const MimeMsstyles *ms, int cls, int part, int state, MimeMsstylesColorImage *out);

#endif
