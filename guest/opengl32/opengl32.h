/*
 * opengl32: OpenGL on the host gpu.
 *
 * the dll keeps the whole GL state in the process and sends Metal level commands to the host through the process's
 * gpu channel, see mime/gpu.h. it has no imports, so the same native image also serves x64 programs: their
 * opengl32.dll forwards every call here through xta64's native calls, see mime/mx64.h.
 *
 * GL names objects per share group; the host only knows channel wide ids, which the device hands out. rows of every
 * image are kept bottom first, as GL orders them.
 */
#ifndef MIME_OPENGL32_H
#define MIME_OPENGL32_H

#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>
#include <mime/gpu.h>
#include <rtl.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#undef GLAPI
#define GLAPI

typedef struct GL_PROC {
    const char *Name;
    const void *Function;
} GL_PROC;

extern const GL_PROC GlProcs[];
extern const ULONG GlProcCount;

/* ---------------------------------------------------------------- runtime */

void *gl_alloc(SIZE_T size); /* zeroed. */
void *gl_realloc(void *p, SIZE_T size);
void gl_free(void *p);
char *gl_strdup(const char *s);
void gl_log(const char *fmt, ...);
uint64_t gl_hash(const void *data, SIZE_T size, uint64_t seed);

typedef struct GL_LOCK {
    volatile LONG Locked;
    HANDLE Owner;
    LONG Depth;
} GL_LOCK;

void gl_lock(GL_LOCK *l);
void gl_unlock(GL_LOCK *l);

/* ---------------------------------------------------------------- device */

typedef struct GlMem {
    uint32_t block;
    uint64_t offset;
    uint8_t *ptr;
    uint64_t size;
} GlMem;

enum { ID_TEXTURE, ID_PROGRAM, ID_PIPELINE, ID_DEPTH_STENCIL, ID_SAMPLER, ID_SURFACE, ID_XFB, ID_QUERY, ID_KINDS };

typedef struct GlDevice {
    GL_LOCK lock;
    void *owner; /* the context whose state the host holds. */
    BOOL open, failed;
    MimeGpuHeader *hdr;
    uint8_t *base;
    uint8_t *ring;
    uint64_t put, published;
    uint64_t fence; /* the last fence emitted. */
    uint8_t *block_base[MIME_GPU_MAX_BLOCKS];
    uint64_t block_size[MIME_GPU_MAX_BLOCKS];
    uint32_t block_count;
    uint64_t features; /* MIME_GPU_FEATURE_* the host announced when the channel opened. */
    uint32_t *free_ids[ID_KINDS];
    uint32_t free_count[ID_KINDS], free_cap[ID_KINDS], next_id[ID_KINDS];
} GlDevice;

extern GlDevice GlDev;

BOOL gl_device_open(void);
BOOL gl_host_has(uint64_t features); /* all of them. false when the channel is not open. */
void gl_device_lock(void);
void gl_device_unlock(void);
uint32_t gl_new_id(int kind);
void gl_free_id(int kind, uint32_t id);
void *gl_emit(uint32_t op, uint32_t size); /* the record, header written; the device lock must be held. */
void gl_publish(void);
uint64_t gl_fence(void);
uint64_t gl_batch_fence(void); /* the fence that will cover the commands written now. */
BOOL gl_fence_done(uint64_t fence);
void gl_want(uint64_t fence);
void gl_wait(uint64_t fence);
BOOL gl_stream_alloc(uint64_t size, uint32_t align, GlMem *mem);
BOOL gl_heap_alloc(uint64_t size, GlMem *mem);
void gl_heap_free(GlMem *mem, uint64_t fence);
MimeGpuMem gl_mem(const GlMem *m);
uint64_t gl_cache_lookup(int kind, const void *key, SIZE_T size, BOOL *created);

/* ---------------------------------------------------------------- objects */

typedef struct GlBuffer {
    GLuint name;
    GLenum usage;
    GlMem mem;
    uint64_t size;
    uint64_t last_use; /* the batch fence of the last command reading it. */
    BOOL mapped;
    GLbitfield map_access;
    uint64_t map_offset, map_length;
    BOOL dirty_index_range;
    uint32_t max_index[3]; /* cached per index type: u8, u16, u32. */
    BOOL immutable;        /* made by glBufferStorage. */
    GLbitfield storage_flags;
    BOOL gpu_write;        /* a shader may write it: it is never moved to new memory while the gpu works. */
} GlBuffer;

typedef struct GlSamplerState {
    GLenum min_filter, mag_filter;
    GLenum wrap_s, wrap_t, wrap_r;
    GLfloat min_lod, max_lod, lod_bias;
    GLenum compare_mode, compare_func;
    GLfloat max_anisotropy;
    GLfloat border[4];
} GlSamplerState;

