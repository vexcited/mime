#include "uianimation_private.h"

/* ------------------------------------------------------------------ the curves of the library, one dimension each */

static void accelerate_decelerate_init(double *p, double initial, double velocity) {
    /* p0 duration, p1 initial, p2 final, p3 cruise speed, p4 p5 the ratios, p6 p7 the two accelerations, p8 the
       vertex of the first parabola, p9 p10 where cruising starts and ends, p11 the first parabola's base, p12 the
       value when cruising starts, p13 the value when it ends. p14 p15 keep the duration and ratio asked for. */
    p[0] = p[14];
    p[5] = p[15];
    double duration = p[0];
    p[1] = initial;
    if (duration == 0.0)
        return;
    double accel = p[4], decel = p[5], final = p[2];
    p[6] = p[7] = p[8] = p[9] = 0.0;
    double distance = final - initial;
    p[11] = initial;
    double cruise = 1.0 - (accel + decel) * 0.5;
    if (accel == 0.0) {
        double speed;
        if (decel <= 0.0) {
            speed = distance / duration;
        } else {
            p[7] = distance / (decel * duration * duration * cruise);
            speed = p[7] * (decel * duration);
        }
        p[3] = speed;
        p[10] = (1.0 - decel) * duration;
    } else {
        double k = 1.0 / (accel * cruise);
        double b = (duration + duration) * velocity - k * distance;
        double discriminant = b * b - (1.0 - k * 0.5) * (velocity * velocity) * (duration * duration * 4.0);
        double cruise_start;
        if (discriminant < 0.0) {
            cruise_start = 0.0;
        } else {
            double root = sqrt(discriminant);
            if (distance < 0.0)
                root = -root;
            double vertex = 0.0, base = initial;
            double a1 = (root - b) / (duration * duration + duration * duration);
            p[6] = a1;
            if (a1 != 0.0) {
                base = initial - velocity * velocity / (a1 + a1);
                vertex = -velocity / a1;
                p[11] = base;
                p[8] = vertex;
            }
            double span = duration - vertex;
            if (decel > 0.0)
                p[7] = (final - base) / (span * span * decel * cruise);
            cruise_start = accel * span + vertex;
            p[9] = cruise_start;
            p[10] = duration - decel * span;
            p[3] = cruise_start * a1 + velocity;
        }
        if (cruise_start <= 0.0) {
            /* already faster than the acceleration would take it: cruise at the speed it has and slow down. */
            p[6] = p[8] = p[9] = 0.0;
            p[11] = initial;
            double ratio = accel == 1.0 ? 0.0 : decel / (1.0 - accel);
            p[5] = ratio;
            double slow, cruise_end;
            if (ratio == 0.0 || distance == 0.0) {
                slow = 0.0;
                cruise_end = distance / velocity;
            } else {
                double q = (1.0 - ratio) / ratio;
                double a2 = (q + 0.5) * (velocity * velocity) / distance;
                slow = velocity / a2;
                p[7] = a2;
                cruise_end = q * (velocity / a2);
            }
            p[10] = cruise_end;
            duration = slow + cruise_end;
            p[3] = velocity;
            p[0] = duration;
        }
    }
    double first = p[9] - p[8];
    double last = duration - p[9];
    p[12] = p[6] * 0.5 * first * first + p[11];
    p[13] = p[2] - p[7] * 0.5 * last * last;
}

static void parabolic_init(double *p, double initial, double velocity) {
    /* p0 duration, p1 the signed acceleration, p2 its size, p3 p4 the start, p5 p6 the end, p7 p8 the vertices of
       the two parabolas, p9 p10 their bases, p11 where one hands over to the other. */
    double a = p[2];
    double v5 = -velocity / a;
    double v6 = p[6] / a;
    double final = p[5];
    p[3] = initial;
    double v9 = v5 * (a * 0.5) * v5;
    double v10 = v6 * (a * 0.5) * v6;
    p[1] = a;
    double v11 = initial - v9;
    p[4] = velocity;
    p[7] = v5;
    p[8] = v6;
    p[9] = v11;
    p[10] = final + v10;
    if (final + v10 > v11) {
        double r = sqrt((final + v10 - v11) / a);
        p[11] = v5 - r;
        if (!(v5 - r < 0.0) && (p[0] = v5 - (r + r) - v6) >= v5 - r)
            return;
        p[11] = v5 + r;
        if (!(v5 + r < 0.0) && (p[0] = v5 + (r + r) - v6) >= v5 + r)
            return;
    }
    double v18 = -v5, v19 = v9 + initial, v20 = -v6, v21 = final - v10;
    p[7] = v18;
    p[8] = v20;
    p[9] = v19;
    p[10] = v21;
    double r = sqrt((v19 - v21) / a);
    p[1] = -a;
    p[11] = v18 - r;
    if (v18 - r < 0.0 || (p[0] = v18 - (r + r) - v20) < v18 - r) {
        p[11] = v18 + r;
        p[0] = v18 + (r + r) - v20;
    }
}

static void smooth_stop_init(double *p, double initial, double velocity) {
    /* p0 duration, p1 the longest it may take, p2 p3 the start, p4 the end, p5 p6 the cubic and square terms. */
    double final = p[4], cubic = 0.0, square;
    p[2] = initial;
    p[3] = velocity;
    double duration;
    if (velocity == 0.0)
        duration = initial == final ? 0.0 : -1.0;
    else
        duration = (final - initial + final - initial) / velocity;
    p[0] = duration;
    if (duration < 0.0 || duration > p[1]) {
        double limit = p[1];
        p[0] = limit;
        if (limit == 0.0) {
            square = 0.0;
        } else {
            double mean = (final - initial) / limit;
            square = (mean * 3.0 - (velocity + velocity)) / limit;
            cubic = (velocity - (mean + mean)) / (limit * limit);
        }
    } else {
        square = duration <= 0.0 ? 0.0 : -velocity / (duration + duration);
    }
    p[5] = cubic;
    p[6] = square;
}

