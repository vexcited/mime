/*
 * the tessellator of glu32. contours are collected between the begin and end calls, classified by how they nest
 * and what the winding rule says about the regions on both sides, and every region is cut into triangles by
 * bridging its holes into its outline and clipping ears. contours that cross one another are not repaired.
 */
#include "glu32_private.h"

#define GLU_TESS_BEGIN 100100
#define GLU_TESS_VERTEX 100101
#define GLU_TESS_END 100102
#define GLU_TESS_ERROR 100103
#define GLU_TESS_EDGE_FLAG 100104
#define GLU_TESS_COMBINE 100105
#define GLU_TESS_BEGIN_DATA 100106
#define GLU_TESS_VERTEX_DATA 100107
#define GLU_TESS_END_DATA 100108
#define GLU_TESS_ERROR_DATA 100109
#define GLU_TESS_EDGE_FLAG_DATA 100110
#define GLU_TESS_COMBINE_DATA 100111
#define GLU_TESS_WINDING_RULE 100140
#define GLU_TESS_BOUNDARY_ONLY 100141
#define GLU_TESS_TOLERANCE 100142
#define GLU_TESS_WINDING_ODD 100130
#define GLU_TESS_WINDING_NONZERO 100131
#define GLU_TESS_WINDING_POSITIVE 100132
#define GLU_TESS_WINDING_NEGATIVE 100133
#define GLU_TESS_WINDING_ABS_GEQ_TWO 100134
#define GLU_TESS_MISSING_BEGIN_POLYGON 100151
#define GLU_TESS_MISSING_BEGIN_CONTOUR 100152
#define GLU_TESS_MISSING_END_POLYGON 100153
#define GLU_TESS_MISSING_END_CONTOUR 100154

typedef struct VERTEX3 {
    double Coords[3];
    void *Data;
} VERTEX3;

typedef struct CONTOUR3 {
    VERTEX3 *Vertices;
    int Count, Capacity;
} CONTOUR3;

typedef struct TESS {
    void *Callbacks[12];
    GLdouble Winding, BoundaryOnly, Tolerance;
    double Normal[3];
    int State;
    void *PolygonData;
    CONTOUR3 *Contours;
    int ContourCount, ContourCapacity;
} TESS;

enum { STATE_OUTSIDE, STATE_POLYGON, STATE_CONTOUR };

/* the callbacks as they are declared by the interface. */
typedef void(GLAPIENTRY *BEGIN_CALLBACK)(GLenum);
typedef void(GLAPIENTRY *VERTEX_CALLBACK)(void *);
typedef void(GLAPIENTRY *END_CALLBACK)(void);
typedef void(GLAPIENTRY *ERROR_CALLBACK)(GLenum);
typedef void(GLAPIENTRY *EDGE_FLAG_CALLBACK)(GLboolean);
typedef void(GLAPIENTRY *BEGIN_DATA_CALLBACK)(GLenum, void *);
typedef void(GLAPIENTRY *VERTEX_DATA_CALLBACK)(void *, void *);
typedef void(GLAPIENTRY *END_DATA_CALLBACK)(void *);
typedef void(GLAPIENTRY *ERROR_DATA_CALLBACK)(GLenum, void *);
typedef void(GLAPIENTRY *EDGE_FLAG_DATA_CALLBACK)(GLboolean, void *);

static void *heap_grow(void *old, SIZE_T size)
{
    HANDLE heap = GetProcessHeap();
    return old ? HeapReAlloc(heap, 0, old, size) : HeapAlloc(heap, 0, size);
}

static void heap_free(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

GLUAPI TESS *GLAPIENTRY gluNewTess(void)
{
    TESS *tess = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*tess));
    if (tess)
        tess->Winding = GLU_TESS_WINDING_ODD;
    return tess;
}

GLUAPI void GLAPIENTRY gluDeleteTess(TESS *tess)
{
    if (!tess)
        return;
    for (int i = 0; i < tess->ContourCount; i++)
        heap_free(tess->Contours[i].Vertices);
    heap_free(tess->Contours);
    heap_free(tess);
}