typedef struct GlTexture {
    GLuint name;
    GLenum target;
    uint32_t id;          /* host texture, 0 before an image. */
    uint32_t mtl_type, mtl_format;
    GLenum internal_format;
    uint32_t width, height, depth;
    uint32_t levels;      /* allocated on the host. */
    uint32_t samples;
    uint32_t swizzle;
    GLenum component_swizzle[4];
    BOOL bgra;            /* stored as bgra. */
    BOOL compressed;
    BOOL generate_mipmap; /* GL_GENERATE_MIPMAP: level 0 uploads rebuild the chain. */
    uint16_t defined_levels;
    GlSamplerState sampler;
    uint32_t sampler_id;  /* 0 when the state changed. */
    GLint base_level, max_level;
    uint32_t immutable_levels; /* glTexStorage or glTextureView made it, with this many levels. */
    struct {
        BOOL is;
        GLuint min_level, levels, min_layer, layers;
    } view;                    /* see textureview.c. */
    /* a buffer texture, see texbuffer.c. */
    struct {
        GLuint buffer;
        GLenum format;
        uint32_t mtl, host_mtl;
        GLintptr offset;
        GLsizeiptr size;
        BOOL ranged;
        GlMem host; /* the heap range the host texture was made over. */
    } tb;
} GlTexture;

typedef struct GlRenderbuffer {
    GLuint name;
    uint32_t id;
    GLenum internal_format;
    uint32_t mtl_format;
    uint32_t width, height, samples;
} GlRenderbuffer;

enum { ATTACH_NONE, ATTACH_TEXTURE, ATTACH_RENDERBUFFER };

typedef struct GlAttachment {
    int type;
    GLuint name;
    GLint level, layer;
} GlAttachment;

typedef struct GlFramebuffer {
    GLuint name;
    GLint default_width, default_height, default_layers, default_samples; /* glFramebufferParameteri. */
    GLboolean default_fixed_locations;
    GlAttachment color[MIME_GPU_MAX_COLORS];
    GlAttachment depth, stencil;
    GLenum draw_buffers[MIME_GPU_MAX_COLORS];
    GLenum read_buffer;
    uint32_t serial;
} GlFramebuffer;

typedef struct GlShader {
    GLuint name;
    GLenum type;
    char *source;
    BOOL deleted;
    LONG refs;
} GlShader;

typedef struct GlUniform {
    char *name;      /* "name" or "name[0]". */
    SIZE_T base_len; /* length without "[0]". */
    GLenum type;
    GLint size;
    uint32_t offset, array_stride, matrix_stride;
    int32_t texture[MIME_GPU_STAGES];
    GLint *units;    /* samplers: the texture unit of each element, images: the image unit. */
    int32_t binding; /* layout(binding), -1 without one. */
    uint32_t block;  /* a member of a named uniform block: its index plus one. */
} GlUniform;

typedef struct GlStorageBlock {
    char *name;
    uint32_t bytes, stages, variables;
    GLuint binding;
    uint32_t slot; /* the block's place in the host program. */
} GlStorageBlock;

/* the atomic counters of one binding, which the host keeps in a storage block of its own. */
typedef struct GlCounterBuffer {
    uint32_t slot, binding, stages, bytes, counters;
} GlCounterBuffer;

typedef struct GlCounter {
    char *name;
    uint32_t binding, offset, size, stages;
} GlCounter;

typedef struct GlUniformBlock {
    char *name;
    uint32_t bytes, stages;
    GLuint binding;
} GlUniformBlock;

typedef struct GlBufferBinding {
    GLuint name;
    uint64_t offset, size;
    BOOL whole;
} GlBufferBinding;

typedef struct GlAttrib {
    char *name;
    GLenum type;
    GLint size;
    GLint location;
    uint32_t slots;
} GlAttrib;

typedef struct GlSubroutines GlSubroutines;

typedef struct GlProgram {
    GLuint name;
    uint32_t id;           /* host program, 0 before a link. */
    GlShader *shaders[8];
    uint32_t shader_count;
    char *bindings;        /* glBindAttribLocation, "name\0location\0" pairs. */
    uint32_t bindings_len;
    char *frag_bindings;   /* glBindFragDataLocationIndexed, "name\0location\0index\0". */
    uint32_t frag_bindings_len;
    GlMem info;            /* MimeGpuProgramInfo written by the host. */
    BOOL linking, linked, link_status, deleted;
    char *log;
    GlUniform *uniforms;
    uint32_t uniform_count;
    GlUniformBlock *blocks;
    uint32_t block_count;
    GlAttrib *attribs;
    uint32_t attrib_count;
    uint32_t attrib_mask;  /* locations the vertex stage reads. */
    /* the fixed function built-ins of GLSL the program reads, see compatglsl.c: the locations of its built-in inputs and
       the array of uniforms they are read from. */
    uint32_t cg_attribs;
    uint32_t cg_offset, cg_stride, cg_slots;
    uint8_t *ublock;
    uint32_t ublock_size;
    BOOL ublock_dirty;
    uint32_t serial;       /* changes with each link. */

    /* stages besides the vertex and fragment ones, transform feedback and separable programs, see stages.c. */
    char *stage_source[3]; /* tessellation control, tessellation evaluation and geometry, as linked. */
    char **feedback_names; /* glTransformFeedbackVaryings, used by the next link. */
    uint32_t feedback_name_count;
    GLenum feedback_mode;
    MimeGpuStageInfo stage;
    struct GlFeedbackVarying {
        char *name;
        GLenum type;
        GLint size;
        GLuint buffer;
    } *feedback;
    uint32_t feedback_count;
    BOOL separable;
    char *main_source[2];   /* the vertex and fragment sources of the last link, null when it had none. */
    uint32_t stage_mask;    /* bit 0 vertex, 1 tessellation control, 2 evaluation, 3 geometry, 4 fragment. */
    uint32_t uniform_version; /* changes whenever a uniform is written. */
    GlSubroutines *subroutines[5]; /* by stage, see subroutine.c. */
    BOOL compute;
    uint32_t local_size[3];
    GlStorageBlock *storage;
    uint32_t storage_count;
    GlCounterBuffer *counter_buffers;
    uint32_t counter_buffer_count;
    GlCounter *counters;
    uint32_t counter_count;
} GlProgram;

