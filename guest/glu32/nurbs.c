/*
 * the nurbs renderer of glu32. curves and surfaces are sampled on a fixed grid and drawn with immediate mode calls.
 * trimming curves are accepted and ignored, and the data callbacks of the renderer are never called.
 */
#include "glu32_private.h"

#define GLU_NURBS_ERROR1 100251
#define GLU_AUTO_LOAD_MATRIX 100200
#define GLU_CULLING 100201
#define GLU_PARAMETRIC_TOLERANCE 100202
#define GLU_SAMPLING_TOLERANCE 100203
#define GLU_DISPLAY_MODE 100204
#define GLU_SAMPLING_METHOD 100205
#define GLU_U_STEP 100206
#define GLU_V_STEP 100207
#define GLU_PATH_LENGTH 100215
#define GLU_OUTLINE_POLYGON 100240
#define GLU_OUTLINE_PATCH 100241
#define GLU_FILL 100012

#define MAX_ORDER 24
#define SEGMENTS_PER_SPAN 12

typedef void(GLAPIENTRY *NURBS_ERROR)(GLenum error);

typedef struct NURBS {
    NURBS_ERROR ErrorCallback;
    int State;
    int TrimDepth;
    GLfloat Properties[8];
    /* what the begin call collected before the end call draws it. */
    BOOL HasGeometry;
    int Dimension;
    int Orders[2], ControlCounts[2];
    int KnotCounts[2];
    GLfloat *Knots[2];
    GLfloat *Control;
} NURBS;

enum { NURBS_OUTSIDE, NURBS_CURVE, NURBS_SURFACE };

static void nurbs_error(NURBS *nurbs, int index)
{
    if (nurbs->ErrorCallback)
        nurbs->ErrorCallback(GLU_NURBS_ERROR1 + index);
}

GLUAPI NURBS *GLAPIENTRY gluNewNurbsRenderer(void)
{
    NURBS *nurbs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*nurbs));
    if (nurbs) {
        nurbs->Properties[0] = 1;
        nurbs->Properties[1] = 0;
        nurbs->Properties[2] = 0.5f;
        nurbs->Properties[3] = 50;
        nurbs->Properties[4] = GLU_FILL;
        nurbs->Properties[5] = GLU_PATH_LENGTH;
        nurbs->Properties[6] = 100;
        nurbs->Properties[7] = 100;
    }
    return nurbs;
}

static void drop_geometry(NURBS *nurbs)
{
    HANDLE heap = GetProcessHeap();
    for (int i = 0; i < 2; i++) {
        if (nurbs->Knots[i])
            HeapFree(heap, 0, nurbs->Knots[i]);
        nurbs->Knots[i] = nullptr;
    }
    if (nurbs->Control)
        HeapFree(heap, 0, nurbs->Control);
    nurbs->Control = nullptr;
    nurbs->HasGeometry = FALSE;
}

GLUAPI void GLAPIENTRY gluDeleteNurbsRenderer(NURBS *nurbs)
{
    if (!nurbs)
        return;
    drop_geometry(nurbs);
    HeapFree(GetProcessHeap(), 0, nurbs);
}

GLUAPI void GLAPIENTRY gluNurbsCallback(NURBS *nurbs, GLenum which, void(GLAPIENTRY *function)(void))
{
    if (!nurbs)
        return;
    if (which == GLU_ERROR)
        nurbs->ErrorCallback = (NURBS_ERROR)function;
}