static void tess_error(TESS *tess, GLenum error)
{
    if (tess->Callbacks[GLU_TESS_ERROR_DATA - GLU_TESS_BEGIN])
        ((ERROR_DATA_CALLBACK)tess->Callbacks[GLU_TESS_ERROR_DATA - GLU_TESS_BEGIN])(error, tess->PolygonData);
    else if (tess->Callbacks[GLU_TESS_ERROR - GLU_TESS_BEGIN])
        ((ERROR_CALLBACK)tess->Callbacks[GLU_TESS_ERROR - GLU_TESS_BEGIN])(error);
}

GLUAPI void GLAPIENTRY gluTessCallback(TESS *tess, GLenum which, void(GLAPIENTRY *function)(void))
{
    if (!tess)
        return;
    if (which < GLU_TESS_BEGIN || which > GLU_TESS_COMBINE_DATA) {
        tess_error(tess, GLU_INVALID_ENUM);
        return;
    }
    tess->Callbacks[which - GLU_TESS_BEGIN] = (void *)function;
}

GLUAPI void GLAPIENTRY gluTessProperty(TESS *tess, GLenum which, GLdouble value)
{
    if (!tess)
        return;
    switch (which) {
    case GLU_TESS_WINDING_RULE:
        if (value < GLU_TESS_WINDING_ODD || value > GLU_TESS_WINDING_ABS_GEQ_TWO)
            break;
        tess->Winding = value;
        return;
    case GLU_TESS_BOUNDARY_ONLY:
        tess->BoundaryOnly = value != 0;
        return;
    case GLU_TESS_TOLERANCE:
        if (value < 0 || value > 1)
            break;
        tess->Tolerance = value;
        return;
    default:
        tess_error(tess, GLU_INVALID_ENUM);
        return;
    }
    tess_error(tess, GLU_INVALID_VALUE);
}

GLUAPI void GLAPIENTRY gluGetTessProperty(TESS *tess, GLenum which, GLdouble *value)
{
    if (!tess || !value)
        return;
    switch (which) {
    case GLU_TESS_WINDING_RULE:
        *value = tess->Winding;
        break;
    case GLU_TESS_BOUNDARY_ONLY:
        *value = tess->BoundaryOnly;
        break;
    case GLU_TESS_TOLERANCE:
        *value = tess->Tolerance;
        break;
    default:
        *value = 0;
        tess_error(tess, GLU_INVALID_ENUM);
    }
}

GLUAPI void GLAPIENTRY gluTessNormal(TESS *tess, GLdouble x, GLdouble y, GLdouble z)
{
    if (!tess)
        return;
    tess->Normal[0] = x;
    tess->Normal[1] = y;
    tess->Normal[2] = z;
}

/* ------------------------------------------------------------------ collecting */

static void clear_contours(TESS *tess)
{
    for (int i = 0; i < tess->ContourCount; i++)
        heap_free(tess->Contours[i].Vertices);
    tess->ContourCount = 0;
}

static void process(TESS *tess);

GLUAPI void GLAPIENTRY gluTessBeginPolygon(TESS *tess, void *data)
{
    if (!tess)
        return;
    if (tess->State != STATE_OUTSIDE) {
        tess_error(tess, GLU_TESS_MISSING_END_POLYGON);
        clear_contours(tess);
    }
    tess->State = STATE_POLYGON;
    tess->PolygonData = data;
}

GLUAPI void GLAPIENTRY gluTessBeginContour(TESS *tess)
{
    if (!tess)
        return;
    if (tess->State == STATE_OUTSIDE) {
        tess_error(tess, GLU_TESS_MISSING_BEGIN_POLYGON);
        return;
    }
    if (tess->State == STATE_CONTOUR)
        tess_error(tess, GLU_TESS_MISSING_END_CONTOUR);
    if (tess->ContourCount == tess->ContourCapacity) {
        int capacity = tess->ContourCapacity ? tess->ContourCapacity * 2 : 4;
        CONTOUR3 *grown = heap_grow(tess->Contours, capacity * sizeof(*grown));
        if (!grown)
            return;
        tess->Contours = grown;
        tess->ContourCapacity = capacity;
    }
    CONTOUR3 *contour = &tess->Contours[tess->ContourCount++];
    contour->Vertices = nullptr;
    contour->Count = contour->Capacity = 0;
    tess->State = STATE_CONTOUR;
}

