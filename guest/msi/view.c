#include "msi_private.h"

/*
 * installer queries: SELECT over one table or a join, with named columns or *, DISTINCT, a WHERE clause of
 * comparisons, IS NULL and IS NOT NULL combined with AND, OR, NOT and parentheses, and ORDER BY. read only. like
 * Windows, a join is the product of its tables filtered by the clause. each condition is tested as soon as its tables
 * are bound, so joins on keys stay cheap.
 */

enum { TOKEN_END, TOKEN_NAME, TOKEN_STRING, TOKEN_NUMBER, TOKEN_SYMBOL, TOKEN_INVALID };
typedef struct SQL_TOKEN {
    PCWSTR Next;
    ULONG Kind;
    BOOL Quoted;
    WCHAR Text[256];
} SQL_TOKEN;

enum { MAX_TABLES = 8, MAX_COLUMNS = 64, MAX_CONDITIONS = 64 };
enum { SQL_SELECT, SQL_INSERT, SQL_UPDATE, SQL_DELETE };
enum { COMPARE_EQ, COMPARE_NE, COMPARE_LT, COMPARE_LE, COMPARE_GT, COMPARE_GE };
enum { OPERAND_COLUMN, OPERAND_LITERAL, OPERAND_PARAMETER };
enum { NODE_COMPARE, NODE_NULL, NODE_NOT_NULL, NODE_AND, NODE_OR, NODE_NOT };

typedef struct SQL_OPERAND {
    ULONG Kind, Table, Column, Parameter;
    MIME_MSI_FIELD Value;
} SQL_OPERAND;

typedef struct SQL_NODE {
    ULONG Kind, Operation, Tables;
    SQL_OPERAND Left, Right;
    struct SQL_NODE *First, *Second;
} SQL_NODE;

typedef struct SQL_COLUMN_REF {
    ULONG Table, Column;
} SQL_COLUMN_REF;

typedef struct MSI_VIEW {
    MIME_MSI_OBJECT Object;
    MIME_MSI_DATABASE *Database;
    MIME_MSI_TABLE *Tables[MAX_TABLES];
    ULONG TableCount;
    SQL_COLUMN_REF Selected[MAX_COLUMNS];
    ULONG Count;
    SQL_NODE *Where;
    SQL_NODE *Conditions[MAX_CONDITIONS];
    ULONG ConditionCount;
    SQL_COLUMN_REF Order[MAX_COLUMNS];
    ULONG OrderCount;
    BOOL Distinct;
    ULONG Parameters;
    ULONG *Tuples, TupleCount, Position, Revision;
    BOOL Executed;
    ULONG Operation;
    SQL_OPERAND Values[MAX_COLUMNS];
    BOOL Temporary;
} MSI_VIEW;

static BOOL name_character(WCHAR ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
           (ch >= L'0' && ch <= L'9') || ch == L'_';
}

static void next_token(SQL_TOKEN *token)
{
    PCWSTR p = token->Next;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n')
        p++;
    ULONG length = 0;
    token->Kind = TOKEN_END;
    token->Quoted = FALSE;
    if (*p == L'`' || *p == L'\'') {
        WCHAR quote = *p++;
        token->Kind = quote == L'`' ? TOKEN_NAME : TOKEN_STRING;
        token->Quoted = quote == L'`';
        while (*p && length < RTL_NUMBER_OF(token->Text) - 1) {
            if (*p == quote) {
                if (p[1] != quote)
                    break;
                p++;
            }
            token->Text[length++] = *p++;
        }
        if (*p != quote)
            token->Kind = TOKEN_INVALID;
        else
            p++;
    } else if ((*p >= L'0' && *p <= L'9') || *p == L'-') {
        token->Kind = TOKEN_NUMBER;
        token->Text[length++] = *p++;
        while (*p >= L'0' && *p <= L'9' && length < RTL_NUMBER_OF(token->Text) - 1)
            token->Text[length++] = *p++;
    } else if (name_character(*p)) {
        token->Kind = TOKEN_NAME;
        while (name_character(*p) && length < RTL_NUMBER_OF(token->Text) - 1)
            token->Text[length++] = *p++;
        /* Windows skips a backquote right after a bare name, and SolidWorks queries rely on it. */
        if (*p == L'`')
            p++;
    } else if (*p) {
        token->Kind = TOKEN_SYMBOL;
        token->Text[length++] = *p++;
        if ((token->Text[0] == L'<' && (*p == L'>' || *p == L'=')) ||
            (token->Text[0] == L'>' && *p == L'='))
            token->Text[length++] = *p++;
    }
    token->Text[length] = 0;
    token->Next = p;
}

/* matches a keyword or symbol. quoted names and strings never match. */
static BOOL take(SQL_TOKEN *token, PCWSTR text)
{
    if (token->Kind == TOKEN_INVALID || token->Kind == TOKEN_STRING || token->Quoted || _wcsicmp(token->Text, text))
        return FALSE;
    next_token(token);
    return TRUE;
}

static BOOL is_keyword(const SQL_TOKEN *token, PCWSTR text)
{
    return token->Kind == TOKEN_NAME && !token->Quoted && !_wcsicmp(token->Text, text);
}

static void free_node(SQL_NODE *node)
{
    if (!node)
        return;
    free_node(node->First);
    free_node(node->Second);
    MimeMsiFree(node->Left.Value.String);
    MimeMsiFree(node->Right.Value.String);
    MimeMsiFree(node);
}

static void destroy_view(MIME_MSI_OBJECT *object)
{
    MSI_VIEW *view = (void *)object;
    free_node(view->Where);
    for (ULONG i = 0; i < MAX_COLUMNS; i++)
        MimeMsiFree(view->Values[i].Value.String);
    MimeMsiFree(view->Tuples);
    for (ULONG i = 0; i < view->TableCount; i++)
        view->Tables[i] = nullptr;
    if (view->Database)
        MimeMsiRelease(&view->Database->Object);
    MimeMsiFree(view);
}

static ULONG find_column(const MIME_MSI_TABLE *table, PCWSTR name)
{
    for (ULONG i = 0; i < table->Count; i++)
        if (!wcscmp(table->Columns[i].Name, name))
            return i;
    return UINT32_MAX;
}

static BOOL is_string_column(const MIME_MSI_COLUMN *column)
{
    return (column->Type & 0x0c00) == 0x0c00;
}

static BOOL is_binary_column(const MIME_MSI_COLUMN *column)
{
    return (column->Type & 0x0fff) == 0x0900;
}