typedef struct GlVertexAttrib {
    BOOL enabled;
    GLint size;
    GLenum type;
    BOOL normalized, integer, is_long;
    GLsizei stride;
    const void *pointer;
    GLuint buffer;
    GLuint divisor;
} GlVertexAttrib;

/* the fixed function arrays: vertex, color, normal, then a texture coordinate array per unit. */
#define FF_UNITS 8
#define FF_ARRAYS (3 + FF_UNITS)

/* ARB_vertex_attrib_binding: the buffer, offset, stride and divisor of a binding index, which attributes point to. */
typedef struct GlVertexBinding {
    GLuint buffer;
    GLintptr offset;
    GLsizei stride;
    GLuint divisor;
} GlVertexBinding;

typedef struct GlVertexArray {
    GLuint name;
    GlVertexAttrib attribs[MIME_GPU_MAX_ATTRIBS];
    GlVertexAttrib legacy[FF_ARRAYS];
    GLuint element_buffer;
    GlVertexBinding bindings[MIME_GPU_MAX_ATTRIBS];
    uint8_t attrib_binding[MIME_GPU_MAX_ATTRIBS]; /* the binding index plus one, 0 for the attribute's own. */
    uint16_t relative_offset[MIME_GPU_MAX_ATTRIBS];
    uint16_t long_mask; /* attributes set by glVertexAttribLFormat. */
} GlVertexArray;

/* names of one kind in a share group. */
typedef struct GlNames {
    void **items;
    GLuint count;
} GlNames;

/* display lists, dlist.c. */
typedef struct GlList GlList;

typedef struct GlListEntry {
    GLuint name;
    GlList *list;
} GlListEntry;

typedef struct GlListTable {
    GL_LOCK lock;
    GlListEntry *entries;
    uint32_t cap, used, count; /* cap is a power of two, used counts the removed slots too. */
    GLuint next;               /* where glGenLists looks first. */
} GlListTable;

typedef struct GlShared {
    LONG refs;
    GlNames buffers, textures, renderbuffers, programs, shaders, samplers;
    GlListTable lists;
} GlShared;

#define GL_RESERVED ((void *)1) /* a generated name without its object yet. */

void *gl_name_get(GlNames *n, GLuint name);
void gl_name_set(GlNames *n, GLuint name, void *object);
GLuint gl_name_new(GlNames *n);

/* ---------------------------------------------------------------- surfaces */

typedef struct GlSurface {
    struct GlSurface *next;
    HWND hwnd;
    HDC memory; /* the memory dc of a surface that no window shows. */
    uint32_t id;
    MIME_GPU_SURFACE info;
    uint32_t depth_format, stencil_format;
    BOOL configured;
    BOOL sync;
    uint64_t frames[3]; /* fences of the last presents. */
    uint32_t frame_index;
} GlSurface;

GlSurface *gl_surface_for(HDC dc, int pixel_format);
void gl_surface_refresh(GlSurface *s, HDC dc);
BOOL WINAPI wglSwapBuffers(HDC dc);

/* ---------------------------------------------------------------- contexts */

enum {
    DIRTY_PIPELINE = 1u << 0,   /* program, vertex formats, blend, targets. */
    DIRTY_DEPTH = 1u << 1,
    DIRTY_RASTER = 1u << 2,
    DIRTY_VIEWPORT = 1u << 3,
    DIRTY_SCISSOR = 1u << 4,
    DIRTY_BLEND_COLOR = 1u << 5,
    DIRTY_TEXTURES = 1u << 6,
    DIRTY_UNIFORMS = 1u << 7,
    DIRTY_VERTEX = 1u << 8,
    DIRTY_ALL = 0x1FF,
};

typedef struct GlTarget {
    MimeGpuAttachment color[MIME_GPU_MAX_COLORS];
    MimeGpuAttachment depth, stencil;
    uint16_t color_format[MIME_GPU_MAX_COLORS];
    uint16_t depth_format, stencil_format;
    uint32_t width, height, samples;
    BOOL valid;
} GlTarget;

typedef struct GlBlend {
    BOOL enabled;
    GLenum src_rgb, dst_rgb, src_alpha, dst_alpha;
    GLenum eq_rgb, eq_alpha;
} GlBlend;

/* ---------------------------------------------------------------- fixed function */

#define FF_MODELVIEW_DEPTH 32
#define FF_PROJECTION_DEPTH 10
#define FF_TEXTURE_DEPTH 10
#define FF_LIGHTS 8
#define FF_STORED_CAPS 64
#define FF_UNIFORM_SLOTS 112
#define FF_CLIP_PLANES 6

