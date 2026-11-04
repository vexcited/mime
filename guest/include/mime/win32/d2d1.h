/* direct2d: types and interfaces of d2d1.h, d2d1_1.h, d2d1_2.h, d2d1_3.h and d2d1svg.h. method order matches windows' vtables. */
#ifndef MIME_WIN32_D2D1_H
#define MIME_WIN32_D2D1_H

#include <mime/win32/objbase.h>
#include <mime/win32/dxgi.h>
#include <mime/win32/dwrite.h>
#include <mime/win32/wincodec.h>
#include <mime/win32/d2dbasetypes.h>

#ifndef D2D1API
#define D2D1API
#endif

/* ------------------------------------------------------------------ errors */

#define D2DERR_WRONG_STATE ((HRESULT)0x88990001)
#define D2DERR_NOT_INITIALIZED ((HRESULT)0x88990002)
#define D2DERR_UNSUPPORTED_OPERATION ((HRESULT)0x88990003)
#define D2DERR_SCANNER_FAILED ((HRESULT)0x88990004)
#define D2DERR_SCREEN_ACCESS_DENIED ((HRESULT)0x88990005)
#define D2DERR_DISPLAY_STATE_INVALID ((HRESULT)0x88990006)
#define D2DERR_ZERO_VECTOR ((HRESULT)0x88990007)
#define D2DERR_INTERNAL_ERROR ((HRESULT)0x88990008)
#define D2DERR_DISPLAY_FORMAT_NOT_SUPPORTED ((HRESULT)0x88990009)
#define D2DERR_INVALID_CALL ((HRESULT)0x8899000A)
#define D2DERR_NO_HARDWARE_DEVICE ((HRESULT)0x8899000B)
#define D2DERR_RECREATE_TARGET ((HRESULT)0x8899000C)
#define D2DERR_TOO_MANY_SHADER_ELEMENTS ((HRESULT)0x8899000D)
#define D2DERR_SHADER_COMPILE_FAILED ((HRESULT)0x8899000E)
#define D2DERR_MAX_TEXTURE_SIZE_EXCEEDED ((HRESULT)0x8899000F)
#define D2DERR_UNSUPPORTED_VERSION ((HRESULT)0x88990010)
#define D2DERR_BAD_NUMBER ((HRESULT)0x88990011)
#define D2DERR_WRONG_FACTORY ((HRESULT)0x88990012)
#define D2DERR_LAYER_ALREADY_IN_USE ((HRESULT)0x88990013)
#define D2DERR_POP_CALL_DID_NOT_MATCH_PUSH ((HRESULT)0x88990014)
#define D2DERR_WRONG_RESOURCE_DOMAIN ((HRESULT)0x88990015)
#define D2DERR_PUSH_POP_UNBALANCED ((HRESULT)0x88990016)
#define D2DERR_RENDER_TARGET_HAS_LAYER_OR_CLIPRECT ((HRESULT)0x88990017)
#define D2DERR_INCOMPATIBLE_BRUSH_TYPES ((HRESULT)0x88990018)
#define D2DERR_WIN32_ERROR ((HRESULT)0x88990019)
#define D2DERR_TARGET_NOT_GDI_COMPATIBLE ((HRESULT)0x8899001A)
#define D2DERR_TEXT_EFFECT_IS_WRONG_TYPE ((HRESULT)0x8899001B)
#define D2DERR_TEXT_RENDERER_NOT_RELEASED ((HRESULT)0x8899001C)
#define D2DERR_EXCEEDS_MAX_BITMAP_SIZE ((HRESULT)0x8899001D)
#define D2DERR_INVALID_GRAPH_CONFIGURATION ((HRESULT)0x8899001E)
#define D2DERR_INVALID_INTERNAL_GRAPH_CONFIGURATION ((HRESULT)0x8899001F)
#define D2DERR_CYCLIC_GRAPH ((HRESULT)0x88990020)
#define D2DERR_BITMAP_CANNOT_DRAW ((HRESULT)0x88990021)
#define D2DERR_OUTSTANDING_BITMAP_REFERENCES ((HRESULT)0x88990022)
#define D2DERR_ORIGINAL_TARGET_NOT_BOUND ((HRESULT)0x88990023)
#define D2DERR_INVALID_TARGET ((HRESULT)0x88990024)
#define D2DERR_BITMAP_BOUND_AS_TARGET ((HRESULT)0x88990025)
#define D2DERR_INSUFFICIENT_DEVICE_CAPABILITIES ((HRESULT)0x88990026)
#define D2DERR_INTERMEDIATE_TOO_LARGE ((HRESULT)0x88990027)
#define D2DERR_EFFECT_IS_NOT_REGISTERED ((HRESULT)0x88990028)
#define D2DERR_INVALID_PROPERTY ((HRESULT)0x88990029)
#define D2DERR_NO_SUBPROPERTIES ((HRESULT)0x8899002A)
#define D2DERR_PRINT_JOB_CLOSED ((HRESULT)0x8899002B)
#define D2DERR_PRINT_FORMAT_NOT_SUPPORTED ((HRESULT)0x8899002C)
#define D2DERR_TOO_MANY_TRANSFORM_INPUTS ((HRESULT)0x8899002D)
#define D2DERR_INVALID_GLYPH_IMAGE ((HRESULT)0x8899002E)

#define D2D1_DEFAULT_FLATTENING_TOLERANCE 0.25f
#define D2D1_INVALID_TAG 0xFFFFFFFFFFFFFFFFull

/* ------------------------------------------------------------------ enumerations */

typedef enum D2D1_ALPHA_MODE {
    D2D1_ALPHA_MODE_UNKNOWN = 0,
    D2D1_ALPHA_MODE_PREMULTIPLIED = 1,
    D2D1_ALPHA_MODE_STRAIGHT = 2,
    D2D1_ALPHA_MODE_IGNORE = 3
} D2D1_ALPHA_MODE;

typedef enum D2D1_FACTORY_TYPE { D2D1_FACTORY_TYPE_SINGLE_THREADED = 0, D2D1_FACTORY_TYPE_MULTI_THREADED = 1 } D2D1_FACTORY_TYPE;
typedef enum D2D1_DEBUG_LEVEL {
    D2D1_DEBUG_LEVEL_NONE = 0,
    D2D1_DEBUG_LEVEL_ERROR = 1,
    D2D1_DEBUG_LEVEL_WARNING = 2,
    D2D1_DEBUG_LEVEL_INFORMATION = 3
} D2D1_DEBUG_LEVEL;
typedef enum D2D1_ANTIALIAS_MODE { D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0, D2D1_ANTIALIAS_MODE_ALIASED = 1 } D2D1_ANTIALIAS_MODE;
typedef enum D2D1_TEXT_ANTIALIAS_MODE {
    D2D1_TEXT_ANTIALIAS_MODE_DEFAULT = 0,
    D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE = 1,
    D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE = 2,
    D2D1_TEXT_ANTIALIAS_MODE_ALIASED = 3
} D2D1_TEXT_ANTIALIAS_MODE;
typedef enum D2D1_DRAW_TEXT_OPTIONS {
    D2D1_DRAW_TEXT_OPTIONS_NO_SNAP = 1,
    D2D1_DRAW_TEXT_OPTIONS_CLIP = 2,
    D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT = 4,
    D2D1_DRAW_TEXT_OPTIONS_DISABLE_COLOR_BITMAP_SNAPPING = 8,
    D2D1_DRAW_TEXT_OPTIONS_NONE = 0
} D2D1_DRAW_TEXT_OPTIONS;
typedef enum D2D1_ARC_SIZE { D2D1_ARC_SIZE_SMALL = 0, D2D1_ARC_SIZE_LARGE = 1 } D2D1_ARC_SIZE;
typedef enum D2D1_SWEEP_DIRECTION { D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE = 0, D2D1_SWEEP_DIRECTION_CLOCKWISE = 1 } D2D1_SWEEP_DIRECTION;
typedef enum D2D1_GAMMA { D2D1_GAMMA_2_2 = 0, D2D1_GAMMA_1_0 = 1 } D2D1_GAMMA;
typedef enum D2D1_OPACITY_MASK_CONTENT {
    D2D1_OPACITY_MASK_CONTENT_GRAPHICS = 0,
    D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL = 1,
    D2D1_OPACITY_MASK_CONTENT_TEXT_GDI_COMPATIBLE = 2
} D2D1_OPACITY_MASK_CONTENT;
typedef enum D2D1_EXTEND_MODE { D2D1_EXTEND_MODE_CLAMP = 0, D2D1_EXTEND_MODE_WRAP = 1, D2D1_EXTEND_MODE_MIRROR = 2 } D2D1_EXTEND_MODE;
typedef enum D2D1_BITMAP_INTERPOLATION_MODE {
    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0,
    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR = 1
} D2D1_BITMAP_INTERPOLATION_MODE;
typedef enum D2D1_CAP_STYLE {
    D2D1_CAP_STYLE_FLAT = 0,
    D2D1_CAP_STYLE_SQUARE = 1,
    D2D1_CAP_STYLE_ROUND = 2,
    D2D1_CAP_STYLE_TRIANGLE = 3
} D2D1_CAP_STYLE;
typedef enum D2D1_LINE_JOIN {
    D2D1_LINE_JOIN_MITER = 0,
    D2D1_LINE_JOIN_BEVEL = 1,
    D2D1_LINE_JOIN_ROUND = 2,
    D2D1_LINE_JOIN_MITER_OR_BEVEL = 3
} D2D1_LINE_JOIN;
typedef enum D2D1_DASH_STYLE {
    D2D1_DASH_STYLE_SOLID = 0,
    D2D1_DASH_STYLE_DASH = 1,
    D2D1_DASH_STYLE_DOT = 2,
    D2D1_DASH_STYLE_DASH_DOT = 3,
    D2D1_DASH_STYLE_DASH_DOT_DOT = 4,
    D2D1_DASH_STYLE_CUSTOM = 5
} D2D1_DASH_STYLE;
typedef enum D2D1_LAYER_OPTIONS { D2D1_LAYER_OPTIONS_NONE = 0, D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE = 1 } D2D1_LAYER_OPTIONS;
typedef enum D2D1_WINDOW_STATE { D2D1_WINDOW_STATE_NONE = 0, D2D1_WINDOW_STATE_OCCLUDED = 1 } D2D1_WINDOW_STATE;
typedef enum D2D1_RENDER_TARGET_TYPE {
    D2D1_RENDER_TARGET_TYPE_DEFAULT = 0,
    D2D1_RENDER_TARGET_TYPE_SOFTWARE = 1,
    D2D1_RENDER_TARGET_TYPE_HARDWARE = 2
} D2D1_RENDER_TARGET_TYPE;
typedef enum D2D1_FEATURE_LEVEL {
    D2D1_FEATURE_LEVEL_DEFAULT = 0,
    D2D1_FEATURE_LEVEL_9 = 0x9100,
    D2D1_FEATURE_LEVEL_10 = 0xa000
} D2D1_FEATURE_LEVEL;
typedef enum D2D1_RENDER_TARGET_USAGE {
    D2D1_RENDER_TARGET_USAGE_NONE = 0,
    D2D1_RENDER_TARGET_USAGE_FORCE_BITMAP_REMOTING = 1,
    D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE = 2
} D2D1_RENDER_TARGET_USAGE;
typedef enum D2D1_PRESENT_OPTIONS {
    D2D1_PRESENT_OPTIONS_NONE = 0,
    D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS = 1,
    D2D1_PRESENT_OPTIONS_IMMEDIATELY = 2
} D2D1_PRESENT_OPTIONS;
typedef enum D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS {
    D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE = 0,
    D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_GDI_COMPATIBLE = 1
} D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS;
typedef enum D2D1_DC_INITIALIZE_MODE { D2D1_DC_INITIALIZE_MODE_COPY = 0, D2D1_DC_INITIALIZE_MODE_CLEAR = 1 } D2D1_DC_INITIALIZE_MODE;
typedef enum D2D1_GEOMETRY_RELATION {
    D2D1_GEOMETRY_RELATION_UNKNOWN = 0,
    D2D1_GEOMETRY_RELATION_DISJOINT = 1,
    D2D1_GEOMETRY_RELATION_IS_CONTAINED = 2,
    D2D1_GEOMETRY_RELATION_CONTAINS = 3,
    D2D1_GEOMETRY_RELATION_OVERLAP = 4
} D2D1_GEOMETRY_RELATION;
typedef enum D2D1_GEOMETRY_SIMPLIFICATION_OPTION {
    D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES = 0,
    D2D1_GEOMETRY_SIMPLIFICATION_OPTION_LINES = 1
} D2D1_GEOMETRY_SIMPLIFICATION_OPTION;
typedef enum D2D1_COMBINE_MODE {
    D2D1_COMBINE_MODE_UNION = 0,
    D2D1_COMBINE_MODE_INTERSECT = 1,
    D2D1_COMBINE_MODE_XOR = 2,
    D2D1_COMBINE_MODE_EXCLUDE = 3
} D2D1_COMBINE_MODE;
typedef enum D2D1_BITMAP_OPTIONS {
    D2D1_BITMAP_OPTIONS_NONE = 0,
    D2D1_BITMAP_OPTIONS_TARGET = 1,
    D2D1_BITMAP_OPTIONS_CANNOT_DRAW = 2,
    D2D1_BITMAP_OPTIONS_CPU_READ = 4,
    D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE = 8
} D2D1_BITMAP_OPTIONS;
typedef enum D2D1_MAP_OPTIONS {
    D2D1_MAP_OPTIONS_NONE = 0,
    D2D1_MAP_OPTIONS_READ = 1,
    D2D1_MAP_OPTIONS_WRITE = 2,
    D2D1_MAP_OPTIONS_DISCARD = 4
} D2D1_MAP_OPTIONS;
typedef enum D2D1_INTERPOLATION_MODE {
    D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0,
    D2D1_INTERPOLATION_MODE_LINEAR = 1,
    D2D1_INTERPOLATION_MODE_CUBIC = 2,
    D2D1_INTERPOLATION_MODE_MULTI_SAMPLE_LINEAR = 3,
    D2D1_INTERPOLATION_MODE_ANISOTROPIC = 4,
    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC = 5
} D2D1_INTERPOLATION_MODE;
typedef enum D2D1_UNIT_MODE { D2D1_UNIT_MODE_DIPS = 0, D2D1_UNIT_MODE_PIXELS = 1 } D2D1_UNIT_MODE;
typedef enum D2D1_COLOR_SPACE { D2D1_COLOR_SPACE_CUSTOM = 0, D2D1_COLOR_SPACE_SRGB = 1, D2D1_COLOR_SPACE_SCRGB = 2 } D2D1_COLOR_SPACE;
typedef enum D2D1_DEVICE_CONTEXT_OPTIONS {
    D2D1_DEVICE_CONTEXT_OPTIONS_NONE = 0,
    D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS = 1
} D2D1_DEVICE_CONTEXT_OPTIONS;
typedef enum D2D1_STROKE_TRANSFORM_TYPE {
    D2D1_STROKE_TRANSFORM_TYPE_NORMAL = 0,
    D2D1_STROKE_TRANSFORM_TYPE_FIXED = 1,
    D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE = 2
} D2D1_STROKE_TRANSFORM_TYPE;
typedef enum D2D1_PRIMITIVE_BLEND {
    D2D1_PRIMITIVE_BLEND_SOURCE_OVER = 0,
    D2D1_PRIMITIVE_BLEND_COPY = 1,
    D2D1_PRIMITIVE_BLEND_MIN = 2,
    D2D1_PRIMITIVE_BLEND_ADD = 3,
    D2D1_PRIMITIVE_BLEND_MAX = 4
} D2D1_PRIMITIVE_BLEND;
typedef enum D2D1_THREADING_MODE { D2D1_THREADING_MODE_SINGLE_THREADED = 0, D2D1_THREADING_MODE_MULTI_THREADED = 1 } D2D1_THREADING_MODE;
typedef enum D2D1_COLOR_INTERPOLATION_MODE {
    D2D1_COLOR_INTERPOLATION_MODE_STRAIGHT = 0,
    D2D1_COLOR_INTERPOLATION_MODE_PREMULTIPLIED = 1
} D2D1_COLOR_INTERPOLATION_MODE;
typedef enum D2D1_BUFFER_PRECISION {
    D2D1_BUFFER_PRECISION_UNKNOWN = 0,
    D2D1_BUFFER_PRECISION_8BPC_UNORM = 1,
    D2D1_BUFFER_PRECISION_8BPC_UNORM_SRGB = 2,
    D2D1_BUFFER_PRECISION_16BPC_UNORM = 3,
    D2D1_BUFFER_PRECISION_16BPC_FLOAT = 4,
    D2D1_BUFFER_PRECISION_32BPC_FLOAT = 5
} D2D1_BUFFER_PRECISION;
typedef enum D2D1_LAYER_OPTIONS1 {
    D2D1_LAYER_OPTIONS1_NONE = 0,
    D2D1_LAYER_OPTIONS1_INITIALIZE_FROM_BACKGROUND = 1,
    D2D1_LAYER_OPTIONS1_IGNORE_ALPHA = 2
} D2D1_LAYER_OPTIONS1;
typedef enum D2D1_PROPERTY_TYPE {
    D2D1_PROPERTY_TYPE_UNKNOWN = 0,
    D2D1_PROPERTY_TYPE_STRING = 1,
    D2D1_PROPERTY_TYPE_BOOL = 2,
    D2D1_PROPERTY_TYPE_UINT32 = 3,
    D2D1_PROPERTY_TYPE_INT32 = 4,
    D2D1_PROPERTY_TYPE_FLOAT = 5,
    D2D1_PROPERTY_TYPE_VECTOR2 = 6,
    D2D1_PROPERTY_TYPE_VECTOR3 = 7,
    D2D1_PROPERTY_TYPE_VECTOR4 = 8,
    D2D1_PROPERTY_TYPE_BLOB = 9,
    D2D1_PROPERTY_TYPE_IUNKNOWN = 10,
    D2D1_PROPERTY_TYPE_ENUM = 11,
    D2D1_PROPERTY_TYPE_ARRAY = 12,
    D2D1_PROPERTY_TYPE_CLSID = 13,
    D2D1_PROPERTY_TYPE_MATRIX_3X2 = 14,
    D2D1_PROPERTY_TYPE_MATRIX_4X3 = 15,
    D2D1_PROPERTY_TYPE_MATRIX_4X4 = 16,
    D2D1_PROPERTY_TYPE_MATRIX_5X4 = 17,
    D2D1_PROPERTY_TYPE_COLOR_CONTEXT = 18
} D2D1_PROPERTY_TYPE;
typedef enum D2D1_PROPERTY {
    D2D1_PROPERTY_CLSID = 0x80000000,
    D2D1_PROPERTY_DISPLAYNAME = 0x80000001,
    D2D1_PROPERTY_AUTHOR = 0x80000002,
    D2D1_PROPERTY_CATEGORY = 0x80000003,
    D2D1_PROPERTY_DESCRIPTION = 0x80000004,
    D2D1_PROPERTY_INPUTS = 0x80000005,
    D2D1_PROPERTY_CACHED = 0x80000006,
    D2D1_PROPERTY_PRECISION = 0x80000007,
    D2D1_PROPERTY_MIN_INPUTS = 0x80000008,
    D2D1_PROPERTY_MAX_INPUTS = 0x80000009
} D2D1_PROPERTY;
typedef enum D2D1_SUBPROPERTY {
    D2D1_SUBPROPERTY_DISPLAYNAME = 0x80000000,
    D2D1_SUBPROPERTY_ISREADONLY = 0x80000001,
    D2D1_SUBPROPERTY_MIN = 0x80000002,
    D2D1_SUBPROPERTY_MAX = 0x80000003,
    D2D1_SUBPROPERTY_DEFAULT = 0x80000004,
    D2D1_SUBPROPERTY_FIELDS = 0x80000005,
    D2D1_SUBPROPERTY_INDEX = 0x80000006
} D2D1_SUBPROPERTY;
typedef enum D2D1_SVG_PAINT_TYPE {
    D2D1_SVG_PAINT_TYPE_NONE = 0,
    D2D1_SVG_PAINT_TYPE_COLOR = 1,
    D2D1_SVG_PAINT_TYPE_CURRENT_COLOR = 2,
    D2D1_SVG_PAINT_TYPE_URI = 3,
    D2D1_SVG_PAINT_TYPE_URI_NONE = 4,
    D2D1_SVG_PAINT_TYPE_URI_COLOR = 5,
    D2D1_SVG_PAINT_TYPE_URI_CURRENT_COLOR = 6
} D2D1_SVG_PAINT_TYPE;
typedef enum D2D1_SVG_LENGTH_UNITS { D2D1_SVG_LENGTH_UNITS_NUMBER = 0, D2D1_SVG_LENGTH_UNITS_PERCENTAGE = 1 } D2D1_SVG_LENGTH_UNITS;
typedef enum D2D1_SVG_DISPLAY { D2D1_SVG_DISPLAY_INLINE = 0, D2D1_SVG_DISPLAY_NONE = 1 } D2D1_SVG_DISPLAY;
typedef enum D2D1_SVG_VISIBILITY { D2D1_SVG_VISIBILITY_VISIBLE = 0, D2D1_SVG_VISIBILITY_HIDDEN = 1 } D2D1_SVG_VISIBILITY;
typedef enum D2D1_SVG_OVERFLOW { D2D1_SVG_OVERFLOW_VISIBLE = 0, D2D1_SVG_OVERFLOW_HIDDEN = 1 } D2D1_SVG_OVERFLOW;
typedef enum D2D1_SVG_LINE_CAP { D2D1_SVG_LINE_CAP_BUTT = 0, D2D1_SVG_LINE_CAP_SQUARE = 1, D2D1_SVG_LINE_CAP_ROUND = 2 } D2D1_SVG_LINE_CAP;
typedef enum D2D1_SVG_LINE_JOIN {
    D2D1_SVG_LINE_JOIN_BEVEL = 1,
    D2D1_SVG_LINE_JOIN_MITER = 3,
    D2D1_SVG_LINE_JOIN_ROUND = 2,
    D2D1_SVG_LINE_JOIN_MITER_CLIP = 4
} D2D1_SVG_LINE_JOIN;
typedef enum D2D1_SVG_ATTRIBUTE_STRING_TYPE { D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG = 0, D2D1_SVG_ATTRIBUTE_STRING_TYPE_ID = 1 } D2D1_SVG_ATTRIBUTE_STRING_TYPE;
typedef enum D2D1_SVG_ATTRIBUTE_POD_TYPE {
    D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT = 0,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR = 1,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_FILL_MODE = 2,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_DISPLAY = 3,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_OVERFLOW = 4,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_LINE_CAP = 5,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_LINE_JOIN = 6,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_VISIBILITY = 7,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_MATRIX = 8,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_UNIT_TYPE = 9,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_EXTEND_MODE = 10,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_PRESERVE_ASPECT_RATIO = 11,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX = 12,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH = 13
} D2D1_SVG_ATTRIBUTE_POD_TYPE;
typedef enum D2D1_SVG_PATH_COMMAND {
    D2D1_SVG_PATH_COMMAND_CLOSE_PATH = 0,
    D2D1_SVG_PATH_COMMAND_MOVE_ABSOLUTE = 1,
    D2D1_SVG_PATH_COMMAND_MOVE_RELATIVE = 2,
    D2D1_SVG_PATH_COMMAND_LINE_ABSOLUTE = 3,
    D2D1_SVG_PATH_COMMAND_LINE_RELATIVE = 4,
    D2D1_SVG_PATH_COMMAND_CUBIC_ABSOLUTE = 5,
    D2D1_SVG_PATH_COMMAND_CUBIC_RELATIVE = 6,
    D2D1_SVG_PATH_COMMAND_QUADRADIC_ABSOLUTE = 7,
    D2D1_SVG_PATH_COMMAND_QUADRADIC_RELATIVE = 8,
    D2D1_SVG_PATH_COMMAND_ARC_ABSOLUTE = 9,
    D2D1_SVG_PATH_COMMAND_ARC_RELATIVE = 10,
    D2D1_SVG_PATH_COMMAND_HORIZONTAL_ABSOLUTE = 11,
    D2D1_SVG_PATH_COMMAND_HORIZONTAL_RELATIVE = 12,
    D2D1_SVG_PATH_COMMAND_VERTICAL_ABSOLUTE = 13,
    D2D1_SVG_PATH_COMMAND_VERTICAL_RELATIVE = 14,
    D2D1_SVG_PATH_COMMAND_CUBIC_SMOOTH_ABSOLUTE = 15,
    D2D1_SVG_PATH_COMMAND_CUBIC_SMOOTH_RELATIVE = 16,
    D2D1_SVG_PATH_COMMAND_QUADRADIC_SMOOTH_ABSOLUTE = 17,
    D2D1_SVG_PATH_COMMAND_QUADRADIC_SMOOTH_RELATIVE = 18
} D2D1_SVG_PATH_COMMAND;
typedef enum D2D1_SVG_UNIT_TYPE { D2D1_SVG_UNIT_TYPE_USER_SPACE_ON_USE = 0, D2D1_SVG_UNIT_TYPE_OBJECT_BOUNDING_BOX = 1 } D2D1_SVG_UNIT_TYPE;
typedef enum D2D1_SVG_ASPECT_ALIGN {
    D2D1_SVG_ASPECT_ALIGN_NONE = 0,
    D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MIN = 1,
    D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MIN = 2,
    D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MIN = 3,
    D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MID = 4,
    D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MID = 5,
    D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MID = 6,
    D2D1_SVG_ASPECT_ALIGN_X_MIN_Y_MAX = 7,
    D2D1_SVG_ASPECT_ALIGN_X_MID_Y_MAX = 8,
    D2D1_SVG_ASPECT_ALIGN_X_MAX_Y_MAX = 9
} D2D1_SVG_ASPECT_ALIGN;
typedef enum D2D1_SVG_ASPECT_SCALING { D2D1_SVG_ASPECT_SCALING_MEET = 0, D2D1_SVG_ASPECT_SCALING_SLICE = 1 } D2D1_SVG_ASPECT_SCALING;
typedef enum D2D1_COMPOSITE_MODE {
    D2D1_COMPOSITE_MODE_SOURCE_OVER = 0,
    D2D1_COMPOSITE_MODE_DESTINATION_OVER = 1,
    D2D1_COMPOSITE_MODE_SOURCE_IN = 2,
    D2D1_COMPOSITE_MODE_DESTINATION_IN = 3,
    D2D1_COMPOSITE_MODE_SOURCE_OUT = 4,
    D2D1_COMPOSITE_MODE_DESTINATION_OUT = 5,
    D2D1_COMPOSITE_MODE_SOURCE_ATOP = 6,
    D2D1_COMPOSITE_MODE_DESTINATION_ATOP = 7,
    D2D1_COMPOSITE_MODE_XOR = 8,
    D2D1_COMPOSITE_MODE_PLUS = 9,
    D2D1_COMPOSITE_MODE_SOURCE_COPY = 10,
    D2D1_COMPOSITE_MODE_BOUNDED_SOURCE_COPY = 11,
    D2D1_COMPOSITE_MODE_MASK_INVERT = 12
} D2D1_COMPOSITE_MODE;
typedef enum D2D1_BLEND_MODE {
    D2D1_BLEND_MODE_MULTIPLY = 0,
    D2D1_BLEND_MODE_SCREEN = 1,
    D2D1_BLEND_MODE_DARKEN = 2,
    D2D1_BLEND_MODE_LIGHTEN = 3,
    D2D1_BLEND_MODE_DISSOLVE = 4,
    D2D1_BLEND_MODE_COLOR_BURN = 5,
    D2D1_BLEND_MODE_LINEAR_BURN = 6,
    D2D1_BLEND_MODE_DARKER_COLOR = 7,
    D2D1_BLEND_MODE_LIGHTER_COLOR = 8,
    D2D1_BLEND_MODE_COLOR_DODGE = 9,
    D2D1_BLEND_MODE_LINEAR_DODGE = 10,
    D2D1_BLEND_MODE_OVERLAY = 11,
    D2D1_BLEND_MODE_SOFT_LIGHT = 12,
    D2D1_BLEND_MODE_HARD_LIGHT = 13,
    D2D1_BLEND_MODE_VIVID_LIGHT = 14,
    D2D1_BLEND_MODE_LINEAR_LIGHT = 15,
    D2D1_BLEND_MODE_PIN_LIGHT = 16,
    D2D1_BLEND_MODE_HARD_MIX = 17,
    D2D1_BLEND_MODE_DIFFERENCE = 18,
    D2D1_BLEND_MODE_EXCLUSION = 19,
    D2D1_BLEND_MODE_HUE = 20,
    D2D1_BLEND_MODE_SATURATION = 21,
    D2D1_BLEND_MODE_COLOR = 22,
    D2D1_BLEND_MODE_LUMINOSITY = 23,
    D2D1_BLEND_MODE_SUBTRACT = 24,
    D2D1_BLEND_MODE_DIVISION = 25
} D2D1_BLEND_MODE;

