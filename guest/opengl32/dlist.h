/*
 * display lists. a command issued while a list is compiled is written to the list's stream of operations, and runs again when
 * the list is called. the commands that are compiled are the ones of guest/opengl32/dlist.txt, whose recorders and replays
 * tools/gengl.c writes; the vertex commands, the arrays and the pixel commands are recorded by hand in dlist.c.
 */
#ifndef MIME_OPENGL32_DLIST_H
#define MIME_OPENGL32_DLIST_H

#include "opengl32.h"

/* operations of the stream that are not a generated command. */
enum {
    GL_DL_OP_ERROR = 1,  /* an error the command raises when the list runs. */
    GL_DL_OP_CALL,
    GL_DL_OP_CALLS,
    GL_DL_OP_BATCH,      /* a glBegin, its vertices and glEnd, in a buffer the gpu reads. */
    GL_DL_OP_VERTEX,     /* the vertex commands outside glBegin and glEnd, which only draw inside the caller's. */
    GL_DL_OP_COLOR,
    GL_DL_OP_NORMAL,
    GL_DL_OP_TEXCOORD,
    GL_DL_OP_EDGEFLAG,
    GL_DL_OP_RASTERPOS,
    GL_DL_OP_IMAGE,      /* a command with pixels, copied when it was issued. */
    GL_DL_OP_POPATTRIB,
    GL_DL_FIRST_GENERATED = 64,
};

typedef void (*GlDlRun)(const uint8_t *payload);
extern const GlDlRun gl_dl_run[];
extern const uint32_t gl_dl_run_count;

/* the payload of an operation, zeroed; NULL when the list is out of memory. */
uint8_t *gl_dl_emit(GlContext *ctx, uint32_t op, size_t payload);
void gl_dl_error(GlContext *ctx, GLenum error);

static inline size_t gl_dl_bytes(long long count, size_t size)
{
    return count > 0 ? (size_t)count * size : 0;
}

/* a copied array takes at least 64 bytes, so a command that reads more than it was given reads zeros. */
static inline size_t gl_dl_blob(size_t bytes)
{
    return bytes < 64 ? 64 : (bytes + 7) & ~(size_t)7;
}

static inline void gl_dl_put_i(uint8_t *p, int slot, uint64_t v)
{
    memcpy(p + (size_t)slot * 8, &v, 8);
}

static inline void gl_dl_put_f(uint8_t *p, int slot, float v)
{
    memcpy(p + (size_t)slot * 8, &v, 4);
}

static inline void gl_dl_put_d(uint8_t *p, int slot, double v)
{
    memcpy(p + (size_t)slot * 8, &v, 8);
}

static inline uint64_t gl_dl_get_i(const uint8_t *p, int slot)
{
    uint64_t v;
    memcpy(&v, p + (size_t)slot * 8, 8);
    return v;
}

static inline float gl_dl_get_f(const uint8_t *p, int slot)
{
    float v;
    memcpy(&v, p + (size_t)slot * 8, 4);
    return v;
}

static inline double gl_dl_get_d(const uint8_t *p, int slot)
{
    double v;
    memcpy(&v, p + (size_t)slot * 8, 8);
    return v;
}

/* how many values the vector form of a command reads for a parameter name. */
int gl_dl_fog_count(GLenum pname);
int gl_dl_light_count(GLenum pname);
int gl_dl_light_model_count(GLenum pname);
int gl_dl_material_count(GLenum pname);
int gl_dl_point_parameter_count(GLenum pname);
int gl_dl_tex_env_count(GLenum pname);
int gl_dl_tex_gen_count(GLenum pname);
int gl_dl_tex_parameter_count(GLenum pname);

/*
 * the vertex commands while a list is compiled. each returns TRUE when the command is not to run now, that is in GL_COMPILE
 * mode; in GL_COMPILE_AND_EXECUTE it records and returns FALSE.
 */
BOOL gl_dl_vertex(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
BOOL gl_dl_color(GlContext *ctx, GLfloat r, GLfloat g, GLfloat b, GLfloat a);
BOOL gl_dl_normal(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z);
BOOL gl_dl_texcoord(GlContext *ctx, GLuint unit, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
BOOL gl_dl_rasterpos(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w);

/* runs the vertex commands the list recorded; the commands of immediate.h, which do not record. */
void gl_raster_position(GlContext *ctx, GLfloat x, GLfloat y, GLfloat z, GLfloat w);

/* the components of vertex i of a client array as floats, and how many there are, 0 for an array that is not enabled. */
int gl_read_array(GlContext *ctx, const GlVertexAttrib *a, GLint i, GLfloat *out);

/*
 * a glBegin, its vertices and glEnd made ready to draw. attributes the list knows are stored in the vertices when they differ
 * from one vertex to the next and are constants otherwise. the ones it does not know, because the list never set them, are
 * the current values of the context that calls the list: dep is the mask of those that no vertex of the batch knows, mixed
 * the mask of those that only the vertices after the first prefix[] do, which a batch in the buffer cannot hold, so it is kept
 * whole on the cpu. bit 0 of the masks is the color, 1 the normal and 2 the texture coordinate.
 */
typedef struct GlDlBatch {
    GLenum mode;
    uint32_t count;
    uint32_t varying, dep, mixed;
    uint32_t prefix[3];
    uint32_t stride, offset[3]; /* of the color, normal and texture coordinate in a vertex, 0 when not stored. */
    GLfloat constant[3][4];
    uint32_t set;               /* the attributes the batch set, which stay current when it ends. */
    GLfloat final[3][4];
    GlMem vb;                   /* the vertices in the heap, size 0 when they are on the cpu. */
    GlFfVertex *cpu;
    GlMem idx[2];               /* indices that list quads, fans and the like as triangles, for smooth and for flat shading. */
    uint32_t idx_count[2], idx_prim[2];
    uint64_t last_use;
} GlDlBatch;

void gl_draw_batch(GlContext *ctx, GlDlBatch *b);
void gl_batch_free(GlDlBatch *b);
BOOL gl_dl_font_bitmaps(GlContext *ctx, HDC dc, DWORD first, DWORD count, DWORD list_base);
uint32_t gl_whole_primitives(GLenum mode, uint32_t count);


#endif
