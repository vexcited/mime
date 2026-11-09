/* What happens to a feature when it is turned on or off. Everything the system does is done through the ops. */
#include "features.h"

uint32_t FeatureEnable(const FEATURE_OPS *ops, const FEATURE_DEF *feature, uint32_t flags)
{
    void *c = ops->Context;
    /* A feature whose files are there is on and nothing is fetched, so a policy that forbids downloads has nothing to refuse. */
    if (ops->Present(c, feature)) {
        if (ops->GetState(c, feature) != FEATURE_STATE_ENABLED)
            ops->SetState(c, feature, FEATURE_STATE_ENABLED, FEATURE_OK, true);
        return FEATURE_OK;
    }
    FEATURE_POLICY policy = ops->Policy(c, feature);
    if (policy == FEATURE_POLICY_DENY)
        return FEATURE_ERROR_ACCESS_DISABLED_BY_POLICY;
    uint32_t available = ops->Available(c, feature);
    if (available != FEATURE_OK)
        return available;
    /* A no is not remembered: the next request asks again. */
    if (policy == FEATURE_POLICY_ASK && (flags & FEATURE_ENABLE_ASK)) {
        uint32_t answer = ops->Consent(c, feature);
        if (answer != FEATURE_OK)
            return answer;
    }
    FEATURE_STATE before = ops->GetState(c, feature);
    ops->Stage(c, feature, FEATURE_STAGE_COLLECTING);
    void *package = nullptr;
    uint32_t error = ops->Acquire(c, feature, &package);
    if (error == FEATURE_OK) {
        ops->Stage(c, feature, FEATURE_STAGE_VERIFYING);
        error = ops->Verify(c, feature, package);
    }
    if (error == FEATURE_OK) {
        ops->Stage(c, feature, FEATURE_STAGE_INSTALLING);
        error = ops->Install(c, feature, package);
    }
    if (package)
        ops->Discard(c, feature, package);
    /* A handler that ended well but left no files did not install the feature. */
    if (error == FEATURE_OK && !ops->Present(c, feature))
        error = FEATURE_ERROR_INSTALL_FAILURE;
    if (error == FEATURE_OK)
        ops->SetState(c, feature, FEATURE_STATE_ENABLED, FEATURE_OK, false);
    else
        ops->SetState(c, feature, before, error, false);
    return error;
}

uint32_t FeatureDisable(const FEATURE_OPS *ops, const FEATURE_DEF *feature)
{
    void *c = ops->Context;
    uint32_t error = FEATURE_OK;
    if (ops->Present(c, feature)) {
        ops->Stage(c, feature, FEATURE_STAGE_REMOVING);
        error = ops->Remove(c, feature);
    }
    if (error == FEATURE_OK)
        ops->SetState(c, feature, FEATURE_STATE_DISABLED, FEATURE_OK, false);
    else
        ops->SetState(c, feature, ops->GetState(c, feature), error, false);
    return error;
}