static double bezier_coordinate(double a, double b, double s) {
    double r = 1.0 - s;
    return 3.0 * r * r * s * a + 3.0 * r * s * s * b + s * s * s;
}

static double bezier_slope(double a, double b, double s) {
    double r = 1.0 - s;
    return 3.0 * r * r * a + 6.0 * r * s * (b - a) + 3.0 * s * s * (1.0 - b);
}

static double bezier_solve(const double *p, double x) {
    double s = x;
    for (int i = 0; i < 8; i++) {
        double d = bezier_slope(p[3], p[5], s);
        if (fabs(d) < 1e-9)
            break;
        double next = s - (bezier_coordinate(p[3], p[5], s) - x) / d;
        if (next < 0.0 || next > 1.0)
            break;
        s = next;
    }
    if (fabs(bezier_coordinate(p[3], p[5], s) - x) > 1e-7) {
        double lo = 0.0, hi = 1.0;
        s = x;
        for (int i = 0; i < 60; i++) {
            if (bezier_coordinate(p[3], p[5], s) < x)
                lo = s;
            else
                hi = s;
            s = (lo + hi) * 0.5;
        }
    }
    return s;
}

static void curve_init(UaKind kind, double *p, double initial, double velocity) {
    switch (kind) {
    case UA_CONSTANT:
        p[1] = initial;
        break;
    case UA_DISCRETE:
        p[2] = initial;
        break;
    case UA_LINEAR:
        p[1] = initial;
        if (p[0] != 0.0)
            p[3] = (p[2] - initial) / p[0];
        break;
    case UA_LINEAR_SPEED:
        p[1] = initial;
        p[3] = p[2] <= initial ? -p[4] : p[4];
        p[0] = (p[2] - initial) / p[3];
        break;
    case UA_SINUSOIDAL_VELOCITY:
        p[4] = initial;
        p[1] = velocity * p[2] / UA_TWO_PI;
        break;
    case UA_SINUSOIDAL_RANGE: {
        double amplitude = p[1], center = p[4];
        double clamped = initial;
        if (clamped > center + amplitude)
            clamped = center + amplitude;
        if (clamped < center - amplitude)
            clamped = center - amplitude;
        if (amplitude == 0.0) {
            p[3] = 0.0;
        } else {
            double s = (clamped - center) / amplitude;
            s = s < -1.0 ? -1.0 : s > 1.0 ? 1.0 : s;
            p[3] = asin(s);
            if (p[6] == UI_ANIMATION_SLOPE_DECREASING)
                p[3] = UA_PI - p[3];
        }
        break;
    }
    case UA_ACCELERATE_DECELERATE:
        accelerate_decelerate_init(p, initial, velocity);
        break;
    case UA_REVERSAL:
        p[1] = initial;
        p[2] = velocity;
        break;
    case UA_CUBIC: {
        double duration = p[0];
        p[1] = initial;
        p[2] = velocity;
        if (duration == 0.0) {
            p[5] = p[6] = 0.0;
            break;
        }
        double mean = (p[3] - initial) / duration;
        p[5] = (p[4] + velocity - (mean + mean)) / (duration * duration);
        p[6] = (mean * 3.0 - (velocity + velocity) - p[4]) / duration;
        break;
    }
    case UA_SMOOTH_STOP:
        smooth_stop_init(p, initial, velocity);
        break;
    case UA_PARABOLIC:
        parabolic_init(p, initial, velocity);
        break;
    case UA_CUBIC_BEZIER:
        p[1] = initial;
        break;
    default:
        break;
    }
}

static double sine_value(const double *p, double t) {
    if (p[1] == 0.0)
        return p[4];
    return sin(t * UA_TWO_PI / p[2] + p[3]) * p[1] + p[4];
}

static double curve_value(UaKind kind, const double *p, double t) {
    switch (kind) {
    case UA_INSTANTANEOUS:
        return p[0];
    case UA_CONSTANT:
        return p[1];
    case UA_DISCRETE:
        return p[0] <= t ? p[3] : p[2];
    case UA_LINEAR:
    case UA_LINEAR_SPEED:
        return t * p[3] + p[1];
    case UA_SINUSOIDAL_VELOCITY:
    case UA_SINUSOIDAL_RANGE:
        return sine_value(p, t);
    case UA_ACCELERATE_DECELERATE:
        if (p[9] <= t) {
            if (p[10] < t)
                return p[2] - p[7] * 0.5 * (p[0] - t) * (p[0] - t);
            return (t - p[9]) * p[3] + p[12];
        }
        return p[6] * 0.5 * (t - p[8]) * (t - p[8]) + p[11];
    case UA_REVERSAL:
        return (t - t * t / p[0]) * p[2] + p[1];
    case UA_CUBIC:
        return t * p[5] * (t * t) + t * t * p[6] + t * p[2] + p[1];
    case UA_SMOOTH_STOP:
        return t * p[5] * (t * t) + t * t * p[6] + t * p[3] + p[2];
    case UA_PARABOLIC:
        if (p[11] <= t) {
            double d = t - (p[8] + p[0]);
            return p[10] - p[1] * 0.5 * d * d;
        }
        return p[1] * 0.5 * (t - p[7]) * (t - p[7]) + p[9];
    case UA_CUBIC_BEZIER: {
        if (p[0] <= 0.0 || t >= p[0])
            return p[2];
        double s = bezier_solve(p, t / p[0]);
        return p[1] + (p[2] - p[1]) * bezier_coordinate(p[4], p[6], s);
    }
    default:
        return 0.0;
    }
}

