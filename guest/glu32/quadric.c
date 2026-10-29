/*
 * the quadrics of glu32: spheres, cylinders, disks and partial disks, drawn through opengl32 as one grid of rows and
 * columns that each shape describes by a point function.
 */
#include "glu32_private.h"

#define GLU_SMOOTH 100000
#define GLU_FLAT 100001
#define GLU_NONE 100002
#define GLU_OUTSIDE 100020
#define GLU_INSIDE 100021
#define GLU_POINT 100010
#define GLU_LINE 100011
#define GLU_FILL 100012
#define GLU_SILHOUETTE 100013

#define TWO_PI 6.28318530717958647692

typedef void(GLAPIENTRY *QUADRIC_ERROR)(GLenum error);

typedef struct QUADRIC {
    GLenum DrawStyle, Normals, Orientation;
    GLboolean Texture;
    QUADRIC_ERROR ErrorCallback;
} QUADRIC;

typedef struct POINT_AT {
    double Position[3], Normal[3], S, T;
} POINT_AT;

typedef struct SHAPE {
    int Rows, Columns;
    BOOL Wrap;
    /* the rings the silhouette draws, either every one inside or the first and the last. */
    BOOL InnerRings;
    /* the point at a column and a row, both in grid units and not always whole. */
    void (*Evaluate)(const struct SHAPE *shape, double column, double row, POINT_AT *point);
    double A, B, C, D, Start, Sweep;
} SHAPE;

static void report(QUADRIC *quadric, GLenum error)
{
    if (quadric && quadric->ErrorCallback)
        quadric->ErrorCallback(error);
}

GLUAPI QUADRIC *GLAPIENTRY gluNewQuadric(void)
{
    QUADRIC *quadric = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*quadric));
    if (quadric) {
        quadric->DrawStyle = GLU_FILL;
        quadric->Normals = GLU_SMOOTH;
        quadric->Orientation = GLU_OUTSIDE;
    }
    return quadric;
}

GLUAPI void GLAPIENTRY gluDeleteQuadric(QUADRIC *quadric)
{
    if (quadric)
        HeapFree(GetProcessHeap(), 0, quadric);
}

GLUAPI void GLAPIENTRY gluQuadricCallback(QUADRIC *quadric, GLenum which, void(GLAPIENTRY *function)(void))
{
    if (!quadric)
        return;
    if (which == GLU_ERROR)
        quadric->ErrorCallback = (QUADRIC_ERROR)function;
    else
        report(quadric, GLU_INVALID_ENUM);
}

GLUAPI void GLAPIENTRY gluQuadricDrawStyle(QUADRIC *quadric, GLenum style)
{
    if (!quadric)
        return;
    if (style < GLU_POINT || style > GLU_SILHOUETTE)
        report(quadric, GLU_INVALID_ENUM);
    else
        quadric->DrawStyle = style;
}

GLUAPI void GLAPIENTRY gluQuadricNormals(QUADRIC *quadric, GLenum normals)
{
    if (!quadric)
        return;
    if (normals < GLU_SMOOTH || normals > GLU_NONE)
        report(quadric, GLU_INVALID_ENUM);
    else
        quadric->Normals = normals;
}

GLUAPI void GLAPIENTRY gluQuadricOrientation(QUADRIC *quadric, GLenum orientation)
{
    if (!quadric)
        return;
    if (orientation != GLU_OUTSIDE && orientation != GLU_INSIDE)
        report(quadric, GLU_INVALID_ENUM);
    else
        quadric->Orientation = orientation;
}

GLUAPI void GLAPIENTRY gluQuadricTexture(QUADRIC *quadric, GLboolean texture)
{
    if (quadric)
        quadric->Texture = texture;
}

/* ------------------------------------------------------------------ drawing */

static void emit(const QUADRIC *quadric, const GL_API *gl, const POINT_AT *point)
{
    if (quadric->Normals != GLU_NONE) {
        double sign = quadric->Orientation == GLU_INSIDE ? -1 : 1;
        gl->Normal3d(sign * point->Normal[0], sign * point->Normal[1], sign * point->Normal[2]);
    }
    if (quadric->Texture)
        gl->TexCoord2d(point->S, point->T);
    gl->Vertex3d(point->Position[0], point->Position[1], point->Position[2]);
}

