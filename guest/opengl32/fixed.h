/* layout of the vec4 uniform array the built-in fixed function program reads, shared by fixedshader.c and fixed.c. */
#ifndef MIME_OPENGL32_FIXED_H
#define MIME_OPENGL32_FIXED_H

#include "opengl32.h"

enum {
    FF_U_MVP = 0,          /* four columns. */
    FF_U_MV = 4,           /* four columns. */
    FF_U_NORMAL = 8,       /* three columns, the inverse transpose of the modelview matrix's upper 3x3. */
    FF_U_TEXMAT = 12,      /* four columns. */
    FF_U_FLAGS0 = 16,      /* lighting, alpha function, alpha reference, fog mode. */
    FF_U_FLAGS1 = 17,      /* texture environment mode, texture format bits, flat shading, normalize. */
    FF_U_ENV_COLOR = 18,
    FF_U_FOG = 19,         /* density, start, end, scale. */
    FF_U_FOG_COLOR = 20,
    FF_U_MISC = 21,        /* point size, color material mode, local viewer, separate specular color. */
    FF_U_MODEL_AMBIENT = 22,
    FF_U_EMISSION = 23,
    FF_U_AMBIENT = 24,
    FF_U_DIFFUSE = 25,
    FF_U_SPECULAR = 26,
    FF_U_SHININESS = 27,
    FF_U_LIGHT = 32,       /* FF_U_LIGHT_SIZE slots per light. */
    FF_U_LIGHT_SIZE = 7,   /* ambient, diffuse, specular, position, spot direction and exponent, spot cutoff, attenuation. */
    FF_U_CLIP = FF_U_LIGHT + FF_LIGHTS * FF_U_LIGHT_SIZE, /* FF_CLIP_PLANES planes in eye coordinates, 0 when off. */
    FF_U_FLAGS2 = FF_U_CLIP + FF_CLIP_PLANES,              /* polygon stipple on. */
    FF_U_STIPPLE = FF_U_FLAGS2 + 2,                        /* 16 slots, two rows of two halves each. */
    FF_U_COUNT = FF_U_STIPPLE + 16,
};

/* bits of the texture format slot. */
enum { FF_TEX_COLOR = 1, FF_TEX_ALPHA = 2, FF_TEX_INTENSITY = 4 };

/* texture environment modes as the shader numbers them; 0 is no texturing. */
enum { FF_ENV_OFF, FF_ENV_MODULATE, FF_ENV_REPLACE, FF_ENV_DECAL, FF_ENV_BLEND, FF_ENV_ADD, FF_ENV_BITMAP };

/* what a window draw is: lines, the pixels of glDrawPixels and the bits of glBitmap. */
enum { FF_RASTER_LINES, FF_RASTER_PIXELS, FF_RASTER_BITMAP };

static_assert(FF_U_COUNT <= FF_UNIFORM_SLOTS, "GlFixed::uniforms is too small");

/* in fragops.c. */
void gl_stipple_pack(const GlContext *ctx, GLfloat (*u)[4]);
BOOL gl_clip_planes_enabled(GlContext *ctx);
uint8_t gl_logic_op(GlContext *ctx);

/* in fixed.c. */
void gl_ff_pack(GlContext *ctx, GLfloat (*u)[4]);
BOOL gl_ff_texture_state(GlContext *ctx, uint32_t *env, uint32_t *format);

/* in fixedshader.c. */
const char *gl_ff_vertex_source(void);
const char *gl_ff_fragment_source(void);

#endif