typedef struct GlLight {
    GLfloat ambient[4], diffuse[4], specular[4];
    GLfloat position[4];        /* eye coordinates. */
    GLfloat spot_direction[3];  /* eye coordinates. */
    GLfloat spot_exponent, spot_cutoff;
    GLfloat attenuation[3];
    BOOL enabled;
} GlLight;

typedef struct GlMaterial {
    GLfloat ambient[4], diffuse[4], specular[4], emission[4];
    GLfloat shininess;
} GlMaterial;

/* one vertex of glBegin: position, then the current color, normal and texture coordinate. */
typedef struct GlFfVertex {
    GLfloat position[4], color[4], normal[4], texcoord[4];
} GlFfVertex;

/* the current raster position and the pixel zoom, see raster.c. */
typedef struct GlRaster {
    BOOL valid;
    GLfloat position[4]; /* window x, y, depth, and the clip w. */
    GLfloat color[4];
    GLfloat zoom[2];
} GlRaster;

typedef struct GlFixed {
    GLenum matrix_mode;
    GLfloat modelview[FF_MODELVIEW_DEPTH][16];
    GLfloat projection[FF_PROJECTION_DEPTH][16];
    GLfloat texture[FF_UNITS][FF_TEXTURE_DEPTH][16];
    GLint modelview_depth, projection_depth, texture_depth[FF_UNITS]; /* entries below the top one. */

    /* current values: color, normal and the texture coordinate of unit 0 stay together for the vertex copy. */
    GLfloat current[3][4];
    GLfloat texcoord[FF_UNITS][4];
    GLfloat secondary[4], fog_coord; /* kept, not read by the pipeline. */
    GLboolean edge_flag;

    /* between glBegin and glEnd. */
    BOOL in_begin;
    GLenum begin_mode;
    GlFfVertex *vertices;
    uint32_t vertex_count, vertex_cap;
    uint32_t varying; /* bit 0 color, 1 normal, 2 texture coordinate: set while the batch is open. */

    GLenum shade_model;
    GLenum alpha_func;
    GLfloat alpha_ref;
    BOOL alpha_test, lighting, color_material, normalize, rescale_normal, fog;
    BOOL texture_2d[FF_UNITS], texture_1d[FF_UNITS], texture_3d[FF_UNITS];
    GLenum color_material_face, color_material_mode;
    GLenum tex_env_mode[FF_UNITS];
    GLfloat tex_env_color[FF_UNITS][4];
    GLenum fog_mode;
    GLfloat fog_density, fog_start, fog_end, fog_color[4];
    GLfloat model_ambient[4];
    BOOL local_viewer, two_side;
    GLenum color_control;
    GlMaterial material[2];
    GlLight lights[FF_LIGHTS];
    GLuint client_unit;
    GLenum render_mode;
    BOOL select_buffer, feedback_buffer;
    GLint name_depth;
    BOOL stored[FF_STORED_CAPS]; /* capabilities that only read back what was set. */
    BOOL unused_arrays[4];

    /* the built-in program and what it was last given. */
    struct GlProgram *program;
    BOOL program_failed, dirty;
    BOOL uniform_found;
    uint32_t uniform_offset, uniform_stride;
    GLfloat uniforms[FF_UNIFORM_SLOTS][4];
    GLfloat constants[4][4];

    /* a draw whose vertices are in window coordinates: pixel rectangles and lines that are wide or stippled. raster_op is
       a FF_RASTER value of fixed.h. */
    BOOL window_draw;
    BOOL polygon_draw; /* the draw in progress is of polygons, which the stipple applies to. */
    int raster_op;
    GLfloat window_mvp[16];
    GLint line_stipple_factor;
    GLushort line_stipple_pattern;

    /* user clip planes in eye coordinates as they were when glClipPlane took them, the logic operation and the polygon
       stipple. the enables are the stored capabilities. */
    GLdouble clip_plane[FF_CLIP_PLANES][4];
    GLenum logic_op;
    GLubyte polygon_stipple[128];
} GlFixed;

/* where a draw reads its vertices: a table of attributes by location, and the values of disabled ones. */
typedef struct GlVertexInput {
    const GlVertexAttrib *attribs;
    uint32_t mask;
    const GLfloat (*constants)[4]; /* NULL: the generic attribute values of the context. */
    BOOL fixed, flat;
    uint32_t constant_mask; /* with constants: the locations they are for, 0 for all of them. */
} GlVertexInput;

/* the display list being compiled, and the one running, see dlist.c. */
typedef struct GlDlBuilder GlDlBuilder;

typedef struct GlDlState {
    GLenum mode;       /* 0 outside glNewList, else GL_COMPILE or GL_COMPILE_AND_EXECUTE. */
    GLuint list;       /* the list glNewList names. */
    GLuint base;       /* glListBase. */
    int depth;         /* lists running inside each other. */
    BOOL in_begin;     /* a glBegin was compiled and its glEnd was not. */
    GlDlBuilder *b;
} GlDlState;