static BOOL degenerate_row(const SHAPE *shape, int row)
{
    POINT_AT a, b;
    shape->Evaluate(shape, 0, row, &a);
    shape->Evaluate(shape, shape->Columns / 2.0, row, &b);
    return a.Position[0] == b.Position[0] && a.Position[1] == b.Position[1] && a.Position[2] == b.Position[2];
}

static void draw_rings(const QUADRIC *quadric, const GL_API *gl, const SHAPE *shape, int first, int last)
{
    for (int row = first; row <= last; row++) {
        if (degenerate_row(shape, row))
            continue;
        gl->Begin(shape->Wrap ? GL_LINE_LOOP : GL_LINE_STRIP);
        for (int column = 0; column < (shape->Wrap ? shape->Columns : shape->Columns + 1); column++) {
            POINT_AT point;
            shape->Evaluate(shape, column, row, &point);
            emit(quadric, gl, &point);
        }
        gl->End();
    }
}

static void draw_shape(const QUADRIC *quadric, const SHAPE *shape, BOOL high_first)
{
    const GL_API *gl = GluGl();
    BOOL reverse = quadric->Orientation == GLU_INSIDE;
    int count = shape->Wrap ? shape->Columns : shape->Columns + 1;

    switch (quadric->DrawStyle) {
    case GLU_FILL:
        for (int row = 0; row < shape->Rows; row++) {
            gl->Begin(GL_QUAD_STRIP);
            for (int column = 0; column <= shape->Columns; column++) {
                POINT_AT first, second;
                shape->Evaluate(shape, column, row, &first);
                shape->Evaluate(shape, column, row + 1, &second);
                if (quadric->Normals == GLU_FLAT && column > 0) {
                    POINT_AT middle;
                    shape->Evaluate(shape, column - 0.5, row + 0.5, &middle);
                    first.Normal[0] = second.Normal[0] = middle.Normal[0];
                    first.Normal[1] = second.Normal[1] = middle.Normal[1];
                    first.Normal[2] = second.Normal[2] = middle.Normal[2];
                }
                /* the first of the pair is the one that makes the faces turn counter clockwise from outside. */
                if (high_first != reverse) {
                    emit(quadric, gl, &second);
                    emit(quadric, gl, &first);
                } else {
                    emit(quadric, gl, &first);
                    emit(quadric, gl, &second);
                }
            }
            gl->End();
        }
        break;
    case GLU_POINT:
        gl->Begin(GL_POINTS);
        for (int row = 0; row <= shape->Rows; row++)
            for (int column = 0; column < count; column++) {
                POINT_AT point;
                shape->Evaluate(shape, column, row, &point);
                emit(quadric, gl, &point);
            }
        gl->End();
        break;
    case GLU_LINE:
        draw_rings(quadric, gl, shape, 0, shape->Rows);
        for (int column = 0; column < count; column++) {
            gl->Begin(GL_LINE_STRIP);
            for (int row = 0; row <= shape->Rows; row++) {
                POINT_AT point;
                shape->Evaluate(shape, column, row, &point);
                emit(quadric, gl, &point);
            }
            gl->End();
        }
        break;
    case GLU_SILHOUETTE:
        if (shape->InnerRings) {
            draw_rings(quadric, gl, shape, 1, shape->Rows - 1);
        } else {
            draw_rings(quadric, gl, shape, 0, 0);
            draw_rings(quadric, gl, shape, shape->Rows, shape->Rows);
        }
        break;
    }
}

/* ------------------------------------------------------------------ the shapes */

/* a is the radius. rows go from the pole at the top to the pole at the bottom. */
static void evaluate_sphere(const SHAPE *shape, double column, double row, POINT_AT *point)
{
    double theta = TWO_PI * column / shape->Columns, rho = 3.14159265358979323846 * row / shape->Rows;
    double ring = GluSin(rho), x = ring * GluCos(theta), y = ring * GluSin(theta), z = GluCos(rho);
    point->Normal[0] = x;
    point->Normal[1] = y;
    point->Normal[2] = z;
    point->Position[0] = x * shape->A;
    point->Position[1] = y * shape->A;
    point->Position[2] = z * shape->A;
    point->S = column / shape->Columns;
    point->T = 1 - row / shape->Rows;
}