GLUAPI void GLAPIENTRY gluNurbsProperty(NURBS *nurbs, GLenum property, GLfloat value)
{
    if (!nurbs)
        return;
    if (property < GLU_AUTO_LOAD_MATRIX || property > GLU_V_STEP) {
        nurbs_error(nurbs, 25);
        return;
    }
    switch (property) {
    case GLU_AUTO_LOAD_MATRIX: nurbs->Properties[0] = value; break;
    case GLU_CULLING: nurbs->Properties[1] = value; break;
    case GLU_PARAMETRIC_TOLERANCE: nurbs->Properties[2] = value; break;
    case GLU_SAMPLING_TOLERANCE: nurbs->Properties[3] = value; break;
    case GLU_DISPLAY_MODE: nurbs->Properties[4] = value; break;
    case GLU_SAMPLING_METHOD: nurbs->Properties[5] = value; break;
    case GLU_U_STEP: nurbs->Properties[6] = value; break;
    case GLU_V_STEP: nurbs->Properties[7] = value; break;
    }
}

GLUAPI void GLAPIENTRY gluGetNurbsProperty(NURBS *nurbs, GLenum property, GLfloat *value)
{
    if (!nurbs || !value)
        return;
    if (property < GLU_AUTO_LOAD_MATRIX || property > GLU_V_STEP) {
        nurbs_error(nurbs, 25);
        return;
    }
    *value = nurbs->Properties[property == GLU_AUTO_LOAD_MATRIX ? 0 : property - GLU_CULLING + 1];
}

/* the sampling matrices only matter for tolerances measured on the screen. */
GLUAPI void GLAPIENTRY gluLoadSamplingMatrices(NURBS *nurbs, const GLfloat model[16], const GLfloat perspective[16],
                                               const GLint view[4])
{
}

GLUAPI void GLAPIENTRY gluBeginCurve(NURBS *nurbs)
{
    if (!nurbs)
        return;
    drop_geometry(nurbs);
    nurbs->State = NURBS_CURVE;
}

GLUAPI void GLAPIENTRY gluBeginSurface(NURBS *nurbs)
{
    if (!nurbs)
        return;
    drop_geometry(nurbs);
    nurbs->State = NURBS_SURFACE;
    nurbs->TrimDepth = 0;
}

GLUAPI void GLAPIENTRY gluBeginTrim(NURBS *nurbs)
{
    if (!nurbs)
        return;
    if (nurbs->State != NURBS_SURFACE) {
        nurbs_error(nurbs, 14);
        return;
    }
    nurbs->TrimDepth++;
}

GLUAPI void GLAPIENTRY gluEndTrim(NURBS *nurbs)
{
    if (!nurbs)
        return;
    if (nurbs->TrimDepth <= 0) {
        nurbs_error(nurbs, 16);
        return;
    }
    nurbs->TrimDepth--;
}

GLUAPI void GLAPIENTRY gluPwlCurve(NURBS *nurbs, GLint count, GLfloat *data, GLint stride, GLenum type)
{
    if (!nurbs)
        return;
    if (nurbs->TrimDepth <= 0)
        nurbs_error(nurbs, 18);
    else if (count < 0)
        nurbs_error(nurbs, 32);
    else if (stride < 0)
        nurbs_error(nurbs, 33);
}

/* ------------------------------------------------------------------ evaluation */

static BOOL check_spline(NURBS *nurbs, int knot_count, const GLfloat *knots, int order)
{
    if (order < 1 || order > MAX_ORDER) {
        nurbs_error(nurbs, 0);
        return FALSE;
    }
    if (knot_count < 2 * order) {
        nurbs_error(nurbs, 1);
        return FALSE;
    }
    for (int i = 1; i < knot_count; i++)
        if (knots[i] < knots[i - 1]) {
            nurbs_error(nurbs, 3);
            return FALSE;
        }
    int control = knot_count - order;
    if (!(knots[control] > knots[order - 1])) {
        nurbs_error(nurbs, 2);
        return FALSE;
    }
    return TRUE;
}