/* one transform feedback object: the buffers it writes and where it stands. */
typedef struct GlXfb {
    GLuint name;
    GLuint buffer[MIME_GPU_MAX_XFB_BUFFERS];
    GLintptr offset[MIME_GPU_MAX_XFB_BUFFERS];
    GLsizeiptr size[MIME_GPU_MAX_XFB_BUFFERS]; /* 0 with a buffer is all of it. */
    BOOL active, paused;
    uint32_t id; /* the host keeps its byte counters under this number. */
    GLenum mode;
    uint32_t stride; /* bytes of a captured vertex in buffer 0, from the program it was begun with. */
    uint32_t count;  /* buffers the program writes. */
    MimeGpuMem sent[MIME_GPU_MAX_XFB_BUFFERS];
    uint64_t sent_size[MIME_GPU_MAX_XFB_BUFFERS];
    BOOL sent_valid;
} GlXfb;

/* state of the stages and queries added after OpenGL 3, see stages.c and feedback.c. */
typedef struct GlStages {
    GLint patch_vertices;
    GLfloat patch_outer[4], patch_inner[2];
    BOOL rasterizer_discard;
    /* viewports and scissors that differ from the plain ones, by index. */
    GLfloat viewport[MIME_GPU_MAX_VIEWPORTS][4];
    GLdouble depth_range[MIME_GPU_MAX_VIEWPORTS][2];
    GLint scissor[MIME_GPU_MAX_VIEWPORTS][4];
    uint32_t viewport_mask, scissor_mask;
    GlNames xfbs;
    GlXfb *xfb, default_xfb;
    GLuint xfb_buffer;
    GLuint active_query[8][4]; /* by query kind and stream. */
    GLuint condition;          /* the query conditional rendering looks at. */
    GLenum condition_mode;
    BOOL skip;                 /* the condition says draws do nothing. */
    GlNames pipelines;         /* program pipeline objects. */
    GLuint pipeline_name;
    struct GlPipeline *pipeline;
    GLuint active_program;     /* glActiveShaderProgram of the default pipeline. */
} GlStages;

typedef struct GlContext {
    GlShared *shared;
    int pixel_format;
    PIXELFORMATDESCRIPTOR pfd;
    int major, minor, profile;
    BOOL software; /* a pixel format of Microsoft's software renderer. */
    GLbitfield flags;          /* GL_CONTEXT_FLAG_* from WGL_CONTEXT_FLAGS_ARB. */
    GLenum reset_strategy;     /* GL_NO_RESET_NOTIFICATION or GL_LOSE_CONTEXT_ON_RESET. */
    BOOL clip_upper_left, clip_zero_to_one; /* glClipControl. */
    HDC dc;
    GlSurface *surface;
    DWORD thread;
    GLenum error;
    BOOL made_current;

    /* bindings. */
    GLuint array_buffer, pixel_pack, pixel_unpack, copy_read, copy_write, uniform_buffer;
    GlBufferBinding uniform_bindings[MIME_GPU_MAX_UNIFORM_BLOCKS];
    GLuint texture_buffer, atomic_buffer, storage_buffer, dispatch_indirect_buffer, draw_indirect_buffer, parameter_buffer;
    GlBufferBinding storage_bindings[MIME_GPU_MAX_STORAGE_BLOCKS];
    GlBufferBinding atomic_bindings[MIME_GPU_MAX_ATOMIC_BINDINGS];
    struct {
        GLuint texture;
        GLint level, layer;
        GLboolean layered;
        GLenum access, format;
    } image_units[MIME_GPU_MAX_IMAGES];
    GlVertexArray default_vao;
    GlVertexArray *vao;
    GlNames vaos;
    GLuint program;
    GlProgram *current_program;
    GLuint draw_fbo, read_fbo;
    GlNames fbos;
    GlNames queries;
    GLuint elapsed_query;
    GLuint renderbuffer;
    GLuint active_texture;
    GLuint units[32][12]; /* see texture.c: unit slots by target. */
    GLuint sampler_units[32]; /* the sampler object bound to each unit, 0 for none. */
    GLenum draw_buffer, read_buffer; /* default framebuffer. */

    /* fixed state. */
    GLfloat clear_color[4];
    GLdouble clear_depth;
    GLint clear_stencil;
    GLint viewport[4];
    GLdouble depth_range[2];
    BOOL scissor_test;
    GLint scissor[4];
    GlBlend blend, blend_n[MIME_GPU_MAX_COLORS]; /* blend is draw buffer 0, blend_n[0] is not used. */
    GLfloat blend_color[4];
    GLboolean color_mask[4], color_mask_n[MIME_GPU_MAX_COLORS][4];
    BOOL depth_test;
    GLboolean depth_mask;
    GLenum depth_func;
    BOOL stencil_test;
    GLenum stencil_func[2];
    GLint stencil_ref[2];
    GLuint stencil_value_mask[2], stencil_write_mask[2];
    GLenum stencil_fail[2], stencil_zfail[2], stencil_zpass[2];
    BOOL cull_face;
    GLenum cull_mode, front_face;
    GLenum polygon_mode;
    BOOL polygon_offset_fill;
    GLfloat offset_factor, offset_units, offset_clamp;
    BOOL dither, multisample, sample_alpha_to_coverage, program_point_size, primitive_restart, depth_clamp;
    BOOL framebuffer_srgb;
    GLfloat line_width, point_size;
    /* the point parameters: the size limits, the fade threshold, the attenuation and the sprite coordinate origin. */
    GLfloat point_size_min, point_size_max, point_fade_threshold, point_attenuation[3];
    GLenum point_sprite_origin;
    GLint pack_alignment, unpack_alignment, unpack_row_length, unpack_skip_rows, unpack_skip_pixels,
        unpack_image_height, unpack_skip_images, pack_row_length, pack_skip_rows, pack_skip_pixels;
    GLfloat generic[MIME_GPU_MAX_ATTRIBS][4];
    GLuint generic_integer[MIME_GPU_MAX_ATTRIBS][4];
    GLenum generic_type[MIME_GPU_MAX_ATTRIBS];
    GLenum hints[8];
    GLint swap_interval;
    BOOL unpack_lsb_first, unpack_swap_bytes;
    GlRaster raster;
    GLenum provoking_vertex, clamp[3]; /* GL_CLAMP_VERTEX_COLOR, GL_CLAMP_FRAGMENT_COLOR, GL_CLAMP_READ_COLOR. */
    GLfloat sample_coverage, min_sample_shading;
    GLboolean sample_coverage_invert;
    GLbitfield sample_mask;
    GLuint restart_index;

    /* what the host has. */
    uint32_t dirty;
    uint32_t pipeline_id, depth_id;
    MimeGpuPipelineDesc pipeline_desc;
    struct {
        uint32_t id, sampler;
    } bound_tex[MIME_GPU_STAGES][MIME_GPU_MAX_TEXTURES];
    uint32_t bound_tex_count[MIME_GPU_STAGES];
    MimeGpuMem bound_vb[MIME_GPU_MAX_ATTRIBS];
    GlProgram *uniforms_of;
    uint32_t uniforms_serial;
    GLint last_viewport[4];

    /* the render pass in progress. */
    BOOL pass_open;
    GlTarget target;
    const void *target_owner;  /* framebuffer or surface. */
    uint32_t target_serial;
    struct {
        GLbitfield mask;
        GLfloat color[4];
        GLfloat depth;
        GLint stencil;
        const void *owner;
        uint32_t serial;
        GlTarget target;
    } pending_clear;
    BOOL dummy_texture_ready;
    uint32_t dummy_texture;

    GlFixed ff;
    GlDlState dl;

    /* the inputs of a draw with a program that reads the built-in vertex variables. */
    struct {
        GlVertexAttrib attribs[MIME_GPU_MAX_ATTRIBS];
        GLfloat constants[MIME_GPU_MAX_ATTRIBS][4];
    } cg;
    GlStages st;

    /* glPushAttrib and glPushClientAttrib, see attrib.c. */
    void *attrib_stack, *client_attrib_stack;
    int attrib_depth, client_attrib_depth;
} GlContext;

