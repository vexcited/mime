#include "glsl_compat.h"

#include <mime/glcompat.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Str {
    char *p;
    size_t n, cap;
} Str;

static void str_add(Str *s, const char *text, size_t len)
{
    if (s->n + len + 1 > s->cap) {
        size_t cap = s->cap ? s->cap : 512;
        while (cap < s->n + len + 1)
            cap *= 2;
        s->p = realloc(s->p, cap);
        s->cap = cap;
    }
    memcpy(s->p + s->n, text, len);
    s->n += len;
    s->p[s->n] = 0;
}

static void str_cat(Str *s, const char *text)
{
    str_add(s, text, strlen(text));
}

static void str_fmt(Str *s, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0)
        str_add(s, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

static char *str_take(Str *s)
{
    if (!s->p)
        s->p = calloc(1, 1);
    char *p = s->p;
    s->p = nullptr;
    s->n = s->cap = 0;
    return p;
}

static bool ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* every name the built-ins are looked up by, in the order the analysis numbers them. */
static const char *const g_names[] = {
    "gl_Vertex", "gl_Normal", "gl_Color", "gl_SecondaryColor", "gl_FogCoord",
    "gl_MultiTexCoord0", "gl_MultiTexCoord1", "gl_MultiTexCoord2", "gl_MultiTexCoord3",
    "gl_MultiTexCoord4", "gl_MultiTexCoord5", "gl_MultiTexCoord6", "gl_MultiTexCoord7",
    "gl_FrontColor", "gl_BackColor", "gl_FrontSecondaryColor", "gl_BackSecondaryColor", "gl_TexCoord",
    "gl_FogFragCoord", "gl_ClipVertex", "ftransform",
    "gl_ModelViewMatrix", "gl_ProjectionMatrix", "gl_ModelViewProjectionMatrix",
    "gl_ModelViewMatrixInverse", "gl_ProjectionMatrixInverse", "gl_ModelViewProjectionMatrixInverse",
    "gl_ModelViewMatrixTranspose", "gl_ProjectionMatrixTranspose", "gl_ModelViewProjectionMatrixTranspose",
    "gl_ModelViewMatrixInverseTranspose", "gl_ProjectionMatrixInverseTranspose",
    "gl_ModelViewProjectionMatrixInverseTranspose",
    "gl_NormalMatrix", "gl_NormalScale",
    "gl_TextureMatrix", "gl_TextureMatrixInverse", "gl_TextureMatrixTranspose", "gl_TextureMatrixInverseTranspose",
    "gl_ClipPlane", "gl_Point", "gl_FrontMaterial", "gl_BackMaterial", "gl_LightSource", "gl_LightModel",
    "gl_FrontLightModelProduct", "gl_BackLightModelProduct", "gl_FrontLightProduct", "gl_BackLightProduct",
    "gl_TextureEnvColor", "gl_EyePlaneS", "gl_EyePlaneT", "gl_EyePlaneR", "gl_EyePlaneQ", "gl_ObjectPlaneS",
    "gl_ObjectPlaneT", "gl_ObjectPlaneR", "gl_ObjectPlaneQ", "gl_Fog", "gl_DepthRange",
    "gl_MaxLights", "gl_MaxClipPlanes", "gl_MaxTextureUnits", "gl_MaxTextureCoords",
    "gl_MaterialParameters", "gl_LightSourceParameters", "gl_LightModelParameters", "gl_LightModelProducts",
    "gl_LightProducts", "gl_PointParameters", "gl_FogParameters", "gl_DepthRangeParameters",
};

enum {
    N_VERTEX, N_NORMAL, N_COLOR, N_SECONDARY, N_FOGCOORD, N_MULTITEX0,
    N_FRONTCOLOR = N_MULTITEX0 + 8, N_BACKCOLOR, N_FRONTSECONDARY, N_BACKSECONDARY, N_TEXCOORD, N_FOGFRAG, N_CLIPVERTEX,
    N_FTRANSFORM, N_MV, N_P, N_MVP, N_MV_INV, N_P_INV, N_MVP_INV, N_MV_T, N_P_T, N_MVP_T, N_MV_IT, N_P_IT, N_MVP_IT,
    N_NORMALMATRIX, N_NORMALSCALE, N_TM, N_TM_INV, N_TM_T, N_TM_IT, N_CLIPPLANE, N_POINT, N_FMAT, N_BMAT, N_LIGHT,
    N_LIGHTMODEL, N_FLMP, N_BLMP, N_FLP, N_BLP, N_TEXENV, N_EYE_S, N_EYE_T, N_EYE_R, N_EYE_Q, N_OBJ_S, N_OBJ_T, N_OBJ_R,
    N_OBJ_Q, N_FOG, N_DEPTH, N_MAXLIGHTS, N_MAXCLIP, N_MAXUNITS, N_MAXCOORDS, N_T_MATERIAL, N_T_LIGHT, N_T_LIGHTMODEL,
    N_T_LMP, N_T_LP, N_T_POINT, N_T_FOG, N_T_DEPTH, N_COUNT
};

static_assert(sizeof(g_names) / sizeof(g_names[0]) == N_COUNT, "names and numbers disagree");

typedef struct Usage {
    bool used[N_COUNT];
    int texcoord_size;     /* the size a redeclaration gave, else 0. */
    int texcoord_literals; /* the highest literal index plus one. */
    bool texcoord_dynamic;
    bool fragment_color, fragment_data;
} Usage;

static int lookup(const char *name, size_t len)
{
    for (int i = 0; i < N_COUNT; i++)
        if (strlen(g_names[i]) == len && !memcmp(g_names[i], name, len))
            return i;
    return -1;
}

/* the end of a comment or a string starting at s, else s. */
static const char *skip_trivia(const char *s)
{
    if (s[0] == '/' && s[1] == '/') {
        while (*s && *s != '\n')
            s++;
        return s;
    }
    if (s[0] == '/' && s[1] == '*') {
        const char *end = strstr(s + 2, "*/");
        return end ? end + 2 : s + strlen(s);
    }
    return s;
}

static void scan(const char *text, Usage *u)
{
    for (const char *s = text; *s;) {
        const char *t = skip_trivia(s);
        if (t != s) {
            s = t;
            continue;
        }
        if (!ident_char(*s) || isdigit((unsigned char)*s)) {
            s++;
            continue;
        }
        const char *start = s;
        while (ident_char(*s))
            s++;
        size_t len = (size_t)(s - start);
        if (start != text && ident_char(start[-1]))
            continue;
        int id = lookup(start, len);
        if (id < 0) {
            if (len == 13 && !memcmp(start, "gl_FragColor", 12))
                u->fragment_color = true;
            continue;
        }
        u->used[id] = true;
        if (id == N_TEXCOORD) {
            const char *q = s;
            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == '[') {
                q++;
                while (*q == ' ')
                    q++;
                if (isdigit((unsigned char)*q)) {
                    int n = atoi(q) + 1;
                    if (n > u->texcoord_literals)
                        u->texcoord_literals = n;
                } else if (*q != ']') {
                    u->texcoord_dynamic = true;
                }
            }
        }
    }
    u->fragment_color = strstr(text, "gl_FragColor") != nullptr;
    u->fragment_data = !u->fragment_color && strstr(text, "gl_FragData") != nullptr;
}