GLUAPI void GLAPIENTRY gluTessVertex(TESS *tess, GLdouble coords[3], void *data)
{
    if (!tess)
        return;
    if (tess->State != STATE_CONTOUR) {
        tess_error(tess, GLU_TESS_MISSING_BEGIN_CONTOUR);
        return;
    }
    CONTOUR3 *contour = &tess->Contours[tess->ContourCount - 1];
    if (contour->Count == contour->Capacity) {
        int capacity = contour->Capacity ? contour->Capacity * 2 : 8;
        VERTEX3 *grown = heap_grow(contour->Vertices, capacity * sizeof(*grown));
        if (!grown)
            return;
        contour->Vertices = grown;
        contour->Capacity = capacity;
    }
    VERTEX3 *vertex = &contour->Vertices[contour->Count++];
    vertex->Coords[0] = coords[0];
    vertex->Coords[1] = coords[1];
    vertex->Coords[2] = coords[2];
    vertex->Data = data;
}

GLUAPI void GLAPIENTRY gluTessEndContour(TESS *tess)
{
    if (!tess)
        return;
    if (tess->State != STATE_CONTOUR) {
        tess_error(tess, GLU_TESS_MISSING_BEGIN_CONTOUR);
        return;
    }
    tess->State = STATE_POLYGON;
}

GLUAPI void GLAPIENTRY gluTessEndPolygon(TESS *tess)
{
    if (!tess)
        return;
    if (tess->State == STATE_OUTSIDE) {
        tess_error(tess, GLU_TESS_MISSING_BEGIN_POLYGON);
        return;
    }
    if (tess->State == STATE_CONTOUR)
        tess_error(tess, GLU_TESS_MISSING_END_CONTOUR);
    tess->State = STATE_OUTSIDE;
    process(tess);
    clear_contours(tess);
    tess->PolygonData = nullptr;
}

/* the older interface is a polygon with its contours in one call sequence. */
GLUAPI void GLAPIENTRY gluBeginPolygon(TESS *tess)
{
    gluTessBeginPolygon(tess, nullptr);
    gluTessBeginContour(tess);
}

GLUAPI void GLAPIENTRY gluNextContour(TESS *tess, GLenum type)
{
    gluTessEndContour(tess);
    gluTessBeginContour(tess);
}

GLUAPI void GLAPIENTRY gluEndPolygon(TESS *tess)
{
    gluTessEndContour(tess);
    gluTessEndPolygon(tess);
}

/* ------------------------------------------------------------------ output */

static void out_begin(TESS *tess, GLenum mode)
{
    if (tess->Callbacks[GLU_TESS_BEGIN_DATA - GLU_TESS_BEGIN])
        ((BEGIN_DATA_CALLBACK)tess->Callbacks[GLU_TESS_BEGIN_DATA - GLU_TESS_BEGIN])(mode, tess->PolygonData);
    else if (tess->Callbacks[GLU_TESS_BEGIN - GLU_TESS_BEGIN])
        ((BEGIN_CALLBACK)tess->Callbacks[GLU_TESS_BEGIN - GLU_TESS_BEGIN])(mode);
}

static void out_vertex(TESS *tess, void *data)
{
    if (tess->Callbacks[GLU_TESS_VERTEX_DATA - GLU_TESS_BEGIN])
        ((VERTEX_DATA_CALLBACK)tess->Callbacks[GLU_TESS_VERTEX_DATA - GLU_TESS_BEGIN])(data, tess->PolygonData);
    else if (tess->Callbacks[GLU_TESS_VERTEX - GLU_TESS_BEGIN])
        ((VERTEX_CALLBACK)tess->Callbacks[GLU_TESS_VERTEX - GLU_TESS_BEGIN])(data);
}

