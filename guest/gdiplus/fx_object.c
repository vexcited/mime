/*
 * the effect objects: making one from the id of its effect, and reading and writing its parameters.
 */
#include "fx_core.h"

static const FxClass *const classes[] = {
    &fx_class_blur,  &fx_class_sharpen, &fx_class_matrix,  &fx_class_lookup,  &fx_class_brightness, &fx_class_hsl,
    &fx_class_levels, &fx_class_tint,   &fx_class_balance, &fx_class_redeye, &fx_class_curve,
};

FxEffect *fx_effect_cast(const void *p)
{
    const FxEffect *e = p;
    return e && e->tag == GP_TAG_EFFECT ? (FxEffect *)e : nullptr;
}

/* the library does not have to be started to make an effect. */
WINGDIPAPI GpStatus WINAPI GdipCreateEffect(const GUID guid, CGpEffect **effect)
{
    if (!effect)
        return InvalidParameter;
    const FxClass *cls = nullptr;
    for (UINT i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        if (IsEqualGUID(&guid, classes[i]->id)) {
            cls = classes[i];
            break;
        }
    }
    if (!cls)
        return GenericError;
    FxEffect *e = gp_alloc_zero(sizeof(FxEffect) + cls->state_size);
    if (!e)
        return OutOfMemory;
    e->tag = GP_TAG_EFFECT;
    e->cls = cls;
    if (cls->init)
        cls->init(FX_STATE(e));
    *effect = e;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteEffect(CGpEffect *effect)
{
    FxEffect *e = fx_effect_cast(effect);
    if (!e)
        return InvalidParameter;
    if (e->cls->finish)
        e->cls->finish(FX_STATE(e));
    e->tag = 0;
    gp_free(e);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetEffectParameterSize(CGpEffect *effect, UINT *size)
{
    FxEffect *e = fx_effect_cast(effect);
    if (!e || !size)
        return InvalidParameter;
    *size = e->cls->param_size(FX_STATE(e));
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetEffectParameters(CGpEffect *effect, const VOID *params, UINT size)
{
    FxEffect *e = fx_effect_cast(effect);
    if (!e || !params)
        return InvalidParameter;
    return e->cls->set(FX_STATE(e), params, size);
}

WINGDIPAPI GpStatus WINAPI GdipGetEffectParameters(CGpEffect *effect, UINT *size, VOID *params)
{
    FxEffect *e = fx_effect_cast(effect);
    if (!e || !size || !params)
        return InvalidParameter;
    UINT need = e->cls->param_size(FX_STATE(e));
    if (*size < need)
        return InvalidParameter;
    e->cls->get(FX_STATE(e), params);
    /* only the color matrix and the lookup tables say how much they wrote. */
    if (e->cls->flags & FX_REPORTS_SIZE)
        *size = need;
    return Ok;
}