/* the end of the statement that starts at s, and the previous identifiers on the way back from s. */
static bool previous_word(const char *text, const char **pos, const char **word, size_t *len)
{
    const char *p = *pos;
    while (p > text && isspace((unsigned char)p[-1]))
        p--;
    if (p == text || !ident_char(p[-1]))
        return false;
    const char *end = p;
    while (p > text && ident_char(p[-1]))
        p--;
    *word = p;
    *len = (size_t)(end - p);
    *pos = p;
    return true;
}

static bool word_is(const char *w, size_t len, const char *s)
{
    return strlen(s) == len && !memcmp(w, s, len);
}

static bool qualifier(const char *w, size_t len)
{
    return word_is(w, len, "varying") || word_is(w, len, "in") || word_is(w, len, "out") ||
           word_is(w, len, "attribute");
}

/*
 * a declaration of a built-in variable that a program repeats, as varying vec4 gl_TexCoord[3]; does: it is blanked, and
 * the size it gave is kept. returns the size of the array or 0.
 */
static int blank_declarations(char *text, int *size)
{
    int found = 0;
    for (char *s = text; *s;) {
        const char *t = skip_trivia(s);
        if (t != s) {
            s = (char *)t;
            continue;
        }
        if (!ident_char(*s) || isdigit((unsigned char)*s)) {
            s++;
            continue;
        }
        char *start = s;
        while (ident_char(*s))
            s++;
        if (start != text && ident_char(start[-1]))
            continue;
        int id = lookup(start, (size_t)(s - start));
        if (id != N_TEXCOORD && id != N_FOGFRAG && id != N_FRONTCOLOR && id != N_BACKCOLOR && id != N_FRONTSECONDARY &&
            id != N_BACKSECONDARY && id != N_COLOR && id != N_SECONDARY)
            continue;
        const char *pos = start, *w;
        size_t len;
        if (!previous_word(text, &pos, &w, &len) || !(word_is(w, len, "vec4") || word_is(w, len, "float")))
            continue;
        const char *declaration = pos;
        if (!previous_word(text, &pos, &w, &len))
            continue;
        if (word_is(w, len, "invariant") || word_is(w, len, "flat") || word_is(w, len, "smooth"))
            if (!previous_word(text, &pos, &w, &len))
                continue;
        if (!qualifier(w, len))
            continue;
        char *end = strchr(s, ';');
        if (!end)
            continue;
        if (id == N_TEXCOORD) {
            char *q = s;
            while (*q == ' ')
                q++;
            if (*q == '[' && isdigit((unsigned char)q[1]) && atoi(q + 1) > *size)
                *size = atoi(q + 1);
        }
        for (char *c = (char *)w; c <= end; c++)
            if (*c != '\n')
                *c = ' ';
        (void)declaration;
        found++;
        s = end + 1;
    }
    return found;
}

/* ------------------------------------------------------------------ generation */

typedef struct Plan {
    const Usage *use[2];
    int texcoords;
    unsigned slots;
    Str header[2], decl[2], tail[2], init[2], epilogue[2], globals[2], types;
} Plan;

static void need(Plan *p, unsigned end)
{
    if (end > p->slots)
        p->slots = end;
}

static bool either(const Plan *p, int id)
{
    return p->use[0]->used[id] || p->use[1]->used[id];
}

static bool in_stage(const Plan *p, int stage, int id)
{
    return p->use[stage]->used[id];
}

static void define(Plan *p, int stage, const char *name, const char *expansion)
{
    str_fmt(&p->header[stage], "#define %s %s\n", name, expansion);
}

static void define_both(Plan *p, const char *name, const char *expansion)
{
    define(p, 0, name, expansion);
    define(p, 1, name, expansion);
}