/* the basis values that are not zero at u, for the control points span - order + 1 up to span. */
static int basis(const GLfloat *knots, int control_count, int order, double u, double values[MAX_ORDER])
{
    int span = order - 1;
    while (span < control_count - 1 && u >= knots[span + 1])
        span++;
    double left[MAX_ORDER], right[MAX_ORDER];
    values[0] = 1;
    for (int j = 1; j < order; j++) {
        left[j] = u - knots[span + 1 - j];
        right[j] = knots[span + j] - u;
        double saved = 0;
        for (int r = 0; r < j; r++) {
            double gap = right[r + 1] + left[j - r];
            double term = gap == 0 ? 0 : values[r] / gap;
            values[r] = saved + right[r + 1] * term;
            saved = left[j - r] * term;
        }
        values[j] = saved;
    }
    return span - order + 1;
}

static void curve_point(const NURBS *nurbs, double u, double out[3])
{
    double values[MAX_ORDER];
    int first = basis(nurbs->Knots[0], nurbs->ControlCounts[0], nurbs->Orders[0], u, values);
    double sum[4] = {};
    for (int k = 0; k < nurbs->Orders[0]; k++) {
        const GLfloat *p = nurbs->Control + (first + k) * nurbs->Dimension;
        for (int d = 0; d < nurbs->Dimension; d++)
            sum[d] += values[k] * p[d];
    }
    double w = nurbs->Dimension == 4 ? sum[3] : 1;
    if (w == 0)
        w = 1;
    out[0] = sum[0] / w;
    out[1] = sum[1] / w;
    out[2] = sum[2] / w;
}

static void surface_point(const NURBS *nurbs, double s, double t, double out[3])
{
    double sv[MAX_ORDER], tv[MAX_ORDER];
    int sf = basis(nurbs->Knots[0], nurbs->ControlCounts[0], nurbs->Orders[0], s, sv);
    int tf = basis(nurbs->Knots[1], nurbs->ControlCounts[1], nurbs->Orders[1], t, tv);
    double sum[4] = {};
    for (int i = 0; i < nurbs->Orders[0]; i++)
        for (int j = 0; j < nurbs->Orders[1]; j++) {
            const GLfloat *p = nurbs->Control + ((sf + i) * nurbs->ControlCounts[1] + tf + j) * nurbs->Dimension;
            for (int d = 0; d < nurbs->Dimension; d++)
                sum[d] += sv[i] * tv[j] * p[d];
        }
    double w = nurbs->Dimension == 4 ? sum[3] : 1;
    if (w == 0)
        w = 1;
    out[0] = sum[0] / w;
    out[1] = sum[1] / w;
    out[2] = sum[2] / w;
}

/* copies the strided control points into a tight array. */
static BOOL keep_points(NURBS *nurbs, int stride_s, int stride_t, const GLfloat *control, GLenum type)
{
    nurbs->Dimension = type == GL_MAP1_VERTEX_4 ? 4 : 3;
    nurbs->Control = HeapAlloc(GetProcessHeap(), 0,
                               (SIZE_T)nurbs->ControlCounts[0] * nurbs->ControlCounts[1] * nurbs->Dimension * sizeof(GLfloat));
    if (!nurbs->Control)
        return FALSE;
    for (int i = 0; i < nurbs->ControlCounts[0]; i++)
        for (int j = 0; j < nurbs->ControlCounts[1]; j++)
            for (int d = 0; d < nurbs->Dimension; d++)
                nurbs->Control[((SIZE_T)i * nurbs->ControlCounts[1] + j) * nurbs->Dimension + d] =
                    control[(SIZE_T)i * stride_s + (SIZE_T)j * stride_t + d];
    return TRUE;
}

static BOOL keep_knots(NURBS *nurbs, int axis, int count, const GLfloat *knots, int order)
{
    nurbs->Knots[axis] = HeapAlloc(GetProcessHeap(), 0, count * sizeof(GLfloat));
    if (!nurbs->Knots[axis])
        return FALSE;
    for (int i = 0; i < count; i++)
        nurbs->Knots[axis][i] = knots[i];
    nurbs->KnotCounts[axis] = count;
    nurbs->Orders[axis] = order;
    nurbs->ControlCounts[axis] = count - order;
    return TRUE;
}