static double curve_velocity(UaKind kind, const double *p, double t) {
    switch (kind) {
    case UA_LINEAR:
    case UA_LINEAR_SPEED:
        return p[3];
    case UA_SINUSOIDAL_VELOCITY:
    case UA_SINUSOIDAL_RANGE:
        if (p[1] == 0.0)
            return 0.0;
        return cos(t * UA_TWO_PI / p[2] + p[3]) * (UA_TWO_PI / p[2] * p[1]);
    case UA_ACCELERATE_DECELERATE:
        if (p[9] <= t)
            return p[10] <= t ? (p[0] - t) * p[7] : p[3];
        return (t - p[8]) * p[6];
    case UA_REVERSAL:
        return p[0] <= t ? -p[2] : (1.0 - (t + t) / p[0]) * p[2];
    case UA_CUBIC:
        if (p[0] <= t)
            return p[4];
        return p[5] * 3.0 * t * t + (p[6] + p[6]) * t + p[2];
    case UA_SMOOTH_STOP:
        if (p[0] <= t)
            return 0.0;
        return p[5] * 3.0 * t * t + (p[6] + p[6]) * t + p[3];
    case UA_PARABOLIC:
        if (t == p[0])
            return p[6];
        if (p[11] <= t)
            return (t - (p[0] + p[8])) * -p[1];
        return (t - p[7]) * p[1];
    case UA_CUBIC_BEZIER: {
        if (p[0] <= 0.0 || t >= p[0])
            return 0.0;
        double s = bezier_solve(p, t / p[0]);
        double dx = bezier_slope(p[3], p[5], s);
        if (dx == 0.0)
            return 0.0;
        return (p[2] - p[1]) / p[0] * bezier_slope(p[4], p[6], s) / dx;
    }
    default:
        return 0.0;
    }
}

static double curve_duration(UaKind kind, const double *p) {
    switch (kind) {
    case UA_INSTANTANEOUS:
        return 0.0;
    case UA_DISCRETE:
        return p[0] + p[1];
    default:
        return p[0];
    }
}

static double curve_final(UaKind kind, const double *p) {
    switch (kind) {
    case UA_INSTANTANEOUS:
        return p[0];
    case UA_CONSTANT:
    case UA_REVERSAL:
        return p[1];
    case UA_DISCRETE:
    case UA_CUBIC:
        return p[3];
    case UA_LINEAR:
    case UA_LINEAR_SPEED:
    case UA_ACCELERATE_DECELERATE:
    case UA_CUBIC_BEZIER:
        return p[2];
    case UA_SINUSOIDAL_VELOCITY:
    case UA_SINUSOIDAL_RANGE:
        return sine_value(p, p[0]);
    case UA_SMOOTH_STOP:
        return p[4];
    case UA_PARABOLIC:
        return p[5];
    default:
        return 0.0;
    }
}

static BOOL curve_takes_duration(UaKind kind) {
    switch (kind) {
    case UA_CONSTANT:
    case UA_LINEAR:
    case UA_SINUSOIDAL_VELOCITY:
    case UA_SINUSOIDAL_RANGE:
    case UA_ACCELERATE_DECELERATE:
    case UA_REVERSAL:
    case UA_CUBIC:
    case UA_CUBIC_BEZIER:
        return TRUE;
    default:
        return FALSE;
    }
}

/* what the duration depends on, as GetDependencies of windows' interpolators reports it. */
static void curve_dependencies(UaKind kind, UI_ANIMATION_DEPENDENCIES *value, UI_ANIMATION_DEPENDENCIES *velocity,
                               UI_ANIMATION_DEPENDENCIES *duration) {
    const UI_ANIMATION_DEPENDENCIES I = UI_ANIMATION_DEPENDENCY_INTERMEDIATE_VALUES,
                                    F = UI_ANIMATION_DEPENDENCY_FINAL_VALUE, V = UI_ANIMATION_DEPENDENCY_FINAL_VELOCITY,
                                    D = UI_ANIMATION_DEPENDENCY_DURATION;
    UI_ANIMATION_DEPENDENCIES a = 0, b = 0, c = 0;
    switch (kind) {
    case UA_CONSTANT:
        a = F | I, c = D;
        break;
    case UA_DISCRETE:
        a = I;
        break;
    case UA_LINEAR:
        a = V | I, c = D | V | I;
        break;
    case UA_LINEAR_SPEED:
        a = D | V | I;
        break;
    case UA_SINUSOIDAL_VELOCITY:
        a = F | I, b = V | F | I, c = D | V | F;
        break;
    case UA_SINUSOIDAL_RANGE:
        a = V | F | I, c = D | V | F;
        break;
    case UA_ACCELERATE_DECELERATE:
        a = D | I, b = D, c = D | F | I;
        break;
    case UA_REVERSAL:
        a = F | I, b = V | I, c = D | I;
        break;
    case UA_CUBIC:
        a = I, b = I, c = D | I;
        break;
    case UA_SMOOTH_STOP:
    case UA_PARABOLIC:
        a = D | I, b = D | I;
        break;
    case UA_CUBIC_BEZIER:
        a = I, c = D | I;
        break;
    default:
        break;
    }
    *value = (UI_ANIMATION_DEPENDENCIES)a;
    *velocity = (UI_ANIMATION_DEPENDENCIES)b;
    *duration = (UI_ANIMATION_DEPENDENCIES)c;
}

/* ------------------------------------------------------------------ interpolation of a transition */