/* ------------------------------------------------------------------ structures */

typedef D2D_POINT_2U D2D1_POINT_2U;
typedef D2D_POINT_2L D2D1_POINT_2L;
typedef D2D_RECT_F D2D1_RECT_F;
typedef D2D_RECT_U D2D1_RECT_U;
typedef D2D_RECT_L D2D1_RECT_L;
typedef D2D_SIZE_F D2D1_SIZE_F;
typedef D2D_SIZE_U D2D1_SIZE_U;
typedef D2D_MATRIX_3X2_F D2D1_MATRIX_3X2_F;
typedef D2D_MATRIX_4X4_F D2D1_MATRIX_4X4_F;
typedef D2D_MATRIX_5X4_F D2D1_MATRIX_5X4_F;
typedef D2D_VECTOR_2F D2D1_VECTOR_2F;
typedef D2D_VECTOR_3F D2D1_VECTOR_3F;
typedef D2D_VECTOR_4F D2D1_VECTOR_4F;
typedef UINT64 D2D1_TAG;

typedef struct D2D1_PIXEL_FORMAT {
    DXGI_FORMAT format;
    D2D1_ALPHA_MODE alphaMode;
} D2D1_PIXEL_FORMAT;

typedef struct D2D1_FACTORY_OPTIONS {
    D2D1_DEBUG_LEVEL debugLevel;
} D2D1_FACTORY_OPTIONS;

typedef struct D2D1_BITMAP_PROPERTIES {
    D2D1_PIXEL_FORMAT pixelFormat;
    FLOAT dpiX;
    FLOAT dpiY;
} D2D1_BITMAP_PROPERTIES;

typedef struct D2D1_GRADIENT_STOP {
    FLOAT position;
    D2D1_COLOR_F color;
} D2D1_GRADIENT_STOP;

typedef struct D2D1_BRUSH_PROPERTIES {
    FLOAT opacity;
    D2D1_MATRIX_3X2_F transform;
} D2D1_BRUSH_PROPERTIES;

typedef struct D2D1_BITMAP_BRUSH_PROPERTIES {
    D2D1_EXTEND_MODE extendModeX;
    D2D1_EXTEND_MODE extendModeY;
    D2D1_BITMAP_INTERPOLATION_MODE interpolationMode;
} D2D1_BITMAP_BRUSH_PROPERTIES;

typedef struct D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES {
    D2D1_POINT_2F startPoint;
    D2D1_POINT_2F endPoint;
} D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES;

typedef struct D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES {
    D2D1_POINT_2F center;
    D2D1_POINT_2F gradientOriginOffset;
    FLOAT radiusX;
    FLOAT radiusY;
} D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES;

typedef struct D2D1_ARC_SEGMENT {
    D2D1_POINT_2F point;
    D2D1_SIZE_F size;
    FLOAT rotationAngle;
    D2D1_SWEEP_DIRECTION sweepDirection;
    D2D1_ARC_SIZE arcSize;
} D2D1_ARC_SEGMENT;

typedef struct D2D1_QUADRATIC_BEZIER_SEGMENT {
    D2D1_POINT_2F point1;
    D2D1_POINT_2F point2;
} D2D1_QUADRATIC_BEZIER_SEGMENT;

typedef struct D2D1_TRIANGLE {
    D2D1_POINT_2F point1;
    D2D1_POINT_2F point2;
    D2D1_POINT_2F point3;
} D2D1_TRIANGLE;

typedef struct D2D1_ELLIPSE {
    D2D1_POINT_2F point;
    FLOAT radiusX;
    FLOAT radiusY;
} D2D1_ELLIPSE;

typedef struct D2D1_ROUNDED_RECT {
    D2D1_RECT_F rect;
    FLOAT radiusX;
    FLOAT radiusY;
} D2D1_ROUNDED_RECT;

typedef struct D2D1_STROKE_STYLE_PROPERTIES {
    D2D1_CAP_STYLE startCap;
    D2D1_CAP_STYLE endCap;
    D2D1_CAP_STYLE dashCap;
    D2D1_LINE_JOIN lineJoin;
    FLOAT miterLimit;
    D2D1_DASH_STYLE dashStyle;
    FLOAT dashOffset;
} D2D1_STROKE_STYLE_PROPERTIES;

typedef struct D2D1_STROKE_STYLE_PROPERTIES1 {
    D2D1_CAP_STYLE startCap;
    D2D1_CAP_STYLE endCap;
    D2D1_CAP_STYLE dashCap;
    D2D1_LINE_JOIN lineJoin;
    FLOAT miterLimit;
    D2D1_DASH_STYLE dashStyle;
    FLOAT dashOffset;
    D2D1_STROKE_TRANSFORM_TYPE transformType;
} D2D1_STROKE_STYLE_PROPERTIES1;

typedef struct ID2D1Geometry ID2D1Geometry;
typedef struct ID2D1Brush ID2D1Brush;

typedef struct D2D1_LAYER_PARAMETERS {
    D2D1_RECT_F contentBounds;
    ID2D1Geometry *geometricMask;
    D2D1_ANTIALIAS_MODE maskAntialiasMode;
    D2D1_MATRIX_3X2_F maskTransform;
    FLOAT opacity;
    ID2D1Brush *opacityBrush;
    D2D1_LAYER_OPTIONS layerOptions;
} D2D1_LAYER_PARAMETERS;

typedef struct D2D1_LAYER_PARAMETERS1 {
    D2D1_RECT_F contentBounds;
    ID2D1Geometry *geometricMask;
    D2D1_ANTIALIAS_MODE maskAntialiasMode;
    D2D1_MATRIX_3X2_F maskTransform;
    FLOAT opacity;
    ID2D1Brush *opacityBrush;
    D2D1_LAYER_OPTIONS1 layerOptions;
} D2D1_LAYER_PARAMETERS1;

typedef struct D2D1_RENDER_TARGET_PROPERTIES {
    D2D1_RENDER_TARGET_TYPE type;
    D2D1_PIXEL_FORMAT pixelFormat;
    FLOAT dpiX;
    FLOAT dpiY;
    D2D1_RENDER_TARGET_USAGE usage;
    D2D1_FEATURE_LEVEL minLevel;
} D2D1_RENDER_TARGET_PROPERTIES;

typedef struct D2D1_HWND_RENDER_TARGET_PROPERTIES {
    HWND hwnd;
    D2D1_SIZE_U pixelSize;
    D2D1_PRESENT_OPTIONS presentOptions;
} D2D1_HWND_RENDER_TARGET_PROPERTIES;

typedef struct D2D1_DRAWING_STATE_DESCRIPTION {
    D2D1_ANTIALIAS_MODE antialiasMode;
    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode;
    D2D1_TAG tag1;
    D2D1_TAG tag2;
    D2D1_MATRIX_3X2_F transform;
} D2D1_DRAWING_STATE_DESCRIPTION;

typedef struct D2D1_DRAWING_STATE_DESCRIPTION1 {
    D2D1_ANTIALIAS_MODE antialiasMode;
    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode;
    D2D1_TAG tag1;
    D2D1_TAG tag2;
    D2D1_MATRIX_3X2_F transform;
    D2D1_PRIMITIVE_BLEND primitiveBlend;
    D2D1_UNIT_MODE unitMode;
} D2D1_DRAWING_STATE_DESCRIPTION1;

typedef struct ID2D1ColorContext ID2D1ColorContext;

typedef struct D2D1_BITMAP_PROPERTIES1 {
    D2D1_PIXEL_FORMAT pixelFormat;
    FLOAT dpiX;
    FLOAT dpiY;
    D2D1_BITMAP_OPTIONS bitmapOptions;
    ID2D1ColorContext *colorContext;
} D2D1_BITMAP_PROPERTIES1;

typedef struct D2D1_MAPPED_RECT {
    UINT32 pitch;
    BYTE *bits;
} D2D1_MAPPED_RECT;

typedef struct D2D1_IMAGE_BRUSH_PROPERTIES {
    D2D1_RECT_F sourceRectangle;
    D2D1_EXTEND_MODE extendModeX;
    D2D1_EXTEND_MODE extendModeY;
    D2D1_INTERPOLATION_MODE interpolationMode;
} D2D1_IMAGE_BRUSH_PROPERTIES;

typedef struct D2D1_BITMAP_BRUSH_PROPERTIES1 {
    D2D1_EXTEND_MODE extendModeX;
    D2D1_EXTEND_MODE extendModeY;
    D2D1_INTERPOLATION_MODE interpolationMode;
} D2D1_BITMAP_BRUSH_PROPERTIES1;

typedef struct D2D1_RENDERING_CONTROLS {
    D2D1_BUFFER_PRECISION bufferPrecision;
    D2D1_SIZE_U tileSize;
} D2D1_RENDERING_CONTROLS;

typedef struct D2D1_CREATION_PROPERTIES {
    D2D1_THREADING_MODE threadingMode;
    D2D1_DEBUG_LEVEL debugLevel;
    D2D1_DEVICE_CONTEXT_OPTIONS options;
} D2D1_CREATION_PROPERTIES;

typedef struct D2D1_POINT_DESCRIPTION {
    D2D1_POINT_2F point;
    D2D1_POINT_2F unitTangentVector;
    UINT32 endSegment;
    UINT32 endFigure;
    FLOAT lengthToEndSegment;
} D2D1_POINT_DESCRIPTION;

typedef struct D2D1_SVG_PRESERVE_ASPECT_RATIO {
    BOOL deferToAspectRatio;
    D2D1_SVG_ASPECT_ALIGN align;
    D2D1_SVG_ASPECT_SCALING meetOrSlice;
} D2D1_SVG_PRESERVE_ASPECT_RATIO;

typedef struct D2D1_SVG_LENGTH {
    FLOAT value;
    D2D1_SVG_LENGTH_UNITS units;
} D2D1_SVG_LENGTH;

typedef struct D2D1_SVG_VIEWBOX {
    FLOAT x, y, width, height;
} D2D1_SVG_VIEWBOX;

typedef enum D2D1_RENDERING_PRIORITY { D2D1_RENDERING_PRIORITY_NORMAL = 0, D2D1_RENDERING_PRIORITY_LOW = 1 } D2D1_RENDERING_PRIORITY;

typedef enum D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION {
    D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DEFAULT = 0,
    D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DISABLE = 1
} D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION;

typedef enum D2D1_PRINT_FONT_SUBSET_MODE {
    D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT = 0,
    D2D1_PRINT_FONT_SUBSET_MODE_EACHPAGE = 1,
    D2D1_PRINT_FONT_SUBSET_MODE_NONE = 2
} D2D1_PRINT_FONT_SUBSET_MODE;

typedef struct D2D1_PRINT_CONTROL_PROPERTIES {
    D2D1_PRINT_FONT_SUBSET_MODE fontSubset;
    FLOAT rasterDPI;
    D2D1_COLOR_SPACE colorSpace;
} D2D1_PRINT_CONTROL_PROPERTIES;