static inline GlContext *gl_current(void)
{
    return (GlContext *)NtCurrentTeb()->glCurrentRC;
}

void gl_error(GlContext *ctx, GLenum error);
int gl_texbuffer_get(GlContext *ctx, GLenum pname, GLdouble *v);
BOOL gl_textureview_param(const GlTexture *t, GLenum pname, GLint *value);
uint32_t gl_texture_sampling_swizzle(const GlTexture *t);
uint32_t gl_mtl_type_of(GLenum target);
uint32_t gl_mtl_texel_bytes(uint32_t mtl);
BOOL gl_texbuffer_ready(GlContext *ctx, GlTexture *t);
BOOL gl_texbuffer_param(GlContext *ctx, const GlTexture *t, GLenum pname, GLint *value);
int gl_atomics_get(GlContext *ctx, GLenum pname, GLdouble *v);
BOOL gl_atomics_get_indexed(GlContext *ctx, GLenum target, GLuint index, GLint *data);
GlBlend *gl_blend_at(GlContext *ctx, unsigned index);       /* the state of a draw buffer. */
GLboolean *gl_color_mask_at(GlContext *ctx, unsigned index);
const char *gl_extension_string(void); /* extensions.c: the space separated list. */
GLuint gl_extension_count(void);
const char *gl_extension_at(GLuint index);
GLfloat gl_point_size(const GlContext *ctx);
void gl_context_init(GlContext *ctx);
void gl_queries_destroy(GlContext *ctx);
GLuint64 gl_timestamp(void);

/* framebuffers and passes, fbo.c. */
void gl_resolve_target(GlContext *ctx, BOOL draw, GlTarget *t, const void **owner, uint32_t *serial);
void gl_begin_pass(GlContext *ctx);
void gl_end_pass(GlContext *ctx);
void gl_flush_clears(GlContext *ctx);
GlFramebuffer *gl_framebuffer(GlContext *ctx, GLuint name);
void gl_surface_to_bitmap(GlContext *ctx);
BOOL gl_read_source(GlContext *ctx, MimeGpuAttachment *a, uint32_t *format, uint32_t *width, uint32_t *height);
BOOL gl_read_depth_source(GlContext *ctx, BOOL stencil, MimeGpuAttachment *a, uint32_t *format, uint32_t *flags,
                          uint32_t *width, uint32_t *height);

/* buffers, buffer.c. */
GlBuffer *gl_buffer(GlContext *ctx, GLuint name);
GlBuffer *gl_bound_buffer(GlContext *ctx, GLenum target);