GLUAPI void GLAPIENTRY gluNurbsCurve(NURBS *nurbs, GLint knot_count, GLfloat *knots, GLint stride,
                                     GLfloat *control, GLint order, GLenum type)
{
    if (!nurbs)
        return;
    if (nurbs->State != NURBS_CURVE) {
        nurbs_error(nurbs, 9);
        return;
    }
    if (nurbs->TrimDepth > 0)
        return;
    /* only the points are drawn, so colors, normals and texture coordinates are accepted and dropped. */
    if (type != GL_MAP1_VERTEX_3 && type != GL_MAP1_VERTEX_4)
        return;
    if (!knots || !control || stride < 0) {
        nurbs_error(nurbs, stride < 0 ? 33 : 35);
        return;
    }
    if (!check_spline(nurbs, knot_count, knots, order))
        return;
    if (nurbs->HasGeometry) {
        nurbs_error(nurbs, 22);
        return;
    }
    if (keep_knots(nurbs, 0, knot_count, knots, order)) {
        nurbs->ControlCounts[1] = 1;
        nurbs->HasGeometry = keep_points(nurbs, stride, 0, control, type);
    }
}

GLUAPI void GLAPIENTRY gluNurbsSurface(NURBS *nurbs, GLint s_count, GLfloat *s_knots, GLint t_count,
                                       GLfloat *t_knots, GLint s_stride, GLint t_stride, GLfloat *control,
                                       GLint s_order, GLint t_order, GLenum type)
{
    if (!nurbs)
        return;
    if (nurbs->State != NURBS_SURFACE) {
        nurbs_error(nurbs, 10);
        return;
    }
    if (type != GL_MAP2_VERTEX_3 && type != GL_MAP2_VERTEX_4)
        return;
    if (!s_knots || !t_knots || !control || s_stride < 0 || t_stride < 0) {
        nurbs_error(nurbs, s_stride < 0 || t_stride < 0 ? 33 : 35);
        return;
    }
    if (!check_spline(nurbs, s_count, s_knots, s_order) || !check_spline(nurbs, t_count, t_knots, t_order))
        return;
    if (nurbs->HasGeometry) {
        nurbs_error(nurbs, 24);
        return;
    }
    if (keep_knots(nurbs, 0, s_count, s_knots, s_order) && keep_knots(nurbs, 1, t_count, t_knots, t_order))
        nurbs->HasGeometry = keep_points(nurbs, s_stride, t_stride, control, type == GL_MAP2_VERTEX_4 ? GL_MAP1_VERTEX_4 : GL_MAP1_VERTEX_3);
}

GLUAPI void GLAPIENTRY gluEndCurve(NURBS *nurbs)
{
    if (!nurbs)
        return;
    if (nurbs->State != NURBS_CURVE) {
        nurbs_error(nurbs, 5);
        return;
    }
    nurbs->State = NURBS_OUTSIDE;
    if (nurbs->HasGeometry) {
        const GL_API *gl = GluGl();
        int spans = nurbs->ControlCounts[0] - nurbs->Orders[0] + 1, steps = spans * SEGMENTS_PER_SPAN;
        double low = nurbs->Knots[0][nurbs->Orders[0] - 1], high = nurbs->Knots[0][nurbs->ControlCounts[0]];
        gl->Begin(GL_LINE_STRIP);
        for (int i = 0; i <= steps; i++) {
            double point[3];
            /* the end of the domain belongs to the last span, so it is nudged back inside. */
            double u = i == steps ? high - (high - low) * 1e-12 : low + (high - low) * i / steps;
            curve_point(nurbs, u, point);
            gl->Vertex3d(point[0], point[1], point[2]);
        }
        gl->End();
    }
    drop_geometry(nurbs);
}