typedef struct D2D1_EFFECT_INPUT_DESCRIPTION {
    struct ID2D1Effect *effect;
    UINT32 inputIndex;
    D2D1_RECT_F inputRectangle;
} D2D1_EFFECT_INPUT_DESCRIPTION;

/* ------------------------------------------------------------------ interface declarations */

typedef struct ID2D1Factory ID2D1Factory;
typedef struct ID2D1Factory1 ID2D1Factory1;
typedef struct ID2D1Resource ID2D1Resource;
typedef struct ID2D1Image ID2D1Image;
typedef struct ID2D1Bitmap ID2D1Bitmap;
typedef struct ID2D1Bitmap1 ID2D1Bitmap1;
typedef struct ID2D1GradientStopCollection ID2D1GradientStopCollection;
typedef struct ID2D1GradientStopCollection1 ID2D1GradientStopCollection1;
typedef struct ID2D1BitmapBrush ID2D1BitmapBrush;
typedef struct ID2D1BitmapBrush1 ID2D1BitmapBrush1;
typedef struct ID2D1ImageBrush ID2D1ImageBrush;
typedef struct ID2D1SolidColorBrush ID2D1SolidColorBrush;
typedef struct ID2D1LinearGradientBrush ID2D1LinearGradientBrush;
typedef struct ID2D1RadialGradientBrush ID2D1RadialGradientBrush;
typedef struct ID2D1StrokeStyle ID2D1StrokeStyle;
typedef struct ID2D1StrokeStyle1 ID2D1StrokeStyle1;
typedef struct ID2D1RectangleGeometry ID2D1RectangleGeometry;
typedef struct ID2D1RoundedRectangleGeometry ID2D1RoundedRectangleGeometry;
typedef struct ID2D1EllipseGeometry ID2D1EllipseGeometry;
typedef struct ID2D1GeometryGroup ID2D1GeometryGroup;
typedef struct ID2D1TransformedGeometry ID2D1TransformedGeometry;
typedef struct ID2D1SimplifiedGeometrySink ID2D1SimplifiedGeometrySink;
typedef struct ID2D1GeometrySink ID2D1GeometrySink;
typedef struct ID2D1TessellationSink ID2D1TessellationSink;
typedef struct ID2D1PathGeometry ID2D1PathGeometry;
typedef struct ID2D1PathGeometry1 ID2D1PathGeometry1;
typedef struct ID2D1Mesh ID2D1Mesh;
typedef struct ID2D1Layer ID2D1Layer;
typedef struct ID2D1DrawingStateBlock ID2D1DrawingStateBlock;
typedef struct ID2D1DrawingStateBlock1 ID2D1DrawingStateBlock1;
typedef struct ID2D1RenderTarget ID2D1RenderTarget;
typedef struct ID2D1BitmapRenderTarget ID2D1BitmapRenderTarget;
typedef struct ID2D1HwndRenderTarget ID2D1HwndRenderTarget;
typedef struct ID2D1GdiInteropRenderTarget ID2D1GdiInteropRenderTarget;
typedef struct ID2D1DCRenderTarget ID2D1DCRenderTarget;
typedef struct ID2D1Device ID2D1Device;
typedef struct ID2D1DeviceContext ID2D1DeviceContext;
typedef struct ID2D1Effect ID2D1Effect;
typedef struct ID2D1Properties ID2D1Properties;
typedef struct ID2D1CommandList ID2D1CommandList;
typedef struct ID2D1CommandSink ID2D1CommandSink;
typedef struct ID2D1GdiMetafile ID2D1GdiMetafile;
typedef struct ID2D1GdiMetafileSink ID2D1GdiMetafileSink;
typedef struct ID2D1PrintControl ID2D1PrintControl;
typedef struct ID2D1Multithread ID2D1Multithread;
typedef struct ID2D1SvgDocument ID2D1SvgDocument;
typedef struct ID2D1SvgElement ID2D1SvgElement;
typedef struct ID2D1SvgAttribute ID2D1SvgAttribute;
typedef struct ID2D1SvgPaint ID2D1SvgPaint;
typedef struct ID2D1SvgStrokeDashArray ID2D1SvgStrokeDashArray;
typedef struct ID2D1SvgPointCollection ID2D1SvgPointCollection;
typedef struct ID2D1SvgPathData ID2D1SvgPathData;
typedef struct ID2D1GeometryRealization ID2D1GeometryRealization;
typedef struct ID2D1Ink ID2D1Ink;
typedef struct ID2D1InkStyle ID2D1InkStyle;
typedef struct ID2D1GradientMesh ID2D1GradientMesh;
typedef struct ID2D1ImageSource ID2D1ImageSource;
typedef struct ID2D1ImageSourceFromWic ID2D1ImageSourceFromWic;
typedef struct ID2D1TransformedImageSource ID2D1TransformedImageSource;
typedef struct ID2D1LookupTable3D ID2D1LookupTable3D;
typedef struct ID2D1SpriteBatch ID2D1SpriteBatch;
typedef struct ID2D1SvgGlyphStyle ID2D1SvgGlyphStyle;
typedef struct ID2D1Device1 ID2D1Device1;
typedef struct ID2D1Device2 ID2D1Device2;
typedef struct ID2D1Device3 ID2D1Device3;
typedef struct ID2D1Device4 ID2D1Device4;
typedef struct ID2D1Device5 ID2D1Device5;
typedef struct ID2D1Device6 ID2D1Device6;
typedef struct ID2D1DeviceContext1 ID2D1DeviceContext1;
typedef struct ID2D1DeviceContext2 ID2D1DeviceContext2;
typedef struct ID2D1DeviceContext3 ID2D1DeviceContext3;
typedef struct ID2D1DeviceContext4 ID2D1DeviceContext4;
typedef struct ID2D1DeviceContext5 ID2D1DeviceContext5;
typedef struct ID2D1DeviceContext6 ID2D1DeviceContext6;
typedef struct ID2D1Factory2 ID2D1Factory2;
typedef struct ID2D1Factory3 ID2D1Factory3;
typedef struct ID2D1Factory4 ID2D1Factory4;
typedef struct ID2D1Factory5 ID2D1Factory5;
typedef struct ID2D1Factory6 ID2D1Factory6;
typedef struct ID2D1Factory7 ID2D1Factory7;
typedef struct ID2D1ColorContext1 ID2D1ColorContext1;

/* ------------------------------------------------------------------ resources */

#define ID2D1Resource_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetFactory)(T * This, ID2D1Factory * *factory);
MIME_COM_INTERFACE(ID2D1Resource)

#define ID2D1Image_METHODS(T) ID2D1Resource_METHODS(T)
MIME_COM_INTERFACE(ID2D1Image)

#define ID2D1Bitmap_METHODS(T)                                                                                         \
    ID2D1Image_METHODS(T)                                                                                              \
    D2D1_SIZE_F *(STDMETHODCALLTYPE *GetSize)(T * This, D2D1_SIZE_F * ret);                                                                 \
    D2D1_SIZE_U *(STDMETHODCALLTYPE *GetPixelSize)(T * This, D2D1_SIZE_U * ret);                                                            \
    D2D1_PIXEL_FORMAT *(STDMETHODCALLTYPE *GetPixelFormat)(T * This, D2D1_PIXEL_FORMAT * ret);                                                    \
    void(STDMETHODCALLTYPE *GetDpi)(T * This, FLOAT * dpi_x, FLOAT * dpi_y);                                           \
    HRESULT(STDMETHODCALLTYPE *CopyFromBitmap)(T * This, const D2D1_POINT_2U *dest, ID2D1Bitmap *bitmap,              \
                                               const D2D1_RECT_U *source);                                             \
    HRESULT(STDMETHODCALLTYPE *CopyFromRenderTarget)(T * This, const D2D1_POINT_2U *dest, ID2D1RenderTarget *target,  \
                                                     const D2D1_RECT_U *source);                                       \
    HRESULT(STDMETHODCALLTYPE *CopyFromMemory)(T * This, const D2D1_RECT_U *dest, const void *data, UINT32 pitch);
MIME_COM_INTERFACE(ID2D1Bitmap)

#define ID2D1ColorContext_METHODS(T)                                                                                   \
    ID2D1Resource_METHODS(T)                                                                                           \
    D2D1_COLOR_SPACE(STDMETHODCALLTYPE *GetColorSpace)(T * This);                                                      \
    UINT32(STDMETHODCALLTYPE *GetProfileSize)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *GetProfile)(T * This, BYTE * profile, UINT32 size);
MIME_COM_INTERFACE(ID2D1ColorContext)

#define ID2D1Bitmap1_METHODS(T)                                                                                        \
    ID2D1Bitmap_METHODS(T)                                                                                             \
    void(STDMETHODCALLTYPE *GetColorContext)(T * This, ID2D1ColorContext * *context);                                  \
    D2D1_BITMAP_OPTIONS(STDMETHODCALLTYPE *GetOptions)(T * This);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetSurface)(T * This, IDXGISurface * *surface);                                         \
    HRESULT(STDMETHODCALLTYPE *Map)(T * This, D2D1_MAP_OPTIONS options, D2D1_MAPPED_RECT * mapped);                    \
    HRESULT(STDMETHODCALLTYPE *Unmap)(T * This);
MIME_COM_INTERFACE(ID2D1Bitmap1)

#define ID2D1GradientStopCollection_METHODS(T)                                                                         \
    ID2D1Resource_METHODS(T)                                                                                           \
    UINT32(STDMETHODCALLTYPE *GetGradientStopCount)(T * This);                                                         \
    void(STDMETHODCALLTYPE *GetGradientStops)(T * This, D2D1_GRADIENT_STOP * stops, UINT32 count);                     \
    D2D1_GAMMA(STDMETHODCALLTYPE *GetColorInterpolationGamma)(T * This);                                               \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendMode)(T * This);
MIME_COM_INTERFACE(ID2D1GradientStopCollection)

#define ID2D1GradientStopCollection1_METHODS(T)                                                                        \
    ID2D1GradientStopCollection_METHODS(T)                                                                             \
    void(STDMETHODCALLTYPE *GetGradientStops1)(T * This, D2D1_GRADIENT_STOP * stops, UINT32 count);                    \
    D2D1_COLOR_SPACE(STDMETHODCALLTYPE *GetPreInterpolationSpace)(T * This);                                           \
    D2D1_COLOR_SPACE(STDMETHODCALLTYPE *GetPostInterpolationSpace)(T * This);                                          \
    D2D1_BUFFER_PRECISION(STDMETHODCALLTYPE *GetBufferPrecision)(T * This);                                            \
    D2D1_COLOR_INTERPOLATION_MODE(STDMETHODCALLTYPE *GetColorInterpolationMode)(T * This);
MIME_COM_INTERFACE(ID2D1GradientStopCollection1)

#define ID2D1Brush_METHODS(T)                                                                                          \
    ID2D1Resource_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *SetOpacity)(T * This, FLOAT opacity);                                                      \
    void(STDMETHODCALLTYPE *SetTransform)(T * This, const D2D1_MATRIX_3X2_F *transform);                               \
    FLOAT(STDMETHODCALLTYPE *GetOpacity)(T * This);                                                                    \
    void(STDMETHODCALLTYPE *GetTransform)(T * This, D2D1_MATRIX_3X2_F * transform);
MIME_COM_INTERFACE(ID2D1Brush)

#define ID2D1BitmapBrush_METHODS(T)                                                                                    \
    ID2D1Brush_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *SetExtendModeX)(T * This, D2D1_EXTEND_MODE mode);                                          \
    void(STDMETHODCALLTYPE *SetExtendModeY)(T * This, D2D1_EXTEND_MODE mode);                                          \
    void(STDMETHODCALLTYPE *SetInterpolationMode)(T * This, D2D1_BITMAP_INTERPOLATION_MODE mode);                      \
    void(STDMETHODCALLTYPE *SetBitmap)(T * This, ID2D1Bitmap * bitmap);                                                \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendModeX)(T * This);                                                     \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendModeY)(T * This);                                                     \
    D2D1_BITMAP_INTERPOLATION_MODE(STDMETHODCALLTYPE *GetInterpolationMode)(T * This);                                 \
    void(STDMETHODCALLTYPE *GetBitmap)(T * This, ID2D1Bitmap * *bitmap);
MIME_COM_INTERFACE(ID2D1BitmapBrush)

#define ID2D1BitmapBrush1_METHODS(T)                                                                                   \
    ID2D1BitmapBrush_METHODS(T)                                                                                        \
    void(STDMETHODCALLTYPE *SetInterpolationMode1)(T * This, D2D1_INTERPOLATION_MODE mode);                            \
    D2D1_INTERPOLATION_MODE(STDMETHODCALLTYPE *GetInterpolationMode1)(T * This);
MIME_COM_INTERFACE(ID2D1BitmapBrush1)

#define ID2D1ImageBrush_METHODS(T)                                                                                     \
    ID2D1Brush_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *SetImage)(T * This, ID2D1Image * image);                                                   \
    void(STDMETHODCALLTYPE *SetExtendModeX)(T * This, D2D1_EXTEND_MODE mode);                                          \
    void(STDMETHODCALLTYPE *SetExtendModeY)(T * This, D2D1_EXTEND_MODE mode);                                          \
    void(STDMETHODCALLTYPE *SetInterpolationMode)(T * This, D2D1_INTERPOLATION_MODE mode);                             \
    void(STDMETHODCALLTYPE *SetSourceRectangle)(T * This, const D2D1_RECT_F *rect);                                    \
    void(STDMETHODCALLTYPE *GetImage)(T * This, ID2D1Image * *image);                                                  \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendModeX)(T * This);                                                     \
    D2D1_EXTEND_MODE(STDMETHODCALLTYPE *GetExtendModeY)(T * This);                                                     \
    D2D1_INTERPOLATION_MODE(STDMETHODCALLTYPE *GetInterpolationMode)(T * This);                                        \
    void(STDMETHODCALLTYPE *GetSourceRectangle)(T * This, D2D1_RECT_F * rect);
MIME_COM_INTERFACE(ID2D1ImageBrush)

#define ID2D1SolidColorBrush_METHODS(T)                                                                                \
    ID2D1Brush_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *SetColor)(T * This, const D2D1_COLOR_F *color);                                            \
    D2D1_COLOR_F *(STDMETHODCALLTYPE *GetColor)(T * This, D2D1_COLOR_F * result);
MIME_COM_INTERFACE(ID2D1SolidColorBrush)

#define ID2D1LinearGradientBrush_METHODS(T)                                                                            \
    ID2D1Brush_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *SetStartPoint)(T * This, D2D1_POINT_2F point);                                             \
    void(STDMETHODCALLTYPE *SetEndPoint)(T * This, D2D1_POINT_2F point);                                               \
    D2D1_POINT_2F *(STDMETHODCALLTYPE *GetStartPoint)(T * This, D2D1_POINT_2F * ret);                                                         \
    D2D1_POINT_2F *(STDMETHODCALLTYPE *GetEndPoint)(T * This, D2D1_POINT_2F * ret);                                                           \
    void(STDMETHODCALLTYPE *GetGradientStopCollection)(T * This, ID2D1GradientStopCollection * *stops);
MIME_COM_INTERFACE(ID2D1LinearGradientBrush)

#define ID2D1RadialGradientBrush_METHODS(T)                                                                            \
    ID2D1Brush_METHODS(T)                                                                                              \
    void(STDMETHODCALLTYPE *SetCenter)(T * This, D2D1_POINT_2F center);                                                \
    void(STDMETHODCALLTYPE *SetGradientOriginOffset)(T * This, D2D1_POINT_2F offset);                                  \
    void(STDMETHODCALLTYPE *SetRadiusX)(T * This, FLOAT radius);                                                       \
    void(STDMETHODCALLTYPE *SetRadiusY)(T * This, FLOAT radius);                                                       \
    D2D1_POINT_2F *(STDMETHODCALLTYPE *GetCenter)(T * This, D2D1_POINT_2F * ret);                                                             \
    D2D1_POINT_2F *(STDMETHODCALLTYPE *GetGradientOriginOffset)(T * This, D2D1_POINT_2F * ret);                                               \
    FLOAT(STDMETHODCALLTYPE *GetRadiusX)(T * This);                                                                    \
    FLOAT(STDMETHODCALLTYPE *GetRadiusY)(T * This);                                                                    \
    void(STDMETHODCALLTYPE *GetGradientStopCollection)(T * This, ID2D1GradientStopCollection * *stops);
MIME_COM_INTERFACE(ID2D1RadialGradientBrush)

#define ID2D1StrokeStyle_METHODS(T)                                                                                    \
    ID2D1Resource_METHODS(T)                                                                                           \
    D2D1_CAP_STYLE(STDMETHODCALLTYPE *GetStartCap)(T * This);                                                          \
    D2D1_CAP_STYLE(STDMETHODCALLTYPE *GetEndCap)(T * This);                                                            \
    D2D1_CAP_STYLE(STDMETHODCALLTYPE *GetDashCap)(T * This);                                                           \
    FLOAT(STDMETHODCALLTYPE *GetMiterLimit)(T * This);                                                                 \
    D2D1_LINE_JOIN(STDMETHODCALLTYPE *GetLineJoin)(T * This);                                                          \
    FLOAT(STDMETHODCALLTYPE *GetDashOffset)(T * This);                                                                 \
    D2D1_DASH_STYLE(STDMETHODCALLTYPE *GetDashStyle)(T * This);                                                        \
    UINT32(STDMETHODCALLTYPE *GetDashesCount)(T * This);                                                               \
    void(STDMETHODCALLTYPE *GetDashes)(T * This, FLOAT * dashes, UINT32 count);
MIME_COM_INTERFACE(ID2D1StrokeStyle)

#define ID2D1StrokeStyle1_METHODS(T)                                                                                   \
    ID2D1StrokeStyle_METHODS(T)                                                                                        \
    D2D1_STROKE_TRANSFORM_TYPE(STDMETHODCALLTYPE *GetStrokeTransformType)(T * This);
MIME_COM_INTERFACE(ID2D1StrokeStyle1)

/* ------------------------------------------------------------------ geometry */

#define ID2D1SimplifiedGeometrySink_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *SetFillMode)(T * This, D2D1_FILL_MODE mode);                                               \
    void(STDMETHODCALLTYPE *SetSegmentFlags)(T * This, D2D1_PATH_SEGMENT flags);                                       \
    void(STDMETHODCALLTYPE *BeginFigure)(T * This, D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin);                      \
    void(STDMETHODCALLTYPE *AddLines)(T * This, const D2D1_POINT_2F *points, UINT32 count);                            \
    void(STDMETHODCALLTYPE *AddBeziers)(T * This, const D2D1_BEZIER_SEGMENT *beziers, UINT32 count);                   \
    void(STDMETHODCALLTYPE *EndFigure)(T * This, D2D1_FIGURE_END end);                                                 \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(ID2D1SimplifiedGeometrySink)

#define ID2D1GeometrySink_METHODS(T)                                                                                   \
    ID2D1SimplifiedGeometrySink_METHODS(T)                                                                             \
    void(STDMETHODCALLTYPE *AddLine)(T * This, D2D1_POINT_2F point);                                                   \
    void(STDMETHODCALLTYPE *AddBezier)(T * This, const D2D1_BEZIER_SEGMENT *bezier);                                   \
    void(STDMETHODCALLTYPE *AddQuadraticBezier)(T * This, const D2D1_QUADRATIC_BEZIER_SEGMENT *bezier);                \
    void(STDMETHODCALLTYPE *AddQuadraticBeziers)(T * This, const D2D1_QUADRATIC_BEZIER_SEGMENT *beziers, UINT32 count);\
    void(STDMETHODCALLTYPE *AddArc)(T * This, const D2D1_ARC_SEGMENT *arc);
MIME_COM_INTERFACE(ID2D1GeometrySink)

#define ID2D1TessellationSink_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *AddTriangles)(T * This, const D2D1_TRIANGLE *triangles, UINT32 count);                     \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(ID2D1TessellationSink)