static void out_end(TESS *tess)
{
    if (tess->Callbacks[GLU_TESS_END_DATA - GLU_TESS_BEGIN])
        ((END_DATA_CALLBACK)tess->Callbacks[GLU_TESS_END_DATA - GLU_TESS_BEGIN])(tess->PolygonData);
    else if (tess->Callbacks[GLU_TESS_END - GLU_TESS_BEGIN])
        ((END_CALLBACK)tess->Callbacks[GLU_TESS_END - GLU_TESS_BEGIN])();
}

static void out_edge_flag(TESS *tess, GLboolean flag)
{
    if (tess->Callbacks[GLU_TESS_EDGE_FLAG_DATA - GLU_TESS_BEGIN])
        ((EDGE_FLAG_DATA_CALLBACK)tess->Callbacks[GLU_TESS_EDGE_FLAG_DATA - GLU_TESS_BEGIN])(flag, tess->PolygonData);
    else if (tess->Callbacks[GLU_TESS_EDGE_FLAG - GLU_TESS_BEGIN])
        ((EDGE_FLAG_CALLBACK)tess->Callbacks[GLU_TESS_EDGE_FLAG - GLU_TESS_BEGIN])(flag);
}

static BOOL wants_edge_flags(const TESS *tess)
{
    return tess->Callbacks[GLU_TESS_EDGE_FLAG_DATA - GLU_TESS_BEGIN] || tess->Callbacks[GLU_TESS_EDGE_FLAG - GLU_TESS_BEGIN];
}

/* ------------------------------------------------------------------ geometry */

typedef struct POINT2 {
    double X, Y;
} POINT2;

/* a contour once flattened, its vertices are a range of the shared point list. */
typedef struct FLAT {
    int First, Count;
    double Area;
    int Parent, Sign, Kind, Owner;
} FLAT;

enum { KIND_IGNORED, KIND_OUTER, KIND_HOLE };

typedef struct NODE {
    int Vertex, Next, Prev;
    BOOL Boundary;
} NODE;

static double cross2(POINT2 a, POINT2 b, POINT2 c)
{
    return (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
}

static BOOL inside_polygon(const POINT2 *points, int first, int count, POINT2 p)
{
    BOOL inside = FALSE;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        POINT2 a = points[first + i], b = points[first + j];
        if ((a.Y > p.Y) != (b.Y > p.Y) && p.X < (b.X - a.X) * (p.Y - a.Y) / (b.Y - a.Y) + a.X)
            inside = !inside;
    }
    return inside;
}

static BOOL winding_filled(double rule, int winding)
{
    switch ((int)rule) {
    case GLU_TESS_WINDING_ODD:
        return winding & 1;
    case GLU_TESS_WINDING_NONZERO:
        return winding != 0;
    case GLU_TESS_WINDING_POSITIVE:
        return winding > 0;
    case GLU_TESS_WINDING_NEGATIVE:
        return winding < 0;
    default:
        return winding >= 2 || winding <= -2;
    }
}

typedef struct TRIANGLES {
    int *Vertex;
    BOOL *Boundary;
    int Count, Capacity;
} TRIANGLES;

static BOOL add_corner(TRIANGLES *out, int vertex, BOOL boundary)
{
    if (out->Count == out->Capacity) {
        int capacity = out->Capacity ? out->Capacity * 2 : 96;
        int *vertices = heap_grow(out->Vertex, capacity * sizeof(int));
        if (!vertices)
            return FALSE;
        out->Vertex = vertices;
        BOOL *flags = heap_grow(out->Boundary, capacity * sizeof(BOOL));
        if (!flags)
            return FALSE;
        out->Boundary = flags;
        out->Capacity = capacity;
    }
    out->Vertex[out->Count] = vertex;
    out->Boundary[out->Count++] = boundary;
    return TRUE;
}

static BOOL same_spot(POINT2 a, POINT2 b)
{
    return a.X == b.X && a.Y == b.Y;
}