GLUAPI void GLAPIENTRY gluEndSurface(NURBS *nurbs)
{
    if (!nurbs)
        return;
    if (nurbs->State != NURBS_SURFACE) {
        nurbs_error(nurbs, 26);
        return;
    }
    if (nurbs->TrimDepth > 0) {
        nurbs_error(nurbs, 11);
        nurbs->TrimDepth = 0;
    }
    nurbs->State = NURBS_OUTSIDE;
    if (nurbs->HasGeometry) {
        const GL_API *gl = GluGl();
        int s_steps = (nurbs->ControlCounts[0] - nurbs->Orders[0] + 1) * SEGMENTS_PER_SPAN;
        int t_steps = (nurbs->ControlCounts[1] - nurbs->Orders[1] + 1) * SEGMENTS_PER_SPAN;
        double s_low = nurbs->Knots[0][nurbs->Orders[0] - 1], s_high = nurbs->Knots[0][nurbs->ControlCounts[0]];
        double t_low = nurbs->Knots[1][nurbs->Orders[1] - 1], t_high = nurbs->Knots[1][nurbs->ControlCounts[1]];
        int mode = (int)nurbs->Properties[4];
        #define PARAMETER(low, high, i, steps) \
            ((i) == (steps) ? (high) - ((high) - (low)) * 1e-12 : (low) + ((high) - (low)) * (i) / (steps))
        if (mode == GLU_FILL) {
            for (int i = 0; i < s_steps; i++) {
                gl->Begin(GL_QUAD_STRIP);
                for (int j = 0; j <= t_steps; j++) {
                    for (int row = 1; row >= 0; row--) {
                        double s = PARAMETER(s_low, s_high, i + row, s_steps), t = PARAMETER(t_low, t_high, j, t_steps);
                        double p[3], ps[3], pt[3];
                        double ds = (s_high - s_low) * 1e-6, dt = (t_high - t_low) * 1e-6;
                        surface_point(nurbs, s, t, p);
                        /* the normal comes from steps taken back inside the domain at its far edges. */
                        double s2 = s + ds > s_high ? s - ds : s + ds, t2 = t + dt > t_high ? t - dt : t + dt;
                        surface_point(nurbs, s2, t, ps);
                        surface_point(nurbs, s, t2, pt);
                        double a[3] = {ps[0] - p[0], ps[1] - p[1], ps[2] - p[2]};
                        double b[3] = {pt[0] - p[0], pt[1] - p[1], pt[2] - p[2]};
                        double n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
                        if (s2 < s)
                            for (int d = 0; d < 3; d++)
                                n[d] = -n[d];
                        if (t2 < t)
                            for (int d = 0; d < 3; d++)
                                n[d] = -n[d];
                        double length = GluSqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        if (length > 0)
                            gl->Normal3d(n[0] / length, n[1] / length, n[2] / length);
                        gl->Vertex3d(p[0], p[1], p[2]);
                    }
                }
                gl->End();
            }
        } else {
            /* the outline of the patch is the grid lines in both directions. */
            for (int i = 0; i <= s_steps; i++) {
                gl->Begin(GL_LINE_STRIP);
                for (int j = 0; j <= t_steps; j++) {
                    double p[3];
                    surface_point(nurbs, PARAMETER(s_low, s_high, i, s_steps), PARAMETER(t_low, t_high, j, t_steps), p);
                    gl->Vertex3d(p[0], p[1], p[2]);
                }
                gl->End();
            }
            for (int j = 0; j <= t_steps; j++) {
                gl->Begin(GL_LINE_STRIP);
                for (int i = 0; i <= s_steps; i++) {
                    double p[3];
                    surface_point(nurbs, PARAMETER(s_low, s_high, i, s_steps), PARAMETER(t_low, t_high, j, t_steps), p);
                    gl->Vertex3d(p[0], p[1], p[2]);
                }
                gl->End();
            }
        }
        #undef PARAMETER
    }
    drop_geometry(nurbs);
}