#define ID2D1Geometry_METHODS(T)                                                                                       \
    ID2D1Resource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetBounds)(T * This, const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds);          \
    HRESULT(STDMETHODCALLTYPE *GetWidenedBounds)(T * This, FLOAT width, ID2D1StrokeStyle * style,                      \
                                                 const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,                  \
                                                 D2D1_RECT_F *bounds);                                                 \
    HRESULT(STDMETHODCALLTYPE *StrokeContainsPoint)(T * This, D2D1_POINT_2F point, FLOAT width,                        \
                                                    ID2D1StrokeStyle *style, const D2D1_MATRIX_3X2_F *transform,       \
                                                    FLOAT tolerance, BOOL *contains);                                  \
    HRESULT(STDMETHODCALLTYPE *FillContainsPoint)(T * This, D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform,   \
                                                  FLOAT tolerance, BOOL *contains);                                    \
    HRESULT(STDMETHODCALLTYPE *CompareWithGeometry)(T * This, ID2D1Geometry * other,                                   \
                                                    const D2D1_MATRIX_3X2_F *other_transform, FLOAT tolerance,         \
                                                    D2D1_GEOMETRY_RELATION *relation);                                 \
    HRESULT(STDMETHODCALLTYPE *Simplify)(T * This, D2D1_GEOMETRY_SIMPLIFICATION_OPTION option,                         \
                                         const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,                          \
                                         ID2D1SimplifiedGeometrySink *sink);                                           \
    HRESULT(STDMETHODCALLTYPE *Tessellate)(T * This, const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,              \
                                           ID2D1TessellationSink *sink);                                               \
    HRESULT(STDMETHODCALLTYPE *CombineWithGeometry)(T * This, ID2D1Geometry * other, D2D1_COMBINE_MODE mode,           \
                                                    const D2D1_MATRIX_3X2_F *other_transform, FLOAT tolerance,         \
                                                    ID2D1SimplifiedGeometrySink *sink);                                \
    HRESULT(STDMETHODCALLTYPE *Outline)(T * This, const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,                 \
                                        ID2D1SimplifiedGeometrySink *sink);                                            \
    HRESULT(STDMETHODCALLTYPE *ComputeArea)(T * This, const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,             \
                                            FLOAT *area);                                                              \
    HRESULT(STDMETHODCALLTYPE *ComputeLength)(T * This, const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,           \
                                              FLOAT *length);                                                          \
    HRESULT(STDMETHODCALLTYPE *ComputePointAtLength)(T * This, FLOAT length, const D2D1_MATRIX_3X2_F *transform,       \
                                                     FLOAT tolerance, D2D1_POINT_2F *point,                            \
                                                     D2D1_POINT_2F *tangent);                                          \
    HRESULT(STDMETHODCALLTYPE *Widen)(T * This, FLOAT width, ID2D1StrokeStyle * style,                                 \
                                      const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,                             \
                                      ID2D1SimplifiedGeometrySink *sink);
MIME_COM_INTERFACE(ID2D1Geometry)

#define ID2D1RectangleGeometry_METHODS(T)                                                                              \
    ID2D1Geometry_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetRect)(T * This, D2D1_RECT_F * rect);
MIME_COM_INTERFACE(ID2D1RectangleGeometry)

#define ID2D1RoundedRectangleGeometry_METHODS(T)                                                                       \
    ID2D1Geometry_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetRoundedRect)(T * This, D2D1_ROUNDED_RECT * rect);
MIME_COM_INTERFACE(ID2D1RoundedRectangleGeometry)

#define ID2D1EllipseGeometry_METHODS(T)                                                                                \
    ID2D1Geometry_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetEllipse)(T * This, D2D1_ELLIPSE * ellipse);
MIME_COM_INTERFACE(ID2D1EllipseGeometry)

#define ID2D1GeometryGroup_METHODS(T)                                                                                  \
    ID2D1Geometry_METHODS(T)                                                                                           \
    D2D1_FILL_MODE(STDMETHODCALLTYPE *GetFillMode)(T * This);                                                          \
    UINT32(STDMETHODCALLTYPE *GetSourceGeometryCount)(T * This);                                                       \
    void(STDMETHODCALLTYPE *GetSourceGeometries)(T * This, ID2D1Geometry * *geometries, UINT32 count);
MIME_COM_INTERFACE(ID2D1GeometryGroup)

#define ID2D1TransformedGeometry_METHODS(T)                                                                            \
    ID2D1Geometry_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetSourceGeometry)(T * This, ID2D1Geometry * *geometry);                                   \
    void(STDMETHODCALLTYPE *GetTransform)(T * This, D2D1_MATRIX_3X2_F * transform);
MIME_COM_INTERFACE(ID2D1TransformedGeometry)

#define ID2D1PathGeometry_METHODS(T)                                                                                   \
    ID2D1Geometry_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Open)(T * This, ID2D1GeometrySink * *sink);                                             \
    HRESULT(STDMETHODCALLTYPE *Stream)(T * This, ID2D1GeometrySink * sink);                                            \
    HRESULT(STDMETHODCALLTYPE *GetSegmentCount)(T * This, UINT32 * count);                                             \
    HRESULT(STDMETHODCALLTYPE *GetFigureCount)(T * This, UINT32 * count);
MIME_COM_INTERFACE(ID2D1PathGeometry)

#define ID2D1PathGeometry1_METHODS(T)                                                                                  \
    ID2D1PathGeometry_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *ComputePointAndSegmentAtLength)(T * This, FLOAT length, UINT32 start_segment,           \
                                                               const D2D1_MATRIX_3X2_F *transform, FLOAT tolerance,    \
                                                               D2D1_POINT_DESCRIPTION *point);
MIME_COM_INTERFACE(ID2D1PathGeometry1)

#define ID2D1Mesh_METHODS(T)                                                                                           \
    ID2D1Resource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Open)(T * This, ID2D1TessellationSink * *sink);
MIME_COM_INTERFACE(ID2D1Mesh)

#define ID2D1Layer_METHODS(T)                                                                                          \
    ID2D1Resource_METHODS(T)                                                                                           \
    D2D1_SIZE_F *(STDMETHODCALLTYPE *GetSize)(T * This, D2D1_SIZE_F * ret);
MIME_COM_INTERFACE(ID2D1Layer)

#define ID2D1DrawingStateBlock_METHODS(T)                                                                              \
    ID2D1Resource_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetDescription)(T * This, D2D1_DRAWING_STATE_DESCRIPTION * description);                   \
    void(STDMETHODCALLTYPE *SetDescription)(T * This, const D2D1_DRAWING_STATE_DESCRIPTION *description);             \
    void(STDMETHODCALLTYPE *SetTextRenderingParams)(T * This, IDWriteRenderingParams * params);                        \
    void(STDMETHODCALLTYPE *GetTextRenderingParams)(T * This, IDWriteRenderingParams * *params);
MIME_COM_INTERFACE(ID2D1DrawingStateBlock)

#define ID2D1DrawingStateBlock1_METHODS(T)                                                                             \
    ID2D1DrawingStateBlock_METHODS(T)                                                                                  \
    void(STDMETHODCALLTYPE *GetDescription1)(T * This, D2D1_DRAWING_STATE_DESCRIPTION1 * description);                 \
    void(STDMETHODCALLTYPE *SetDescription1)(T * This, const D2D1_DRAWING_STATE_DESCRIPTION1 *description);
MIME_COM_INTERFACE(ID2D1DrawingStateBlock1)

/* ------------------------------------------------------------------ render targets */

#define ID2D1RenderTarget_METHODS(T)                                                                                   \
    ID2D1Resource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateBitmap)(T * This, D2D1_SIZE_U size, const void *data, UINT32 pitch,               \
                                             const D2D1_BITMAP_PROPERTIES *props, ID2D1Bitmap **bitmap);               \
    HRESULT(STDMETHODCALLTYPE *CreateBitmapFromWicBitmap)(T * This, IWICBitmapSource * source,                         \
                                                          const D2D1_BITMAP_PROPERTIES *props, ID2D1Bitmap **bitmap);  \
    HRESULT(STDMETHODCALLTYPE *CreateSharedBitmap)(T * This, REFIID riid, void *data,                                  \
                                                   const D2D1_BITMAP_PROPERTIES *props, ID2D1Bitmap **bitmap);         \
    HRESULT(STDMETHODCALLTYPE *CreateBitmapBrush)(T * This, ID2D1Bitmap * bitmap,                                      \
                                                  const D2D1_BITMAP_BRUSH_PROPERTIES *bitmap_props,                    \
                                                  const D2D1_BRUSH_PROPERTIES *brush_props,                            \
                                                  ID2D1BitmapBrush **brush);                                           \
    HRESULT(STDMETHODCALLTYPE *CreateSolidColorBrush)(T * This, const D2D1_COLOR_F *color,                             \
                                                      const D2D1_BRUSH_PROPERTIES *props,                              \
                                                      ID2D1SolidColorBrush **brush);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateGradientStopCollection)(T * This, const D2D1_GRADIENT_STOP *stops,                \
                                                             UINT32 count, D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend,  \
                                                             ID2D1GradientStopCollection **collection);                \
    HRESULT(STDMETHODCALLTYPE *CreateLinearGradientBrush)(T * This, const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gp,  \
                                                          const D2D1_BRUSH_PROPERTIES *props,                          \
                                                          ID2D1GradientStopCollection *stops,                          \
                                                          ID2D1LinearGradientBrush **brush);                           \
    HRESULT(STDMETHODCALLTYPE *CreateRadialGradientBrush)(T * This, const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gp,  \
                                                          const D2D1_BRUSH_PROPERTIES *props,                          \
                                                          ID2D1GradientStopCollection *stops,                          \
                                                          ID2D1RadialGradientBrush **brush);                           \
    HRESULT(STDMETHODCALLTYPE *CreateCompatibleRenderTarget)(T * This, const D2D1_SIZE_F *size,                        \
                                                             const D2D1_SIZE_U *pixel_size,                            \
                                                             const D2D1_PIXEL_FORMAT *format,                          \
                                                             D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options,            \
                                                             ID2D1BitmapRenderTarget **target);                        \
    HRESULT(STDMETHODCALLTYPE *CreateLayer)(T * This, const D2D1_SIZE_F *size, ID2D1Layer **layer);                    \
    HRESULT(STDMETHODCALLTYPE *CreateMesh)(T * This, ID2D1Mesh * *mesh);                                               \
    void(STDMETHODCALLTYPE *DrawLine)(T * This, D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush * brush,                \
                                      FLOAT width, ID2D1StrokeStyle *style);                                           \
    void(STDMETHODCALLTYPE *DrawRectangle)(T * This, const D2D1_RECT_F *rect, ID2D1Brush *brush, FLOAT width,          \
                                           ID2D1StrokeStyle *style);                                                   \
    void(STDMETHODCALLTYPE *FillRectangle)(T * This, const D2D1_RECT_F *rect, ID2D1Brush *brush);                      \
    void(STDMETHODCALLTYPE *DrawRoundedRectangle)(T * This, const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush,          \
                                                  FLOAT width, ID2D1StrokeStyle *style);                               \
    void(STDMETHODCALLTYPE *FillRoundedRectangle)(T * This, const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush);         \
    void(STDMETHODCALLTYPE *DrawEllipse)(T * This, const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, FLOAT width,        \
                                         ID2D1StrokeStyle *style);                                                     \
    void(STDMETHODCALLTYPE *FillEllipse)(T * This, const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush);                    \
    void(STDMETHODCALLTYPE *DrawGeometry)(T * This, ID2D1Geometry * geometry, ID2D1Brush * brush, FLOAT width,         \
                                          ID2D1StrokeStyle *style);                                                    \
    void(STDMETHODCALLTYPE *FillGeometry)(T * This, ID2D1Geometry * geometry, ID2D1Brush * brush,                      \
                                          ID2D1Brush *opacity_brush);                                                  \
    void(STDMETHODCALLTYPE *FillMesh)(T * This, ID2D1Mesh * mesh, ID2D1Brush * brush);                                 \
    void(STDMETHODCALLTYPE *FillOpacityMask)(T * This, ID2D1Bitmap * mask, ID2D1Brush * brush,                         \
                                             D2D1_OPACITY_MASK_CONTENT content, const D2D1_RECT_F *dest,               \
                                             const D2D1_RECT_F *source);                                               \
    void(STDMETHODCALLTYPE *DrawBitmap)(T * This, ID2D1Bitmap * bitmap, const D2D1_RECT_F *dest, FLOAT opacity,        \
                                        D2D1_BITMAP_INTERPOLATION_MODE mode, const D2D1_RECT_F *source);              \
    void(STDMETHODCALLTYPE *DrawText)(T * This, const WCHAR *string, UINT32 length, IDWriteTextFormat *format,         \
                                      const D2D1_RECT_F *layout_rect, ID2D1Brush *brush,                               \
                                      D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring);                \
    void(STDMETHODCALLTYPE *DrawTextLayout)(T * This, D2D1_POINT_2F origin, IDWriteTextLayout * layout,                \
                                            ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options);                        \
    void(STDMETHODCALLTYPE *DrawGlyphRun)(T * This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,                 \
                                          ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring);                         \
    void(STDMETHODCALLTYPE *SetTransform)(T * This, const D2D1_MATRIX_3X2_F *transform);                               \
    void(STDMETHODCALLTYPE *GetTransform)(T * This, D2D1_MATRIX_3X2_F * transform);                                    \
    void(STDMETHODCALLTYPE *SetAntialiasMode)(T * This, D2D1_ANTIALIAS_MODE mode);                                     \
    D2D1_ANTIALIAS_MODE(STDMETHODCALLTYPE *GetAntialiasMode)(T * This);                                                \
    void(STDMETHODCALLTYPE *SetTextAntialiasMode)(T * This, D2D1_TEXT_ANTIALIAS_MODE mode);                            \
    D2D1_TEXT_ANTIALIAS_MODE(STDMETHODCALLTYPE *GetTextAntialiasMode)(T * This);                                       \
    void(STDMETHODCALLTYPE *SetTextRenderingParams)(T * This, IDWriteRenderingParams * params);                        \
    void(STDMETHODCALLTYPE *GetTextRenderingParams)(T * This, IDWriteRenderingParams * *params);                       \
    void(STDMETHODCALLTYPE *SetTags)(T * This, D2D1_TAG tag1, D2D1_TAG tag2);                                          \
    void(STDMETHODCALLTYPE *GetTags)(T * This, D2D1_TAG * tag1, D2D1_TAG * tag2);                                      \
    void(STDMETHODCALLTYPE *PushLayer)(T * This, const D2D1_LAYER_PARAMETERS *params, ID2D1Layer *layer);              \
    void(STDMETHODCALLTYPE *PopLayer)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Flush)(T * This, D2D1_TAG * tag1, D2D1_TAG * tag2);                                     \
    void(STDMETHODCALLTYPE *SaveDrawingState)(T * This, ID2D1DrawingStateBlock * block);                               \
    void(STDMETHODCALLTYPE *RestoreDrawingState)(T * This, ID2D1DrawingStateBlock * block);                            \
    void(STDMETHODCALLTYPE *PushAxisAlignedClip)(T * This, const D2D1_RECT_F *clip, D2D1_ANTIALIAS_MODE mode);        \
    void(STDMETHODCALLTYPE *PopAxisAlignedClip)(T * This);                                                             \
    void(STDMETHODCALLTYPE *Clear)(T * This, const D2D1_COLOR_F *color);                                               \
    void(STDMETHODCALLTYPE *BeginDraw)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *EndDraw)(T * This, D2D1_TAG * tag1, D2D1_TAG * tag2);                                   \
    D2D1_PIXEL_FORMAT *(STDMETHODCALLTYPE *GetPixelFormat)(T * This, D2D1_PIXEL_FORMAT * ret);                                                    \
    void(STDMETHODCALLTYPE *SetDpi)(T * This, FLOAT dpi_x, FLOAT dpi_y);                                               \
    void(STDMETHODCALLTYPE *GetDpi)(T * This, FLOAT * dpi_x, FLOAT * dpi_y);                                           \
    D2D1_SIZE_F *(STDMETHODCALLTYPE *GetSize)(T * This, D2D1_SIZE_F * ret);                                                                 \
    D2D1_SIZE_U *(STDMETHODCALLTYPE *GetPixelSize)(T * This, D2D1_SIZE_U * ret);                                                            \
    UINT32(STDMETHODCALLTYPE *GetMaximumBitmapSize)(T * This);                                                         \
    BOOL(STDMETHODCALLTYPE *IsSupported)(T * This, const D2D1_RENDER_TARGET_PROPERTIES *props);
MIME_COM_INTERFACE(ID2D1RenderTarget)

#define ID2D1BitmapRenderTarget_METHODS(T)                                                                             \
    ID2D1RenderTarget_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *GetBitmap)(T * This, ID2D1Bitmap * *bitmap);
MIME_COM_INTERFACE(ID2D1BitmapRenderTarget)

#define ID2D1HwndRenderTarget_METHODS(T)                                                                               \
    ID2D1RenderTarget_METHODS(T)                                                                                       \
    D2D1_WINDOW_STATE(STDMETHODCALLTYPE *CheckWindowState)(T * This);                                                  \
    HRESULT(STDMETHODCALLTYPE *Resize)(T * This, const D2D1_SIZE_U *size);                                             \
    HWND(STDMETHODCALLTYPE *GetHwnd)(T * This);
MIME_COM_INTERFACE(ID2D1HwndRenderTarget)

#define ID2D1GdiInteropRenderTarget_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetDC)(T * This, D2D1_DC_INITIALIZE_MODE mode, HDC * dc);                               \
    HRESULT(STDMETHODCALLTYPE *ReleaseDC)(T * This, const RECT *update);
MIME_COM_INTERFACE(ID2D1GdiInteropRenderTarget)

#define ID2D1DCRenderTarget_METHODS(T)                                                                                 \
    ID2D1RenderTarget_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *BindDC)(T * This, const HDC dc, const RECT *rect);
MIME_COM_INTERFACE(ID2D1DCRenderTarget)

/* ------------------------------------------------------------------ the factory */

#define ID2D1Factory_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ReloadSystemMetrics)(T * This);                                                         \
    void(STDMETHODCALLTYPE *GetDesktopDpi)(T * This, FLOAT * dpi_x, FLOAT * dpi_y);                                    \
    HRESULT(STDMETHODCALLTYPE *CreateRectangleGeometry)(T * This, const D2D1_RECT_F *rect,                             \
                                                        ID2D1RectangleGeometry **geometry);                            \
    HRESULT(STDMETHODCALLTYPE *CreateRoundedRectangleGeometry)(T * This, const D2D1_ROUNDED_RECT *rect,                \
                                                               ID2D1RoundedRectangleGeometry **geometry);              \
    HRESULT(STDMETHODCALLTYPE *CreateEllipseGeometry)(T * This, const D2D1_ELLIPSE *ellipse,                           \
                                                      ID2D1EllipseGeometry **geometry);                                \
    HRESULT(STDMETHODCALLTYPE *CreateGeometryGroup)(T * This, D2D1_FILL_MODE mode, ID2D1Geometry **geometries,         \
                                                    UINT32 count, ID2D1GeometryGroup **group);                         \
    HRESULT(STDMETHODCALLTYPE *CreateTransformedGeometry)(T * This, ID2D1Geometry * source,                            \
                                                          const D2D1_MATRIX_3X2_F *transform,                          \
                                                          ID2D1TransformedGeometry **geometry);                        \
    HRESULT(STDMETHODCALLTYPE *CreatePathGeometry)(T * This, ID2D1PathGeometry * *geometry);                           \
    HRESULT(STDMETHODCALLTYPE *CreateStrokeStyle)(T * This, const D2D1_STROKE_STYLE_PROPERTIES *props,                 \
                                                  const FLOAT *dashes, UINT32 dashes_count,                            \
                                                  ID2D1StrokeStyle **style);                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDrawingStateBlock)(T * This, const D2D1_DRAWING_STATE_DESCRIPTION *description,   \
                                                        IDWriteRenderingParams *params,                                \
                                                        ID2D1DrawingStateBlock **block);                               \
    HRESULT(STDMETHODCALLTYPE *CreateWicBitmapRenderTarget)(T * This, IWICBitmap * target,                              \
                                                            const D2D1_RENDER_TARGET_PROPERTIES *props,                \
                                                            ID2D1RenderTarget **render_target);                        \
    HRESULT(STDMETHODCALLTYPE *CreateHwndRenderTarget)(T * This, const D2D1_RENDER_TARGET_PROPERTIES *props,           \
                                                       const D2D1_HWND_RENDER_TARGET_PROPERTIES *hwnd_props,           \
                                                       ID2D1HwndRenderTarget **render_target);                         \
    HRESULT(STDMETHODCALLTYPE *CreateDxgiSurfaceRenderTarget)(T * This, IDXGISurface * surface,                        \
                                                              const D2D1_RENDER_TARGET_PROPERTIES *props,              \
                                                              ID2D1RenderTarget **render_target);                      \
    HRESULT(STDMETHODCALLTYPE *CreateDCRenderTarget)(T * This, const D2D1_RENDER_TARGET_PROPERTIES *props,            \
                                                     ID2D1DCRenderTarget **render_target);
