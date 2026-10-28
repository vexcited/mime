/*
 * the Condition table. when costing ends, each row whose condition holds sets the level of its feature, and a level of
 * zero turns the feature off. a later row wins over an earlier one.
 */
#include "msi_private.h"

int MimeMsiFeatureLevel(MIME_MSI_PACKAGE *package, PCWSTR name, int level)
{
    for (ULONG i = 0; i < package->FeatureCount; i++)
        if (package->Features[i].LevelSet && !wcscmp(package->Features[i].Name, name))
            return package->Features[i].Level;
    return level;
}

UINT MimeMsiProcessConditions(MIME_MSI_PACKAGE *package, BOOL *changed)
{
    *changed = FALSE;
    UINT error = MimeMsiLoadFeatures(package);
    if (error)
        return error;
    for (ULONG i = 0; i < package->FeatureCount; i++)
        package->Features[i].LevelSet = FALSE;
    MIME_MSI_TABLE *table;
    error = MimeMsiLoadTable(package->Database, L"Condition", &table);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        return ERROR_SUCCESS;
    if (error)
        return error;
    ULONG feature = MimeMsiColumn(table, L"Feature_", TRUE);
    ULONG level = MimeMsiColumn(table, L"Level", FALSE);
    ULONG condition = MimeMsiColumn(table, L"Condition", TRUE);
    if (feature == UINT32_MAX || level == UINT32_MAX || condition == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR name = MimeMsiText(package->Database, table, row, feature);
        PCWSTR expression = MimeMsiText(package->Database, table, row, condition);
        int value = MimeMsiInteger(table, row, level);
        if (!name || !expression || value == MSI_NULL_INTEGER || value < 0)
            return ERROR_INSTALL_PACKAGE_INVALID;
        MIME_MSI_FEATURE *target = nullptr;
        for (ULONG i = 0; i < package->FeatureCount; i++)
            if (!wcscmp(package->Features[i].Name, name))
                target = &package->Features[i];
        if (!target)
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (MimeMsiEvaluateCondition(package, expression) != MSICONDITION_TRUE)
            continue;
        target->Level = value;
        target->LevelSet = *changed = TRUE;
    }
    return ERROR_SUCCESS;
}