/* joins a hole to the outline through the vertex that the hole's rightmost point can see. */
static BOOL bridge_hole(NODE *nodes, int *count, const POINT2 *points, int outline, int hole_first, int hole_count)
{
    int rightmost = hole_first;
    for (int i = 0; i < hole_count; i++)
        if (points[nodes[hole_first + i].Vertex].X > points[nodes[rightmost].Vertex].X)
            rightmost = hole_first + i;
    POINT2 m = points[nodes[rightmost].Vertex];

    /* the nearest outline edge to the right of the point on its own height. */
    double best = 0;
    int edge = -1;
    POINT2 hit = m;
    int cursor = outline;
    do {
        POINT2 a = points[nodes[cursor].Vertex], b = points[nodes[nodes[cursor].Next].Vertex];
        if ((a.Y <= m.Y && b.Y >= m.Y) || (a.Y >= m.Y && b.Y <= m.Y)) {
            double x = a.Y == b.Y ? (a.X > b.X ? a.X : b.X) : a.X + (m.Y - a.Y) * (b.X - a.X) / (b.Y - a.Y);
            if (x >= m.X && (edge < 0 || x < best)) {
                best = x;
                edge = cursor;
                hit.X = x;
                hit.Y = m.Y;
            }
        }
        cursor = nodes[cursor].Next;
    } while (cursor != outline);
    if (edge < 0)
        return FALSE;

    int a = edge, b = nodes[edge].Next;
    int visible = points[nodes[a].Vertex].X > points[nodes[b].Vertex].X ? a : b;
    if (same_spot(points[nodes[a].Vertex], hit))
        visible = a;
    else if (same_spot(points[nodes[b].Vertex], hit))
        visible = b;
    else {
        /* an outline vertex inside the triangle of the point, the hit and the end of the edge hides the end. */
        POINT2 p = points[nodes[visible].Vertex];
        double best_tangent = 0;
        BOOL found = FALSE;
        cursor = outline;
        do {
            POINT2 q = points[nodes[cursor].Vertex];
            if (!same_spot(q, p) && !same_spot(q, m) && !same_spot(q, hit)) {
                POINT2 t0 = m, t1 = hit, t2 = p;
                if (cross2(t0, t1, t2) < 0) {
                    POINT2 swap = t1;
                    t1 = t2;
                    t2 = swap;
                }
                if (cross2(t0, t1, q) >= 0 && cross2(t1, t2, q) >= 0 && cross2(t2, t0, q) >= 0) {
                    double dx = q.X - m.X, dy = q.Y - m.Y;
                    double tangent = dx == 0 ? 1e300 : (dy < 0 ? -dy : dy) / dx;
                    if (!found || tangent < best_tangent) {
                        found = TRUE;
                        best_tangent = tangent;
                        visible = cursor;
                    }
                }
            }
            cursor = nodes[cursor].Next;
        } while (cursor != outline);
    }

    /* visible -> rightmost, around the hole, -> copy of rightmost -> copy of visible -> what followed visible. */
    int visible_copy = (*count)++, rightmost_copy = (*count)++;
    nodes[visible_copy] = nodes[visible];
    nodes[rightmost_copy] = nodes[rightmost];
    nodes[visible_copy].Prev = rightmost_copy;
    nodes[nodes[visible].Next].Prev = visible_copy;
    nodes[rightmost_copy].Next = visible_copy;
    nodes[rightmost_copy].Boundary = FALSE;
    int before = nodes[rightmost].Prev;
    nodes[before].Next = rightmost_copy;
    nodes[rightmost_copy].Prev = before;
    nodes[visible].Next = rightmost;
    nodes[visible].Boundary = FALSE;
    nodes[rightmost].Prev = visible;
    return TRUE;
}