MIME_COM_INTERFACE(ID2D1Factory)

/* ------------------------------------------------------------------ direct2d 1.1 */

#define ID2D1Properties_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    UINT32(STDMETHODCALLTYPE *GetPropertyCount)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *GetPropertyName)(T * This, UINT32 index, WCHAR * name, UINT32 count);                   \
    UINT32(STDMETHODCALLTYPE *GetPropertyNameLength)(T * This, UINT32 index);                                          \
    D2D1_PROPERTY_TYPE(STDMETHODCALLTYPE *GetType)(T * This, UINT32 index);                                            \
    UINT32(STDMETHODCALLTYPE *GetPropertyIndex)(T * This, const WCHAR *name);                                          \
    HRESULT(STDMETHODCALLTYPE *SetValueByName)(T * This, const WCHAR *name, D2D1_PROPERTY_TYPE type,                  \
                                               const BYTE *data, UINT32 size);                                         \
    HRESULT(STDMETHODCALLTYPE *SetValue)(T * This, UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *data,            \
                                         UINT32 size);                                                                 \
    HRESULT(STDMETHODCALLTYPE *GetValueByName)(T * This, const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *data,       \
                                               UINT32 size);                                                           \
    HRESULT(STDMETHODCALLTYPE *GetValue)(T * This, UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *data, UINT32 size);    \
    UINT32(STDMETHODCALLTYPE *GetValueSize)(T * This, UINT32 index);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetSubProperties)(T * This, UINT32 index, ID2D1Properties * *properties);
MIME_COM_INTERFACE(ID2D1Properties)

#define ID2D1Effect_METHODS(T)                                                                                         \
    ID2D1Properties_METHODS(T)                                                                                         \
    void(STDMETHODCALLTYPE *SetInput)(T * This, UINT32 index, ID2D1Image * input, BOOL invalidate);                    \
    HRESULT(STDMETHODCALLTYPE *SetInputCount)(T * This, UINT32 count);                                                 \
    void(STDMETHODCALLTYPE *GetInput)(T * This, UINT32 index, ID2D1Image * *input);                                    \
    UINT32(STDMETHODCALLTYPE *GetInputCount)(T * This);                                                                \
    void(STDMETHODCALLTYPE *GetOutput)(T * This, ID2D1Image * *output);
MIME_COM_INTERFACE(ID2D1Effect)

#define ID2D1CommandSink_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *BeginDraw)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *EndDraw)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *SetAntialiasMode)(T * This, D2D1_ANTIALIAS_MODE mode);                                  \
    HRESULT(STDMETHODCALLTYPE *SetTags)(T * This, D2D1_TAG tag1, D2D1_TAG tag2);                                       \
    HRESULT(STDMETHODCALLTYPE *SetTextAntialiasMode)(T * This, D2D1_TEXT_ANTIALIAS_MODE mode);                         \
    HRESULT(STDMETHODCALLTYPE *SetTextRenderingParams)(T * This, IDWriteRenderingParams * params);                     \
    HRESULT(STDMETHODCALLTYPE *SetTransform)(T * This, const D2D1_MATRIX_3X2_F *transform);                            \
    HRESULT(STDMETHODCALLTYPE *SetPrimitiveBlend)(T * This, D2D1_PRIMITIVE_BLEND blend);                               \
    HRESULT(STDMETHODCALLTYPE *SetUnitMode)(T * This, D2D1_UNIT_MODE mode);                                            \
    HRESULT(STDMETHODCALLTYPE *Clear)(T * This, const D2D1_COLOR_F *color);                                            \
    HRESULT(STDMETHODCALLTYPE *DrawGlyphRun)(T * This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,              \
                                             const DWRITE_GLYPH_RUN_DESCRIPTION *description,                          \
                                             ID2D1Brush *brush, DWRITE_MEASURING_MODE measuring);                      \
    HRESULT(STDMETHODCALLTYPE *DrawLine)(T * This, D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush * brush,             \
                                         FLOAT width, ID2D1StrokeStyle *style);                                        \
    HRESULT(STDMETHODCALLTYPE *DrawGeometry)(T * This, ID2D1Geometry * geometry, ID2D1Brush * brush, FLOAT width,      \
                                             ID2D1StrokeStyle *style);                                                 \
    HRESULT(STDMETHODCALLTYPE *DrawRectangle)(T * This, const D2D1_RECT_F *rect, ID2D1Brush *brush, FLOAT width,       \
                                              ID2D1StrokeStyle *style);                                                \
    HRESULT(STDMETHODCALLTYPE *DrawBitmap)(T * This, ID2D1Bitmap * bitmap, const D2D1_RECT_F *dest, FLOAT opacity,     \
                                           D2D1_INTERPOLATION_MODE mode, const D2D1_RECT_F *source,                    \
                                           const D2D1_MATRIX_4X4_F *perspective);                                      \
    HRESULT(STDMETHODCALLTYPE *DrawImage)(T * This, ID2D1Image * image, D2D1_INTERPOLATION_MODE mode,                  \
                                          const D2D1_POINT_2F *target_offset, const D2D1_RECT_F *source,               \
                                          UINT32 composite);                                                           \
    HRESULT(STDMETHODCALLTYPE *DrawGdiMetafile)(T * This, ID2D1GdiMetafile * metafile,                                 \
                                                const D2D1_POINT_2F *target_offset);                                   \
    HRESULT(STDMETHODCALLTYPE *FillMesh)(T * This, ID2D1Mesh * mesh, ID2D1Brush * brush);                              \
    HRESULT(STDMETHODCALLTYPE *FillOpacityMask)(T * This, ID2D1Bitmap * mask, ID2D1Brush * brush,                      \
                                                const D2D1_RECT_F *dest, const D2D1_RECT_F *source);                   \
    HRESULT(STDMETHODCALLTYPE *FillGeometry)(T * This, ID2D1Geometry * geometry, ID2D1Brush * brush,                   \
                                             ID2D1Brush *opacity_brush);                                               \
    HRESULT(STDMETHODCALLTYPE *FillRectangle)(T * This, const D2D1_RECT_F *rect, ID2D1Brush *brush);                   \
    HRESULT(STDMETHODCALLTYPE *PushAxisAlignedClip)(T * This, const D2D1_RECT_F *clip, D2D1_ANTIALIAS_MODE mode);     \
    HRESULT(STDMETHODCALLTYPE *PushLayer)(T * This, const D2D1_LAYER_PARAMETERS1 *params, ID2D1Layer *layer);          \
    HRESULT(STDMETHODCALLTYPE *PopAxisAlignedClip)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *PopLayer)(T * This);
MIME_COM_INTERFACE(ID2D1CommandSink)

#define ID2D1CommandList_METHODS(T)                                                                                    \
    ID2D1Image_METHODS(T)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *Stream)(T * This, ID2D1CommandSink * sink);                                             \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(ID2D1CommandList)

#define ID2D1GdiMetafileSink_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    BOOL(STDMETHODCALLTYPE *ProcessRecord)(T * This, DWORD type, const void *data, DWORD size);
MIME_COM_INTERFACE(ID2D1GdiMetafileSink)

#define ID2D1GdiMetafile_METHODS(T)                                                                                    \
    ID2D1Resource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Stream)(T * This, ID2D1GdiMetafileSink * sink);                                         \
    HRESULT(STDMETHODCALLTYPE *GetBounds)(T * This, D2D1_RECT_F * bounds);
MIME_COM_INTERFACE(ID2D1GdiMetafile)

#define ID2D1Multithread_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    BOOL(STDMETHODCALLTYPE *GetMultithreadProtected)(T * This);                                                        \
    void(STDMETHODCALLTYPE *Enter)(T * This);                                                                          \
    void(STDMETHODCALLTYPE *Leave)(T * This);
MIME_COM_INTERFACE(ID2D1Multithread)

#define ID2D1Device_METHODS(T)                                                                                         \
    ID2D1Resource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                     \
                                                    ID2D1DeviceContext **context);                                     \
    HRESULT(STDMETHODCALLTYPE *CreatePrintControl)(T * This, IWICImagingFactory * factory, void *job_target,           \
                                                   const D2D1_PRINT_CONTROL_PROPERTIES *props,                         \
                                                   ID2D1PrintControl **control);                                       \
    void(STDMETHODCALLTYPE *SetMaximumTextureMemory)(T * This, UINT64 maximum);                                        \
    UINT64(STDMETHODCALLTYPE *GetMaximumTextureMemory)(T * This);                                                      \
    void(STDMETHODCALLTYPE *ClearResources)(T * This, UINT32 milliseconds_since_use);
MIME_COM_INTERFACE(ID2D1Device)

#define ID2D1Device1_METHODS(T)                                                                                        \
    ID2D1Device_METHODS(T)                                                                                             \
    D2D1_RENDERING_PRIORITY(STDMETHODCALLTYPE *GetRenderingPriority)(T * This);                                        \
    void(STDMETHODCALLTYPE *SetRenderingPriority)(T * This, D2D1_RENDERING_PRIORITY priority);                         \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext1)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                    \
                                                     ID2D1DeviceContext1 **context);
MIME_COM_INTERFACE(ID2D1Device1)

#define ID2D1Device2_METHODS(T)                                                                                        \
    ID2D1Device1_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext2)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                    \
                                                     ID2D1DeviceContext2 **context);                                   \
    void(STDMETHODCALLTYPE *FlushDeviceContexts)(T * This, ID2D1Bitmap * bitmap);                                      \
    HRESULT(STDMETHODCALLTYPE *GetDxgiDevice)(T * This, IDXGIDevice * *dxgi_device);
MIME_COM_INTERFACE(ID2D1Device2)

#define ID2D1Device3_METHODS(T)                                                                                        \
    ID2D1Device2_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext3)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                    \
                                                     ID2D1DeviceContext3 **context);
MIME_COM_INTERFACE(ID2D1Device3)

#define ID2D1Device4_METHODS(T)                                                                                        \
    ID2D1Device3_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext4)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                    \
                                                     ID2D1DeviceContext4 **context);                                   \
    void(STDMETHODCALLTYPE *SetMaximumColorGlyphCacheMemory)(T * This, UINT64 size);                                   \
    UINT64(STDMETHODCALLTYPE *GetMaximumColorGlyphCacheMemory)(T * This);
MIME_COM_INTERFACE(ID2D1Device4)

#define ID2D1Device5_METHODS(T)                                                                                        \
    ID2D1Device4_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext5)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                    \
                                                     ID2D1DeviceContext5 **context);
MIME_COM_INTERFACE(ID2D1Device5)

#define ID2D1Device6_METHODS(T)                                                                                        \
    ID2D1Device5_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDeviceContext6)(T * This, D2D1_DEVICE_CONTEXT_OPTIONS options,                    \
                                                     ID2D1DeviceContext6 **context);
MIME_COM_INTERFACE(ID2D1Device6)

#define ID2D1DeviceContext_METHODS(T)                                                                                  \
    ID2D1RenderTarget_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *CreateBitmap1)(T * This, D2D1_SIZE_U size, const void *data, UINT32 pitch,              \
                                              const D2D1_BITMAP_PROPERTIES1 *props, ID2D1Bitmap1 **bitmap);            \
    HRESULT(STDMETHODCALLTYPE *CreateBitmapFromWicBitmap1)(T * This, IWICBitmapSource * source,                        \
                                                           const D2D1_BITMAP_PROPERTIES1 *props,                       \
                                                           ID2D1Bitmap1 **bitmap);                                     \
    HRESULT(STDMETHODCALLTYPE *CreateColorContext)(T * This, D2D1_COLOR_SPACE space, const BYTE *profile,              \
                                                   UINT32 size, ID2D1ColorContext **context);                          \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromFilename)(T * This, const WCHAR *path,                            \
                                                               ID2D1ColorContext **context);                           \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromWicColorContext)(T * This, IWICColorContext * wic,                \
                                                                      ID2D1ColorContext **context);                    \
    HRESULT(STDMETHODCALLTYPE *CreateBitmapFromDxgiSurface)(T * This, IDXGISurface * surface,                          \
                                                            const D2D1_BITMAP_PROPERTIES1 *props,                      \
                                                            ID2D1Bitmap1 **bitmap);                                    \
    HRESULT(STDMETHODCALLTYPE *CreateEffect)(T * This, REFCLSID clsid, ID2D1Effect * *effect);                         \
    HRESULT(STDMETHODCALLTYPE *CreateGradientStopCollection1)(T * This, const D2D1_GRADIENT_STOP *stops,               \
                                                              UINT32 count, D2D1_COLOR_SPACE pre,                      \
                                                              D2D1_COLOR_SPACE post, D2D1_BUFFER_PRECISION precision,  \
                                                              D2D1_EXTEND_MODE extend,                                 \
                                                              D2D1_COLOR_INTERPOLATION_MODE mode,                      \
                                                              ID2D1GradientStopCollection1 **collection);              \
    HRESULT(STDMETHODCALLTYPE *CreateImageBrush)(T * This, ID2D1Image * image,                                         \
                                                 const D2D1_IMAGE_BRUSH_PROPERTIES *image_props,                       \
                                                 const D2D1_BRUSH_PROPERTIES *brush_props, ID2D1ImageBrush **brush);   \
    HRESULT(STDMETHODCALLTYPE *CreateBitmapBrush1)(T * This, ID2D1Bitmap * bitmap,                                     \
                                                   const D2D1_BITMAP_BRUSH_PROPERTIES1 *bitmap_props,                  \
                                                   const D2D1_BRUSH_PROPERTIES *brush_props,                           \
                                                   ID2D1BitmapBrush1 **brush);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateCommandList)(T * This, ID2D1CommandList * *list);                                 \
    BOOL(STDMETHODCALLTYPE *IsDxgiFormatSupported)(T * This, DXGI_FORMAT format);                                      \
    BOOL(STDMETHODCALLTYPE *IsBufferPrecisionSupported)(T * This, D2D1_BUFFER_PRECISION precision);                    \
    HRESULT(STDMETHODCALLTYPE *GetImageLocalBounds)(T * This, ID2D1Image * image, D2D1_RECT_F * bounds);               \
    HRESULT(STDMETHODCALLTYPE *GetImageWorldBounds)(T * This, ID2D1Image * image, D2D1_RECT_F * bounds);               \
    HRESULT(STDMETHODCALLTYPE *GetGlyphRunWorldBounds)(T * This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,    \
                                                       DWRITE_MEASURING_MODE measuring, D2D1_RECT_F *bounds);          \
    void(STDMETHODCALLTYPE *GetDevice)(T * This, ID2D1Device * *device);                                               \
    void(STDMETHODCALLTYPE *SetTarget)(T * This, ID2D1Image * target);                                                 \
    void(STDMETHODCALLTYPE *GetTarget)(T * This, ID2D1Image * *target);                                                \
    void(STDMETHODCALLTYPE *SetRenderingControls)(T * This, const D2D1_RENDERING_CONTROLS *controls);                  \
    void(STDMETHODCALLTYPE *GetRenderingControls)(T * This, D2D1_RENDERING_CONTROLS * controls);                       \
    void(STDMETHODCALLTYPE *SetPrimitiveBlend)(T * This, D2D1_PRIMITIVE_BLEND blend);                                  \
    D2D1_PRIMITIVE_BLEND(STDMETHODCALLTYPE *GetPrimitiveBlend)(T * This);                                              \
    void(STDMETHODCALLTYPE *SetUnitMode)(T * This, D2D1_UNIT_MODE mode);                                               \
    D2D1_UNIT_MODE(STDMETHODCALLTYPE *GetUnitMode)(T * This);                                                          \
    void(STDMETHODCALLTYPE *DrawGlyphRun1)(T * This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,                \
                                           const DWRITE_GLYPH_RUN_DESCRIPTION *description, ID2D1Brush *brush,         \
                                           DWRITE_MEASURING_MODE measuring);                                           \
    void(STDMETHODCALLTYPE *DrawImage)(T * This, ID2D1Image * image, const D2D1_POINT_2F *target_offset,               \
                                       const D2D1_RECT_F *source, D2D1_INTERPOLATION_MODE mode,                        \
                                       UINT32 composite);                                                              \
    void(STDMETHODCALLTYPE *DrawGdiMetafile)(T * This, ID2D1GdiMetafile * metafile,                                    \
                                             const D2D1_POINT_2F *target_offset);                                      \
    void(STDMETHODCALLTYPE *DrawBitmap1)(T * This, ID2D1Bitmap * bitmap, const D2D1_RECT_F *dest, FLOAT opacity,       \
                                         D2D1_INTERPOLATION_MODE mode, const D2D1_RECT_F *source,                      \
                                         const D2D1_MATRIX_4X4_F *perspective);                                        \
    void(STDMETHODCALLTYPE *PushLayer1)(T * This, const D2D1_LAYER_PARAMETERS1 *params, ID2D1Layer *layer);            \
    HRESULT(STDMETHODCALLTYPE *InvalidateEffectInputRectangle)(T * This, ID2D1Effect * effect, UINT32 input,           \
                                                               const D2D1_RECT_F *rect);                               \
    HRESULT(STDMETHODCALLTYPE *GetEffectInvalidRectangleCount)(T * This, ID2D1Effect * effect, UINT32 * count);        \
    HRESULT(STDMETHODCALLTYPE *GetEffectInvalidRectangles)(T * This, ID2D1Effect * effect, D2D1_RECT_F * rects,        \
                                                           UINT32 count);                                              \
    HRESULT(STDMETHODCALLTYPE *GetEffectRequiredInputRectangles)(T * This, ID2D1Effect * effect,                       \
                                                                 const D2D1_RECT_F *output_rect,                       \
                                                                 const D2D1_EFFECT_INPUT_DESCRIPTION *inputs,          \
                                                                 D2D1_RECT_F *required, UINT32 count);                 \
    void(STDMETHODCALLTYPE *FillOpacityMask1)(T * This, ID2D1Bitmap * mask, ID2D1Brush * brush,                        \
                                              const D2D1_RECT_F *dest, const D2D1_RECT_F *source);
MIME_COM_INTERFACE(ID2D1DeviceContext)

#define ID2D1DeviceContext1_METHODS(T)                                                                                 \
    ID2D1DeviceContext_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *CreateFilledGeometryRealization)(T * This, ID2D1Geometry * geometry, FLOAT tolerance,    \
                                                                ID2D1GeometryRealization **realization);               \
    HRESULT(STDMETHODCALLTYPE *CreateStrokedGeometryRealization)(T * This, ID2D1Geometry * geometry, FLOAT tolerance,  \
                                                                 FLOAT width, ID2D1StrokeStyle *style,                 \
                                                                 ID2D1GeometryRealization **realization);              \
    void(STDMETHODCALLTYPE *DrawGeometryRealization)(T * This, ID2D1GeometryRealization * realization,                 \
                                                     ID2D1Brush *brush);