static const char *const g_matrix_names[3][4] = {
    {"gl_ModelViewMatrix", "gl_ModelViewMatrixInverse", "gl_ModelViewMatrixTranspose", "gl_ModelViewMatrixInverseTranspose"},
    {"gl_ProjectionMatrix", "gl_ProjectionMatrixInverse", "gl_ProjectionMatrixTranspose", "gl_ProjectionMatrixInverseTranspose"},
    {"gl_ModelViewProjectionMatrix", "gl_ModelViewProjectionMatrixInverse", "gl_ModelViewProjectionMatrixTranspose",
     "gl_ModelViewProjectionMatrixInverseTranspose"},
};
static const int g_matrix_ids[3][4] = {
    {N_MV, N_MV_INV, N_MV_T, N_MV_IT}, {N_P, N_P_INV, N_P_T, N_P_IT}, {N_MVP, N_MVP_INV, N_MVP_T, N_MVP_IT}};
static const int g_matrix_slots[3][2] = {{MIME_GLC_MV, MIME_GLC_MV_INVERSE}, {MIME_GLC_P, MIME_GLC_P_INVERSE},
                                         {MIME_GLC_MVP, MIME_GLC_MVP_INVERSE}};

static const char *const g_attribute_names[] = {"Vertex", "Normal", "Color", "SecondaryColor", "FogCoord"};

static void generate_matrices(Plan *p)
{
    bool any = false;
    for (int m = 0; m < 3; m++) {
        for (int k = 0; k < 4; k++) {
            if (!either(p, g_matrix_ids[m][k]))
                continue;
            any = true;
            bool inverse = k == 1 || k == 3, transposed = k >= 2;
            int slot = g_matrix_slots[m][inverse ? 1 : 0];
            char expansion[96];
            snprintf(expansion, sizeof(expansion), transposed ? "(transpose(mime_cg_m(%d)))" : "(mime_cg_m(%d))", slot);
            define_both(p, g_matrix_names[m][k], expansion);
            need(p, (unsigned)slot + 4);
        }
    }
    if (either(p, N_FTRANSFORM)) {
        any = true;
        define(p, 0, "ftransform", "mime_cg_ftransform");
        str_fmt(&p->decl[0], "vec4 mime_cg_ftransform() { return mime_cg_m(%d) * mime_cg_Vertex; }\n", MIME_GLC_MVP);
        need(p, MIME_GLC_MVP + 4);
    }
    if (either(p, N_NORMALMATRIX)) {
        define_both(p, "gl_NormalMatrix", "mat3(mime_cg_u[24].xyz, mime_cg_u[25].xyz, mime_cg_u[26].xyz)");
        need(p, MIME_GLC_NORMAL + 3);
    }
    if (either(p, N_NORMALSCALE)) {
        define_both(p, "gl_NormalScale", "mime_cg_u[27].x");
        need(p, MIME_GLC_MISC + 1);
    }
    (void)any;
}

