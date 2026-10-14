#include "msi_private.h"

enum { COND_END, COND_WORD, COND_NUMBER, COND_STRING, COND_OPERATOR };
typedef struct MSI_CONDITION_PARSER {
    MIME_MSI_PACKAGE *Package;
    PCWSTR Cursor;
    WCHAR Token[512];
    ULONG Kind, Depth;
    BOOL Error;
} MSI_CONDITION_PARSER;

typedef struct MSI_CONDITION_VALUE {
    PWSTR Text;
    int Number;
    BOOL Integer;
} MSI_CONDITION_VALUE;

static BOOL word_character(WCHAR ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
           (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'.';
}

static void next_condition(MSI_CONDITION_PARSER *parser)
{
    PCWSTR p = parser->Cursor;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n')
        p++;
    ULONG length = 0;
    parser->Kind = COND_END;
    if (*p == L'"') {
        parser->Kind = COND_STRING;
        p++;
        while (*p && *p != L'"' && length < RTL_NUMBER_OF(parser->Token) - 1)
            parser->Token[length++] = *p++;
        if (*p != L'"')
            parser->Error = TRUE;
        else
            p++;
    } else if ((*p >= L'0' && *p <= L'9') || *p == L'-') {
        parser->Kind = COND_NUMBER;
        parser->Token[length++] = *p++;
        while (*p >= L'0' && *p <= L'9' && length < RTL_NUMBER_OF(parser->Token) - 1)
            parser->Token[length++] = *p++;
    } else if (word_character(*p) || *p == L'%' || *p == L'$' || *p == L'?' || *p == L'&' || *p == L'!') {
        parser->Kind = COND_WORD;
        parser->Token[length++] = *p++;
        while (word_character(*p) && length < RTL_NUMBER_OF(parser->Token) - 1)
            parser->Token[length++] = *p++;
    } else if (*p) {
        parser->Kind = COND_OPERATOR;
        if (*p == L'~')
            parser->Token[length++] = *p++;
        WCHAR first = *p;
        if (*p)
            parser->Token[length++] = *p++;
        if ((first == L'<' || first == L'>') && (*p == L'<' || *p == L'>' || *p == L'='))
            parser->Token[length++] = *p++;
    }
    parser->Token[length] = 0;
    parser->Cursor = p;
    if (length == RTL_NUMBER_OF(parser->Token) - 1 && word_character(*p))
        parser->Error = TRUE;
}

static BOOL number_value(PCWSTR text, int *number)
{
    BOOL negative = *text == L'-';
    if (negative)
        text++;
    if (!*text)
        return FALSE;
    ULONG limit = negative ? 0x80000000u : 0x7fffffffu, value = 0;
    while (*text) {
        if (*text < L'0' || *text > L'9' || value > (limit - (*text - L'0')) / 10)
            return FALSE;
        value = value * 10 + *text++ - L'0';
    }
    *number = negative ? (int)(0u - value) : (int)value;
    return TRUE;
}

static MSI_CONDITION_VALUE condition_value(MSI_CONDITION_PARSER *parser)
{
    MSI_CONDITION_VALUE value = {0};
    if (parser->Kind == COND_NUMBER) {
        value.Integer = TRUE;
        if (!number_value(parser->Token, &value.Number))
            parser->Error = TRUE;
    } else if (parser->Kind == COND_STRING)
        value.Text = MimeMsiDuplicate(parser->Token);
    else if (parser->Kind == COND_WORD) {
        WCHAR prefix = parser->Token[0];
        if (!_wcsicmp(parser->Token, L"AND") || !_wcsicmp(parser->Token, L"OR") ||
            !_wcsicmp(parser->Token, L"XOR") || !_wcsicmp(parser->Token, L"EQV") ||
            !_wcsicmp(parser->Token, L"IMP") || !_wcsicmp(parser->Token, L"NOT"))
            parser->Error = TRUE;
        else if (prefix == L'&' || prefix == L'!') {
            INSTALLSTATE installed, action;
            value.Integer = TRUE;
            if (!parser->Package || !parser->Token[1] ||
                MimeMsiFeatureState(parser->Package, parser->Token + 1, &installed, &action))
                parser->Error = TRUE;
            else
                value.Number = prefix == L'&' ? action : installed;
        } else if (prefix == L'$' || prefix == L'?')
            parser->Error = TRUE;
        else if (prefix == L'%') {
            DWORD length = GetEnvironmentVariableW(parser->Token + 1, NULL, 0);
            if (length) {
                value.Text = MimeMsiAllocate((SIZE_T)length * sizeof(WCHAR));
                if (value.Text && GetEnvironmentVariableW(parser->Token + 1, value.Text, length) >= length)
                    parser->Error = TRUE;
            } else
                value.Text = MimeMsiDuplicate(L"");
        } else
            value.Text = MimeMsiDuplicate(parser->Package ? MimeMsiGetProperty(parser->Package, parser->Token) : L"");
    } else
        parser->Error = TRUE;
    if (!value.Integer && !value.Text)
        parser->Error = TRUE;
    next_condition(parser);
    return value;
}

enum { CMP_EQ, CMP_NE, CMP_GT, CMP_GE, CMP_LT, CMP_LE, CMP_CONTAINS, CMP_PREFIX, CMP_SUFFIX, CMP_NONE };
static ULONG comparison(PCWSTR token, BOOL *ignore_case)
{
    static const PCWSTR operators[] = {L"=", L"<>", L">", L">=", L"<", L"<=", L"><", L"<<", L">>"};
    *ignore_case = *token == L'~';
    if (*ignore_case)
        token++;
    for (ULONG i = 0; i < RTL_NUMBER_OF(operators); i++)
        if (!wcscmp(token, operators[i]))
            return i;
    return CMP_NONE;
}

static BOOL compare_values(const MSI_CONDITION_VALUE *left, const MSI_CONDITION_VALUE *right,
                           ULONG operation, BOOL ignore_case)
{
    int order;
    if (left->Integer || right->Integer) {
        int a = left->Number, b = right->Number;
        if ((!left->Integer && !number_value(left->Text, &a)) ||
            (!right->Integer && !number_value(right->Text, &b)))
            return operation == CMP_NE;
        if (operation == CMP_CONTAINS)
            return !!((ULONG)a & (ULONG)b);
        if (operation == CMP_PREFIX)
            return ((ULONG)a >> 16) == (ULONG)b;
        if (operation == CMP_SUFFIX)
            return ((ULONG)a & 0xffff) == (ULONG)b;
        order = a < b ? -1 : a > b;
    } else {
        SIZE_T a = wcslen(left->Text), b = wcslen(right->Text);
        if (operation >= CMP_CONTAINS) {
            if (b > a)
                return FALSE;
            SIZE_T first = operation == CMP_SUFFIX ? a - b : 0;
            SIZE_T last = operation == CMP_CONTAINS ? a - b : first;
            for (SIZE_T i = first; i <= last; i++) {
                int same = ignore_case ? _wcsnicmp(left->Text + i, right->Text, b) : wcsncmp(left->Text + i, right->Text, b);
                if (!same)
                    return TRUE;
            }
            return FALSE;
        }
        order = ignore_case ? _wcsicmp(left->Text, right->Text) : wcscmp(left->Text, right->Text);
    }
    switch (operation) {
    case CMP_EQ: return order == 0;
    case CMP_NE: return order != 0;
    case CMP_GT: return order > 0;
    case CMP_GE: return order >= 0;
    case CMP_LT: return order < 0;
    case CMP_LE: return order <= 0;
    default: return FALSE;
    }
}

static BOOL expression(MSI_CONDITION_PARSER *parser, ULONG minimum);

static BOOL condition_term(MSI_CONDITION_PARSER *parser)
{
    if (parser->Error || ++parser->Depth > 64) {
        parser->Error = TRUE;
        return FALSE;
    }
    BOOL value = FALSE;
    if (parser->Kind == COND_WORD && !_wcsicmp(parser->Token, L"NOT")) {
        next_condition(parser);
        value = !condition_term(parser);
    } else if (parser->Kind == COND_OPERATOR && !wcscmp(parser->Token, L"(")) {
        next_condition(parser);
        value = expression(parser, 1);
        if (parser->Kind != COND_OPERATOR || wcscmp(parser->Token, L")"))
            parser->Error = TRUE;
        else
            next_condition(parser);
    } else {
        MSI_CONDITION_VALUE left = condition_value(parser);
        BOOL ignore_case;
        ULONG operation = parser->Kind == COND_OPERATOR ? comparison(parser->Token, &ignore_case) : CMP_NONE;
        if (!parser->Error && operation != CMP_NONE) {
            next_condition(parser);
            MSI_CONDITION_VALUE right = condition_value(parser);
            if (!parser->Error)
                value = compare_values(&left, &right, operation, ignore_case);
            MimeMsiFree(right.Text);
        } else if (!parser->Error)
            value = left.Integer ? left.Number != 0 : *left.Text != 0;
        MimeMsiFree(left.Text);
    }
    parser->Depth--;
    return value;
}

static ULONG logical_precedence(const MSI_CONDITION_PARSER *parser)
{
    static const PCWSTR operators[] = {L"", L"IMP", L"EQV", L"XOR", L"OR", L"AND"};
    if (parser->Kind == COND_WORD)
        for (ULONG i = 1; i < RTL_NUMBER_OF(operators); i++)
            if (!_wcsicmp(parser->Token, operators[i]))
                return i;
    return 0;
}

static BOOL expression(MSI_CONDITION_PARSER *parser, ULONG minimum)
{
    BOOL left = condition_term(parser);
    while (!parser->Error) {
        ULONG precedence = logical_precedence(parser);
        if (precedence < minimum)
            break;
        next_condition(parser);
        BOOL right = expression(parser, precedence + 1);
        switch (precedence) {
        case 1: left = !left || right; break;
        case 2: left = left == right; break;
        case 3: left = left != right; break;
        case 4: left = left || right; break;
        case 5: left = left && right; break;
        }
    }
    return left;
}

MSICONDITION MimeMsiEvaluateCondition(MIME_MSI_PACKAGE *package, PCWSTR condition)
{
    if (!condition)
        return MSICONDITION_ERROR;
    MSI_CONDITION_PARSER parser = {.Package = package, .Cursor = condition};
    next_condition(&parser);
    if (parser.Kind == COND_END)
        return MSICONDITION_NONE;
    BOOL result = expression(&parser, 1);
    if (parser.Error || parser.Kind != COND_END)
        return MSICONDITION_ERROR;
    return result ? MSICONDITION_TRUE : MSICONDITION_FALSE;
}

MSICONDITION WINAPI MsiEvaluateConditionW(MSIHANDLE handle, LPCWSTR condition)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
    MSICONDITION result = handle && !package ? MSICONDITION_ERROR : MimeMsiEvaluateCondition(package, condition);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return result;
}