MIME_COM_INTERFACE(ID2D1DeviceContext1)

#define ID2D1DeviceContext2_METHODS(T)                                                                                 \
    ID2D1DeviceContext1_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateInk)(T * This, const void *start_point, ID2D1Ink **ink);                          \
    HRESULT(STDMETHODCALLTYPE *CreateInkStyle)(T * This, const void *props, ID2D1InkStyle **style);                    \
    HRESULT(STDMETHODCALLTYPE *CreateGradientMesh)(T * This, const void *patches, UINT32 count,                        \
                                                   ID2D1GradientMesh **mesh);                                          \
    HRESULT(STDMETHODCALLTYPE *CreateImageSourceFromWic)(T * This, IWICBitmapSource * source, UINT32 options,          \
                                                         ID2D1ImageSourceFromWic **image_source);                      \
    HRESULT(STDMETHODCALLTYPE *CreateLookupTable3D)(T * This, UINT32 precision, const UINT32 *extents,                 \
                                                    const BYTE *data, UINT32 data_size, const UINT32 *strides,         \
                                                    ID2D1LookupTable3D **table);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateImageSourceFromDxgi)(T * This, IDXGISurface *const *surfaces, UINT32 count,       \
                                                          D2D1_COLOR_SPACE space, UINT32 options,                      \
                                                          ID2D1ImageSource **image_source);                            \
    HRESULT(STDMETHODCALLTYPE *GetGradientMeshWorldBounds)(T * This, ID2D1GradientMesh * mesh,                         \
                                                           D2D1_RECT_F *bounds);                                       \
    void(STDMETHODCALLTYPE *DrawInk)(T * This, ID2D1Ink * ink, ID2D1Brush * brush, ID2D1InkStyle * style);             \
    void(STDMETHODCALLTYPE *DrawGradientMesh)(T * This, ID2D1GradientMesh * mesh);                                     \
    void(STDMETHODCALLTYPE *DrawGdiMetafile2)(T * This, ID2D1GdiMetafile * metafile, const D2D1_RECT_F *dest,          \
                                              const D2D1_RECT_F *source);                                              \
    HRESULT(STDMETHODCALLTYPE *CreateTransformedImageSource)(T * This, ID2D1ImageSource * source,                      \
                                                             const void *props,                                        \
                                                             ID2D1TransformedImageSource **transformed);
MIME_COM_INTERFACE(ID2D1DeviceContext2)

#define ID2D1DeviceContext3_METHODS(T)                                                                                 \
    ID2D1DeviceContext2_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateSpriteBatch)(T * This, ID2D1SpriteBatch * *batch);                                \
    void(STDMETHODCALLTYPE *DrawSpriteBatch)(T * This, ID2D1SpriteBatch * batch, UINT32 start, UINT32 count,           \
                                             ID2D1Bitmap *bitmap, D2D1_BITMAP_INTERPOLATION_MODE mode,                 \
                                             UINT32 options);
MIME_COM_INTERFACE(ID2D1DeviceContext3)

#define ID2D1DeviceContext4_METHODS(T)                                                                                 \
    ID2D1DeviceContext3_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateSvgGlyphStyle)(T * This, ID2D1SvgGlyphStyle * *style);                            \
    void(STDMETHODCALLTYPE *DrawText1)(T * This, const WCHAR *string, UINT32 length, IDWriteTextFormat *format,        \
                                       const D2D1_RECT_F *layout_rect, ID2D1Brush *brush,                              \
                                       ID2D1SvgGlyphStyle *style, UINT32 palette, D2D1_DRAW_TEXT_OPTIONS options,      \
                                       DWRITE_MEASURING_MODE measuring);                                               \
    void(STDMETHODCALLTYPE *DrawTextLayout1)(T * This, D2D1_POINT_2F origin, IDWriteTextLayout * layout,               \
                                             ID2D1Brush *brush, ID2D1SvgGlyphStyle *style, UINT32 palette,             \
                                             D2D1_DRAW_TEXT_OPTIONS options);                                          \
    void(STDMETHODCALLTYPE *DrawColorBitmapGlyphRun)(T * This, UINT32 format, D2D1_POINT_2F origin,                    \
                                                     const DWRITE_GLYPH_RUN *run, DWRITE_MEASURING_MODE measuring,     \
                                                     D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION snap);                        \
    void(STDMETHODCALLTYPE *DrawSvgGlyphRun)(T * This, D2D1_POINT_2F origin, const DWRITE_GLYPH_RUN *run,             \
                                             ID2D1Brush *brush, ID2D1SvgGlyphStyle *style, UINT32 palette,             \
                                             DWRITE_MEASURING_MODE measuring);                                         \
    HRESULT(STDMETHODCALLTYPE *GetColorBitmapGlyphImage)(T * This, UINT32 format, D2D1_POINT_2F origin,                \
                                                         IDWriteFontFace *face, FLOAT size, UINT16 glyph,              \
                                                         BOOL sideways, const D2D1_MATRIX_3X2_F *transform,            \
                                                         FLOAT dpi_x, FLOAT dpi_y, D2D1_MATRIX_3X2_F *glyph_transform, \
                                                         ID2D1Bitmap **bitmap);                                        \
    HRESULT(STDMETHODCALLTYPE *GetSvgGlyphImage)(T * This, D2D1_POINT_2F origin, IDWriteFontFace *face, FLOAT size,    \
                                                 UINT16 glyph, BOOL sideways, const D2D1_MATRIX_3X2_F *transform,      \
                                                 ID2D1Brush *brush, ID2D1SvgGlyphStyle *style, UINT32 palette,         \
                                                 D2D1_MATRIX_3X2_F *glyph_transform, ID2D1CommandList **list);
MIME_COM_INTERFACE(ID2D1DeviceContext4)

#define ID2D1DeviceContext5_METHODS(T)                                                                                 \
    ID2D1DeviceContext4_METHODS(T)                                                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateSvgDocument)(T * This, IStream * stream, D2D1_SIZE_F viewport_size,               \
                                                  ID2D1SvgDocument **document);                                        \
    void(STDMETHODCALLTYPE *DrawSvgDocument)(T * This, ID2D1SvgDocument * document);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromDxgiColorSpace)(T * This, UINT32 space,                           \
                                                                     ID2D1ColorContext1 **context);                    \
    HRESULT(STDMETHODCALLTYPE *CreateColorContextFromSimpleColorProfile)(T * This, const void *profile,                \
                                                                         ID2D1ColorContext1 **context);
MIME_COM_INTERFACE(ID2D1DeviceContext5)

#define ID2D1DeviceContext6_METHODS(T)                                                                                 \
    ID2D1DeviceContext5_METHODS(T)                                                                                     \
    void(STDMETHODCALLTYPE *BlendImage)(T * This, ID2D1Image * image, UINT32 mode, const D2D1_POINT_2F *offset,        \
                                        const D2D1_RECT_F *source, D2D1_INTERPOLATION_MODE interpolation);
MIME_COM_INTERFACE(ID2D1DeviceContext6)

#define ID2D1Factory1_METHODS(T)                                                                                       \
    ID2D1Factory_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *CreateDevice)(T * This, IDXGIDevice * dxgi_device, ID2D1Device * *device);              \
    HRESULT(STDMETHODCALLTYPE *CreateStrokeStyle1)(T * This, const D2D1_STROKE_STYLE_PROPERTIES1 *props,               \
                                                   const FLOAT *dashes, UINT32 dashes_count,                           \
                                                   ID2D1StrokeStyle1 **style);                                         \
    HRESULT(STDMETHODCALLTYPE *CreatePathGeometry1)(T * This, ID2D1PathGeometry1 * *geometry);                         \
    HRESULT(STDMETHODCALLTYPE *CreateDrawingStateBlock1)(T * This, const D2D1_DRAWING_STATE_DESCRIPTION1 *description,  \
                                                         IDWriteRenderingParams *params,                               \
                                                         ID2D1DrawingStateBlock1 **block);                             \
    HRESULT(STDMETHODCALLTYPE *CreateGdiMetafile)(T * This, IStream * stream, ID2D1GdiMetafile * *metafile);           \
    HRESULT(STDMETHODCALLTYPE *RegisterEffectFromStream)(T * This, REFCLSID clsid, IStream * property_xml,             \
                                                         const void *bindings, UINT32 count, void *factory);           \
    HRESULT(STDMETHODCALLTYPE *RegisterEffectFromString)(T * This, REFCLSID clsid, const WCHAR *property_xml,          \
                                                         const void *bindings, UINT32 count, void *factory);           \
    HRESULT(STDMETHODCALLTYPE *UnregisterEffect)(T * This, REFCLSID clsid);                                            \
    HRESULT(STDMETHODCALLTYPE *GetRegisteredEffects)(T * This, CLSID * effects, UINT32 count, UINT32 * returned,       \
                                                     UINT32 *registered);                                              \
    HRESULT(STDMETHODCALLTYPE *GetEffectProperties)(T * This, REFCLSID clsid, ID2D1Properties * *properties);
MIME_COM_INTERFACE(ID2D1Factory1)

#define ID2D1Factory2_METHODS(T)                                                                                       \
    ID2D1Factory1_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice2)(T * This, IDXGIDevice * dxgi_device, ID2D1Device1 * *device);
MIME_COM_INTERFACE(ID2D1Factory2)

#define ID2D1Factory3_METHODS(T)                                                                                       \
    ID2D1Factory2_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice3)(T * This, IDXGIDevice * dxgi_device, ID2D1Device2 * *device);
MIME_COM_INTERFACE(ID2D1Factory3)

#define ID2D1Factory4_METHODS(T)                                                                                       \
    ID2D1Factory3_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice4)(T * This, IDXGIDevice * dxgi_device, ID2D1Device3 * *device);
MIME_COM_INTERFACE(ID2D1Factory4)

#define ID2D1Factory5_METHODS(T)                                                                                       \
    ID2D1Factory4_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice5)(T * This, IDXGIDevice * dxgi_device, ID2D1Device4 * *device);
MIME_COM_INTERFACE(ID2D1Factory5)

#define ID2D1Factory6_METHODS(T)                                                                                       \
    ID2D1Factory5_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice6)(T * This, IDXGIDevice * dxgi_device, ID2D1Device5 * *device);
MIME_COM_INTERFACE(ID2D1Factory6)

#define ID2D1Factory7_METHODS(T)                                                                                       \
    ID2D1Factory6_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateDevice7)(T * This, IDXGIDevice * dxgi_device, ID2D1Device6 * *device);
MIME_COM_INTERFACE(ID2D1Factory7)

/* ------------------------------------------------------------------ svg */

#define ID2D1SvgAttribute_METHODS(T)                                                                                   \
    ID2D1Resource_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetElement)(T * This, ID2D1SvgElement * *element);                                         \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, ID2D1SvgAttribute * *attribute);
MIME_COM_INTERFACE(ID2D1SvgAttribute)

#define ID2D1SvgPaint_METHODS(T)                                                                                       \
    ID2D1SvgAttribute_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *SetPaintType)(T * This, D2D1_SVG_PAINT_TYPE type);                                      \
    D2D1_SVG_PAINT_TYPE(STDMETHODCALLTYPE *GetPaintType)(T * This);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetColor)(T * This, const D2D1_COLOR_F *color);                                         \
    void(STDMETHODCALLTYPE *GetColor)(T * This, D2D1_COLOR_F * color);                                                 \
    HRESULT(STDMETHODCALLTYPE *SetId)(T * This, const WCHAR *id);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetId)(T * This, WCHAR * id, UINT32 count);                                             \
    UINT32(STDMETHODCALLTYPE *GetIdLength)(T * This);
MIME_COM_INTERFACE(ID2D1SvgPaint)

#define ID2D1SvgStrokeDashArray_METHODS(T)                                                                             \
    ID2D1SvgAttribute_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *RemoveDashesAtEnd)(T * This, UINT32 count);                                             \
    HRESULT(STDMETHODCALLTYPE *UpdateDashes)(T * This, const D2D1_SVG_LENGTH *dashes, UINT32 count, UINT32 index);     \
    HRESULT(STDMETHODCALLTYPE *UpdateDashesFloat)(T * This, const FLOAT *dashes, UINT32 count, UINT32 index);          \
    HRESULT(STDMETHODCALLTYPE *GetDashes)(T * This, D2D1_SVG_LENGTH * dashes, UINT32 count, UINT32 index);             \
    HRESULT(STDMETHODCALLTYPE *GetDashesFloat)(T * This, FLOAT * dashes, UINT32 count, UINT32 index);                  \
    UINT32(STDMETHODCALLTYPE *GetDashesCount)(T * This);
MIME_COM_INTERFACE(ID2D1SvgStrokeDashArray)

#define ID2D1SvgPointCollection_METHODS(T)                                                                             \
    ID2D1SvgAttribute_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *RemovePointsAtEnd)(T * This, UINT32 count);                                             \
    HRESULT(STDMETHODCALLTYPE *UpdatePoints)(T * This, const D2D1_POINT_2F *points, UINT32 count, UINT32 index);       \
    HRESULT(STDMETHODCALLTYPE *GetPoints)(T * This, D2D1_POINT_2F * points, UINT32 count, UINT32 index);               \
    UINT32(STDMETHODCALLTYPE *GetPointsCount)(T * This);
MIME_COM_INTERFACE(ID2D1SvgPointCollection)

#define ID2D1SvgPathData_METHODS(T)                                                                                    \
    ID2D1SvgAttribute_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *RemoveSegmentDataAtEnd)(T * This, UINT32 count);                                        \
    HRESULT(STDMETHODCALLTYPE *UpdateSegmentData)(T * This, const FLOAT *data, UINT32 count, UINT32 index);            \
    HRESULT(STDMETHODCALLTYPE *GetSegmentData)(T * This, FLOAT * data, UINT32 count, UINT32 index);                    \
    UINT32(STDMETHODCALLTYPE *GetSegmentDataCount)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *RemoveCommandsAtEnd)(T * This, UINT32 count);                                           \
    HRESULT(STDMETHODCALLTYPE *UpdateCommands)(T * This, const D2D1_SVG_PATH_COMMAND *commands, UINT32 count,          \
                                               UINT32 index);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetCommands)(T * This, D2D1_SVG_PATH_COMMAND * commands, UINT32 count, UINT32 index);    \
    UINT32(STDMETHODCALLTYPE *GetCommandsCount)(T * This);                                                             \
    HRESULT(STDMETHODCALLTYPE *CreatePathGeometry)(T * This, D2D1_FILL_MODE mode, ID2D1PathGeometry1 * *geometry);
MIME_COM_INTERFACE(ID2D1SvgPathData)

#define ID2D1SvgElement_METHODS(T)                                                                                     \
    ID2D1Resource_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *GetDocument)(T * This, ID2D1SvgDocument * *document);                                      \
    HRESULT(STDMETHODCALLTYPE *GetTagName)(T * This, WCHAR * name, UINT32 count);                                      \
    UINT32(STDMETHODCALLTYPE *GetTagNameLength)(T * This);                                                             \
    BOOL(STDMETHODCALLTYPE *IsTextContent)(T * This);                                                                  \
    void(STDMETHODCALLTYPE *GetParent)(T * This, ID2D1SvgElement * *parent);                                           \
    BOOL(STDMETHODCALLTYPE *HasChildren)(T * This);                                                                    \
    void(STDMETHODCALLTYPE *GetFirstChild)(T * This, ID2D1SvgElement * *child);                                        \
    void(STDMETHODCALLTYPE *GetLastChild)(T * This, ID2D1SvgElement * *child);                                         \
    HRESULT(STDMETHODCALLTYPE *GetPreviousChild)(T * This, ID2D1SvgElement * reference, ID2D1SvgElement * *child);     \
    HRESULT(STDMETHODCALLTYPE *GetNextChild)(T * This, ID2D1SvgElement * reference, ID2D1SvgElement * *child);         \
    HRESULT(STDMETHODCALLTYPE *InsertChildBefore)(T * This, ID2D1SvgElement * child, ID2D1SvgElement * reference);     \
    HRESULT(STDMETHODCALLTYPE *AppendChild)(T * This, ID2D1SvgElement * child);                                        \
    HRESULT(STDMETHODCALLTYPE *ReplaceChild)(T * This, ID2D1SvgElement * child, ID2D1SvgElement * reference);          \
    HRESULT(STDMETHODCALLTYPE *RemoveChild)(T * This, ID2D1SvgElement * child);                                        \
    HRESULT(STDMETHODCALLTYPE *CreateChild)(T * This, const WCHAR *tag, ID2D1SvgElement **child);                      \
    BOOL(STDMETHODCALLTYPE *IsAttributeSpecified)(T * This, const WCHAR *name, BOOL *inherited);                       \
    UINT32(STDMETHODCALLTYPE *GetSpecifiedAttributeCount)(T * This);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetSpecifiedAttributeName)(T * This, UINT32 index, WCHAR * name, UINT32 count,          \
                                                          BOOL *inherited);                                            \
    HRESULT(STDMETHODCALLTYPE *GetSpecifiedAttributeNameLength)(T * This, UINT32 index, UINT32 * length,               \
                                                                BOOL *inherited);                                      \
    HRESULT(STDMETHODCALLTYPE *RemoveAttribute)(T * This, const WCHAR *name);                                          \
    HRESULT(STDMETHODCALLTYPE *SetTextValue)(T * This, const WCHAR *name, UINT32 count);                               \
    HRESULT(STDMETHODCALLTYPE *GetTextValue)(T * This, WCHAR * name, UINT32 count);                                    \
    UINT32(STDMETHODCALLTYPE *GetTextValueLength)(T * This);                                                           \
    HRESULT(STDMETHODCALLTYPE *SetAttributeValue)(T * This, const WCHAR *name, ID2D1SvgAttribute *value);              \
    HRESULT(STDMETHODCALLTYPE *GetAttributeValue)(T * This, const WCHAR *name, REFIID riid, void **value);             \
    HRESULT(STDMETHODCALLTYPE *SetAttributeValuePod)(T * This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_POD_TYPE type,    \
                                                     const void *value, UINT32 size);                                  \
    HRESULT(STDMETHODCALLTYPE *GetAttributeValuePod)(T * This, const WCHAR *name, D2D1_SVG_ATTRIBUTE_POD_TYPE type,    \
                                                     void *value, UINT32 size);                                        \
    HRESULT(STDMETHODCALLTYPE *SetAttributeValueString)(T * This, const WCHAR *name,                                   \
                                                        D2D1_SVG_ATTRIBUTE_STRING_TYPE type, const WCHAR *value);      \
    HRESULT(STDMETHODCALLTYPE *GetAttributeValueString)(T * This, const WCHAR *name,                                   \
                                                        D2D1_SVG_ATTRIBUTE_STRING_TYPE type, WCHAR *value,             \
                                                        UINT32 count);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetAttributeValueLength)(T * This, const WCHAR *name,                                   \
                                                        D2D1_SVG_ATTRIBUTE_STRING_TYPE type, UINT32 *length);
MIME_COM_INTERFACE(ID2D1SvgElement)