/* textures, texture.c. */
GlTexture *gl_texture(GlContext *ctx, GLuint name);
GlTexture *gl_texture_create(GlContext *ctx, GLuint name, GLenum target);
GLuint *gl_texture_slot(GlContext *ctx, GLenum target);
void gl_texture_copy_sub(GlContext *ctx, GlTexture *t, GLint level, uint32_t slice, GLint dx, GLint dy, GLint x, GLint y,
                         GLsizei w, GLsizei h);
BOOL gl_texture_read_compressed(GlContext *ctx, GlTexture *t, GLint level, uint32_t slice, GLint x, GLint y, GLsizei w,
                                GLsizei h, uint8_t *dst);
BOOL gl_query_create(GlContext *ctx, GLuint name, GLenum target);

/* vertex array objects and the binding model, vattrib.c. */
GlVertexArray *gl_vao_get(GlContext *ctx, GLuint name);
void gl_vao_legacy_pointer(GlVertexArray *v, GLuint index);
void gl_vao_legacy_divisor(GlVertexArray *v, GLuint index);
int gl_dsa_get(GlContext *ctx, GLenum pname, GLdouble *v); /* dsa.c: the limits and state of the 4.x additions. */
BOOL gl_vertex_binding_get(GlContext *ctx, GLenum pname, GLuint index, GLint64 *value);
GlTexture *gl_unit_texture(GlContext *ctx, GLuint unit, GLenum sampler_type);
uint32_t gl_texture_sampler(GlTexture *t);
uint32_t gl_sampler_state_id(const GlSamplerState *s, uint32_t levels, GLint base_level, GLint max_level);
uint32_t gl_unit_sampler(GlContext *ctx, GLuint unit, GlTexture *t); /* the sampler object bound to the unit or the texture's. */
BOOL gl_texture_ready(GlContext *ctx, GlTexture *t);
uint32_t gl_mtl_format(GLenum internal_format, GLenum format, GLenum type, uint32_t *swizzle, BOOL *compressed);
BOOL gl_format_is_depth(uint32_t mtl_format);
BOOL gl_format_has_stencil(uint32_t mtl_format);
uint32_t gl_format_bytes(uint32_t mtl_format);
uint32_t gl_dummy_texture(GlContext *ctx);
GlRenderbuffer *gl_renderbuffer(GlContext *ctx, GLuint name);
uint32_t gl_client_pixel_bytes(GLenum format, GLenum type);
GLenum gl_pixel_transfer_error(uint32_t mtl_format, GLenum format, GLenum type);
void gl_unpack_row(uint32_t dst_format, uint8_t *dst, GLenum format, GLenum type, const uint8_t *src, uint32_t width);
void gl_pack_row(uint32_t src_format, const uint8_t *src, GLenum format, GLenum type, uint8_t *dst, uint32_t width);
BOOL gl_read_pixels_into(GlContext *ctx, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                         void *pixels);

/* compute.c: shader storage buffers, images, dispatches and barriers. */
BOOL gl_is_image_type(GLenum type);
BOOL gl_emit_storage(GlContext *ctx, GlProgram *p); /* the device lock must be held. */
void gl_emit_images(GlContext *ctx, GlProgram *p);
void gl_emit_compute_state(GlContext *ctx, GlProgram *p); /* draw.c */
int gl_compute_get(GlContext *ctx, GLenum pname, GLdouble *v);
BOOL gl_compute_get_indexed(GlContext *ctx, GLenum target, GLuint index, GLint *data);
BOOL gl_buffer_read_ready(GlBuffer *b); /* waits for the gpu to finish writing a buffer a shader may write. */

/* programs, program.c. */
GlProgram *gl_program(GlContext *ctx, GLuint name);
BOOL gl_program_ready(GlProgram *p);
BOOL gl_program_link_sources(GlProgram *p, const char *vs, const char *fs);
void gl_program_free_internal(GlProgram *p);

