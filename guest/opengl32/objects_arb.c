/*
 * GL_ARB_shader_objects: the handle based calls that do not map to a single call of OpenGL 2.0. a handle is the name of a
 * shader or of a program, which share one name space here.
 */
#include "opengl32.h"

void APIENTRY glDeleteShader(GLuint shader);
void APIENTRY glDeleteProgram(GLuint program);
GLboolean APIENTRY glIsShader(GLuint shader);
GLboolean APIENTRY glIsProgram(GLuint program);
void APIENTRY glDetachShader(GLuint program, GLuint shader);
void APIENTRY glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
void APIENTRY glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *log);
void APIENTRY glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length, GLchar *log);

#define OBJECT_TYPE 0x8B4E
#define OBJECT_SUBTYPE 0x8B4F
#define PROGRAM_OBJECT 0x8B40
#define SHADER_OBJECT 0x8B48

GLAPI void APIENTRY glDeleteObjectARB(GLuint object)
{
    GL_CONTEXT_OR_RETURN();
    if (!object)
        return;
    if (glIsProgram(object))
        glDeleteProgram(object);
    else if (glIsShader(object))
        glDeleteShader(object);
    else
        gl_error(ctx, GL_INVALID_VALUE);
}

GLAPI GLuint APIENTRY glGetHandleARB(GLenum pname)
{
    GL_CONTEXT_OR_RETURN(0);
    if (pname != PROGRAM_OBJECT) {
        gl_error(ctx, GL_INVALID_ENUM);
        return 0;
    }
    return ctx->program;
}

GLAPI void APIENTRY glDetachObjectARB(GLuint container, GLuint attached)
{
    glDetachShader(container, attached);
}

static void object_parameter(GLuint object, GLenum pname, GLint *value)
{
    GL_CONTEXT_OR_RETURN();
    BOOL program = glIsProgram(object);
    if (!program && !glIsShader(object)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    if (pname == OBJECT_TYPE) {
        *value = program ? PROGRAM_OBJECT : SHADER_OBJECT;
    } else if (pname == OBJECT_SUBTYPE) {
        if (program) {
            gl_error(ctx, GL_INVALID_ENUM);
            return;
        }
        glGetShaderiv(object, GL_SHADER_TYPE, value);
    } else if (program) {
        glGetProgramiv(object, pname, value);
    } else {
        glGetShaderiv(object, pname, value);
    }
}

GLAPI void APIENTRY glGetObjectParameterivARB(GLuint object, GLenum pname, GLint *params)
{
    object_parameter(object, pname, params);
}

GLAPI void APIENTRY glGetObjectParameterfvARB(GLuint object, GLenum pname, GLfloat *params)
{
    GLint value = 0;
    object_parameter(object, pname, &value);
    *params = (GLfloat)value;
}

GLAPI void APIENTRY glGetInfoLogARB(GLuint object, GLsizei size, GLsizei *length, GLchar *log)
{
    GL_CONTEXT_OR_RETURN();
    if (glIsProgram(object))
        glGetProgramInfoLog(object, size, length, log);
    else if (glIsShader(object))
        glGetShaderInfoLog(object, size, length, log);
    else
        gl_error(ctx, GL_INVALID_VALUE);
}