#define ID2D1SvgDocument_METHODS(T)                                                                                    \
    ID2D1Resource_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetViewportSize)(T * This, D2D1_SIZE_F size);                                           \
    D2D1_SIZE_F *(STDMETHODCALLTYPE *GetViewportSize)(T * This, D2D1_SIZE_F * ret);                                                         \
    HRESULT(STDMETHODCALLTYPE *SetRoot)(T * This, ID2D1SvgElement * root);                                             \
    void(STDMETHODCALLTYPE *GetRoot)(T * This, ID2D1SvgElement * *root);                                               \
    HRESULT(STDMETHODCALLTYPE *FindElementById)(T * This, const WCHAR *id, ID2D1SvgElement **element);                 \
    HRESULT(STDMETHODCALLTYPE *Serialize)(T * This, IStream * stream, ID2D1SvgElement * subtree);                      \
    HRESULT(STDMETHODCALLTYPE *Deserialize)(T * This, IStream * stream, ID2D1SvgElement * *subtree);                   \
    HRESULT(STDMETHODCALLTYPE *CreatePaint)(T * This, D2D1_SVG_PAINT_TYPE type, const D2D1_COLOR_F *color,             \
                                            const WCHAR *id, ID2D1SvgPaint **paint);                                   \
    HRESULT(STDMETHODCALLTYPE *CreateStrokeDashArray)(T * This, const D2D1_SVG_LENGTH *dashes, UINT32 count,           \
                                                      ID2D1SvgStrokeDashArray **array);                                \
    HRESULT(STDMETHODCALLTYPE *CreatePointCollection)(T * This, const D2D1_POINT_2F *points, UINT32 count,             \
                                                      ID2D1SvgPointCollection **collection);                           \
    HRESULT(STDMETHODCALLTYPE *CreatePathData)(T * This, const FLOAT *data, UINT32 data_count,                         \
                                               const D2D1_SVG_PATH_COMMAND *commands, UINT32 command_count,            \
                                               ID2D1SvgPathData **path);
MIME_COM_INTERFACE(ID2D1SvgDocument)

/* ------------------------------------------------------------------ entry points */

D2D1API HRESULT WINAPI D2D1CreateFactory(D2D1_FACTORY_TYPE type, REFIID riid, const D2D1_FACTORY_OPTIONS *options,
                                         void **factory);
D2D1API void WINAPI D2D1MakeRotateMatrix(FLOAT angle, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F *matrix);
D2D1API void WINAPI D2D1MakeSkewMatrix(FLOAT angle_x, FLOAT angle_y, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F *matrix);
D2D1API BOOL WINAPI D2D1IsMatrixInvertible(const D2D1_MATRIX_3X2_F *matrix);
D2D1API BOOL WINAPI D2D1InvertMatrix(D2D1_MATRIX_3X2_F *matrix);
D2D1API HRESULT WINAPI D2D1CreateDevice(IDXGIDevice *dxgi_device, const D2D1_CREATION_PROPERTIES *props,
                                        ID2D1Device **device);
D2D1API HRESULT WINAPI D2D1CreateDeviceContext(IDXGISurface *surface, const D2D1_CREATION_PROPERTIES *props,
                                               ID2D1DeviceContext **context);
D2D1API D2D1_COLOR_F *WINAPI D2D1ConvertColorSpace(D2D1_COLOR_F *result, D2D1_COLOR_SPACE source,
                                                   D2D1_COLOR_SPACE destination, const D2D1_COLOR_F *color);
D2D1API void WINAPI D2D1SinCos(FLOAT angle, FLOAT *s, FLOAT *c);
D2D1API FLOAT WINAPI D2D1Tan(FLOAT angle);
D2D1API FLOAT WINAPI D2D1Vec3Length(FLOAT x, FLOAT y, FLOAT z);
D2D1API FLOAT WINAPI D2D1ComputeMaximumScaleFactor(const D2D1_MATRIX_3X2_F *matrix);
D2D1API HRESULT WINAPI D2D1GetGradientMeshInteriorPointsFromCoonsPatch(const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1,
                                                                       const D2D1_POINT_2F *p2, const D2D1_POINT_2F *p3,
                                                                       const D2D1_POINT_2F *p4, const D2D1_POINT_2F *p5,
                                                                       const D2D1_POINT_2F *p6, const D2D1_POINT_2F *p7,
                                                                       const D2D1_POINT_2F *p8, const D2D1_POINT_2F *p9,
                                                                       const D2D1_POINT_2F *p10, const D2D1_POINT_2F *p11,
                                                                       D2D1_POINT_2F *tensor11, D2D1_POINT_2F *tensor12,
                                                                       D2D1_POINT_2F *tensor21, D2D1_POINT_2F *tensor22);

/* ------------------------------------------------------------------ interface and effect ids */