/* a is the base radius, b the top radius and c the height. */
static void evaluate_cylinder(const SHAPE *shape, double column, double row, POINT_AT *point)
{
    double theta = TWO_PI * column / shape->Columns, fraction = row / shape->Rows;
    double radius = shape->A + (shape->B - shape->A) * fraction;
    double x = GluCos(theta), y = GluSin(theta);
    /* the normal leans along the slope of the side. */
    double length = GluSqrt(shape->C * shape->C + (shape->A - shape->B) * (shape->A - shape->B));
    double lean = length == 0 ? 0 : (shape->A - shape->B) / length, flat = length == 0 ? 1 : shape->C / length;
    point->Normal[0] = x * flat;
    point->Normal[1] = y * flat;
    point->Normal[2] = lean;
    point->Position[0] = x * radius;
    point->Position[1] = y * radius;
    point->Position[2] = shape->C * fraction;
    point->S = column / shape->Columns;
    point->T = fraction;
}

/* a is the inner radius, b the outer radius, start and sweep in degrees. zero degrees is up and turns clockwise. */
static void evaluate_disk(const SHAPE *shape, double column, double row, POINT_AT *point)
{
    double angle = (shape->Start + shape->Sweep * column / shape->Columns) * 3.14159265358979323846 / 180;
    double radius = shape->A + (shape->B - shape->A) * row / shape->Rows;
    double x = radius * GluSin(angle), y = radius * GluCos(angle);
    point->Normal[0] = 0;
    point->Normal[1] = 0;
    point->Normal[2] = 1;
    point->Position[0] = x;
    point->Position[1] = y;
    point->Position[2] = 0;
    point->S = 0.5 + x / (2 * shape->B);
    point->T = 0.5 + y / (2 * shape->B);
}

GLUAPI void GLAPIENTRY gluSphere(QUADRIC *quadric, GLdouble radius, GLint slices, GLint stacks)
{
    if (!quadric)
        return;
    if (slices < 2 || stacks < 2 || radius < 0) {
        report(quadric, GLU_INVALID_VALUE);
        return;
    }
    SHAPE shape = {.Rows = stacks, .Columns = slices, .Wrap = TRUE, .InnerRings = TRUE,
                   .Evaluate = evaluate_sphere, .A = radius};
    draw_shape(quadric, &shape, FALSE);
}

GLUAPI void GLAPIENTRY gluCylinder(QUADRIC *quadric, GLdouble base, GLdouble top, GLdouble height, GLint slices,
                                   GLint stacks)
{
    if (!quadric)
        return;
    if (slices < 2 || stacks < 1 || base < 0 || top < 0 || height < 0) {
        report(quadric, GLU_INVALID_VALUE);
        return;
    }
    SHAPE shape = {.Rows = stacks, .Columns = slices, .Wrap = TRUE, .Evaluate = evaluate_cylinder, .A = base,
                   .B = top, .C = height};
    draw_shape(quadric, &shape, TRUE);
}

GLUAPI void GLAPIENTRY gluPartialDisk(QUADRIC *quadric, GLdouble inner, GLdouble outer, GLint slices, GLint loops,
                                      GLdouble start, GLdouble sweep)
{
    if (!quadric)
        return;
    if (slices < 2 || loops < 1 || outer <= 0 || inner < 0 || inner > outer) {
        report(quadric, GLU_INVALID_VALUE);
        return;
    }
    if (sweep < -360)
        sweep = 360;
    if (sweep > 360)
        sweep = 360;
    if (sweep < 0) {
        start += sweep;
        sweep = -sweep;
    }
    SHAPE shape = {.Rows = loops, .Columns = slices, .Wrap = sweep == 360, .Evaluate = evaluate_disk, .A = inner,
                   .B = outer, .Start = start, .Sweep = sweep};
    /* the strip runs clockwise seen from above, so the outer vertex of the pair comes first for a face up. */
    draw_shape(quadric, &shape, TRUE);
}

GLUAPI void GLAPIENTRY gluDisk(QUADRIC *quadric, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
    gluPartialDisk(quadric, inner, outer, slices, loops, 0, 360);
}
