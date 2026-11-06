#ifndef MIME_HOST_GPU_GLSL_COMPAT_H
#define MIME_HOST_GPU_GLSL_COMPAT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * the fixed function built-ins of GLSL (gl_Vertex, gl_ModelViewMatrix, gl_FrontColor, gl_LightSource...) as the
 * compatibility profile has them, rewritten to ordinary inputs, outputs and one uniform array, see mime/glcompat.h. the
 * analysis looks at both stages of a program, so that the interface between them and the array come out the same.
 */
typedef struct MimeGlslCompat {
    bool active;
    unsigned slots; /* vec4 of mime_cg_u, 0 when the program reads none. */
    struct {
        char *header; /* macros, after the version line. */
        char *decl;   /* declarations, after the last extension line. */
        char *tail;   /* the main function that calls the program's. */
    } stage[2];
    void *private_data;
} MimeGlslCompat;

/* fragment_output: 0 for none, 1 gl_FragColor, 2 gl_FragData with fragment_outputs elements. */
bool mime_glsl_compat_analyze(const char *vs, size_t vs_len, const char *fs, size_t fs_len, MimeGlslCompat *c,
                              int fragment_output, int fragment_outputs);
/* blanks the redeclarations of built-in variables in a stage's text. */
void mime_glsl_compat_blank(const MimeGlslCompat *c, bool fragment, char *text);
void mime_glsl_compat_free(MimeGlslCompat *c);

#endif