static void init_line(Plan *p, int stage, const char *fmt, ...)
{
    char tmp[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    str_cat(&p->init[stage], "    ");
    str_add(&p->init[stage], tmp, strlen(tmp));
    str_cat(&p->init[stage], "\n");
}

/* a global that both stages may use, filled by the program's start. */
static void global_array(Plan *p, const char *id_name, int id, const char *type, const char *name, int count,
                         const char *fill)
{
    (void)id_name;
    if (!either(p, id))
        return;
    char expansion[80];
    snprintf(expansion, sizeof(expansion), "mime_cg_%s", name);
    char gl_name[80];
    snprintf(gl_name, sizeof(gl_name), "gl_%s", name);
    define_both(p, gl_name, expansion);
    for (int s = 0; s < 2; s++) {
        if (!in_stage(p, s, id))
            continue;
        str_fmt(&p->globals[s], "%s mime_cg_%s[%d];\n", type, name, count);
        init_line(p, s, "for (int i = 0; i < %d; i++) mime_cg_%s[i] = %s;", count, name, fill);
    }
}

static void generate_state(Plan *p)
{
    bool types = false;
    global_array(p, "TextureMatrix", N_TM, "mat4", "TextureMatrix", 8, "mime_cg_m(53 + 4 * i)");
    global_array(p, "TextureMatrixInverse", N_TM_INV, "mat4", "TextureMatrixInverse", 8, "inverse(mime_cg_m(53 + 4 * i))");
    global_array(p, "TextureMatrixTranspose", N_TM_T, "mat4", "TextureMatrixTranspose", 8, "transpose(mime_cg_m(53 + 4 * i))");
    global_array(p, "TextureMatrixInverseTranspose", N_TM_IT, "mat4", "TextureMatrixInverseTranspose", 8,
                 "transpose(inverse(mime_cg_m(53 + 4 * i)))");
    if (either(p, N_TM) || either(p, N_TM_INV) || either(p, N_TM_T) || either(p, N_TM_IT)) {
        need(p, MIME_GLC_TEXMAT + 32);
        for (int s = 0; s < 2; s++)
            if (in_stage(p, s, N_TM) || in_stage(p, s, N_TM_INV) || in_stage(p, s, N_TM_T) || in_stage(p, s, N_TM_IT))
                ;
    }
    global_array(p, "ClipPlane", N_CLIPPLANE, "vec4", "ClipPlane", 8, "mime_cg_u[85 + i]");
    if (either(p, N_CLIPPLANE))
        need(p, MIME_GLC_CLIP + 8);
    global_array(p, "TextureEnvColor", N_TEXENV, "vec4", "TextureEnvColor", 8, "mime_cg_u[45 + i]");
    if (either(p, N_TEXENV))
        need(p, MIME_GLC_TEXENV + 8);

    /* texture generation planes have no state in this implementation: the defaults of the specification. */
    static const struct {
        int id;
        const char *name, *value;
    } planes[] = {
        {N_EYE_S, "EyePlaneS", "vec4(1.0, 0.0, 0.0, 0.0)"}, {N_EYE_T, "EyePlaneT", "vec4(0.0, 1.0, 0.0, 0.0)"},
        {N_EYE_R, "EyePlaneR", "vec4(0.0)"}, {N_EYE_Q, "EyePlaneQ", "vec4(0.0)"},
        {N_OBJ_S, "ObjectPlaneS", "vec4(1.0, 0.0, 0.0, 0.0)"}, {N_OBJ_T, "ObjectPlaneT", "vec4(0.0, 1.0, 0.0, 0.0)"},
        {N_OBJ_R, "ObjectPlaneR", "vec4(0.0)"}, {N_OBJ_Q, "ObjectPlaneQ", "vec4(0.0)"},
    };
    for (size_t i = 0; i < sizeof(planes) / sizeof(planes[0]); i++) {
        if (!either(p, planes[i].id))
            continue;
        char gl_name[64], expansion[64];
        snprintf(gl_name, sizeof(gl_name), "gl_%s", planes[i].name);
        snprintf(expansion, sizeof(expansion), "mime_cg_%s", planes[i].name);
        define_both(p, gl_name, expansion);
        for (int s = 0; s < 2; s++) {
            if (!in_stage(p, s, planes[i].id))
                continue;
            str_fmt(&p->globals[s], "vec4 mime_cg_%s[8];\n", planes[i].name);
            init_line(p, s, "for (int i = 0; i < 8; i++) mime_cg_%s[i] = %s;", planes[i].name, planes[i].value);
        }
    }

    /* structures. */
    static const struct {
        int id;
        const char *name;
    } type_names[] = {{N_T_MATERIAL, "MaterialParameters"}, {N_T_LIGHT, "LightSourceParameters"},
                      {N_T_LIGHTMODEL, "LightModelParameters"}, {N_T_LMP, "LightModelProducts"},
                      {N_T_LP, "LightProducts"}, {N_T_POINT, "PointParameters"}, {N_T_FOG, "FogParameters"},
                      {N_T_DEPTH, "DepthRangeParameters"}};
    for (size_t i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++) {
        char gl_name[64], expansion[64];
        snprintf(gl_name, sizeof(gl_name), "gl_%s", type_names[i].name);
        snprintf(expansion, sizeof(expansion), "mime_cg_%s", type_names[i].name);
        define_both(p, gl_name, expansion);
    }
    bool material = either(p, N_FMAT) || either(p, N_BMAT) || either(p, N_FLMP) || either(p, N_BLMP) ||
                    either(p, N_FLP) || either(p, N_BLP) || either(p, N_T_MATERIAL);
    bool lights = either(p, N_LIGHT) || either(p, N_FLP) || either(p, N_BLP) || either(p, N_T_LIGHT);
    bool model = either(p, N_LIGHTMODEL) || either(p, N_FLMP) || either(p, N_BLMP) || either(p, N_T_LIGHTMODEL);
    if (material)
        str_cat(&p->types, "struct mime_cg_MaterialParameters { vec4 emission; vec4 ambient; vec4 diffuse; vec4 specular; float shininess; };\n");
    if (lights)
        str_cat(&p->types, "struct mime_cg_LightSourceParameters { vec4 ambient; vec4 diffuse; vec4 specular; vec4 position; vec4 halfVector; "
                "vec3 spotDirection; float spotExponent; float spotCutoff; float spotCosCutoff; float constantAttenuation; "
                "float linearAttenuation; float quadraticAttenuation; };\n");
    if (model)
        str_fmt(&p->types, "struct mime_cg_LightModelParameters { vec4 ambient; };\n");
    if (either(p, N_FLMP) || either(p, N_BLMP) || either(p, N_T_LMP))
        str_fmt(&p->types, "struct mime_cg_LightModelProducts { vec4 sceneColor; };\n");
    if (either(p, N_FLP) || either(p, N_BLP) || either(p, N_T_LP))
        str_fmt(&p->types, "struct mime_cg_LightProducts { vec4 ambient; vec4 diffuse; vec4 specular; };\n");
    if (either(p, N_POINT) || either(p, N_T_POINT))
        str_fmt(&p->types, "struct mime_cg_PointParameters { float size; float sizeMin; float sizeMax; float fadeThresholdSize; "
                           "float distanceConstantAttenuation; float distanceLinearAttenuation; "
                           "float distanceQuadraticAttenuation; };\n");
    if (either(p, N_FOG) || either(p, N_T_FOG))
        str_fmt(&p->types, "struct mime_cg_FogParameters { vec4 color; float density; float start; float end; float scale; };\n");
    if (either(p, N_DEPTH) || either(p, N_T_DEPTH))
        str_fmt(&p->types, "struct mime_cg_DepthRangeParameters { float near; float far; float diff; };\n");
    (void)types;

    for (int s = 0; s < 2; s++) {
        int fm = MIME_GLC_FRONT_MATERIAL, bm = MIME_GLC_BACK_MATERIAL;
        bool front = in_stage(p, s, N_FMAT) || in_stage(p, s, N_FLMP) || in_stage(p, s, N_FLP);
        bool back = in_stage(p, s, N_BMAT) || in_stage(p, s, N_BLMP) || in_stage(p, s, N_BLP);
        bool lm = in_stage(p, s, N_LIGHTMODEL) || in_stage(p, s, N_FLMP) || in_stage(p, s, N_BLMP);
        for (int side = 0; side < 2; side++) {
            if (!(side ? back : front))
                continue;
            int b = side ? bm : fm;
            const char *n = side ? "BackMaterial" : "FrontMaterial";
            str_fmt(&p->globals[s], "mime_cg_MaterialParameters mime_cg_%s;\n", n);
            init_line(p, s,
                      "mime_cg_%s = mime_cg_MaterialParameters(mime_cg_u[%d], mime_cg_u[%d], mime_cg_u[%d], mime_cg_u[%d], "
                      "mime_cg_u[%d].x);",
                      n, b, b + 1, b + 2, b + 3, b + 4);
            need(p, (unsigned)b + 5);
        }
        if (lm) {
            str_cat(&p->globals[s], "mime_cg_LightModelParameters mime_cg_LightModel;\n");
            init_line(p, s, "mime_cg_LightModel.ambient = mime_cg_u[%d];", MIME_GLC_LIGHT_MODEL);
            need(p, MIME_GLC_LIGHT_MODEL + 1);
        }
        for (int side = 0; side < 2; side++) {
            if (!in_stage(p, s, side ? N_BLMP : N_FLMP))
                continue;
            int b = side ? bm : fm;
            const char *n = side ? "BackLightModelProduct" : "FrontLightModelProduct";
            str_fmt(&p->globals[s], "mime_cg_LightModelProducts mime_cg_%s;\n", n);
            init_line(p, s, "mime_cg_%s.sceneColor = mime_cg_u[%d] + mime_cg_u[%d] * mime_cg_u[%d];", n, b, b + 1,
                      MIME_GLC_LIGHT_MODEL);
        }
        if (in_stage(p, s, N_LIGHT) || in_stage(p, s, N_FLP) || in_stage(p, s, N_BLP) ) {
            if (in_stage(p, s, N_LIGHT)) {
                str_cat(&p->globals[s], "mime_cg_LightSourceParameters mime_cg_LightSource[8];\n");
                init_line(p, s, "for (int i = 0; i < 8; i++) {");
                init_line(p, s, "    int b = %d + %d * i;", MIME_GLC_LIGHT, MIME_GLC_LIGHT_SIZE);
                init_line(p, s,
                          "    mime_cg_LightSource[i] = mime_cg_LightSourceParameters(mime_cg_u[b], mime_cg_u[b + 1], "
                          "mime_cg_u[b + 2], mime_cg_u[b + 3], mime_cg_u[b + 4], mime_cg_u[b + 5].xyz, mime_cg_u[b + 5].w, "
                          "mime_cg_u[b + 6].x, mime_cg_u[b + 6].y, mime_cg_u[b + 6].z, mime_cg_u[b + 6].w, mime_cg_u[b + 7].x);");
                init_line(p, s, "}");
            }
            for (int side = 0; side < 2; side++) {
                if (!in_stage(p, s, side ? N_BLP : N_FLP))
                    continue;
                int b = side ? bm : fm;
                const char *n = side ? "BackLightProduct" : "FrontLightProduct";
                str_fmt(&p->globals[s], "mime_cg_LightProducts mime_cg_%s[8];\n", n);
                init_line(p, s, "for (int i = 0; i < 8; i++) {");
                init_line(p, s, "    int b = %d + %d * i;", MIME_GLC_LIGHT, MIME_GLC_LIGHT_SIZE);
                init_line(p, s,
                          "    mime_cg_%s[i] = mime_cg_LightProducts(mime_cg_u[%d] * mime_cg_u[b], mime_cg_u[%d] * "
                          "mime_cg_u[b + 1], mime_cg_u[%d] * mime_cg_u[b + 2]);",
                          n, b + 1, b + 2, b + 3);
                init_line(p, s, "}");
            }
            need(p, MIME_GLC_LIGHT + 8 * MIME_GLC_LIGHT_SIZE);
        }
        if (in_stage(p, s, N_POINT)) {
            str_cat(&p->globals[s], "mime_cg_PointParameters mime_cg_Point;\n");
            init_line(p, s,
                      "mime_cg_Point = mime_cg_PointParameters(mime_cg_u[%d].x, mime_cg_u[%d].y, mime_cg_u[%d].z, "
                      "mime_cg_u[%d].w, mime_cg_u[%d].x, mime_cg_u[%d].y, mime_cg_u[%d].z);",
                      MIME_GLC_POINT0, MIME_GLC_POINT0, MIME_GLC_POINT0, MIME_GLC_POINT0, MIME_GLC_POINT1, MIME_GLC_POINT1,
                      MIME_GLC_POINT1);
            need(p, MIME_GLC_POINT1 + 1);
        }
        if (in_stage(p, s, N_FOG)) {
            str_cat(&p->globals[s], "mime_cg_FogParameters mime_cg_Fog;\n");
            init_line(p, s,
                      "mime_cg_Fog = mime_cg_FogParameters(mime_cg_u[%d], mime_cg_u[%d].x, mime_cg_u[%d].y, mime_cg_u[%d].z, "
                      "mime_cg_u[%d].w);",
                      MIME_GLC_FOG_COLOR, MIME_GLC_FOG, MIME_GLC_FOG, MIME_GLC_FOG, MIME_GLC_FOG);
            need(p, MIME_GLC_FOG + 1);
        }
        if (in_stage(p, s, N_DEPTH)) {
            str_cat(&p->globals[s], "mime_cg_DepthRangeParameters mime_cg_DepthRange;\n");
            init_line(p, s, "mime_cg_DepthRange = mime_cg_DepthRangeParameters(mime_cg_u[%d].y, mime_cg_u[%d].z, mime_cg_u[%d].w);",
                      MIME_GLC_MISC, MIME_GLC_MISC, MIME_GLC_MISC);
            need(p, MIME_GLC_MISC + 1);
        }
    }
    static const struct {
        int id;
        const char *name, *value;
    } objects[] = {{N_FMAT, "FrontMaterial", "mime_cg_FrontMaterial"}, {N_BMAT, "BackMaterial", "mime_cg_BackMaterial"},
                   {N_LIGHT, "LightSource", "mime_cg_LightSource"}, {N_LIGHTMODEL, "LightModel", "mime_cg_LightModel"},
                   {N_FLMP, "FrontLightModelProduct", "mime_cg_FrontLightModelProduct"},
                   {N_BLMP, "BackLightModelProduct", "mime_cg_BackLightModelProduct"},
                   {N_FLP, "FrontLightProduct", "mime_cg_FrontLightProduct"},
                   {N_BLP, "BackLightProduct", "mime_cg_BackLightProduct"}, {N_POINT, "Point", "mime_cg_Point"},
                   {N_FOG, "Fog", "mime_cg_Fog"}, {N_DEPTH, "DepthRange", "mime_cg_DepthRange"}};
    for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++) {
        if (!either(p, objects[i].id))
            continue;
        char gl_name[64];
        snprintf(gl_name, sizeof(gl_name), "gl_%s", objects[i].name);
        define_both(p, gl_name, objects[i].value);
    }
    define_both(p, "gl_MaxLights", "8");
    define_both(p, "gl_MaxClipPlanes", "8");
    define_both(p, "gl_MaxTextureUnits", "8");
    define_both(p, "gl_MaxTextureCoords", "8");
    if (either(p, N_TEXENV) && !either(p, N_TM))
        ;
}