/* a column, optionally table qualified. a bare name must belong to exactly one table of the query. */
static UINT parse_reference(MSI_VIEW *view, SQL_TOKEN *token, ULONG *table_out, ULONG *column_out)
{
    if (token->Kind != TOKEN_NAME)
        return ERROR_BAD_QUERY_SYNTAX;
    WCHAR first[256];
    wcscpy(first, token->Text);
    next_token(token);
    if (take(token, L".")) {
        if (token->Kind != TOKEN_NAME)
            return ERROR_BAD_QUERY_SYNTAX;
        for (ULONG t = 0; t < view->TableCount; t++) {
            if (wcscmp(view->Tables[t]->Name, first))
                continue;
            ULONG column = find_column(view->Tables[t], token->Text);
            if (column == UINT32_MAX)
                return ERROR_BAD_QUERY_SYNTAX;
            *table_out = t;
            *column_out = column;
            next_token(token);
            return ERROR_SUCCESS;
        }
        return ERROR_BAD_QUERY_SYNTAX;
    }
    ULONG found = UINT32_MAX, found_column = 0;
    for (ULONG t = 0; t < view->TableCount; t++) {
        ULONG column = find_column(view->Tables[t], first);
        if (column == UINT32_MAX)
            continue;
        if (found != UINT32_MAX)
            return ERROR_BAD_QUERY_SYNTAX;
        found = t;
        found_column = column;
    }
    if (found == UINT32_MAX)
        return ERROR_BAD_QUERY_SYNTAX;
    *table_out = found;
    *column_out = found_column;
    return ERROR_SUCCESS;
}

static UINT parse_operand(MSI_VIEW *view, SQL_TOKEN *token, SQL_OPERAND *operand)
{
    if (take(token, L"NULL")) {
        operand->Kind = OPERAND_LITERAL;
        return ERROR_SUCCESS;
    }
    if (take(token, L"?")) {
        operand->Kind = OPERAND_PARAMETER;
        operand->Parameter = ++view->Parameters;
        return ERROR_SUCCESS;
    }
    if (token->Kind == TOKEN_STRING || token->Kind == TOKEN_NUMBER) {
        operand->Kind = OPERAND_LITERAL;
        UINT error = MimeMsiFieldString(&operand->Value, token->Text);
        if (error)
            return error;
        if (token->Kind == TOKEN_NUMBER) {
            int number = MimeMsiFieldGetInteger(&operand->Value);
            if (number == MSI_NULL_INTEGER)
                return ERROR_BAD_QUERY_SYNTAX;
            MimeMsiFieldInteger(&operand->Value, number);
        } else if (operand->Value.Kind == FIELD_NULL) {
            /* an empty string is a string. */
            operand->Value.Kind = FIELD_STRING;
            operand->Value.String = MimeMsiDuplicate(L"");
            if (!operand->Value.String)
                return ERROR_NOT_ENOUGH_MEMORY;
        }
        next_token(token);
        return ERROR_SUCCESS;
    }
    operand->Kind = OPERAND_COLUMN;
    UINT error = parse_reference(view, token, &operand->Table, &operand->Column);
    return error;
}

static ULONG operand_tables(const SQL_OPERAND *operand)
{
    return operand->Kind == OPERAND_COLUMN ? 1u << operand->Table : 0;
}

static UINT parse_expression(MSI_VIEW *view, SQL_TOKEN *token, ULONG depth, SQL_NODE **out);

static UINT parse_primary(MSI_VIEW *view, SQL_TOKEN *token, ULONG depth, SQL_NODE **out)
{
    if (depth > 32)
        return ERROR_BAD_QUERY_SYNTAX;
    SQL_NODE *node = MimeMsiAllocate(sizeof(*node));
    if (!node)
        return ERROR_NOT_ENOUGH_MEMORY;
    UINT error = ERROR_SUCCESS;
    if (take(token, L"(")) {
        MimeMsiFree(node);
        node = nullptr;
        error = parse_expression(view, token, depth + 1, &node);
        if (!error && !take(token, L")")) {
            free_node(node);
            error = ERROR_BAD_QUERY_SYNTAX;
        }
        if (!error)
            *out = node;
        return error;
    }
    if (take(token, L"NOT")) {
        node->Kind = NODE_NOT;
        error = parse_primary(view, token, depth + 1, &node->First);
        if (!error)
            node->Tables = node->First->Tables;
    } else {
        error = parse_operand(view, token, &node->Left);
        if (!error && is_keyword(token, L"IS")) {
            next_token(token);
            node->Kind = take(token, L"NOT") ? NODE_NOT_NULL : NODE_NULL;
            if (!take(token, L"NULL") || node->Left.Kind != OPERAND_COLUMN)
                error = ERROR_BAD_QUERY_SYNTAX;
        } else if (!error) {
            static const PCWSTR operators[] = {L"=", L"<>", L"<", L"<=", L">", L">="};
            ULONG op;
            for (op = 0; op < RTL_NUMBER_OF(operators); op++)
                if (take(token, operators[op]))
                    break;
            if (op == RTL_NUMBER_OF(operators))
                error = ERROR_BAD_QUERY_SYNTAX;
            else {
                node->Kind = NODE_COMPARE;
                node->Operation = op;
                error = parse_operand(view, token, &node->Right);
            }
        }
        if (!error)
            node->Tables = operand_tables(&node->Left) | operand_tables(&node->Right);
    }
    if (error) {
        free_node(node);
        return error;
    }
    *out = node;
    return ERROR_SUCCESS;
}