MIME_DEFINE_GUID(IID_ID2D1Resource, 0x2cd90691, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Brush, 0x2cd906a8, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Image, 0x65019f75, 0x8da2, 0x497c, 0xb3, 0x2c, 0xdf, 0xa3, 0x4e, 0x48, 0xed, 0xe6);
MIME_DEFINE_GUID(IID_ID2D1Bitmap, 0xa2296057, 0xea42, 0x4099, 0x98, 0x3b, 0x53, 0x9f, 0xb6, 0x50, 0x54, 0x26);
MIME_DEFINE_GUID(IID_ID2D1BitmapBrush, 0x2cd906aa, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1RenderTarget, 0x2cd90694, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Geometry, 0x2cd906a1, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1BitmapRenderTarget, 0x2cd90695, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1DCRenderTarget, 0x1c51bc64, 0xde61, 0x46fd, 0x98, 0x99, 0x63, 0xa5, 0xd8, 0xf0, 0x39, 0x50);
MIME_DEFINE_GUID(IID_ID2D1DrawingStateBlock, 0x28506e39, 0xebf6, 0x46a1, 0xbb, 0x47, 0xfd, 0x85, 0x56, 0x5a, 0xb9, 0x57);
MIME_DEFINE_GUID(IID_ID2D1SimplifiedGeometrySink, 0x2cd9069e, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1EllipseGeometry, 0x2cd906a4, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Factory, 0x06152247, 0x6f50, 0x465a, 0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07);
MIME_DEFINE_GUID(IID_ID2D1GdiInteropRenderTarget, 0xe0db51c3, 0x6f77, 0x4bae, 0xb3, 0xd5, 0xe4, 0x75, 0x09, 0xb3, 0x58, 0x38);
MIME_DEFINE_GUID(IID_ID2D1GeometryGroup, 0x2cd906a6, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1GeometrySink, 0x2cd9069f, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1GradientStopCollection, 0x2cd906a7, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Layer, 0x2cd9069b, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1LinearGradientBrush, 0x2cd906ab, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Mesh, 0x2cd906c2, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1PathGeometry, 0x2cd906a5, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1RadialGradientBrush, 0x2cd906ac, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1RectangleGeometry, 0x2cd906a2, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1RoundedRectangleGeometry, 0x2cd906a3, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1SolidColorBrush, 0x2cd906a9, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1StrokeStyle, 0x2cd9069d, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1TessellationSink, 0x2cd906c1, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1TransformedGeometry, 0x2cd906bb, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);
MIME_DEFINE_GUID(IID_ID2D1Properties, 0x483473d7, 0xcd46, 0x4f9d, 0x9d, 0x3a, 0x31, 0x12, 0xaa, 0x80, 0x15, 0x9d);
MIME_DEFINE_GUID(IID_ID2D1GdiMetafileSink, 0x82237326, 0x8111, 0x4f7c, 0xbc, 0xf4, 0xb5, 0xc1, 0x17, 0x55, 0x64, 0xfe);
MIME_DEFINE_GUID(IID_ID2D1GdiMetafile, 0x2f543dc3, 0xcfc1, 0x4211, 0x86, 0x4f, 0xcf, 0xd9, 0x1c, 0x6f, 0x33, 0x95);
MIME_DEFINE_GUID(IID_ID2D1StrokeStyle1, 0x10a72a66, 0xe91c, 0x43f4, 0x99, 0x3f, 0xdd, 0xf4, 0xb8, 0x2b, 0x0b, 0x4a);
MIME_DEFINE_GUID(IID_ID2D1CommandSink, 0x54d7898a, 0xa061, 0x40a7, 0xbe, 0xc7, 0xe4, 0x65, 0xbc, 0xba, 0x2c, 0x4f);
MIME_DEFINE_GUID(IID_ID2D1CommandList, 0xb4f34a19, 0x2383, 0x4d76, 0x94, 0xf6, 0xec, 0x34, 0x36, 0x57, 0xc3, 0xdc);
MIME_DEFINE_GUID(IID_ID2D1PrintControl, 0x2c1d867d, 0xc290, 0x41c8, 0xae, 0x7e, 0x34, 0xa9, 0x87, 0x02, 0xe9, 0xa5);
MIME_DEFINE_GUID(IID_ID2D1Effect, 0x28211a43, 0x7d89, 0x476f, 0x81, 0x81, 0x2d, 0x61, 0x59, 0xb2, 0x20, 0xad);
MIME_DEFINE_GUID(IID_ID2D1Bitmap1, 0xa898a84c, 0x3873, 0x4588, 0xb0, 0x8b, 0xeb, 0xbf, 0x97, 0x8d, 0xf0, 0x41);
MIME_DEFINE_GUID(IID_ID2D1ImageBrush, 0xfe9e984d, 0x3f95, 0x407c, 0xb5, 0xdb, 0xcb, 0x94, 0xd4, 0xe8, 0xf8, 0x7c);
MIME_DEFINE_GUID(IID_ID2D1BitmapBrush1, 0x41343a53, 0xe41a, 0x49a2, 0x91, 0xcd, 0x21, 0x79, 0x3b, 0xbb, 0x62, 0xe5);
MIME_DEFINE_GUID(IID_ID2D1GradientStopCollection1, 0xae1572f4, 0x5dd0, 0x4777, 0x99, 0x8b, 0x92, 0x79, 0x47, 0x2a, 0xe6, 0x3b);
MIME_DEFINE_GUID(IID_ID2D1ColorContext, 0x1c4820bb, 0x5771, 0x4518, 0xa5, 0x81, 0x2f, 0xe4, 0xdd, 0x0e, 0xc6, 0x57);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext, 0xe8f7fe7a, 0x191c, 0x466d, 0xad, 0x95, 0x97, 0x56, 0x78, 0xbd, 0xa9, 0x98);
MIME_DEFINE_GUID(IID_ID2D1Device, 0x47dd575d, 0xac05, 0x4cdd, 0x80, 0x49, 0x9b, 0x02, 0xcd, 0x16, 0xf4, 0x4c);
MIME_DEFINE_GUID(IID_ID2D1DrawingStateBlock1, 0x689f1f85, 0xc72e, 0x4e33, 0x8f, 0x19, 0x85, 0x75, 0x4e, 0xfd, 0x5a, 0xce);
MIME_DEFINE_GUID(IID_ID2D1PathGeometry1, 0x62baa2d2, 0xab54, 0x41b7, 0xb8, 0x72, 0x78, 0x7e, 0x01, 0x06, 0xa4, 0x21);
MIME_DEFINE_GUID(IID_ID2D1Factory1, 0xbb12d362, 0xdaee, 0x4b9a, 0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f);
MIME_DEFINE_GUID(IID_ID2D1Multithread, 0x31e6e7bc, 0xe0ff, 0x4d46, 0x8c, 0x64, 0xa0, 0xa8, 0xc4, 0x1c, 0x15, 0xd3);
MIME_DEFINE_GUID(IID_ID2D1GeometryRealization, 0xa16907d7, 0xbc02, 0x4801, 0x99, 0xe8, 0x8c, 0xf7, 0xf4, 0x85, 0xf7, 0x74);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext1, 0xd37f57e4, 0x6908, 0x459f, 0xa1, 0x99, 0xe7, 0x2f, 0x24, 0xf7, 0x99, 0x87);
MIME_DEFINE_GUID(IID_ID2D1Device1, 0xd21768e1, 0x23a4, 0x4823, 0xa1, 0x4b, 0x7c, 0x3e, 0xba, 0x85, 0xd6, 0x58);
MIME_DEFINE_GUID(IID_ID2D1Factory2, 0x94f81a73, 0x9212, 0x4376, 0x9c, 0x58, 0xb1, 0x6a, 0x3a, 0x0d, 0x39, 0x92);
MIME_DEFINE_GUID(IID_ID2D1CommandSink1, 0x9eb767fd, 0x4269, 0x4467, 0xb8, 0xc2, 0xeb, 0x30, 0xcb, 0x30, 0x57, 0x43);
MIME_DEFINE_GUID(IID_ID2D1InkStyle, 0xbae8b344, 0x23fc, 0x4071, 0x8c, 0xb5, 0xd0, 0x5d, 0x6f, 0x07, 0x38, 0x48);
MIME_DEFINE_GUID(IID_ID2D1Ink, 0xb499923b, 0x7029, 0x478f, 0xa8, 0xb3, 0x43, 0x2c, 0x7c, 0x5f, 0x53, 0x12);
MIME_DEFINE_GUID(IID_ID2D1GradientMesh, 0xf292e401, 0xc050, 0x4cde, 0x83, 0xd7, 0x04, 0x96, 0x2d, 0x3b, 0x23, 0xc2);
MIME_DEFINE_GUID(IID_ID2D1ImageSource, 0xc9b664e5, 0x74a1, 0x4378, 0x9a, 0xc2, 0xee, 0xfc, 0x37, 0xa3, 0xf4, 0xd8);
MIME_DEFINE_GUID(IID_ID2D1ImageSourceFromWic, 0x77395441, 0x1c8f, 0x4555, 0x86, 0x83, 0xf5, 0x0d, 0xab, 0x0f, 0xe7, 0x92);
MIME_DEFINE_GUID(IID_ID2D1TransformedImageSource, 0x7f1f79e5, 0x2796, 0x416c, 0x8f, 0x55, 0x70, 0x0f, 0x91, 0x14, 0x45, 0xe5);
MIME_DEFINE_GUID(IID_ID2D1LookupTable3D, 0x53dd9855, 0xa3b0, 0x4d5b, 0x82, 0xe1, 0x26, 0xe2, 0x5c, 0x5e, 0x57, 0x97);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext2, 0x394ea6a3, 0x0c34, 0x4321, 0x95, 0x0b, 0x6c, 0xa2, 0x0f, 0x0b, 0xe6, 0xc7);
MIME_DEFINE_GUID(IID_ID2D1Device2, 0xa44472e1, 0x8dfb, 0x4e60, 0x84, 0x92, 0x6e, 0x28, 0x61, 0xc9, 0xca, 0x8b);
MIME_DEFINE_GUID(IID_ID2D1Factory3, 0x0869759f, 0x4f00, 0x413f, 0xb0, 0x3e, 0x2b, 0xda, 0x45, 0x40, 0x4d, 0x0f);
MIME_DEFINE_GUID(IID_ID2D1CommandSink2, 0x3bab440e, 0x417e, 0x47df, 0xa2, 0xe2, 0xbc, 0x0b, 0xe6, 0xa0, 0x09, 0x16);
MIME_DEFINE_GUID(IID_ID2D1GdiMetafile1, 0x2e69f9e8, 0xdd3f, 0x4bf9, 0x95, 0xba, 0xc0, 0x4f, 0x49, 0xd7, 0x88, 0xdf);
MIME_DEFINE_GUID(IID_ID2D1GdiMetafileSink1, 0xfd0ecb6b, 0x91e6, 0x411e, 0x86, 0x55, 0x39, 0x5e, 0x76, 0x0f, 0x91, 0xb4);
MIME_DEFINE_GUID(IID_ID2D1SpriteBatch, 0x4dc583bf, 0x3a10, 0x438a, 0x87, 0x22, 0xe9, 0x76, 0x52, 0x24, 0xf1, 0xf1);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext3, 0x235a7496, 0x8351, 0x414c, 0xbc, 0xd4, 0x66, 0x72, 0xab, 0x2d, 0x8e, 0x00);
MIME_DEFINE_GUID(IID_ID2D1Device3, 0x852f2087, 0x802c, 0x4037, 0xab, 0x60, 0xff, 0x2e, 0x7e, 0xe6, 0xfc, 0x01);
MIME_DEFINE_GUID(IID_ID2D1Factory4, 0xbd4ec2d2, 0x0662, 0x4bee, 0xba, 0x8e, 0x6f, 0x29, 0xf0, 0x32, 0xe0, 0x96);
MIME_DEFINE_GUID(IID_ID2D1CommandSink3, 0x18079135, 0x4cf3, 0x4868, 0xbc, 0x8e, 0x06, 0x06, 0x7e, 0x6d, 0x24, 0x2d);
MIME_DEFINE_GUID(IID_ID2D1SvgGlyphStyle, 0xaf671749, 0xd241, 0x4db8, 0x8e, 0x41, 0xdc, 0xc2, 0xe5, 0xc1, 0xa4, 0x38);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext4, 0x8c427831, 0x3d90, 0x4476, 0xb6, 0x47, 0xc4, 0xfa, 0xe3, 0x49, 0xe4, 0xdb);
MIME_DEFINE_GUID(IID_ID2D1Device4, 0xd7bdb159, 0x5683, 0x4a46, 0xbc, 0x9c, 0x72, 0xdc, 0x72, 0x0b, 0x85, 0x8b);
MIME_DEFINE_GUID(IID_ID2D1Factory5, 0xc4349994, 0x838e, 0x4b0f, 0x8c, 0xab, 0x44, 0x99, 0x7d, 0x9e, 0xea, 0xcc);
MIME_DEFINE_GUID(IID_ID2D1CommandSink4, 0xc78a6519, 0x40d6, 0x4218, 0xb2, 0xde, 0xbe, 0xee, 0xb7, 0x44, 0xbb, 0x3e);
MIME_DEFINE_GUID(IID_ID2D1ColorContext1, 0x1ab42875, 0xc57f, 0x4be9, 0xbd, 0x85, 0x9c, 0xd7, 0x8d, 0x6f, 0x55, 0xee);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext5, 0x7836d248, 0x68cc, 0x4df6, 0xb9, 0xe8, 0xde, 0x99, 0x1b, 0xf6, 0x2e, 0xb7);
MIME_DEFINE_GUID(IID_ID2D1Device5, 0xd55ba0a4, 0x6405, 0x4694, 0xae, 0xf5, 0x08, 0xee, 0x1a, 0x43, 0x58, 0xb4);
MIME_DEFINE_GUID(IID_ID2D1Factory6, 0xf9976f46, 0xf642, 0x44c1, 0x97, 0xca, 0xda, 0x32, 0xea, 0x2a, 0x26, 0x35);
MIME_DEFINE_GUID(IID_ID2D1CommandSink5, 0x7047dd26, 0xb1e7, 0x44a7, 0x95, 0x9a, 0x83, 0x49, 0xe2, 0x14, 0x4f, 0xa8);
MIME_DEFINE_GUID(IID_ID2D1DeviceContext6, 0x985f7e37, 0x4ed0, 0x4a19, 0x98, 0xa3, 0x15, 0xb0, 0xed, 0xfd, 0xe3, 0x06);
MIME_DEFINE_GUID(IID_ID2D1Device6, 0x7bfef914, 0x2d75, 0x4bad, 0xbe, 0x87, 0xe1, 0x8d, 0xdb, 0x07, 0x7b, 0x6d);
MIME_DEFINE_GUID(IID_ID2D1Factory7, 0xbdc2bdd3, 0xb96c, 0x4de6, 0xbd, 0xf7, 0x99, 0xd4, 0x74, 0x54, 0x54, 0xde);
MIME_DEFINE_GUID(IID_ID2D1SvgAttribute, 0xc9cdb0dd, 0xf8c9, 0x4e70, 0xb7, 0xc2, 0x30, 0x1c, 0x80, 0x29, 0x2c, 0x5e);
MIME_DEFINE_GUID(IID_ID2D1SvgPaint, 0xd59bab0a, 0x68a2, 0x455b, 0xa5, 0xdc, 0x9e, 0xb2, 0x85, 0x4e, 0x24, 0x90);
MIME_DEFINE_GUID(IID_ID2D1SvgStrokeDashArray, 0xf1c0ca52, 0x92a3, 0x4f00, 0xb4, 0xce, 0xf3, 0x56, 0x91, 0xef, 0xd9, 0xd9);
MIME_DEFINE_GUID(IID_ID2D1SvgPointCollection, 0x9dbe4c0d, 0x3572, 0x4dd9, 0x98, 0x25, 0x55, 0x30, 0x81, 0x3b, 0xb7, 0x12);
MIME_DEFINE_GUID(IID_ID2D1SvgPathData, 0xc095e4f4, 0xbb98, 0x43d6, 0x97, 0x45, 0x4d, 0x1b, 0x84, 0xec, 0x98, 0x88);
MIME_DEFINE_GUID(IID_ID2D1SvgElement, 0xac7b67a6, 0x183e, 0x49c1, 0xa8, 0x23, 0x0e, 0xbe, 0x40, 0xb0, 0xdb, 0x29);
MIME_DEFINE_GUID(IID_ID2D1SvgDocument, 0x86b88e4d, 0xafa4, 0x4d7b, 0x88, 0xe4, 0x68, 0xa5, 0x1c, 0x4a, 0x0a, 0xec);
MIME_DEFINE_GUID(IID_ID2D1VertexBuffer, 0x9b8b1336, 0x00a5, 0x4668, 0x92, 0xb7, 0xce, 0xd5, 0xd8, 0xbf, 0x9b, 0x7b);
MIME_DEFINE_GUID(IID_ID2D1ResourceTexture, 0x688d15c3, 0x02b0, 0x438d, 0xb1, 0x3a, 0xd1, 0xb4, 0x4c, 0x32, 0xc3, 0x9a);
MIME_DEFINE_GUID(IID_ID2D1RenderInfo, 0x519ae1bd, 0xd19a, 0x420d, 0xb8, 0x49, 0x36, 0x4f, 0x59, 0x47, 0x76, 0xb7);
MIME_DEFINE_GUID(IID_ID2D1DrawInfo, 0x693ce632, 0x7f2f, 0x45de, 0x93, 0xfe, 0x18, 0xd8, 0x8b, 0x37, 0xaa, 0x21);
MIME_DEFINE_GUID(IID_ID2D1TransformNode, 0xb2efe1e7, 0x729f, 0x4102, 0x94, 0x9f, 0x50, 0x5f, 0xa2, 0x1b, 0xf6, 0x66);
MIME_DEFINE_GUID(IID_ID2D1Transform, 0xef1a287d, 0x342a, 0x4f76, 0x8f, 0xdb, 0xda, 0x0d, 0x6e, 0xa9, 0xf9, 0x2b);
MIME_DEFINE_GUID(IID_ID2D1DrawTransform, 0x36bfdcb6, 0x9739, 0x435d, 0xa3, 0x0d, 0xa6, 0x53, 0xbe, 0xff, 0x6a, 0x6f);
MIME_DEFINE_GUID(IID_ID2D1TransformGraph, 0x13d29038, 0xc3e6, 0x4034, 0x90, 0x81, 0x13, 0xb5, 0x3a, 0x41, 0x79, 0x92);
MIME_DEFINE_GUID(IID_ID2D1ConcreteTransform, 0x1a799d8a, 0x69f7, 0x4e4c, 0x9f, 0xed, 0x43, 0x7c, 0xcc, 0x66, 0x84, 0xcc);
MIME_DEFINE_GUID(IID_ID2D1BlendTransform, 0x63ac0b32, 0xba44, 0x450f, 0x88, 0x06, 0x7f, 0x4c, 0xa1, 0xff, 0x2f, 0x1b);
MIME_DEFINE_GUID(IID_ID2D1BorderTransform, 0x4998735c, 0x3a19, 0x473c, 0x97, 0x81, 0x65, 0x68, 0x47, 0xe3, 0xa3, 0x47);
MIME_DEFINE_GUID(IID_ID2D1OffsetTransform, 0x3fe6adea, 0x7643, 0x4f53, 0xbd, 0x14, 0xa0, 0xce, 0x63, 0xf2, 0x40, 0x42);
MIME_DEFINE_GUID(IID_ID2D1BoundsAdjustmentTransform, 0x90f732e2, 0x5092, 0x4606, 0xa8, 0x19, 0x86, 0x51, 0x97, 0x0b, 0xac, 0xcd);
MIME_DEFINE_GUID(IID_ID2D1EffectContext, 0x3d9f916b, 0x27dc, 0x4ad7, 0xb4, 0xf1, 0x64, 0x94, 0x53, 0x40, 0xf5, 0x63);
MIME_DEFINE_GUID(IID_ID2D1EffectImpl, 0xa248fd3f, 0x3e6c, 0x4e63, 0x9f, 0x03, 0x7f, 0x68, 0xec, 0xc9, 0x1d, 0xb9);
MIME_DEFINE_GUID(CLSID_D2D12DAffineTransform, 0x6aa97485, 0x6354, 0x4cfc, 0x90, 0x8c, 0xe4, 0xa7, 0x4f, 0x62, 0xc9, 0x6c);
MIME_DEFINE_GUID(CLSID_D2D13DPerspectiveTransform, 0xc2844d0b, 0x3d86, 0x46e7, 0x85, 0xba, 0x52, 0x6c, 0x92, 0x40, 0xf3, 0xfb);
MIME_DEFINE_GUID(CLSID_D2D13DTransform, 0xe8467b04, 0xec61, 0x4b8a, 0xb5, 0xde, 0xd4, 0xd7, 0x3d, 0xeb, 0xea, 0x5a);
MIME_DEFINE_GUID(CLSID_D2D1ArithmeticComposite, 0xfc151437, 0x049a, 0x4784, 0xa2, 0x4a, 0xf1, 0xc4, 0xda, 0xf2, 0x09, 0x87);
MIME_DEFINE_GUID(CLSID_D2D1Atlas, 0x913e2be4, 0xfdcf, 0x4fe2, 0xa5, 0xf0, 0x24, 0x54, 0xf1, 0x4f, 0xf4, 0x8);
MIME_DEFINE_GUID(CLSID_D2D1BitmapSource, 0x5fb6c24d, 0xc6dd, 0x4231, 0x94, 0x04, 0x50, 0xf4, 0xd5, 0xc3, 0x25, 0x2d);
MIME_DEFINE_GUID(CLSID_D2D1Blend, 0x81c5b77b, 0x13f8, 0x4cdd, 0xad, 0x20, 0xc8, 0x90, 0x54, 0x7a, 0xc6, 0x5d);
MIME_DEFINE_GUID(CLSID_D2D1Border, 0x2a2d49c0, 0x4acf, 0x43c7, 0x8c, 0x6a, 0x7c, 0x4a, 0x27, 0x87, 0x4d, 0x27);
MIME_DEFINE_GUID(CLSID_D2D1Brightness, 0x8cea8d1e, 0x77b0, 0x4986, 0xb3, 0xb9, 0x2f, 0x0c, 0x0e, 0xae, 0x78, 0x87);
MIME_DEFINE_GUID(CLSID_D2D1ColorManagement, 0x1a28524c, 0xfdd6, 0x4aa4, 0xae, 0x8f, 0x83, 0x7e, 0xb8, 0x26, 0x7b, 0x37);
MIME_DEFINE_GUID(CLSID_D2D1ColorMatrix, 0x921f03d6, 0x641c, 0x47df, 0x85, 0x2d, 0xb4, 0xbb, 0x61, 0x53, 0xae, 0x11);
MIME_DEFINE_GUID(CLSID_D2D1Composite, 0x48fc9f51, 0xf6ac, 0x48f1, 0x8b, 0x58, 0x3b, 0x28, 0xac, 0x46, 0xf7, 0x6d);
MIME_DEFINE_GUID(CLSID_D2D1ConvolveMatrix, 0x407f8c08, 0x5533, 0x4331, 0xa3, 0x41, 0x23, 0xcc, 0x38, 0x77, 0x84, 0x3e);
MIME_DEFINE_GUID(CLSID_D2D1Crop, 0xe23f7110, 0x0e9a, 0x4324, 0xaf, 0x47, 0x6a, 0x2c, 0x0c, 0x46, 0xf3, 0x5b);
MIME_DEFINE_GUID(CLSID_D2D1DirectionalBlur, 0x174319a6, 0x58e9, 0x49b2, 0xbb, 0x63, 0xca, 0xf2, 0xc8, 0x11, 0xa3, 0xdb);
MIME_DEFINE_GUID(CLSID_D2D1DiscreteTransfer, 0x90866fcd, 0x488e, 0x454b, 0xaf, 0x06, 0xe5, 0x04, 0x1b, 0x66, 0xc3, 0x6c);
MIME_DEFINE_GUID(CLSID_D2D1DisplacementMap, 0xedc48364, 0x0417, 0x4111, 0x94, 0x50, 0x43, 0x84, 0x5f, 0xa9, 0xf8, 0x90);
MIME_DEFINE_GUID(CLSID_D2D1DistantDiffuse, 0x3e7efd62, 0xa32d, 0x46d4, 0xa8, 0x3c, 0x52, 0x78, 0x88, 0x9a, 0xc9, 0x54);
MIME_DEFINE_GUID(CLSID_D2D1DistantSpecular, 0x428c1ee5, 0x77b8, 0x4450, 0x8a, 0xb5, 0x72, 0x21, 0x9c, 0x21, 0xab, 0xda);
MIME_DEFINE_GUID(CLSID_D2D1DpiCompensation, 0x6c26c5c7, 0x34e0, 0x46fc, 0x9c, 0xfd, 0xe5, 0x82, 0x37, 0x6, 0xe2, 0x28);
MIME_DEFINE_GUID(CLSID_D2D1Flood, 0x61c23c20, 0xae69, 0x4d8e, 0x94, 0xcf, 0x50, 0x07, 0x8d, 0xf6, 0x38, 0xf2);
MIME_DEFINE_GUID(CLSID_D2D1GammaTransfer, 0x409444c4, 0xc419, 0x41a0, 0xb0, 0xc1, 0x8c, 0xd0, 0xc0, 0xa1, 0x8e, 0x42);
MIME_DEFINE_GUID(CLSID_D2D1GaussianBlur, 0x1feb6d69, 0x2fe6, 0x4ac9, 0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5);
MIME_DEFINE_GUID(CLSID_D2D1Scale, 0x9daf9369, 0x3846, 0x4d0e, 0xa4, 0x4e, 0xc, 0x60, 0x79, 0x34, 0xa5, 0xd7);
MIME_DEFINE_GUID(CLSID_D2D1Histogram, 0x881db7d0, 0xf7ee, 0x4d4d, 0xa6, 0xd2, 0x46, 0x97, 0xac, 0xc6, 0x6e, 0xe8);
MIME_DEFINE_GUID(CLSID_D2D1HueRotation, 0x0f4458ec, 0x4b32, 0x491b, 0x9e, 0x85, 0xbd, 0x73, 0xf4, 0x4d, 0x3e, 0xb6);
MIME_DEFINE_GUID(CLSID_D2D1LinearTransfer, 0xad47c8fd, 0x63ef, 0x4acc, 0x9b, 0x51, 0x67, 0x97, 0x9c, 0x03, 0x6c, 0x06);
MIME_DEFINE_GUID(CLSID_D2D1LuminanceToAlpha, 0x41251ab7, 0x0beb, 0x46f8, 0x9d, 0xa7, 0x59, 0xe9, 0x3f, 0xcc, 0xe5, 0xde);
MIME_DEFINE_GUID(CLSID_D2D1Morphology, 0xeae6c40d, 0x626a, 0x4c2d, 0xbf, 0xcb, 0x39, 0x10, 0x01, 0xab, 0xe2, 0x02);
MIME_DEFINE_GUID(CLSID_D2D1OpacityMetadata, 0x6c53006a, 0x4450, 0x4199, 0xaa, 0x5b, 0xad, 0x16, 0x56, 0xfe, 0xce, 0x5e);
MIME_DEFINE_GUID(CLSID_D2D1PointDiffuse, 0xb9e303c3, 0xc08c, 0x4f91, 0x8b, 0x7b, 0x38, 0x65, 0x6b, 0xc4, 0x8c, 0x20);
MIME_DEFINE_GUID(CLSID_D2D1PointSpecular, 0x09c3ca26, 0x3ae2, 0x4f09, 0x9e, 0xbc, 0xed, 0x38, 0x65, 0xd5, 0x3f, 0x22);
MIME_DEFINE_GUID(CLSID_D2D1Premultiply, 0x06eab419, 0xdeed, 0x4018, 0x80, 0xd2, 0x3e, 0x1d, 0x47, 0x1a, 0xde, 0xb2);
MIME_DEFINE_GUID(CLSID_D2D1Saturation, 0x5cb2d9cf, 0x327d, 0x459f, 0xa0, 0xce, 0x40, 0xc0, 0xb2, 0x08, 0x6b, 0xf7);
MIME_DEFINE_GUID(CLSID_D2D1Shadow, 0xc67ea361, 0x1863, 0x4e69, 0x89, 0xdb, 0x69, 0x5d, 0x3e, 0x9a, 0x5b, 0x6b);
MIME_DEFINE_GUID(CLSID_D2D1SpotDiffuse, 0x818a1105, 0x7932, 0x44f4, 0xaa, 0x86, 0x08, 0xae, 0x7b, 0x2f, 0x2c, 0x93);
MIME_DEFINE_GUID(CLSID_D2D1SpotSpecular, 0xedae421e, 0x7654, 0x4a37, 0x9d, 0xb8, 0x71, 0xac, 0xc1, 0xbe, 0xb3, 0xc1);
MIME_DEFINE_GUID(CLSID_D2D1TableTransfer, 0x5bf818c3, 0x5e43, 0x48cb, 0xb6, 0x31, 0x86, 0x83, 0x96, 0xd6, 0xa1, 0xd4);
MIME_DEFINE_GUID(CLSID_D2D1Tile, 0xb0784138, 0x3b76, 0x4bc5, 0xb1, 0x3b, 0x0f, 0xa2, 0xad, 0x02, 0x65, 0x9f);
MIME_DEFINE_GUID(CLSID_D2D1Turbulence, 0xcf2bb6ae, 0x889a, 0x4ad7, 0xba, 0x29, 0xa2, 0xfd, 0x73, 0x2c, 0x9f, 0xc9);
MIME_DEFINE_GUID(CLSID_D2D1UnPremultiply, 0xfb9ac489, 0xad8d, 0x41ed, 0x99, 0x99, 0xbb, 0x63, 0x47, 0xd1, 0x10, 0xf7);
MIME_DEFINE_GUID(CLSID_D2D1YCbCr, 0x99503cc1, 0x66c7, 0x45c9, 0xa8, 0x75, 0x8a, 0xd8, 0xa7, 0x91, 0x44, 0x01);
MIME_DEFINE_GUID(CLSID_D2D1Contrast, 0xb648a78a, 0x0ed5, 0x4f80, 0xa9, 0x4a, 0x8e, 0x82, 0x5a, 0xca, 0x6b, 0x77);
MIME_DEFINE_GUID(CLSID_D2D1RgbToHue, 0x23f3e5ec, 0x91e8, 0x4d3d, 0xad, 0x0a, 0xaf, 0xad, 0xc1, 0x00, 0x4a, 0xa1);
MIME_DEFINE_GUID(CLSID_D2D1HueToRgb, 0x7b78a6bd, 0x0141, 0x4def, 0x8a, 0x52, 0x63, 0x56, 0xee, 0x0c, 0xbd, 0xd5);
MIME_DEFINE_GUID(CLSID_D2D1ChromaKey, 0x74c01f5b, 0x2a0d, 0x408c, 0x88, 0xe2, 0xc7, 0xa3, 0xc7, 0x19, 0x77, 0x42);
MIME_DEFINE_GUID(CLSID_D2D1Emboss, 0xb1c5eb2b, 0x0348, 0x43f0, 0x81, 0x07, 0x49, 0x57, 0xca, 0xcb, 0xa2, 0xae);
MIME_DEFINE_GUID(CLSID_D2D1Exposure, 0xb56c8cfa, 0xf634, 0x41ee, 0xbe, 0xe0, 0xff, 0xa6, 0x17, 0x10, 0x60, 0x04);
MIME_DEFINE_GUID(CLSID_D2D1Grayscale, 0x36dde0eb, 0x3725, 0x42e0, 0x83, 0x6d, 0x52, 0xfb, 0x20, 0xae, 0xe6, 0x44);
MIME_DEFINE_GUID(CLSID_D2D1Invert, 0xe0c3784d, 0xcb39, 0x4e84, 0xb6, 0xfd, 0x6b, 0x72, 0xf0, 0x81, 0x02, 0x63);
MIME_DEFINE_GUID(CLSID_D2D1Posterize, 0x2188945e, 0x33a3, 0x4366, 0xb7, 0xbc, 0x08, 0x6b, 0xd0, 0x2d, 0x08, 0x84);
MIME_DEFINE_GUID(CLSID_D2D1Sepia, 0x3a1af410, 0x5f1d, 0x4dbe, 0x84, 0xdf, 0x91, 0x5d, 0xa7, 0x9b, 0x71, 0x53);
MIME_DEFINE_GUID(CLSID_D2D1Sharpen, 0xc9b887cb, 0xc5ff, 0x4dc5, 0x97, 0x79, 0x27, 0x3d, 0xcf, 0x41, 0x7c, 0x7d);
MIME_DEFINE_GUID(CLSID_D2D1Straighten, 0x4da47b12, 0x79a3, 0x4fb0, 0x82, 0x37, 0xbb, 0xc3, 0xb2, 0xa4, 0xde, 0x08);
MIME_DEFINE_GUID(CLSID_D2D1TemperatureTint, 0x89176087, 0x8af9, 0x4a08, 0xae, 0xb1, 0x89, 0x5f, 0x38, 0xdb, 0x17, 0x66);
MIME_DEFINE_GUID(CLSID_D2D1Vignette, 0xc00c40be, 0x5e67, 0x4ca3, 0x95, 0xb4, 0xf4, 0xb0, 0x2c, 0x11, 0x51, 0x35);
MIME_DEFINE_GUID(CLSID_D2D1EdgeDetection, 0xeff583ca, 0xcb07, 0x4aa9, 0xac, 0x5d, 0x2c, 0xc4, 0x4c, 0x76, 0x46, 0x0f);
MIME_DEFINE_GUID(CLSID_D2D1HighlightsShadows, 0xcadc8384, 0x323f, 0x4c7e, 0xa3, 0x61, 0x2e, 0x2b, 0x24, 0xdf, 0x6e, 0xe4);
MIME_DEFINE_GUID(CLSID_D2D1LookupTable3D, 0x349e0eda, 0x0088, 0x4a79, 0x9c, 0xa3, 0xc7, 0xe3, 0x00, 0x20, 0x20, 0x20);
MIME_DEFINE_GUID(CLSID_D2D1Opacity, 0x811d79a4, 0xde28, 0x4454, 0x80, 0x94, 0xc6, 0x46, 0x85, 0xf8, 0xbd, 0x4c);
MIME_DEFINE_GUID(CLSID_D2D1AlphaMask, 0xc80ecff0, 0x3fd5, 0x4f05, 0x83, 0x28, 0xc5, 0xd1, 0x72, 0x4b, 0x4f, 0x0a);
MIME_DEFINE_GUID(CLSID_D2D1CrossFade, 0x12f575e8, 0x4db1, 0x485f, 0x9a, 0x84, 0x03, 0xa0, 0x7d, 0xd3, 0x82, 0x9f);
MIME_DEFINE_GUID(CLSID_D2D1Tint, 0x36312b17, 0xf7dd, 0x4014, 0x91, 0x5d, 0xff, 0xca, 0x76, 0x8c, 0xf2, 0x11);
MIME_DEFINE_GUID(CLSID_D2D1WhiteLevelAdjustment, 0x44a1cadb, 0x6cdd, 0x4818, 0x8f, 0xf4, 0x26, 0xc1, 0xcf, 0xe9, 0x5b, 0xdb);
MIME_DEFINE_GUID(CLSID_D2D1HdrToneMap, 0x7b0b748d, 0x4610, 0x4486, 0xa9, 0x0c, 0x99, 0x9d, 0x9a, 0x2e, 0x2b, 0x11);

MIME_DEFINE_GUID(IID_ID2D1HwndRenderTarget, 0x2cd90698, 0x12e2, 0x11dc, 0x9f, 0xed, 0x00, 0x11, 0x43, 0xa0, 0x55, 0xf9);

#endif