static void generate_attributes(Plan *p)
{
    static const int attribute_ids[] = {N_VERTEX, N_NORMAL, N_COLOR, N_SECONDARY, N_FOGCOORD};
    static const int attribute_locations[] = {MIME_GLC_ATTRIB_VERTEX, MIME_GLC_ATTRIB_NORMAL, MIME_GLC_ATTRIB_COLOR,
                                              MIME_GLC_ATTRIB_SECONDARY, MIME_GLC_ATTRIB_FOG};
    static const char *const types[] = {"vec4", "vec3", "vec4", "vec4", "float"};
    bool ftransform = in_stage(p, 0, N_FTRANSFORM);
    for (int i = 0; i < 5; i++) {
        int id = attribute_ids[i];
        if (!in_stage(p, 0, id) && !(id == N_VERTEX && ftransform))
            continue;
        char gl_name[40], expansion[48];
        snprintf(gl_name, sizeof(gl_name), "gl_%s", g_attribute_names[i]);
        snprintf(expansion, sizeof(expansion), "mime_cg_%s", g_attribute_names[i]);
        define(p, 0, gl_name, expansion);
        str_fmt(&p->decl[0], "layout(location = %d) in %s mime_cg_%s;\n", attribute_locations[i], types[i],
                g_attribute_names[i]);
    }
    for (int t = 0; t < 8; t++) {
        if (!in_stage(p, 0, N_MULTITEX0 + t))
            continue;
        char gl_name[40], expansion[48];
        snprintf(gl_name, sizeof(gl_name), "gl_MultiTexCoord%d", t);
        snprintf(expansion, sizeof(expansion), "mime_cg_MultiTexCoord%d", t);
        define(p, 0, gl_name, expansion);
        str_fmt(&p->decl[0], "layout(location = %d) in vec4 mime_cg_MultiTexCoord%d;\n", MIME_GLC_ATTRIB_TEXCOORD + t, t);
    }
}