static UINT parse_and(MSI_VIEW *view, SQL_TOKEN *token, ULONG depth, SQL_NODE **out)
{
    SQL_NODE *left = nullptr;
    UINT error = parse_primary(view, token, depth, &left);
    while (!error && take(token, L"AND")) {
        SQL_NODE *right = nullptr, *node;
        error = parse_primary(view, token, depth, &right);
        if (error)
            break;
        node = MimeMsiAllocate(sizeof(*node));
        if (!node) {
            free_node(right);
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        node->Kind = NODE_AND;
        node->First = left;
        node->Second = right;
        node->Tables = left->Tables | right->Tables;
        left = node;
    }
    if (error) {
        free_node(left);
        return error;
    }
    *out = left;
    return ERROR_SUCCESS;
}

static UINT parse_expression(MSI_VIEW *view, SQL_TOKEN *token, ULONG depth, SQL_NODE **out)
{
    SQL_NODE *left = nullptr;
    UINT error = parse_and(view, token, depth, &left);
    while (!error && take(token, L"OR")) {
        SQL_NODE *right = nullptr, *node;
        error = parse_and(view, token, depth, &right);
        if (error)
            break;
        node = MimeMsiAllocate(sizeof(*node));
        if (!node) {
            free_node(right);
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        node->Kind = NODE_OR;
        node->First = left;
        node->Second = right;
        node->Tables = left->Tables | right->Tables;
        left = node;
    }
    if (error) {
        free_node(left);
        return error;
    }
    *out = left;
    return ERROR_SUCCESS;
}

/* each top level AND term becomes one test, run at the depth of the last table it names. */
static UINT split_conditions(MSI_VIEW *view, SQL_NODE *node)
{
    if (node->Kind == NODE_AND) {
        UINT error = split_conditions(view, node->First);
        return error ? error : split_conditions(view, node->Second);
    }
    if (view->ConditionCount == MAX_CONDITIONS)
        return ERROR_BAD_QUERY_SYNTAX;
    view->Conditions[view->ConditionCount++] = node;
    return ERROR_SUCCESS;
}

/* DML shares SELECT's name, value and predicate parser. Schema-changing SQL is deliberately not accepted. */
static UINT parse_mutation(MSI_VIEW *view, SQL_TOKEN *token)
{
    if (take(token, L"INSERT")) {
        view->Operation = SQL_INSERT;
        if (!take(token, L"INTO"))
            return ERROR_BAD_QUERY_SYNTAX;
    } else if (take(token, L"UPDATE"))
        view->Operation = SQL_UPDATE;
    else if (take(token, L"DELETE")) {
        view->Operation = SQL_DELETE;
        if (!take(token, L"FROM"))
            return ERROR_BAD_QUERY_SYNTAX;
    } else
        return ERROR_BAD_QUERY_SYNTAX;
    if (token->Kind != TOKEN_NAME)
        return ERROR_BAD_QUERY_SYNTAX;
    UINT error = MimeMsiLoadTable(view->Database, token->Text, &view->Tables[0]);
    if (error)
        return error;
    view->TableCount = 1;
    next_token(token);
    if (view->Operation == SQL_INSERT) {
        if (!take(token, L"("))
            return ERROR_BAD_QUERY_SYNTAX;
        do {
            if (view->Count == MAX_COLUMNS)
                return ERROR_BAD_QUERY_SYNTAX;
            SQL_COLUMN_REF *ref = &view->Selected[view->Count++];
            error = parse_reference(view, token, &ref->Table, &ref->Column);
            if (error)
                return error;
        } while (take(token, L","));
        if (!take(token, L")") || !take(token, L"VALUES") || !take(token, L"("))
            return ERROR_BAD_QUERY_SYNTAX;
        for (ULONG i = 0; i < view->Count; i++) {
            if (i && !take(token, L","))
                return ERROR_BAD_QUERY_SYNTAX;
            error = parse_operand(view, token, &view->Values[i]);
            if (error || view->Values[i].Kind == OPERAND_COLUMN)
                return ERROR_BAD_QUERY_SYNTAX;
        }
        if (!take(token, L")"))
            return ERROR_BAD_QUERY_SYNTAX;
        view->Temporary = take(token, L"TEMPORARY");
    } else if (view->Operation == SQL_UPDATE) {
        if (!take(token, L"SET"))
            return ERROR_BAD_QUERY_SYNTAX;
        do {
            if (view->Count == MAX_COLUMNS)
                return ERROR_BAD_QUERY_SYNTAX;
            ULONG i = view->Count++;
            SQL_COLUMN_REF *ref = &view->Selected[i];
            error = parse_reference(view, token, &ref->Table, &ref->Column);
            if (error || !take(token, L"="))
                return ERROR_BAD_QUERY_SYNTAX;
            error = parse_operand(view, token, &view->Values[i]);
            if (error || view->Values[i].Kind == OPERAND_COLUMN)
                return ERROR_BAD_QUERY_SYNTAX;
        } while (take(token, L","));
    }
    for (ULONG i = 0; i < view->Count; i++)
        for (ULONG j = 0; j < i; j++)
            if (view->Selected[i].Column == view->Selected[j].Column)
                return ERROR_BAD_QUERY_SYNTAX;
    if (view->Operation != SQL_INSERT && take(token, L"WHERE")) {
        error = parse_expression(view, token, 0, &view->Where);
        if (error)
            return error;
    }
    return token->Kind == TOKEN_END ? ERROR_SUCCESS : ERROR_BAD_QUERY_SYNTAX;
}

static UINT parse_view(MSI_VIEW *view, PCWSTR query)
{
    SQL_TOKEN token = {.Next = query};
    next_token(&token);
    if (!take(&token, L"SELECT"))
        return parse_mutation(view, &token);
    view->Distinct = take(&token, L"DISTINCT");
    /* the column list comes before FROM, so scan ahead for the tables first. */
    SQL_TOKEN scan = token;
    BOOL found = FALSE;
    while (scan.Kind != TOKEN_END && scan.Kind != TOKEN_INVALID) {
        if (is_keyword(&scan, L"FROM")) {
            found = TRUE;
            break;
        }
        next_token(&scan);
    }
    if (!found)
        return ERROR_BAD_QUERY_SYNTAX;
    SQL_TOKEN columns = token;
    token = scan;
    next_token(&token);
    do {
        if (token.Kind != TOKEN_NAME || view->TableCount == MAX_TABLES)
            return ERROR_BAD_QUERY_SYNTAX;
        UINT error = MimeMsiLoadTable(view->Database, token.Text, &view->Tables[view->TableCount]);
        if (error)
            return error;
        view->TableCount++;
        next_token(&token);
    } while (take(&token, L","));
    /* now the tables are known, parse the column list. */
    if (take(&columns, L"*")) {
        if (!is_keyword(&columns, L"FROM"))
            return ERROR_BAD_QUERY_SYNTAX;
        for (ULONG t = 0; t < view->TableCount; t++) {
            for (ULONG c = 0; c < view->Tables[t]->Count; c++) {
                if (view->Count == MAX_COLUMNS)
                    return ERROR_BAD_QUERY_SYNTAX;
                view->Selected[view->Count++] = (SQL_COLUMN_REF){t, c};
            }
        }
    } else {
        do {
            if (view->Count == MAX_COLUMNS)
                return ERROR_BAD_QUERY_SYNTAX;
            ULONG t, c;
            UINT error = parse_reference(view, &columns, &t, &c);
            if (error)
                return error;
            view->Selected[view->Count++] = (SQL_COLUMN_REF){t, c};
        } while (take(&columns, L","));
        if (!is_keyword(&columns, L"FROM"))
            return ERROR_BAD_QUERY_SYNTAX;
    }
    if (take(&token, L"WHERE")) {
        UINT error = parse_expression(view, &token, 0, &view->Where);
        if (error)
            return error;
        error = split_conditions(view, view->Where);
        if (error)
            return error;
    }
    if (take(&token, L"ORDER")) {
        if (!take(&token, L"BY"))
            return ERROR_BAD_QUERY_SYNTAX;
        do {
            if (view->OrderCount == MAX_COLUMNS)
                return ERROR_BAD_QUERY_SYNTAX;
            ULONG t, c;
            UINT error = parse_reference(view, &token, &t, &c);
            if (error)
                return error;
            view->Order[view->OrderCount++] = (SQL_COLUMN_REF){t, c};
        } while (take(&token, L","));
    }
    return token.Kind == TOKEN_END ? ERROR_SUCCESS : ERROR_BAD_QUERY_SYNTAX;
}

UINT WINAPI MsiDatabaseOpenViewW(MSIHANDLE handle, LPCWSTR query, MSIHANDLE *out)
{
    MIME_REMOTE(PROXY_DatabaseOpenViewW, UINT, MIME_ARG(handle), MIME_ARG(query), MIME_ARG(out));
    if (!query || !out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    if (!database) {
        /* a package stands for its database. */
        MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
        if (package)
            database = package->Database;
    }
    UINT error = database ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    MSI_VIEW *view = nullptr;
    if (!error) {
        view = MimeMsiAllocate(sizeof(*view));
        if (!view)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        view->Object.Kind = MIME_MSI_KIND_VIEW;
        view->Object.Destroy = destroy_view;
        view->Database = database;
        database->Object.References++;
        error = parse_view(view, query);
        if (!error && !(*out = MimeMsiAddHandle(&view->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error && view)
        destroy_view(&view->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (error)
        DbgPrint("msi: the query %ws does not open, error %u\n", query, error);
    return error;
}

/* a cell or literal value, not copied. */
typedef struct SQL_VALUE {
    ULONG Kind;
    int Integer;
    PCWSTR String;
} SQL_VALUE;

static SQL_VALUE cell_value(const MSI_VIEW *view, ULONG table, ULONG column, ULONG row)
{
    SQL_VALUE value = {FIELD_NULL};
    const MIME_MSI_TABLE *source = view->Tables[table];
    const MIME_MSI_COLUMN *info = &source->Columns[column];
    ULONG raw = MimeMsiCell(source, row, column);
    if (!raw)
        return value;
    if (is_string_column(info)) {
        value.Kind = FIELD_STRING;
        value.String = view->Database->Strings[raw];
        if (!value.String)
            value.String = L"";
    } else if (is_binary_column(info)) {
        value.Kind = FIELD_INTEGER;
        value.Integer = 1;
    } else {
        value.Kind = FIELD_INTEGER;
        value.Integer = info->Width == 2 ? (int)raw - 0x8000 : (int)(raw ^ 0x80000000u);
    }
    return value;
}

static SQL_VALUE operand_value(const MSI_VIEW *view, const SQL_OPERAND *operand, const ULONG *rows,
                               const MIME_MSI_RECORD *parameters)
{
    SQL_VALUE value = {FIELD_NULL};
    if (operand->Kind == OPERAND_COLUMN)
        return cell_value(view, operand->Table, operand->Column, rows[operand->Table]);
    const MIME_MSI_FIELD *field = operand->Kind == OPERAND_PARAMETER ? &parameters->Fields[operand->Parameter] :
                                                                        &operand->Value;
    value.Kind = field->Kind;
    value.Integer = field->Integer;
    value.String = field->String;
    return value;
}

/* a number against a string compares as text, like a string column against a number in a clause. */
static int compare_values(const SQL_VALUE *a, const SQL_VALUE *b)
{
    if (a->Kind == FIELD_INTEGER && b->Kind == FIELD_INTEGER)
        return a->Integer < b->Integer ? -1 : a->Integer > b->Integer;
    WCHAR number[2][16];
    PCWSTR text[2] = {a->String, b->String};
    const SQL_VALUE *values[2] = {a, b};
    for (ULONG i = 0; i < 2; i++) {
        if (values[i]->Kind == FIELD_INTEGER) {
            _snwprintf(number[i], RTL_NUMBER_OF(number[i]), L"%d", values[i]->Integer);
            text[i] = number[i];
        }
        if (!text[i])
            text[i] = L"";
    }
    return wcscmp(text[0], text[1]);
}

static BOOL evaluate(const MSI_VIEW *view, const SQL_NODE *node, const ULONG *rows, const MIME_MSI_RECORD *parameters)
{
    switch (node->Kind) {
    case NODE_AND:
        return evaluate(view, node->First, rows, parameters) && evaluate(view, node->Second, rows, parameters);
    case NODE_OR:
        return evaluate(view, node->First, rows, parameters) || evaluate(view, node->Second, rows, parameters);
    case NODE_NOT:
        return !evaluate(view, node->First, rows, parameters);
    case NODE_NULL:
        return operand_value(view, &node->Left, rows, parameters).Kind == FIELD_NULL;
    case NODE_NOT_NULL:
        return operand_value(view, &node->Left, rows, parameters).Kind != FIELD_NULL;
    default:
        break;
    }
    SQL_VALUE a = operand_value(view, &node->Left, rows, parameters);
    SQL_VALUE b = operand_value(view, &node->Right, rows, parameters);
    if (a.Kind == FIELD_NULL || b.Kind == FIELD_NULL)
        return FALSE;
    /* a numeric column against a string holding a number compares as numbers. */
    SQL_VALUE converted;
    BOOL number_column = node->Left.Kind == OPERAND_COLUMN && a.Kind == FIELD_INTEGER;
    if (number_column && b.Kind == FIELD_STRING) {
        MIME_MSI_FIELD field = {FIELD_STRING, 0, (PWSTR)b.String};
        int number = MimeMsiFieldGetInteger(&field);
        if (number == MSI_NULL_INTEGER)
            return FALSE;
        converted = (SQL_VALUE){FIELD_INTEGER, number, nullptr};
        b = converted;
    } else if (node->Right.Kind == OPERAND_COLUMN && b.Kind == FIELD_INTEGER && a.Kind == FIELD_STRING) {
        MIME_MSI_FIELD field = {FIELD_STRING, 0, (PWSTR)a.String};
        int number = MimeMsiFieldGetInteger(&field);
        if (number == MSI_NULL_INTEGER)
            return FALSE;
        converted = (SQL_VALUE){FIELD_INTEGER, number, nullptr};
        a = converted;
    }
    int order = compare_values(&a, &b);
    switch (node->Operation) {
    case COMPARE_EQ: return order == 0;
    case COMPARE_NE: return order != 0;
    case COMPARE_LT: return order < 0;
    case COMPARE_LE: return order <= 0;
    case COMPARE_GT: return order > 0;
    case COMPARE_GE: return order >= 0;
    default: return FALSE;
    }
}

typedef struct SQL_RUN {
    MSI_VIEW *View;
    const MIME_MSI_RECORD *Parameters;
    ULONG Rows[MAX_TABLES];
    ULONG *Tuples, Count, Capacity;
    UINT Error;
} SQL_RUN;

static void add_tuple(SQL_RUN *run)
{
    MSI_VIEW *view = run->View;
    if (run->Count == run->Capacity) {
        ULONG capacity = run->Capacity ? run->Capacity * 2 : 64;
        ULONG *bigger = MimeMsiAllocate((SIZE_T)capacity * view->TableCount * sizeof(ULONG));
        if (!bigger) {
            run->Error = ERROR_NOT_ENOUGH_MEMORY;
            return;
        }
        if (run->Tuples)
            memcpy(bigger, run->Tuples, (SIZE_T)run->Count * view->TableCount * sizeof(ULONG));
        MimeMsiFree(run->Tuples);
        run->Tuples = bigger;
        run->Capacity = capacity;
    }
    memcpy(run->Tuples + (SIZE_T)run->Count * view->TableCount, run->Rows, view->TableCount * sizeof(ULONG));
    run->Count++;
}

static void enumerate(SQL_RUN *run, ULONG depth)
{
    MSI_VIEW *view = run->View;
    if (depth == view->TableCount) {
        add_tuple(run);
        return;
    }
    const MIME_MSI_TABLE *table = view->Tables[depth];
    for (ULONG row = 0; row < table->Rows && !run->Error; row++) {
        run->Rows[depth] = row;
        BOOL pass = TRUE;
        for (ULONG i = 0; i < view->ConditionCount && pass; i++) {
            const SQL_NODE *condition = view->Conditions[i];
            ULONG last = condition->Tables ? 31 - __builtin_clz(condition->Tables) : 0;
            if (last == depth)
                pass = evaluate(view, condition, run->Rows, run->Parameters);
        }
        if (pass)
            enumerate(run, depth + 1);
    }
}

static int compare_tuples(const MSI_VIEW *view, const ULONG *a, const ULONG *b)
{
    for (ULONG i = 0; i < view->OrderCount; i++) {
        const SQL_COLUMN_REF *ref = &view->Order[i];
        SQL_VALUE x = cell_value(view, ref->Table, ref->Column, a[ref->Table]);
        SQL_VALUE y = cell_value(view, ref->Table, ref->Column, b[ref->Table]);
        int order = x.Kind == FIELD_NULL || y.Kind == FIELD_NULL ? (x.Kind != FIELD_NULL) - (y.Kind != FIELD_NULL) :
                                                                  compare_values(&x, &y);
        if (order)
            return order;
    }
    return 0;
}

static int compare_selected(const MSI_VIEW *view, const ULONG *a, const ULONG *b)
{
    for (ULONG i = 0; i < view->Count; i++) {
        const SQL_COLUMN_REF *ref = &view->Selected[i];
        SQL_VALUE x = cell_value(view, ref->Table, ref->Column, a[ref->Table]);
        SQL_VALUE y = cell_value(view, ref->Table, ref->Column, b[ref->Table]);
        int order = x.Kind == FIELD_NULL || y.Kind == FIELD_NULL ? (x.Kind != FIELD_NULL) - (y.Kind != FIELD_NULL) :
                                                                  compare_values(&x, &y);
        if (order)
            return order;
    }
    return 0;
}

/* a stable merge sort, since table order is meaningful for equal rows. */
static BOOL sort_tuples(MSI_VIEW *view, ULONG *tuples, ULONG count, BOOL by_selected)
{
    ULONG width = view->TableCount;
    ULONG *scratch = MimeMsiAllocate((SIZE_T)count * width * sizeof(ULONG) + 1);
    if (!scratch)
        return FALSE;
    for (ULONG run = 1; run < count; run *= 2) {
        for (ULONG start = 0; start < count; start += 2 * run) {
            ULONG middle = start + run < count ? start + run : count;
            ULONG end = start + 2 * run < count ? start + 2 * run : count;
            ULONG i = start, j = middle, k = start;
            while (i < middle && j < end) {
                const ULONG *a = tuples + (SIZE_T)i * width, *b = tuples + (SIZE_T)j * width;
                int order = by_selected ? compare_selected(view, a, b) : compare_tuples(view, a, b);
                memcpy(scratch + (SIZE_T)k * width, order <= 0 ? a : b, width * sizeof(ULONG));
                if (order <= 0)
                    i++;
                else
                    j++;
                k++;
            }
            while (i < middle)
                memcpy(scratch + (SIZE_T)k++ * width, tuples + (SIZE_T)i++ * width, width * sizeof(ULONG));
            while (j < end)
                memcpy(scratch + (SIZE_T)k++ * width, tuples + (SIZE_T)j++ * width, width * sizeof(ULONG));
        }
        memcpy(tuples, scratch, (SIZE_T)count * width * sizeof(ULONG));
    }
    MimeMsiFree(scratch);
    return TRUE;
}

static UINT execute_mutation(MSI_VIEW *view, const MIME_MSI_RECORD *parameters);

UINT WINAPI MsiViewExecute(MSIHANDLE handle, MSIHANDLE parameters)
{
    MIME_REMOTE(PROXY_ViewExecute, UINT, MIME_ARG(handle), MIME_ARG(parameters));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(parameters, MIME_MSI_KIND_RECORD);
    UINT error = !view || (parameters && !record) ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
    if (!error && view->Parameters && (!record || record->Count < view->Parameters))
        error = ERROR_INVALID_PARAMETER;
    if (!error && view->Operation != SQL_SELECT) {
        error = execute_mutation(view, record);
        ReleaseSRWLockExclusive(&MimeMsiLock);
        return error;
    }
    if (!error) {
        MimeMsiFree(view->Tuples);
        view->Tuples = nullptr;
        view->Executed = FALSE;
        view->TupleCount = view->Position = 0;
        SQL_RUN run = {.View = view, .Parameters = record};
        enumerate(&run, 0);
        error = run.Error;
        if (!error && view->OrderCount && !sort_tuples(view, run.Tuples, run.Count, FALSE))
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (!error && view->Distinct && run.Count > 1) {
            /* sorting by the selected columns brings equal rows together. */
            if (!sort_tuples(view, run.Tuples, run.Count, TRUE))
                error = ERROR_NOT_ENOUGH_MEMORY;
            ULONG kept = 1, width = view->TableCount;
            for (ULONG i = 1; !error && i < run.Count; i++) {
                if (!compare_selected(view, run.Tuples + (SIZE_T)(kept - 1) * width, run.Tuples + (SIZE_T)i * width))
                    continue;
                if (kept != i)
                    memcpy(run.Tuples + (SIZE_T)kept * width, run.Tuples + (SIZE_T)i * width, width * sizeof(ULONG));
                kept++;
            }
            run.Count = kept;
        }
        if (error)
            MimeMsiFree(run.Tuples);
        else {
            view->Tuples = run.Tuples;
            view->TupleCount = run.Count;
            view->Executed = TRUE;
            view->Revision = view->Database->Revision;
        }
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

/* a binary cell's stream is named after the table and its key values, joined by dots. */
static UINT binary_field(const MSI_VIEW *view, ULONG table, ULONG row, MIME_MSI_FIELD *field)
{
    const MIME_MSI_TABLE *source = view->Tables[table];
    WCHAR name[256];
    _snwprintf(name, RTL_NUMBER_OF(name), L"%s", source->Name);
    for (ULONG c = 0; c < source->Count; c++) {
        if (!(source->Columns[c].Type & 0x2000))
            continue;
        SQL_VALUE key = cell_value(view, table, c, row);
        SIZE_T length = wcslen(name);
        if (key.Kind == FIELD_INTEGER)
            _snwprintf(name + length, RTL_NUMBER_OF(name) - length, L".%d", key.Integer);
        else
            _snwprintf(name + length, RTL_NUMBER_OF(name) - length, L".%s", key.String ? key.String : L"");
    }
    BYTE *data;
    SIZE_T size;
    UINT error = MimeMsiReadBinary(view->Database, name, &data, &size);
    if (error == ERROR_FILE_NOT_FOUND)
        return ERROR_SUCCESS;
    if (error)
        return error;
    MIME_MSI_BLOB *blob = MimeMsiBlobCreate(data, size);
    MimeMsiFree(data);
    if (!blob)
        return ERROR_NOT_ENOUGH_MEMORY;
    error = MimeMsiFieldStream(field, blob);
    MimeMsiBlobRelease(blob);
    return error;
}

UINT WINAPI MsiViewFetch(MSIHANDLE handle, MSIHANDLE *out)
{
    MIME_REMOTE(PROXY_ViewFetch, UINT, MIME_ARG(handle), MIME_ARG(out));
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    UINT error = !view ? ERROR_INVALID_HANDLE : !view->Executed || view->Revision != view->Database->Revision ? ERROR_FUNCTION_FAILED :
                 view->Position == view->TupleCount ? ERROR_NO_MORE_ITEMS : ERROR_SUCCESS;
    MIME_MSI_RECORD *record = nullptr;
    if (!error && !(record = MimeMsiRecordCreate(view->Count)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        const ULONG *rows = view->Tuples + (SIZE_T)view->Position * view->TableCount;
        for (ULONG i = 0; i < view->Count && !error; i++) {
            const SQL_COLUMN_REF *ref = &view->Selected[i];
            const MIME_MSI_COLUMN *column = &view->Tables[ref->Table]->Columns[ref->Column];
            SQL_VALUE value = cell_value(view, ref->Table, ref->Column, rows[ref->Table]);
            if (value.Kind == FIELD_NULL)
                continue;
            if (is_binary_column(column))
                error = binary_field(view, ref->Table, rows[ref->Table], &record->Fields[i + 1]);
            else if (value.Kind == FIELD_STRING)
                error = MimeMsiFieldString(&record->Fields[i + 1], value.String);
            else
                MimeMsiFieldInteger(&record->Fields[i + 1], value.Integer);
        }
        if (!error && !(*out = MimeMsiAddHandle(&record->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (!error)
            view->Position++;
    }
    if (error && record)
        record->Object.Destroy(&record->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiViewClose(MSIHANDLE handle)
{
    MIME_REMOTE(PROXY_ViewClose, UINT, MIME_ARG(handle));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    if (view) {
        MimeMsiFree(view->Tuples);
        view->Tuples = nullptr;
        view->TupleCount = view->Position = 0;
        view->Executed = FALSE;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return view ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

/* ------------------------------------------------------------------ changes to the rows of a table */

/*
 * the tables of a package are changed in memory only: the rows a custom action inserts, updates or deletes are seen
 * by the queries that run after, and are never written back to the file.
 */

enum { MODIFY_SEEK = -1, MODIFY_REFRESH, MODIFY_INSERT, MODIFY_UPDATE, MODIFY_ASSIGN, MODIFY_REPLACE, MODIFY_MERGE,
       MODIFY_DELETE, MODIFY_INSERT_TEMPORARY, MODIFY_VALIDATE, MODIFY_VALIDATE_NEW, MODIFY_VALIDATE_FIELD,
       MODIFY_VALIDATE_DELETE };

/* the number of a string of the pool, which a new string joins. */
static UINT intern_string(MIME_MSI_DATABASE *database, PCWSTR text, ULONG *id)
{
    if (!*text) {
        *id = 0;
        return ERROR_SUCCESS;
    }
    for (ULONG i = 1; i < database->StringCount; i++) {
        if (database->Strings[i] && !wcscmp(database->Strings[i], text)) {
            *id = i;
            return ERROR_SUCCESS;
        }
    }
    if (database->StringCount >= (1u << (8 * database->StringWidth)))
        return ERROR_FUNCTION_FAILED;
    SIZE_T length = wcslen(text) + 1;
    PWSTR copy = MimeMsiAllocate(length * sizeof(WCHAR));
    PWSTR *grown = MimeMsiAllocate(((SIZE_T)database->StringCount + 1) * sizeof(PWSTR));
    if (!copy || !grown) {
        MimeMsiFree(copy);
        MimeMsiFree(grown);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(copy, text, length * sizeof(WCHAR));
    memcpy(grown, database->Strings, (SIZE_T)database->StringCount * sizeof(PWSTR));
    grown[database->StringCount] = copy;
    MimeMsiFree(database->Strings);
    database->Strings = grown;
    *id = database->StringCount++;
    return ERROR_SUCCESS;
}

static void write_cell(MIME_MSI_TABLE *table, ULONG row, ULONG column, ULONG value)
{
    MIME_MSI_COLUMN *field = &table->Columns[column];
    BYTE *at = table->Data + field->Offset + (SIZE_T)row * field->Width;
    for (ULONG i = 0; i < field->Width; i++)
        at[i] = (BYTE)(value >> (i * 8));
}

/* the table with another number of rows, keeping the cells that stay. */
static UINT resize_rows(MIME_MSI_TABLE *table, ULONG rows)
{
    SIZE_T total = 0;
    for (ULONG c = 0; c < table->Count; c++)
        total += (SIZE_T)rows * table->Columns[c].Width;
    BYTE *data = MimeMsiAllocate(total ? total : 1);
    if (!data)
        return ERROR_NOT_ENOUGH_MEMORY;
    ULONG keep = rows < table->Rows ? rows : table->Rows;
    SIZE_T offset = 0;
    for (ULONG c = 0; c < table->Count; c++) {
        MIME_MSI_COLUMN *column = &table->Columns[c];
        memcpy(data + offset, table->Data + column->Offset, (SIZE_T)keep * column->Width);
        column->Offset = offset;
        offset += (SIZE_T)rows * column->Width;
    }
    MimeMsiFree(table->Data);
    table->Data = data;
    table->Rows = rows;
    return ERROR_SUCCESS;
}

/* what the record puts in a column, as the number the cell holds. */
static UINT cell_for_field(MIME_MSI_DATABASE *database, const MIME_MSI_COLUMN *column, const MIME_MSI_FIELD *field,
                           ULONG *value)
{
    BOOL nullable = !!(column->Type & 0x1000);
    if (is_binary_column(column))
        return field->Kind == FIELD_NULL ? (nullable ? ERROR_SUCCESS : ERROR_FUNCTION_FAILED) : ERROR_FUNCTION_FAILED;
    if (field->Kind == FIELD_NULL) {
        *value = 0;
        return nullable ? ERROR_SUCCESS : ERROR_FUNCTION_FAILED;
    }
    if (is_string_column(column)) {
        if (field->Kind == FIELD_STRING)
            return intern_string(database, field->String ? field->String : L"", value);
        if (field->Kind == FIELD_INTEGER) {
            WCHAR number[16];
            _snwprintf(number, RTL_NUMBER_OF(number), L"%d", field->Integer);
            return intern_string(database, number, value);
        }
        return ERROR_FUNCTION_FAILED;
    }
    if (field->Kind != FIELD_INTEGER)
        return ERROR_FUNCTION_FAILED;
    if (column->Width == 2) {
        if (field->Integer < -32767 || field->Integer > 32767)
            return ERROR_FUNCTION_FAILED;
        *value = (ULONG)(field->Integer + 0x8000);
    } else {
        *value = (ULONG)field->Integer ^ 0x80000000u;
    }
    return ERROR_SUCCESS;
}

static BOOL same_cell(MIME_MSI_DATABASE *database, const MIME_MSI_COLUMN *column, ULONG a, ULONG b)
{
    if (a == b)
        return TRUE;
    if (!a || !b || !is_string_column(column) || a >= database->StringCount || b >= database->StringCount)
        return FALSE;
    return !wcscmp(database->Strings[a] ? database->Strings[a] : L"", database->Strings[b] ? database->Strings[b] : L"");
}

static BOOL rows_have_key(MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table, ULONG row, const ULONG *values, BOOL *identical)
{
    BOOL same_keys = TRUE;
    for (ULONG c = 0; c < table->Count; c++) {
        if ((table->Columns[c].Type & 0x2000) && !same_cell(database, &table->Columns[c], MimeMsiCell(table, row, c), values[c])) {
            same_keys = FALSE;
            break;
        }
    }
    if (same_keys && identical) {
        *identical = TRUE;
        for (ULONG c = 0; c < table->Count; c++)
            if (!same_cell(database, &table->Columns[c], MimeMsiCell(table, row, c), values[c]))
                *identical = FALSE;
    }
    return same_keys;
}

static UINT modify_rows(MSI_VIEW *view, int mode, const MIME_MSI_RECORD *record)
{
    if (view->TableCount != 1 || !view->Executed)
        return ERROR_FUNCTION_FAILED;
    MIME_MSI_DATABASE *database = view->Database;
    MIME_MSI_TABLE *table = view->Tables[0];
    if (mode == MODIFY_DELETE) {
        if (!view->Position)
            return ERROR_FUNCTION_FAILED;
        ULONG row = view->Tuples[view->Position - 1];
        for (ULONG c = 0; c < table->Count; c++) {
            MIME_MSI_COLUMN *column = &table->Columns[c];
            BYTE *base = table->Data + column->Offset;
            memmove(base + (SIZE_T)row * column->Width, base + (SIZE_T)(row + 1) * column->Width,
                    (SIZE_T)(table->Rows - row - 1) * column->Width);
        }
        UINT error = resize_rows(table, table->Rows - 1);
        if (error)
            return error;
        memmove(view->Tuples + view->Position - 1, view->Tuples + view->Position,
                (SIZE_T)(view->TupleCount - view->Position) * sizeof(ULONG));
        view->TupleCount--;
        view->Position--;
        for (ULONG i = 0; i < view->TupleCount; i++)
            if (view->Tuples[i] > row)
                view->Tuples[i]--;
        return ERROR_SUCCESS;
    }
    if (!record || record->Count < view->Count)
        return ERROR_INVALID_PARAMETER;
    ULONG values[MAX_COLUMNS] = {};
    BOOL given[MAX_COLUMNS] = {};
    UINT error = ERROR_SUCCESS;
    for (ULONG i = 0; i < view->Count && !error; i++) {
        ULONG column = view->Selected[i].Column;
        error = cell_for_field(database, &table->Columns[column], &record->Fields[i + 1], &values[column]);
        given[column] = TRUE;
    }
    if (error)
        return error;
    if (mode == MODIFY_UPDATE) {
        if (!view->Position)
            return ERROR_FUNCTION_FAILED;
        ULONG row = view->Tuples[view->Position - 1];
        for (ULONG c = 0; c < table->Count; c++) {
            if (!given[c])
                continue;
            /* a key is the row's identity and does not change. */
            if ((table->Columns[c].Type & 0x2000) && !same_cell(database, &table->Columns[c], MimeMsiCell(table, row, c), values[c]))
                return ERROR_FUNCTION_FAILED;
            write_cell(table, row, c, values[c]);
        }
        return ERROR_SUCCESS;
    }
    /* the columns the query leaves out are null, which only a column that may be null allows. */
    for (ULONG c = 0; c < table->Count; c++)
        if (!given[c] && !(table->Columns[c].Type & 0x1000))
            return ERROR_FUNCTION_FAILED;
    ULONG existing = table->Rows;
    BOOL identical = FALSE;
    for (ULONG row = 0; row < table->Rows; row++) {
        if (rows_have_key(database, table, row, values, &identical)) {
            existing = row;
            break;
        }
    }
    if (existing != table->Rows) {
        if (mode == MODIFY_INSERT || mode == MODIFY_INSERT_TEMPORARY || (mode == MODIFY_MERGE && !identical))
            return ERROR_FUNCTION_FAILED;
        if (mode == MODIFY_MERGE)
            return ERROR_SUCCESS;
        for (ULONG c = 0; c < table->Count; c++)
            write_cell(table, existing, c, values[c]);
        return ERROR_SUCCESS;
    }
    error = resize_rows(table, table->Rows + 1);
    if (error)
        return error;
    for (ULONG c = 0; c < table->Count; c++)
        write_cell(table, table->Rows - 1, c, values[c]);
    return ERROR_SUCCESS;
}

static UINT stage_table(const MIME_MSI_TABLE *table, MIME_MSI_TABLE *staged)
{
    *staged = *table;
    SIZE_T size = 0;
    for (ULONG c = 0; c < table->Count; c++)
        size += (SIZE_T)table->Rows * table->Columns[c].Width;
    staged->Data = MimeMsiAllocate(size ? size : 1);
    if (!staged->Data)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(staged->Data, table->Data, size);
    return ERROR_SUCCESS;
}

static void finish_change(MIME_MSI_DATABASE *database, MIME_MSI_TABLE *table, MIME_MSI_TABLE *staged,
                          ULONG strings, UINT error)
{
    if (error) {
        MimeMsiFree(staged->Data);
        while (database->StringCount > strings) {
            ULONG id = --database->StringCount;
            MimeMsiFree(database->Strings[id]);
            database->Strings[id] = nullptr;
        }
    } else {
        MimeMsiFree(table->Data);
        *table = *staged;
        database->Changes++;
    }
}

/* Each SQL statement changes a private table image. A bad value, duplicate key or allocation failure leaves
 * every original row intact, including when an UPDATE or DELETE matches more than one row. */
static UINT execute_mutation(MSI_VIEW *view, const MIME_MSI_RECORD *parameters)
{
    MIME_MSI_DATABASE *database = view->Database;
    /* Persistent handles must not silently serialize temporary rows. */
    if (view->Temporary && database->Writable)
        return ERROR_CALL_NOT_IMPLEMENTED;
    MIME_MSI_TABLE *table = view->Tables[0], staged;
    UINT error = stage_table(table, &staged);
    if (error)
        return error;
    ULONG strings = database->StringCount;
    MSI_VIEW work = *view;
    work.Tables[0] = &staged;
    work.Executed = TRUE;
    MIME_MSI_RECORD *values = MimeMsiRecordCreate(view->Count);
    if (!values)
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG i = 0; !error && i < view->Count; i++) {
        const SQL_OPERAND *operand = &view->Values[i];
        const MIME_MSI_FIELD *field = operand->Kind == OPERAND_PARAMETER ?
                                     &parameters->Fields[operand->Parameter] : &operand->Value;
        if (field->Kind == FIELD_STRING)
            error = MimeMsiFieldString(&values->Fields[i + 1], field->String);
        else if (field->Kind == FIELD_INTEGER)
            MimeMsiFieldInteger(&values->Fields[i + 1], field->Integer);
        else if (field->Kind != FIELD_NULL)
            error = ERROR_FUNCTION_FAILED;
    }
    if (!error && view->Operation == SQL_INSERT)
        error = modify_rows(&work, view->Temporary ? MODIFY_INSERT_TEMPORARY : MODIFY_INSERT, values);
    else {
        /* Descending original row indices remain valid as DELETE removes later rows. */
        for (ULONG row = table->Rows; !error && row; ) {
            ULONG index = --row;
            if (view->Where && !evaluate(view, view->Where, &index, parameters))
                continue;
            work.Tuples = &index;
            work.TupleCount = work.Position = 1;
            error = modify_rows(&work, view->Operation == SQL_DELETE ? MODIFY_DELETE : MODIFY_UPDATE, values);
        }
    }
    if (values)
        values->Object.Destroy(&values->Object);
    finish_change(database, table, &staged, strings, error);
    MimeMsiFree(view->Tuples);
    view->Tuples = nullptr;
    view->TupleCount = view->Position = 0;
    view->Executed = !error;
    view->Revision = database->Revision;
    return error;
}

static UINT modify_atomic(MSI_VIEW *view, int mode, const MIME_MSI_RECORD *record)
{
    if (view->Operation != SQL_SELECT || view->TableCount != 1 || !view->Executed ||
        view->Revision != view->Database->Revision)
        return ERROR_FUNCTION_FAILED;
    if (mode == MODIFY_INSERT_TEMPORARY && view->Database->Writable)
        return ERROR_CALL_NOT_IMPLEMENTED;
    MIME_MSI_TABLE *table = view->Tables[0], staged;
    UINT error = stage_table(table, &staged);
    if (error)
        return error;
    ULONG strings = view->Database->StringCount;
    MSI_VIEW work = *view;
    work.Tables[0] = &staged;
    work.Tuples = MimeMsiAllocate(((SIZE_T)view->TupleCount + 1) * sizeof(ULONG));
    if (!work.Tuples)
        error = ERROR_NOT_ENOUGH_MEMORY;
    else {
        memcpy(work.Tuples, view->Tuples, (SIZE_T)view->TupleCount * sizeof(ULONG));
        error = modify_rows(&work, mode, record);
    }
    finish_change(view->Database, table, &staged, strings, error);
    if (!error) {
        MimeMsiFree(view->Tuples);
        view->Tuples = work.Tuples;
        view->TupleCount = work.TupleCount;
        view->Position = work.Position;
        view->Revision = view->Database->Revision;
    } else
        MimeMsiFree(work.Tuples);
    return error;
}

UINT WINAPI MsiViewModify(MSIHANDLE handle, int mode, MSIHANDLE record_handle)
{
    MIME_REMOTE(PROXY_ViewModify, UINT, MIME_ARG(handle), MIME_ARG(mode), MIME_ARG(record_handle));
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(record_handle, MIME_MSI_KIND_RECORD);
    UINT error;
    if (!view || (record_handle && !record))
        error = ERROR_INVALID_HANDLE;
    else if (mode < MODIFY_SEEK || mode > MODIFY_VALIDATE_DELETE)
        error = ERROR_INVALID_PARAMETER;
    else if (mode == MODIFY_SEEK || mode == MODIFY_REFRESH || mode >= MODIFY_VALIDATE)
        error = ERROR_FUNCTION_FAILED;
    else
        error = modify_atomic(view, mode, record);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

/* the MsiViewGetColumnInfo type text: a letter for the kind, uppercase when nullable. */
static void column_type_text(const MIME_MSI_COLUMN *column, WCHAR out[16])
{
    BOOL nullable = !!(column->Type & 0x1000);
    WCHAR letter;
    ULONG length = 0;
    if (is_binary_column(column)) {
        letter = L'v';
    } else if (is_string_column(column)) {
        letter = column->Type & 0x0200 ? L'l' : L's';
        length = column->Type & 0xff;
    } else {
        letter = L'i';
        length = column->Width;
    }
    if (nullable)
        letter -= L'a' - L'A';
    _snwprintf(out, 16, L"%c%u", letter, length);
}

UINT WINAPI MsiViewGetColumnInfo(MSIHANDLE handle, UINT info, MSIHANDLE *out)
{
    MIME_REMOTE(PROXY_ViewGetColumnInfo, UINT, MIME_ARG(handle), MIME_ARG(info), MIME_ARG(out));
    if (!out || (info != MSICOLINFO_NAMES && info != MSICOLINFO_TYPES))
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    UINT error = view ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    MIME_MSI_RECORD *record = nullptr;
    if (!error && !(record = MimeMsiRecordCreate(view->Count)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG i = 0; !error && i < view->Count; i++) {
        const MIME_MSI_COLUMN *column = &view->Tables[view->Selected[i].Table]->Columns[view->Selected[i].Column];
        WCHAR type[16];
        if (info == MSICOLINFO_TYPES)
            column_type_text(column, type);
        error = MimeMsiFieldString(&record->Fields[i + 1], info == MSICOLINFO_NAMES ? column->Name : type);
    }
    if (!error && !(*out = MimeMsiAddHandle(&record->Object)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (error && record)
        record->Object.Destroy(&record->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiDatabaseGetPrimaryKeysW(MSIHANDLE handle, LPCWSTR table_name, MSIHANDLE *out)
{
    MIME_REMOTE(PROXY_DatabaseGetPrimaryKeysW, UINT, MIME_ARG(handle), MIME_ARG(table_name), MIME_ARG(out));
    if (!table_name || !out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    if (!database) {
        MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_PACKAGE);
        if (package)
            database = package->Database;
    }
    MIME_MSI_TABLE *table = nullptr;
    UINT error = database ? MimeMsiLoadTable(database, table_name, &table) : ERROR_INVALID_HANDLE;
    if (error == ERROR_BAD_QUERY_SYNTAX)
        error = ERROR_INVALID_TABLE;
    ULONG keys = 0;
    for (ULONG c = 0; !error && c < table->Count; c++)
        keys += !!(table->Columns[c].Type & 0x2000);
    MIME_MSI_RECORD *record = nullptr;
    if (!error && !(record = MimeMsiRecordCreate(keys)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG c = 0, at = 1; !error && c < table->Count; c++)
        if (table->Columns[c].Type & 0x2000)
            error = MimeMsiFieldString(&record->Fields[at++], table->Columns[c].Name);
    if (!error && !(*out = MimeMsiAddHandle(&record->Object)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (error && record)
        record->Object.Destroy(&record->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

/* no modification is ever refused here. */
UINT WINAPI MsiViewGetErrorW(MSIHANDLE handle, LPWSTR column, LPDWORD size)
{
    MIME_REMOTE(PROXY_ViewGetErrorW, UINT, MIME_ARG(handle), MIME_ARG(column), MIME_ARG(size));
    if (!size)
        return 0;
    AcquireSRWLockShared(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    ReleaseSRWLockShared(&MimeMsiLock);
    if (!view)
        return 0xFFFFFFFF; /* MSIDBERROR_INVALIDARG */
    if (column && *size)
        column[0] = 0;
    *size = 0;
    return 0; /* MSIDBERROR_NOERROR */
}