static BOOL ear_free(const NODE *nodes, const POINT2 *points, int start, int a, int b, int c)
{
    POINT2 pa = points[nodes[a].Vertex], pb = points[nodes[b].Vertex], pc = points[nodes[c].Vertex];
    int cursor = start;
    do {
        POINT2 p = points[nodes[cursor].Vertex];
        if (cursor != a && cursor != b && cursor != c && !same_spot(p, pa) && !same_spot(p, pb) &&
            !same_spot(p, pc) && cross2(pa, pb, p) >= 0 && cross2(pb, pc, p) >= 0 && cross2(pc, pa, p) >= 0)
            return FALSE;
        cursor = nodes[cursor].Next;
    } while (cursor != start);
    return TRUE;
}

static BOOL clip_ears(NODE *nodes, const POINT2 *points, int start, int remaining, TRIANGLES *out)
{
    int cursor = start, misses = 0;
    while (remaining > 3) {
        int a = nodes[cursor].Prev, b = cursor, c = nodes[cursor].Next;
        double area = cross2(points[nodes[a].Vertex], points[nodes[b].Vertex], points[nodes[c].Vertex]);
        BOOL clip = FALSE, emit_triangle = TRUE;
        if (area == 0) {
            clip = TRUE;
            emit_triangle = FALSE;
        } else if (area > 0 && ear_free(nodes, points, cursor, a, b, c)) {
            clip = TRUE;
        } else if (misses > remaining) {
            /* nothing is a clean ear, so the first convex corner goes. */
            clip = TRUE;
            emit_triangle = area > 0;
        }
        if (!clip) {
            cursor = nodes[cursor].Next;
            misses++;
            continue;
        }
        if (emit_triangle) {
            if (!add_corner(out, nodes[a].Vertex, nodes[a].Boundary) ||
                !add_corner(out, nodes[b].Vertex, nodes[b].Boundary) || !add_corner(out, nodes[c].Vertex, FALSE))
                return FALSE;
        }
        nodes[a].Next = c;
        nodes[c].Prev = a;
        if (emit_triangle)
            nodes[a].Boundary = FALSE;
        cursor = a;
        remaining--;
        misses = 0;
    }
    if (remaining == 3) {
        int a = cursor, b = nodes[a].Next, c = nodes[b].Next;
        double area = cross2(points[nodes[a].Vertex], points[nodes[b].Vertex], points[nodes[c].Vertex]);
        if (area != 0 && (!add_corner(out, nodes[a].Vertex, nodes[a].Boundary) ||
                          !add_corner(out, nodes[b].Vertex, nodes[b].Boundary) ||
                          !add_corner(out, nodes[c].Vertex, nodes[c].Boundary)))
            return FALSE;
    }
    return TRUE;
}