static void generate_varyings(Plan *p)
{
    /* the color varyings. the vertex stage writes them, the fragment stage reads the front one or the back one. */
    bool color = in_stage(p, 1, N_COLOR) || in_stage(p, 0, N_FRONTCOLOR) || in_stage(p, 0, N_BACKCOLOR);
    bool secondary = in_stage(p, 1, N_SECONDARY) || in_stage(p, 0, N_FRONTSECONDARY) || in_stage(p, 0, N_BACKSECONDARY);
    bool back_color = in_stage(p, 0, N_BACKCOLOR), back_secondary = in_stage(p, 0, N_BACKSECONDARY);
    static const struct {
        int id;
        const char *name;
    } outs[] = {{N_FRONTCOLOR, "FrontColor"}, {N_BACKCOLOR, "BackColor"}, {N_FRONTSECONDARY, "FrontSecondaryColor"},
                {N_BACKSECONDARY, "BackSecondaryColor"}};
    for (int i = 0; i < 4; i++) {
        if (!in_stage(p, 0, outs[i].id))
            continue;
        char gl_name[48], expansion[48];
        snprintf(gl_name, sizeof(gl_name), "gl_%s", outs[i].name);
        snprintf(expansion, sizeof(expansion), "mime_cg_%s", outs[i].name);
        define(p, 0, gl_name, expansion);
    }
    if (color || in_stage(p, 0, N_FRONTCOLOR)) {
        for (int s = 0; s < 2; s++)
            str_fmt(&p->decl[s], "%s vec4 mime_cg_FrontColor;\n", s ? "in" : "out");
    }
    if (back_color && (color || in_stage(p, 0, N_BACKCOLOR))) {
        for (int s = 0; s < 2; s++)
            str_fmt(&p->decl[s], "%s vec4 mime_cg_BackColor;\n", s ? "in" : "out");
    }
    if (secondary || in_stage(p, 0, N_FRONTSECONDARY)) {
        for (int s = 0; s < 2; s++)
            str_fmt(&p->decl[s], "%s vec4 mime_cg_FrontSecondaryColor;\n", s ? "in" : "out");
    }
    if (back_secondary) {
        for (int s = 0; s < 2; s++)
            str_fmt(&p->decl[s], "%s vec4 mime_cg_BackSecondaryColor;\n", s ? "in" : "out");
    }
    if (in_stage(p, 1, N_COLOR)) {
        define(p, 1, "gl_Color", "mime_cg_fragment_color()");
        str_fmt(&p->decl[1], "vec4 mime_cg_fragment_color() { return %s; }\n",
                back_color ? "(mime_cg_u[30].w != 0.0 && !gl_FrontFacing) ? mime_cg_BackColor : mime_cg_FrontColor"
                           : "mime_cg_FrontColor");
        need(p, MIME_GLC_STATE0 + 1);
    }
    if (in_stage(p, 1, N_SECONDARY)) {
        define(p, 1, "gl_SecondaryColor", "mime_cg_fragment_secondary()");
        str_fmt(&p->decl[1], "vec4 mime_cg_fragment_secondary() { return %s; }\n",
                back_secondary ? "(mime_cg_u[30].w != 0.0 && !gl_FrontFacing) ? mime_cg_BackSecondaryColor : "
                                 "mime_cg_FrontSecondaryColor"
                               : "mime_cg_FrontSecondaryColor");
        need(p, MIME_GLC_STATE0 + 1);
    }
    if (either(p, N_TEXCOORD)) {
        define_both(p, "gl_TexCoord", "mime_cg_TexCoord");
        for (int s = 0; s < 2; s++)
            str_fmt(&p->decl[s], "%s vec4 mime_cg_TexCoord[%d];\n", s ? "in" : "out", p->texcoords);
    }
    if (either(p, N_FOGFRAG)) {
        define_both(p, "gl_FogFragCoord", "mime_cg_FogFragCoord");
        for (int s = 0; s < 2; s++)
            str_fmt(&p->decl[s], "%s float mime_cg_FogFragCoord;\n", s ? "in" : "out");
    }
    if (in_stage(p, 0, N_CLIPVERTEX)) {
        define(p, 0, "gl_ClipVertex", "mime_cg_ClipVertex");
        str_cat(&p->globals[0], "vec4 mime_cg_ClipVertex;\n");
        str_cat(&p->decl[0], "out float gl_ClipDistance[6];\n");
        str_fmt(&p->epilogue[0],
                "    for (int i = 0; i < 6; i++)\n"
                "        gl_ClipDistance[i] = ((int(mime_cg_u[%d].y + 0.5) >> i) & 1) != 0 ? dot(mime_cg_u[%d + i], mime_cg_ClipVertex) : 1.0;\n",
                MIME_GLC_STATE1, MIME_GLC_CLIP);
        need(p, MIME_GLC_STATE1 + 1);
        need(p, MIME_GLC_CLIP + 8);
    }
    /* clamping of the written colors. */
    if (in_stage(p, 0, N_FRONTCOLOR) || in_stage(p, 0, N_BACKCOLOR) || in_stage(p, 0, N_FRONTSECONDARY) ||
        in_stage(p, 0, N_BACKSECONDARY)) {
        str_fmt(&p->epilogue[0], "    if (mime_cg_u[%d].z != 0.0) {\n", MIME_GLC_STATE0);
        if (in_stage(p, 0, N_FRONTCOLOR))
            str_cat(&p->epilogue[0], "        mime_cg_FrontColor = clamp(mime_cg_FrontColor, 0.0, 1.0);\n");
        if (in_stage(p, 0, N_BACKCOLOR))
            str_cat(&p->epilogue[0], "        mime_cg_BackColor = clamp(mime_cg_BackColor, 0.0, 1.0);\n");
        if (in_stage(p, 0, N_FRONTSECONDARY))
            str_cat(&p->epilogue[0], "        mime_cg_FrontSecondaryColor = clamp(mime_cg_FrontSecondaryColor, 0.0, 1.0);\n");
        if (in_stage(p, 0, N_BACKSECONDARY))
            str_cat(&p->epilogue[0], "        mime_cg_BackSecondaryColor = clamp(mime_cg_BackSecondaryColor, 0.0, 1.0);\n");
        str_cat(&p->epilogue[0], "    }\n");
        need(p, MIME_GLC_STATE0 + 1);
    }
}