HRESULT UaInterpInit(UaTransition *t, const double *value, const double *velocity) {
    if (t->kind == UA_CUSTOM)
        return t->custom->lpVtbl->SetInitialValueAndVelocity(t->custom, value[0], velocity[0]);
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->SetInitialValueAndVelocity(t->custom2, (double *)value, (double *)velocity,
                                                              t->dimension);
    if (t->shared_speed) {
        double sum = 0.0;
        for (UINT i = 0; i < t->dimension; i++) {
            double d = t->curve[i].p[2] - value[i];
            sum += d * d;
        }
        double duration = sum > 0.0 ? sqrt(sum) / t->speed : 0.0;
        for (UINT i = 0; i < t->dimension; i++) {
            double *p = t->curve[i].p;
            p[0] = duration;
            p[1] = value[i];
            p[3] = duration != 0.0 ? (p[2] - value[i]) / duration : 0.0;
        }
        return S_OK;
    }
    for (UINT i = 0; i < t->dimension; i++)
        curve_init(t->kind, t->curve[i].p, value[i], velocity[i]);
    return S_OK;
}

HRESULT UaInterpSetDuration(UaTransition *t, double duration) {
    if (t->kind == UA_CUSTOM)
        return t->custom->lpVtbl->SetDuration(t->custom, duration);
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->SetDuration(t->custom2, duration);
    if (!curve_takes_duration(t->kind) || t->shared_speed)
        return E_NOTIMPL;
    for (UINT i = 0; i < t->dimension; i++) {
        t->curve[i].p[0] = duration;
        if (t->kind == UA_ACCELERATE_DECELERATE)
            t->curve[i].p[14] = duration;
    }
    return S_OK;
}

HRESULT UaInterpDuration(UaTransition *t, double *duration) {
    *duration = 0.0;
    if (t->kind == UA_CUSTOM)
        return t->custom->lpVtbl->GetDuration(t->custom, duration);
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->GetDuration(t->custom2, duration);
    for (UINT i = 0; i < t->dimension; i++) {
        double d = curve_duration(t->kind, t->curve[i].p);
        if (d > *duration)
            *duration = d;
    }
    return S_OK;
}

HRESULT UaInterpFinal(UaTransition *t, double *value) {
    if (t->kind == UA_CUSTOM)
        return t->custom->lpVtbl->GetFinalValue(t->custom, value);
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->GetFinalValue(t->custom2, value, t->dimension);
    for (UINT i = 0; i < t->dimension; i++)
        value[i] = curve_final(t->kind, t->curve[i].p);
    return S_OK;
}

HRESULT UaInterpValue(UaTransition *t, double offset, double *value) {
    if (t->kind == UA_CUSTOM)
        return t->custom->lpVtbl->InterpolateValue(t->custom, offset, value);
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->InterpolateValue(t->custom2, offset, value, t->dimension);
    for (UINT i = 0; i < t->dimension; i++)
        value[i] = curve_value(t->kind, t->curve[i].p, offset);
    return S_OK;
}

HRESULT UaInterpVelocity(UaTransition *t, double offset, double *velocity) {
    if (t->kind == UA_CUSTOM)
        return t->custom->lpVtbl->InterpolateVelocity(t->custom, offset, velocity);
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->InterpolateVelocity(t->custom2, offset, velocity, t->dimension);
    for (UINT i = 0; i < t->dimension; i++)
        velocity[i] = curve_velocity(t->kind, t->curve[i].p, offset);
    return S_OK;
}

static void dependencies(UaTransition *t, UI_ANIMATION_DEPENDENCIES *value, UI_ANIMATION_DEPENDENCIES *velocity,
                         UI_ANIMATION_DEPENDENCIES *duration) {
    *value = *velocity = *duration = UI_ANIMATION_DEPENDENCY_NONE;
    HRESULT hr = S_OK;
    if (t->kind == UA_CUSTOM)
        hr = t->custom->lpVtbl->GetDependencies(t->custom, value, velocity, duration);
    else if (t->kind == UA_CUSTOM2)
        hr = t->custom2->lpVtbl->GetDependencies(t->custom2, value, velocity, duration);
    else
        curve_dependencies(t->kind, value, velocity, duration);
    if (FAILED(hr)) {
        /* an interpolator that cannot say is taken to depend on everything. */
        *value = *velocity = (UI_ANIMATION_DEPENDENCIES)0xF;
    }
}