/* stages, stages.c, feedback.c and pipeline.c. */
BOOL gl_draw_mode_valid(GlContext *ctx, GLenum mode);
BOOL gl_draw_mode_fits(GlContext *ctx, GlProgram *p, GLenum mode); /* raises the error when the program does not take it. */
uint32_t gl_pipeline_topology(GlContext *ctx, const GlProgram *p);
uint32_t gl_primitive_of(GLenum mode);
void gl_program_send_stages(GlProgram *p);   /* with the device lock held, before the program's create command. */
void gl_program_take_stages(GlProgram *p, const uint8_t *base, const MimeGpuProgramInfo *info);
void gl_program_free_stages(GlProgram *p);
BOOL gl_program_query_stage(GlProgram *p, GLenum pname, GLint *value);
int gl_stage_get(GlContext *ctx, GLenum pname, GLdouble *v);
void gl_stage_context_destroy(GlContext *ctx);
void gl_xfb_prepare(GlContext *ctx);               /* before a draw: the buffers the host captures into. */
BOOL gl_xfb_get_indexed(GlContext *ctx, GLenum target, GLuint index, GLint64 *value);
BOOL gl_xfb_bind(GlContext *ctx, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size, BOOL whole);
void gl_xfb_buffer_deleted(GlContext *ctx, GLuint buffer);
GlProgram *gl_pipeline_active_program(GlContext *ctx);
GlProgram *gl_pipeline_resolve(GlContext *ctx);
GlProgram *gl_draw_program(GlContext *ctx);   /* the program draws use: the one in use, else the pipeline's. */
void gl_pipeline_context_destroy(GlContext *ctx);
char *gl_subroutine_rewrite(const char *src, int stage, GlSubroutines **out);
void gl_subroutines_free(GlSubroutines *s);
void gl_program_set_int(GlContext *ctx, GlProgram *p, GLint location, GLint value);
void gl_program_get_int(GlProgram *p, GLint location, GLint *value);
char *gl_standin_vertex(const char *fragment);
void gl_program_free_stages_only(GlProgram *p);
void gl_program_copy_uniforms(GlProgram *dst, const GlProgram *src);
void gl_program_set_feedback_names(GlProgram *p, GLsizei count, const GLchar *const *names, GLenum mode);
void gl_stage_viewport_changed(GlContext *ctx);
void gl_stage_scissor_changed(GlContext *ctx);
void gl_stage_depth_changed(GlContext *ctx);
BOOL gl_program_stages_ok(GlProgram *p, const char *control, const char *evaluation, const char *geometry);
void gl_program_set_stage_sources(GlProgram *p, const char *control, const char *evaluation, const char *geometry);
void gl_stage_emit_viewports(GlContext *ctx, uint32_t dirty); /* the indexed viewports and scissors, after the plain ones. */
BOOL gl_query_target_known(GLenum target);
void gl_draw_feedback(GlContext *ctx, GLenum mode, uint32_t object, uint32_t buffer, uint32_t stride, GLsizei instances);

/* the fixed function built-in variables of GLSL, compatglsl.c. */
void gl_cg_input(GlContext *ctx, GlProgram *p, GlVertexInput *in);
void gl_cg_prepare(GlContext *ctx, GlProgram *p);
BOOL gl_cg_current(GlContext *ctx, uint32_t location, GLfloat out[4]);
BOOL gl_cg_core_violation(const char *source);

/* draws, draw.c. */
void gl_invalidate_bindings(GlContext *ctx);
void gl_draw_immediate(GlContext *ctx);

/* the fixed function pipeline, fixed.c, immediate.c and fixedshader.c. */
void gl_ff_init(GlContext *ctx);
void gl_ff_destroy(GlContext *ctx);
BOOL gl_ff_capability(GlContext *ctx, GLenum cap, BOOL on);
BOOL gl_ff_is_enabled(GlContext *ctx, GLenum cap, BOOL *known);
int gl_ff_get(GlContext *ctx, GLenum pname, GLdouble *v, BOOL *colors);
void gl_ff_track_color(GlContext *ctx);
int gl_ff_unused_array(GLenum cap);
GlProgram *gl_ff_begin_draw(GlContext *ctx, GlVertexInput *in, BOOL immediate, GLenum mode);
void gl_ff_prepare(GlContext *ctx, GlProgram *p);
BOOL gl_ff_stored(GlContext *ctx, GLenum cap);
BOOL gl_ff_stored_in(const BOOL *stored, GLenum cap);

/* raster.c: the raster position, bitmaps and pixel rectangles. */
void gl_raster_init(GlContext *ctx);

/* lines.c: lines that are wide or stippled are drawn as triangles made on the cpu. */
BOOL gl_lines_wanted(GlContext *ctx, GLenum mode);
void gl_lines_draw(GlContext *ctx, GLenum mode, const GlFfVertex *vertices, uint32_t count);
void gl_lines_draw_arrays(GlContext *ctx, GLenum mode, GLint first, GLsizei count);
void gl_lines_draw_elements(GlContext *ctx, GLenum mode, GLsizei count, GLenum type, const void *indices, GLint base_vertex);
void gl_ff_array_vertex(GlContext *ctx, GLint index, GlFfVertex *out);

/* draw.c: a fixed function draw of vertices that are not in the context's batch. */
void gl_draw_vertices(GlContext *ctx, GLenum mode, const GlFfVertex *vertices, uint32_t count, uint32_t varying);

/* display lists, dlist.c. */
void gl_dl_destroy(GlContext *ctx);
void gl_dl_shared_destroy(GlShared *shared);

/* between glBegin and glEnd only the vertex commands run: any other one is a GL_INVALID_OPERATION. */
#define GL_CONTEXT_OR_RETURN(...)                                                                                      \
    GlContext *ctx = gl_current();                                                                                     \
    if (!ctx)                                                                                                          \
        return __VA_ARGS__;                                                                                            \
    if (ctx->ff.in_begin) {                                                                                            \
        gl_error(ctx, GL_INVALID_OPERATION);                                                                           \
        return __VA_ARGS__;                                                                                            \
    }

/* the commands allowed between glBegin and glEnd. */
#define GL_CONTEXT_ALLOWED_IN_BEGIN(...)                                                                               \
    GlContext *ctx = gl_current();                                                                                     \
    if (!ctx)                                                                                                          \
        return __VA_ARGS__;

#ifndef MIME_GL_NO_RENAME
/* the commands a display list compiles are exported by wrappers that record them, and implemented here under another name. */
#include "dlist_rename.h"
#endif

#endif
