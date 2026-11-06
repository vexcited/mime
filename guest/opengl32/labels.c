/* object labels of KHR_debug: kept by identifier and name, and given back by the queries. */
#include "opengl32.h"

void APIENTRY glGetIntegeri_v(GLenum target, GLuint index, GLint *data);

typedef struct GlLabel {
    struct GlLabel *next;
    GLenum identifier;
    GLuint name;
    const void *pointer;
    char *text;
} GlLabel;

static GlLabel *g_labels;
static GL_LOCK g_labels_lock;

static BOOL identifier_valid(GLenum id)
{
    switch (id) {
    case GL_BUFFER:
    case GL_SHADER:
    case GL_PROGRAM:
    case GL_VERTEX_ARRAY:
    case GL_QUERY:
    case GL_PROGRAM_PIPELINE:
    case GL_TRANSFORM_FEEDBACK:
    case GL_SAMPLER:
    case GL_TEXTURE:
    case GL_RENDERBUFFER:
    case GL_FRAMEBUFFER:
        return TRUE;
    default:
        return FALSE;
    }
}

static GlLabel **find(GLenum identifier, GLuint name, const void *pointer)
{
    GlLabel **at = &g_labels;
    while (*at && !((*at)->identifier == identifier && (*at)->name == name && (*at)->pointer == pointer))
        at = &(*at)->next;
    return at;
}

static void set_label(GLenum identifier, GLuint name, const void *pointer, GLsizei length, const GLchar *label)
{
    GlContext *ctx = gl_current();
    if (length < 0 && label)
        length = (GLsizei)strlen(label);
    if (length >= 256) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_lock(&g_labels_lock);
    GlLabel **at = find(identifier, name, pointer);
    if (*at) {
        gl_free((*at)->text);
        (*at)->text = nullptr;
    } else if (label) {
        GlLabel *l = gl_alloc(sizeof(GlLabel));
        if (l) {
            l->identifier = identifier;
            l->name = name;
            l->pointer = pointer;
            *at = l;
        }
    }
    if (*at && label) {
        (*at)->text = gl_alloc((SIZE_T)length + 1);
        if ((*at)->text)
            memcpy((*at)->text, label, (SIZE_T)length);
    }
    gl_unlock(&g_labels_lock);
}

static void get_label(GLenum identifier, GLuint name, const void *pointer, GLsizei size, GLsizei *length, GLchar *label)
{
    GlContext *ctx = gl_current();
    if (size < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    gl_lock(&g_labels_lock);
    GlLabel *l = *find(identifier, name, pointer);
    SIZE_T n = l && l->text ? strlen(l->text) : 0;
    if (label && size > 0) {
        SIZE_T copy = n < (SIZE_T)size - 1 ? n : (SIZE_T)size - 1;
        if (copy)
            memcpy(label, l->text, copy);
        label[copy] = 0;
        n = copy;
    }
    if (length)
        *length = (GLsizei)n;
    gl_unlock(&g_labels_lock);
}

GLAPI void APIENTRY glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar *label)
{
    GL_CONTEXT_OR_RETURN();
    if (!identifier_valid(identifier)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    set_label(identifier, name, nullptr, length, label);
}

GLAPI void APIENTRY glGetObjectLabel(GLenum identifier, GLuint name, GLsizei size, GLsizei *length, GLchar *label)
{
    GL_CONTEXT_OR_RETURN();
    if (!identifier_valid(identifier)) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    get_label(identifier, name, nullptr, size, length, label);
}

GLAPI void APIENTRY glObjectPtrLabel(const void *ptr, GLsizei length, const GLchar *label)
{
    GL_CONTEXT_OR_RETURN();
    if (!ptr) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    set_label(GL_SYNC_FENCE, 0, ptr, length, label);
}

GLAPI void APIENTRY glGetObjectPtrLabel(const void *ptr, GLsizei size, GLsizei *length, GLchar *label)
{
    GL_CONTEXT_OR_RETURN();
    get_label(GL_SYNC_FENCE, 0, ptr, size, length, label);
}

GLAPI void APIENTRY glGetInteger64i_v(GLenum target, GLuint index, GLint64 *data)
{
    GLint v[4] = {};
    glGetIntegeri_v(target, index, v);
    *data = v[0];
}