/* the pieces of a curve for direct composition, from begin on. a piece it has no closed form for is sampled. */
HRESULT UaInterpPrimitive(UaTransition *t, IUIAnimationPrimitiveInterpolation *primitive, double begin) {
    if (t->kind == UA_CUSTOM2)
        return t->custom2->lpVtbl->GetPrimitiveInterpolation(t->custom2, primitive, t->dimension);
    double duration;
    HRESULT hr = UaInterpDuration(t, &duration);
    if (FAILED(hr))
        return hr;
    for (UINT d = 0; d < t->dimension; d++) {
        const double *p = t->kind == UA_CUSTOM ? nullptr : t->curve[d].p;
        switch (t->kind) {
        case UA_LINEAR:
        case UA_LINEAR_SPEED:
            hr = primitive->lpVtbl->AddCubic(primitive, d, begin, (float)p[1], (float)p[3], 0.0f, 0.0f);
            break;
        case UA_CUBIC:
            hr = primitive->lpVtbl->AddCubic(primitive, d, begin, (float)p[1], (float)p[2], (float)p[6], (float)p[5]);
            break;
        case UA_SMOOTH_STOP:
            hr = primitive->lpVtbl->AddCubic(primitive, d, begin, (float)p[2], (float)p[3], (float)p[6], (float)p[5]);
            break;
        case UA_REVERSAL:
            hr = primitive->lpVtbl->AddCubic(primitive, d, begin, (float)p[1], (float)p[2],
                                             p[0] != 0.0 ? (float)(-p[2] / p[0]) : 0.0f, 0.0f);
            break;
        case UA_SINUSOIDAL_VELOCITY:
        case UA_SINUSOIDAL_RANGE:
            hr = primitive->lpVtbl->AddSinusoidal(primitive, d, begin, (float)p[4], (float)p[1], (float)(1.0 / p[2]),
                                                  (float)(p[3] * 180.0 / UA_PI));
            break;
        case UA_DISCRETE:
            hr = primitive->lpVtbl->AddCubic(primitive, d, begin, (float)p[2], 0.0f, 0.0f, 0.0f);
            if (SUCCEEDED(hr))
                hr = primitive->lpVtbl->AddCubic(primitive, d, begin + p[0], (float)p[3], 0.0f, 0.0f, 0.0f);
            break;
        case UA_INSTANTANEOUS:
        case UA_CONSTANT:
            hr = primitive->lpVtbl->AddCubic(primitive, d, begin, (float)curve_final(t->kind, p), 0.0f, 0.0f, 0.0f);
            break;
        default: {
            const int steps = 16;
            double values[UA_MAX_DIMENSION], next[UA_MAX_DIMENSION];
            double h = duration / steps;
            hr = UaInterpValue(t, 0.0, values);
            for (int i = 0; i < steps && SUCCEEDED(hr) && h > 0.0; i++) {
                hr = UaInterpValue(t, h * (i + 1), next);
                if (SUCCEEDED(hr))
                    hr = primitive->lpVtbl->AddCubic(primitive, d, begin + h * i, (float)values[d],
                                                     (float)((next[d] - values[d]) / h), 0.0f, 0.0f);
                values[d] = next[d];
            }
            break;
        }
        }
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ transition objects */

static const IUIAnimationTransitionVtbl g_transition_vtbl;
static const IUIAnimationTransition2Vtbl g_transition2_vtbl;

UaTransition *UaTransitionFrom(const void *iface, int version) {
    if (!iface)
        return nullptr;
    const void *vtbl = *(const void *const *)iface;
    if (vtbl != (version == 2 ? (const void *)&g_transition2_vtbl : (const void *)&g_transition_vtbl))
        return nullptr;
    return (UaTransition *)iface;
}

static HRESULT STDMETHODCALLTYPE transition_query(IUnknown *iface, REFIID iid, void **out) {
    UaTransition *t = (UaTransition *)iface;
    return UaQuery(iface, t->version == 2 ? &IID_IUIAnimationTransition2 : &IID_IUIAnimationTransition, iid, out);
}

static ULONG STDMETHODCALLTYPE transition_addref(IUnknown *iface) {
    return InterlockedIncrement(&((UaTransition *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE transition_release(IUnknown *iface) {
    UaTransition *t = (UaTransition *)iface;
    ULONG refs = InterlockedDecrement(&t->refs);
    if (!refs) {
        if (t->custom)
            t->custom->lpVtbl->Release(t->custom);
        if (t->custom2)
            t->custom2->lpVtbl->Release(t->custom2);
        UaFree(t);
        UaObjectCountChange(-1);
    }
    return refs;
}

static HRESULT transition_check(UaTransition *t, BOOL sealing) {
    UaStoryboard *storyboard = t->storyboard;
    if (!storyboard)
        return S_OK;
    if (!storyboard->manager || storyboard->manager->shutdown)
        return UI_E_SHUTDOWN_CALLED;
    if (UaManagerWrongThread(storyboard->manager))
        return UI_E_WRONG_THREAD;
    if (UaManagerInCallback(storyboard->manager, UA_CALLBACK_VARIABLE | UA_CALLBACK_STORYBOARD | UA_CALLBACK_UPDATED))
        return UI_E_ILLEGAL_REENTRANCY;
    BOOL active =
        storyboard->status != UI_ANIMATION_STORYBOARD_BUILDING && storyboard->status != UI_ANIMATION_STORYBOARD_READY;
    if (active)
        return sealing ? UI_E_OBJECT_SEALED : UI_E_STORYBOARD_ACTIVE;
    return S_OK;
}

static HRESULT set_initial(UaTransition *t, const double *values, UINT count, BOOL velocity) {
    if (count != t->dimension)
        return UI_E_INVALID_DIMENSION;
    HRESULT hr = transition_check(t, TRUE);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < count; i++)
        (velocity ? t->velocity : t->value)[i] = values[i];
    if (velocity)
        t->velocity_set = TRUE;
    else
        t->value_set = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transition_set_value(IUIAnimationTransition *iface, double value) {
    UaTransition *t = (UaTransition *)iface;
    double values[UA_MAX_DIMENSION];
    for (UINT i = 0; i < t->dimension; i++)
        values[i] = value;
    return set_initial(t, values, t->dimension, FALSE);
}

static HRESULT STDMETHODCALLTYPE transition_set_velocity(IUIAnimationTransition *iface, double velocity) {
    UaTransition *t = (UaTransition *)iface;
    double values[UA_MAX_DIMENSION];
    for (UINT i = 0; i < t->dimension; i++)
        values[i] = velocity;
    return set_initial(t, values, t->dimension, TRUE);
}

/* the duration is known once what it depends on is: either set on the transition, or given by scheduling. */
static HRESULT known_duration(UaTransition *t, double *duration) {
    *duration = UA_NONE;
    if (t->item && t->item->resolved) {
        *duration = t->item->finish - t->item->begin;
        return S_OK;
    }
    UI_ANIMATION_DEPENDENCIES value, velocity, ignored;
    dependencies(t, &value, &velocity, &ignored);
    if ((value & UI_ANIMATION_DEPENDENCY_DURATION) && !t->value_set)
        return S_FALSE;
    if ((velocity & UI_ANIMATION_DEPENDENCY_DURATION) && !t->velocity_set)
        return S_FALSE;
    if ((value & UI_ANIMATION_DEPENDENCY_DURATION) || (velocity & UI_ANIMATION_DEPENDENCY_DURATION)) {
        double v[UA_MAX_DIMENSION] = {0}, s[UA_MAX_DIMENSION] = {0};
        for (UINT i = 0; i < t->dimension; i++) {
            v[i] = t->value_set ? t->value[i] : 0.0;
            s[i] = t->velocity_set ? t->velocity[i] : 0.0;
        }
        HRESULT hr = UaInterpInit(t, v, s);
        if (FAILED(hr))
            return hr;
    }
    HRESULT hr = UaInterpDuration(t, duration);
    if (FAILED(hr))
        return hr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transition_is_duration_known(IUIAnimationTransition *iface) {
    UaTransition *t = (UaTransition *)iface;
    HRESULT hr = transition_check(t, FALSE);
    if (FAILED(hr))
        return hr;
    double duration;
    return known_duration(t, &duration);
}

static HRESULT STDMETHODCALLTYPE transition_get_duration(IUIAnimationTransition *iface, double *duration) {
    UaTransition *t = (UaTransition *)iface;
    if (!duration)
        return E_POINTER;
    *duration = 0.0;
    HRESULT hr = transition_check(t, FALSE);
    if (FAILED(hr))
        return hr;
    double d;
    hr = known_duration(t, &d);
    if (FAILED(hr))
        return hr;
    if (hr == S_FALSE)
        return UI_E_VALUE_NOT_DETERMINED;
    *duration = d;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transition2_get_dimension(IUIAnimationTransition2 *iface, UINT *dimension) {
    if (!dimension)
        return E_POINTER;
    *dimension = ((UaTransition *)iface)->dimension;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE transition2_set_vector_value(IUIAnimationTransition2 *iface, double *value,
                                                              UINT count) {
    if (!value)
        return E_POINTER;
    return set_initial((UaTransition *)iface, value, count, FALSE);
}

static HRESULT STDMETHODCALLTYPE transition2_set_vector_velocity(IUIAnimationTransition2 *iface, double *velocity,
                                                                 UINT count) {
    if (!velocity)
        return E_POINTER;
    return set_initial((UaTransition *)iface, velocity, count, TRUE);
}

static const IUIAnimationTransitionVtbl g_transition_vtbl = {
    VTBL_FN(transition_query), VTBL_FN(transition_addref),   VTBL_FN(transition_release), transition_set_value,
    transition_set_velocity,   transition_is_duration_known, transition_get_duration,
};

static const IUIAnimationTransition2Vtbl g_transition2_vtbl = {
    VTBL_FN(transition_query),        VTBL_FN(transition_addref),      VTBL_FN(transition_release),
    transition2_get_dimension,        VTBL_FN(transition_set_value),   transition2_set_vector_value,
    VTBL_FN(transition_set_velocity), transition2_set_vector_velocity, VTBL_FN(transition_is_duration_known),
    VTBL_FN(transition_get_duration),
};

static UaTransition *transition_new(int version, UaKind kind, UINT dimension) {
    UaTransition *t = UaAlloc(sizeof(*t));
    if (!t)
        return nullptr;
    t->lpVtbl = version == 2 ? (const void *)&g_transition2_vtbl : (const void *)&g_transition_vtbl;
    t->refs = 1;
    t->version = version;
    t->kind = kind;
    t->dimension = dimension;
    UaObjectCountChange(1);
    return t;
}

/* a, b and c carry the arguments of the library call, per dimension where they are vectors. */
HRESULT UaTransitionCreateLibrary(int version, UaKind kind, UINT dimension, const double *a, const double *b,
                                  const double *c, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!dimension || dimension > UA_MAX_DIMENSION)
        return E_INVALIDARG;
    UaTransition *t = transition_new(version, kind, dimension);
    if (!t)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < dimension; i++) {
        double *p = t->curve[i].p;
        switch (kind) {
        case UA_INSTANTANEOUS:
            p[0] = b[i];
            break;
        case UA_CONSTANT:
            p[0] = a[0];
            break;
        case UA_DISCRETE:
            p[0] = a[0], p[3] = b[i], p[1] = c[0];
            break;
        case UA_LINEAR:
            p[0] = a[0], p[2] = b[i];
            break;
        case UA_LINEAR_SPEED:
            p[4] = a[0], p[2] = b[i];
            break;
        case UA_SINUSOIDAL_VELOCITY:
            p[0] = a[0], p[2] = a[1];
            break;
        case UA_SINUSOIDAL_RANGE:
            p[0] = a[0], p[2] = a[3], p[6] = a[4];
            p[4] = (a[1] + a[2]) * 0.5;
            p[1] = fabs(a[2] - a[1]) * 0.5;
            break;
        case UA_ACCELERATE_DECELERATE:
            p[0] = p[14] = a[0], p[2] = b[i], p[4] = a[1], p[15] = p[5] = a[2];
            break;
        case UA_REVERSAL:
            p[0] = a[0];
            break;
        case UA_CUBIC:
            p[0] = a[0], p[3] = b[i], p[4] = c[i];
            break;
        case UA_SMOOTH_STOP:
            p[1] = a[0], p[4] = b[i];
            break;
        case UA_PARABOLIC:
            p[5] = b[i], p[6] = c[i], p[2] = fabs(a[0]);
            break;
        case UA_CUBIC_BEZIER:
            p[0] = a[0], p[2] = b[i], p[3] = a[1], p[4] = a[2], p[5] = a[3], p[6] = a[4];
            break;
        default:
            break;
        }
    }
    if (kind == UA_LINEAR_SPEED && dimension > 1) {
        t->shared_speed = TRUE;
        t->speed = a[0];
    }
    *out = t;
    return S_OK;
}

/* ------------------------------------------------------------------ the transition library */

typedef struct UaLibrary {
    const void *lpVtbl;
    LONG refs;
    int version;
} UaLibrary;

static HRESULT STDMETHODCALLTYPE library_query(IUnknown *iface, REFIID iid, void **out) {
    UaLibrary *library = (UaLibrary *)iface;
    return UaQuery(iface,
                   library->version == 2 ? &IID_IUIAnimationTransitionLibrary2 : &IID_IUIAnimationTransitionLibrary,
                   iid, out);
}

static ULONG STDMETHODCALLTYPE library_addref(IUnknown *iface) {
    return InterlockedIncrement(&((UaLibrary *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE library_release(IUnknown *iface) {
    UaLibrary *library = (UaLibrary *)iface;
    ULONG refs = InterlockedDecrement(&library->refs);
    if (!refs) {
        UaFree(library);
        UaObjectCountChange(-1);
    }
    return refs;
}

#define LIB(iface) (((UaLibrary *)(iface))->version)

static HRESULT STDMETHODCALLTYPE lib_instantaneous(IUnknown *iface, double final, void **out) {
    return UaTransitionCreateLibrary(LIB(iface), UA_INSTANTANEOUS, 1, nullptr, &final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_constant(IUnknown *iface, double duration, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_CONSTANT, 1, &duration, nullptr, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_discrete(IUnknown *iface, double delay, double final, double hold, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (delay < 0.0 || hold < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_DISCRETE, 1, &delay, &final, &hold, out);
}

static HRESULT STDMETHODCALLTYPE lib_linear(IUnknown *iface, double duration, double final, void **out) {
    return UaTransitionCreateLibrary(LIB(iface), UA_LINEAR, 1, &duration, &final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_linear_speed(IUnknown *iface, double speed, double final, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (speed <= 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_LINEAR_SPEED, 1, &speed, &final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_sinusoidal_velocity(IUnknown *iface, double duration, double period, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0 || period <= 0.0)
        return E_INVALIDARG;
    double a[2] = {duration, period};
    return UaTransitionCreateLibrary(LIB(iface), UA_SINUSOIDAL_VELOCITY, 1, a, nullptr, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_sinusoidal_range(IUnknown *iface, double duration, double minimum, double maximum,
                                                      double period, UI_ANIMATION_SLOPE slope, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0 || period <= 0.0 || (UINT)slope > 1)
        return E_INVALIDARG;
    double a[5] = {duration, minimum, maximum, period, (double)slope};
    return UaTransitionCreateLibrary(LIB(iface), UA_SINUSOIDAL_RANGE, 1, a, nullptr, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_accelerate_decelerate(IUnknown *iface, double duration, double final, double accel,
                                                           double decel, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0 || accel < 0.0 || decel < 0.0 || accel + decel > 1.0)
        return E_INVALIDARG;
    double a[3] = {duration, accel, decel};
    return UaTransitionCreateLibrary(LIB(iface), UA_ACCELERATE_DECELERATE, 1, a, &final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_reversal(IUnknown *iface, double duration, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_REVERSAL, 1, &duration, nullptr, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_cubic(IUnknown *iface, double duration, double final, double velocity,
                                           void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_CUBIC, 1, &duration, &final, &velocity, out);
}

static HRESULT STDMETHODCALLTYPE lib_smooth_stop(IUnknown *iface, double maximum, double final, void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (maximum < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_SMOOTH_STOP, 1, &maximum, &final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib_parabolic(IUnknown *iface, double final, double velocity, double acceleration,
                                               void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (acceleration == 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(LIB(iface), UA_PARABOLIC, 1, &acceleration, &final, &velocity, out);
}

static HRESULT STDMETHODCALLTYPE lib2_instantaneous_vector(IUnknown *iface, double *final, UINT count, void **out) {
    if (!out || !final)
        return E_POINTER;
    *out = nullptr;
    return UaTransitionCreateLibrary(2, UA_INSTANTANEOUS, count, nullptr, final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib2_discrete_vector(IUnknown *iface, double delay, double *final, UINT count,
                                                      double hold, void **out) {
    if (!out || !final)
        return E_POINTER;
    *out = nullptr;
    if (delay < 0.0 || hold < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(2, UA_DISCRETE, count, &delay, final, &hold, out);
}

static HRESULT STDMETHODCALLTYPE lib2_linear_vector(IUnknown *iface, double duration, double *final, UINT count,
                                                    void **out) {
    if (!out || !final)
        return E_POINTER;
    *out = nullptr;
    return UaTransitionCreateLibrary(2, UA_LINEAR, count, &duration, final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib2_linear_vector_speed(IUnknown *iface, double speed, double *final, UINT count,
                                                          void **out) {
    if (!out || !final)
        return E_POINTER;
    *out = nullptr;
    if (speed <= 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(2, UA_LINEAR_SPEED, count, &speed, final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib2_cubic_vector(IUnknown *iface, double duration, double *final, double *velocity,
                                                   UINT count, void **out) {
    if (!out || !final || !velocity)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0)
        return E_INVALIDARG;
    return UaTransitionCreateLibrary(2, UA_CUBIC, count, &duration, final, velocity, out);
}

static HRESULT bezier(int version, double duration, double *final, UINT count, double x1, double y1, double x2,
                      double y2, void **out) {
    if (!out || !final)
        return E_POINTER;
    *out = nullptr;
    if (duration < 0.0 || x1 < 0.0 || x1 > 1.0 || x2 < 0.0 || x2 > 1.0)
        return E_INVALIDARG;
    double a[5] = {duration, x1, y1, x2, y2};
    return UaTransitionCreateLibrary(version, UA_CUBIC_BEZIER, count, a, final, nullptr, out);
}

static HRESULT STDMETHODCALLTYPE lib2_bezier(IUnknown *iface, double duration, double final, double x1, double y1,
                                             double x2, double y2, void **out) {
    return bezier(2, duration, &final, 1, x1, y1, x2, y2, out);
}

static HRESULT STDMETHODCALLTYPE lib2_bezier_vector(IUnknown *iface, double duration, double *final, UINT count,
                                                    double x1, double y1, double x2, double y2, void **out) {
    return bezier(2, duration, final, count, x1, y1, x2, y2, out);
}

static const IUIAnimationTransitionLibraryVtbl g_library_vtbl = {
    VTBL_FN(library_query),
    VTBL_FN(library_addref),
    VTBL_FN(library_release),
    VTBL_FN(lib_instantaneous),
    VTBL_FN(lib_constant),
    VTBL_FN(lib_discrete),
    VTBL_FN(lib_linear),
    VTBL_FN(lib_linear_speed),
    VTBL_FN(lib_sinusoidal_velocity),
    VTBL_FN(lib_sinusoidal_range),
    VTBL_FN(lib_accelerate_decelerate),
    VTBL_FN(lib_reversal),
    VTBL_FN(lib_cubic),
    VTBL_FN(lib_smooth_stop),
    VTBL_FN(lib_parabolic),
};

static const IUIAnimationTransitionLibrary2Vtbl g_library2_vtbl = {
    VTBL_FN(library_query),
    VTBL_FN(library_addref),
    VTBL_FN(library_release),
    VTBL_FN(lib_instantaneous),
    VTBL_FN(lib2_instantaneous_vector),
    VTBL_FN(lib_constant),
    VTBL_FN(lib_discrete),
    VTBL_FN(lib2_discrete_vector),
    VTBL_FN(lib_linear),
    VTBL_FN(lib2_linear_vector),
    VTBL_FN(lib_linear_speed),
    VTBL_FN(lib2_linear_vector_speed),
    VTBL_FN(lib_sinusoidal_velocity),
    VTBL_FN(lib_sinusoidal_range),
    VTBL_FN(lib_accelerate_decelerate),
    VTBL_FN(lib_reversal),
    VTBL_FN(lib_cubic),
    VTBL_FN(lib2_cubic_vector),
    VTBL_FN(lib_smooth_stop),
    VTBL_FN(lib_parabolic),
    VTBL_FN(lib2_bezier),
    VTBL_FN(lib2_bezier_vector),
};

HRESULT UaLibraryCreate(int version, REFIID iid, void **out) {
    UaLibrary *library = UaAlloc(sizeof(*library));
    if (!library)
        return E_OUTOFMEMORY;
    library->lpVtbl = version == 2 ? (const void *)&g_library2_vtbl : (const void *)&g_library_vtbl;
    library->refs = 1;
    library->version = version;
    UaObjectCountChange(1);
    HRESULT hr = library_query((IUnknown *)library, iid, out);
    library_release((IUnknown *)library);
    return hr;
}

/* ------------------------------------------------------------------ the transition factory */

static HRESULT STDMETHODCALLTYPE factory_query(IUnknown *iface, REFIID iid, void **out) {
    UaLibrary *factory = (UaLibrary *)iface;
    return UaQuery(iface,
                   factory->version == 2 ? &IID_IUIAnimationTransitionFactory2 : &IID_IUIAnimationTransitionFactory,
                   iid, out);
}

static HRESULT STDMETHODCALLTYPE factory_create_transition(IUIAnimationTransitionFactory *iface,
                                                           IUIAnimationInterpolator *interpolator,
                                                           IUIAnimationTransition **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!interpolator)
        return E_POINTER;
    UaTransition *t = transition_new(1, UA_CUSTOM, 1);
    if (!t)
        return E_OUTOFMEMORY;
    t->custom = interpolator;
    interpolator->lpVtbl->AddRef(interpolator);
    *out = (IUIAnimationTransition *)t;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory2_create_transition(IUIAnimationTransitionFactory2 *iface,
                                                            IUIAnimationInterpolator2 *interpolator,
                                                            IUIAnimationTransition2 **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!interpolator)
        return E_POINTER;
    UINT dimension = 0;
    HRESULT hr = interpolator->lpVtbl->GetDimension(interpolator, &dimension);
    if (FAILED(hr))
        return hr;
    if (!dimension || dimension > UA_MAX_DIMENSION)
        return UI_E_INVALID_DIMENSION;
    UaTransition *t = transition_new(2, UA_CUSTOM2, dimension);
    if (!t)
        return E_OUTOFMEMORY;
    t->custom2 = interpolator;
    interpolator->lpVtbl->AddRef(interpolator);
    *out = (IUIAnimationTransition2 *)t;
    return S_OK;
}

static const IUIAnimationTransitionFactoryVtbl g_factory_vtbl = {
    VTBL_FN(factory_query),
    VTBL_FN(library_addref),
    VTBL_FN(library_release),
    factory_create_transition,
};

static const IUIAnimationTransitionFactory2Vtbl g_factory2_vtbl = {
    VTBL_FN(factory_query),
    VTBL_FN(library_addref),
    VTBL_FN(library_release),
    factory2_create_transition,
};

HRESULT UaFactoryCreate(int version, REFIID iid, void **out) {
    UaLibrary *factory = UaAlloc(sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;
    factory->lpVtbl = version == 2 ? (const void *)&g_factory2_vtbl : (const void *)&g_factory_vtbl;
    factory->refs = 1;
    factory->version = version;
    UaObjectCountChange(1);
    HRESULT hr = factory_query((IUnknown *)factory, iid, out);
    library_release((IUnknown *)factory);
    return hr;
}
