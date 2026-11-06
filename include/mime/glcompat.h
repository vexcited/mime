/*
 * layout of the vec4 array a compatibility program reads for the fixed function built-ins of GLSL: the host rewrites the
 * built-in variables into reads of mime_cg_u, the guest fills it from the context's fixed function state. the array has as
 * many vec4 as the highest slot the program reads needs, both stages the same.
 */
#ifndef MIME_GLCOMPAT_H
#define MIME_GLCOMPAT_H

#define MIME_GLC_PREFIX "mime_cg_"
#define MIME_GLC_ARRAY "mime_cg_u"

enum {
    MIME_GLC_MV = 0,          /* four columns each: modelview, projection, their product, then the three inverses. */
    MIME_GLC_P = 4,
    MIME_GLC_MVP = 8,
    MIME_GLC_MV_INVERSE = 12,
    MIME_GLC_P_INVERSE = 16,
    MIME_GLC_MVP_INVERSE = 20,
    MIME_GLC_NORMAL = 24,     /* three columns. */
    MIME_GLC_MISC = 27,       /* normal scale, depth range near, far and far minus near. */
    MIME_GLC_POINT0 = 28,     /* size, minimum, maximum, fade threshold. */
    MIME_GLC_POINT1 = 29,     /* the three distance attenuation factors. */
    MIME_GLC_STATE0 = 30,     /* alpha function as in the fixed shader (7 always), alpha reference, clamp vertex color, two side. */
    MIME_GLC_STATE1 = 31,     /* clamp fragment color. */
    MIME_GLC_FOG_COLOR = 32,
    MIME_GLC_FOG = 33,        /* density, start, end, scale. */
    MIME_GLC_LIGHT_MODEL = 34, /* ambient. */
    MIME_GLC_FRONT_MATERIAL = 35, /* emission, ambient, diffuse, specular, shininess in x. */
    MIME_GLC_BACK_MATERIAL = 40,
    MIME_GLC_TEXENV = 45,     /* 8 colors. */
    MIME_GLC_TEXMAT = 53,     /* 8 matrices of four columns. */
    MIME_GLC_CLIP = 85,       /* 8 planes in eye coordinates. */
    MIME_GLC_LIGHT = 93,      /* 8 lights of MIME_GLC_LIGHT_SIZE slots. */
    MIME_GLC_LIGHT_SIZE = 8,  /* ambient, diffuse, specular, position, half vector, spot direction and exponent, spot cutoff and its
                                 cosine and the constant and linear attenuation, quadratic attenuation. */
    MIME_GLC_COUNT = MIME_GLC_LIGHT + 8 * MIME_GLC_LIGHT_SIZE,
};

/* attribute locations the built-in vertex inputs have, as the generic attributes they alias. */
enum {
    MIME_GLC_ATTRIB_VERTEX = 0,
    MIME_GLC_ATTRIB_NORMAL = 2,
    MIME_GLC_ATTRIB_COLOR = 3,
    MIME_GLC_ATTRIB_SECONDARY = 4,
    MIME_GLC_ATTRIB_FOG = 5,
    MIME_GLC_ATTRIB_TEXCOORD = 8, /* 8 to 15. */
};

#endif