static void generate_fragment_epilogue(Plan *p, int fragment_output, int outputs)
{
    if (!fragment_output)
        return;
    const char *first = fragment_output == 1 ? "mime_FragColor" : "mime_FragData[0]";
    str_fmt(&p->epilogue[1], "    {\n        float a = %s.a;\n        int f = int(mime_cg_u[%d].x + 0.5);\n", first, MIME_GLC_STATE0);
    str_fmt(&p->epilogue[1], "        float r = mime_cg_u[%d].y;\n", MIME_GLC_STATE0);
    str_cat(&p->epilogue[1], "        bool pass = f == 7 ? true : f == 0 ? false : f == 1 ? a < r : f == 2 ? a == r : f == 3 ? a <= r : "
            "f == 4 ? a > r : f == 5 ? a != r : a >= r;\n        if (!pass) discard;\n    }\n");
    str_fmt(&p->epilogue[1], "    if (mime_cg_u[%d].x != 0.0) {\n", MIME_GLC_STATE1);
    if (fragment_output == 1) {
        str_cat(&p->epilogue[1], "        mime_FragColor = clamp(mime_FragColor, 0.0, 1.0);\n");
    } else {
        str_fmt(&p->epilogue[1], "        for (int i = 0; i < %d; i++) mime_FragData[i] = clamp(mime_FragData[i], 0.0, 1.0);\n", outputs);
    }
    str_cat(&p->epilogue[1], "    }\n");
    need(p, MIME_GLC_STATE1 + 1);
}