static void process(TESS *tess)
{
    int total = 0, kept = 0;
    for (int i = 0; i < tess->ContourCount; i++) {
        CONTOUR3 *contour = &tess->Contours[i];
        /* a closing vertex that repeats the first one is dropped. */
        while (contour->Count > 1 && contour->Vertices[contour->Count - 1].Coords[0] == contour->Vertices[0].Coords[0] &&
               contour->Vertices[contour->Count - 1].Coords[1] == contour->Vertices[0].Coords[1] &&
               contour->Vertices[contour->Count - 1].Coords[2] == contour->Vertices[0].Coords[2])
            contour->Count--;
        if (contour->Count >= 3) {
            total += contour->Count;
            kept++;
        }
    }
    if (!kept)
        return;

    /* the normal the contours are projected along. */
    double normal[3] = {tess->Normal[0], tess->Normal[1], tess->Normal[2]};
    if (normal[0] == 0 && normal[1] == 0 && normal[2] == 0) {
        for (int i = 0; i < tess->ContourCount; i++) {
            CONTOUR3 *contour = &tess->Contours[i];
            if (contour->Count < 3)
                continue;
            for (int j = 0; j < contour->Count; j++) {
                const double *p = contour->Vertices[j].Coords, *q = contour->Vertices[(j + 1) % contour->Count].Coords;
                normal[0] += (p[1] - q[1]) * (p[2] + q[2]);
                normal[1] += (p[2] - q[2]) * (p[0] + q[0]);
                normal[2] += (p[0] - q[0]) * (p[1] + q[1]);
            }
        }
    }
    double length = GluSqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (length == 0)
        return;
    for (int i = 0; i < 3; i++)
        normal[i] /= length;
    double helper[3] = {0, 0, 0};
    int smallest = 0;
    for (int i = 1; i < 3; i++)
        if ((normal[i] < 0 ? -normal[i] : normal[i]) < (normal[smallest] < 0 ? -normal[smallest] : normal[smallest]))
            smallest = i;
    helper[smallest] = 1;
    double u[3] = {helper[1] * normal[2] - helper[2] * normal[1], helper[2] * normal[0] - helper[0] * normal[2],
                   helper[0] * normal[1] - helper[1] * normal[0]};
    length = GluSqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    for (int i = 0; i < 3; i++)
        u[i] /= length;
    double v[3] = {normal[1] * u[2] - normal[2] * u[1], normal[2] * u[0] - normal[0] * u[2],
                   normal[0] * u[1] - normal[1] * u[0]};

    POINT2 *points = heap_grow(nullptr, total * sizeof(POINT2));
    VERTEX3 **vertices = heap_grow(nullptr, total * sizeof(VERTEX3 *));
    FLAT *flats = heap_grow(nullptr, kept * sizeof(FLAT));
    NODE *nodes = heap_grow(nullptr, (total + 2 * kept) * sizeof(NODE));
    int *order = heap_grow(nullptr, kept * sizeof(int));
    TRIANGLES triangles = {};
    if (!points || !vertices || !flats || !nodes || !order)
        goto done;

    int at = 0, flat_count = 0;
    for (int i = 0; i < tess->ContourCount; i++) {
        CONTOUR3 *contour = &tess->Contours[i];
        if (contour->Count < 3)
            continue;
        FLAT *flat = &flats[flat_count++];
        flat->First = at;
        flat->Count = contour->Count;
        flat->Area = 0;
        for (int j = 0; j < contour->Count; j++) {
            const double *p = contour->Vertices[j].Coords;
            points[at + j].X = p[0] * u[0] + p[1] * u[1] + p[2] * u[2];
            points[at + j].Y = p[0] * v[0] + p[1] * v[1] + p[2] * v[2];
            vertices[at + j] = &contour->Vertices[j];
        }
        for (int j = 0; j < contour->Count; j++) {
            POINT2 p = points[at + j], q = points[at + (j + 1) % contour->Count];
            flat->Area += p.X * q.Y - q.X * p.Y;
        }
        flat->Area /= 2;
        flat->Sign = flat->Area > 0 ? 1 : flat->Area < 0 ? -1 : 0;
        flat->Parent = -1;
        flat->Kind = KIND_IGNORED;
        flat->Owner = -1;
        at += contour->Count;
    }

    /* the nesting, and the winding on both sides of every contour. */
    int *inside = heap_grow(nullptr, flat_count * sizeof(int));
    int *outside = heap_grow(nullptr, flat_count * sizeof(int));
    if (!inside || !outside) {
        heap_free(inside);
        heap_free(outside);
        goto done;
    }
    for (int i = 0; i < flat_count; i++) {
        int sum = 0;
        double parent_area = 0;
        for (int j = 0; j < flat_count; j++) {
            if (j == i || flats[j].Sign == 0)
                continue;
            if (inside_polygon(points, flats[j].First, flats[j].Count, points[flats[i].First])) {
                sum += flats[j].Sign;
                double area = flats[j].Area < 0 ? -flats[j].Area : flats[j].Area;
                if (flats[i].Parent < 0 || area < parent_area) {
                    flats[i].Parent = j;
                    parent_area = area;
                }
            }
        }
        outside[i] = sum;
        inside[i] = sum + flats[i].Sign;
    }
    for (int i = 0; i < flat_count; i++) {
        if (flats[i].Sign == 0)
            continue;
        BOOL in = winding_filled(tess->Winding, inside[i]), out = winding_filled(tess->Winding, outside[i]);
        flats[i].Kind = in && !out ? KIND_OUTER : !in && out ? KIND_HOLE : KIND_IGNORED;
    }
    heap_free(inside);
    heap_free(outside);

    if (tess->BoundaryOnly) {
        for (int i = 0; i < flat_count; i++) {
            if (flats[i].Kind == KIND_IGNORED)
                continue;
            out_begin(tess, GL_LINE_LOOP);
            for (int j = 0; j < flats[i].Count; j++)
                out_vertex(tess, vertices[flats[i].First + j]->Data);
            out_end(tess);
        }
        goto done;
    }

    /* every hole belongs to the nearest outer contour around it. */
    for (int i = 0; i < flat_count; i++) {
        if (flats[i].Kind != KIND_HOLE)
            continue;
        int owner = flats[i].Parent;
        while (owner >= 0 && flats[owner].Kind != KIND_OUTER)
            owner = flats[owner].Parent;
        flats[i].Owner = owner;
    }

    for (int i = 0; i < flat_count; i++) {
        if (flats[i].Kind != KIND_OUTER)
            continue;
        int node_count = 0, outline = 0;
        /* outer contours run counter clockwise and holes clockwise, so the triangles face along the normal. */
        #define BUILD_RING(index, counter_clockwise, first_node) \
            do { \
                int base = node_count; \
                BOOL forward = (flats[index].Sign > 0) == (counter_clockwise); \
                for (int j = 0; j < flats[index].Count; j++) { \
                    int source = forward ? j : flats[index].Count - 1 - j; \
                    nodes[base + j].Vertex = flats[index].First + source; \
                    nodes[base + j].Next = base + (j + 1) % flats[index].Count; \
                    nodes[base + j].Prev = base + (j + flats[index].Count - 1) % flats[index].Count; \
                    nodes[base + j].Boundary = TRUE; \
                } \
                node_count += flats[index].Count; \
                first_node = base; \
            } while (0)
        BUILD_RING(i, TRUE, outline);
        int remaining = flats[i].Count;

        int hole_count = 0;
        for (int j = 0; j < flat_count; j++)
            if (flats[j].Kind == KIND_HOLE && flats[j].Owner == i)
                order[hole_count++] = j;
        /* the holes furthest to the right go first. */
        for (int a = 1; a < hole_count; a++) {
            int moving = order[a], b = a;
            double key = -1e300;
            for (int k = 0; k < flats[moving].Count; k++)
                if (points[flats[moving].First + k].X > key)
                    key = points[flats[moving].First + k].X;
            while (b > 0) {
                double other = -1e300;
                for (int k = 0; k < flats[order[b - 1]].Count; k++)
                    if (points[flats[order[b - 1]].First + k].X > other)
                        other = points[flats[order[b - 1]].First + k].X;
                if (other >= key)
                    break;
                order[b] = order[b - 1];
                b--;
            }
            order[b] = moving;
        }
        for (int h = 0; h < hole_count; h++) {
            int hole_first;
            BUILD_RING(order[h], FALSE, hole_first);
            if (bridge_hole(nodes, &node_count, points, outline, hole_first, flats[order[h]].Count))
                remaining += flats[order[h]].Count + 2;
        }
        #undef BUILD_RING
        if (!clip_ears(nodes, points, outline, remaining, &triangles))
            break;
    }

    if (triangles.Count) {
        BOOL flags = wants_edge_flags(tess);
        int last = -1;
        out_begin(tess, GL_TRIANGLES);
        for (int i = 0; i < triangles.Count; i++) {
            if (flags && last != triangles.Boundary[i]) {
                last = triangles.Boundary[i];
                out_edge_flag(tess, (GLboolean)last);
            }
            out_vertex(tess, vertices[triangles.Vertex[i]]->Data);
        }
        out_end(tess);
    }

done:
    heap_free(triangles.Vertex);
    heap_free(triangles.Boundary);
    heap_free(points);
    heap_free(vertices);
    heap_free(flats);
    heap_free(nodes);
    heap_free(order);
}