static bool any_builtin(const Usage *u)
{
    for (int i = 0; i < N_COUNT; i++)
        if (u->used[i])
            return true;
    return false;
}

bool mime_glsl_compat_analyze(const char *vs, size_t vs_len, const char *fs, size_t fs_len, MimeGlslCompat *c,
                              int fragment_output, int fragment_outputs)
{
    memset(c, 0, sizeof(*c));
    Usage *use = calloc(2, sizeof(Usage));
    char *text[2] = {strndup(vs, vs_len), strndup(fs, fs_len)};
    for (int s = 0; s < 2; s++) {
        scan(text[s], &use[s]);
        int size = 0;
        blank_declarations(text[s], &size);
        if (size > use[s].texcoord_size)
            use[s].texcoord_size = size;
        /* a redeclaration blanked above is not a use: scan again without it. */
        Usage again = {};
        scan(text[s], &again);
        again.texcoord_size = use[s].texcoord_size;
        use[s] = again;
        free(text[s]);
    }
    if (!any_builtin(&use[0]) && !any_builtin(&use[1])) {
        free(use);
        return true;
    }
    /* a stage that reads or writes the array of texture coordinates fixes its size for both. */
    Plan *p = calloc(1, sizeof(Plan));
    p->use[0] = &use[0];
    p->use[1] = &use[1];
    int literals = use[0].texcoord_literals > use[1].texcoord_literals ? use[0].texcoord_literals : use[1].texcoord_literals;
    int declared = use[0].texcoord_size > use[1].texcoord_size ? use[0].texcoord_size : use[1].texcoord_size;
    p->texcoords = use[0].texcoord_dynamic || use[1].texcoord_dynamic ? 8 : literals > declared ? literals : declared;
    if (p->texcoords < 1)
        p->texcoords = 1;
    if (p->texcoords > 8)
        p->texcoords = 8;

    generate_attributes(p);
    generate_varyings(p);
    generate_matrices(p);
    generate_state(p);
    generate_fragment_epilogue(p, fragment_output, fragment_outputs);

    c->active = true;
    c->slots = p->slots;
    for (int s = 0; s < 2; s++) {
        Str header = {}, decl = {}, tail = {};
        str_cat(&header, "#define main mime_cg_main\n");
        str_add(&header, p->header[s].p ? p->header[s].p : "", p->header[s].n);
        if (p->slots)
            str_fmt(&decl, "uniform vec4 mime_cg_u[%u];\n", p->slots);
        if (p->slots)
            str_cat(&decl, "mat4 mime_cg_m(int s) { return mat4(mime_cg_u[s], mime_cg_u[s + 1], mime_cg_u[s + 2], mime_cg_u[s + 3]); }\n");
        str_add(&decl, p->types.p ? p->types.p : "", p->types.n);
        str_add(&decl, p->decl[s].p ? p->decl[s].p : "", p->decl[s].n);
        str_add(&decl, p->globals[s].p ? p->globals[s].p : "", p->globals[s].n);
        str_cat(&decl, "void mime_cg_init() {\n");
        str_add(&decl, p->init[s].p ? p->init[s].p : "", p->init[s].n);
        str_cat(&decl, "}\n");
        str_cat(&decl, "void mime_cg_epilogue() {\n");
        str_add(&decl, p->epilogue[s].p ? p->epilogue[s].p : "", p->epilogue[s].n);
        str_cat(&decl, "}\n");
        str_cat(&tail, "\n#undef main\nvoid main() { mime_cg_init(); mime_cg_main(); mime_cg_epilogue(); }\n");
        c->stage[s].header = str_take(&header);
        c->stage[s].decl = str_take(&decl);
        c->stage[s].tail = str_take(&tail);
    }
    for (int s = 0; s < 2; s++) {
        free(p->header[s].p);
        free(p->decl[s].p);
        free(p->init[s].p);
        free(p->epilogue[s].p);
        free(p->globals[s].p);
        free(p->tail[s].p);
    }
    free(p->types.p);
    free(p);
    free(use);
    return true;
}

void mime_glsl_compat_blank(const MimeGlslCompat *c, bool fragment, char *text)
{
    (void)fragment;
    if (!c->active)
        return;
    int size = 0;
    blank_declarations(text, &size);
}

void mime_glsl_compat_free(MimeGlslCompat *c)
{
    for (int s = 0; s < 2; s++) {
        free(c->stage[s].header);
        free(c->stage[s].decl);
        free(c->stage[s].tail);
    }
    memset(c, 0, sizeof(*c));
}
