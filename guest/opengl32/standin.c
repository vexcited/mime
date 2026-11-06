/*
 * a vertex shader to stand in for the one a separable fragment program does not have: the host links whole programs, and
 * a fragment input needs an output of the same name in the stage before it. the stand-in declares one for each input of
 * the fragment source, with its layout and interpolation qualifiers, and writes zeros to them.
 */
#include "opengl32.h"

static BOOL id_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

typedef struct Text {
    char *data;
    SIZE_T len, cap;
} Text;

static void text_add(Text *t, const char *s, SIZE_T n)
{
    if (t->len + n + 1 > t->cap) {
        SIZE_T cap = t->cap ? t->cap : 512;
        while (cap < t->len + n + 1)
            cap *= 2;
        char *d = gl_realloc(t->data, cap);
        if (!d)
            return;
        t->data = d;
        t->cap = cap;
    }
    memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = 0;
}

static void text_str(Text *t, const char *s)
{
    text_add(t, s, strlen(s));
}

/* one statement of the top level, as words: layout(...) kept whole, the rest split at spaces and brackets. */
static void declaration(const char *s, SIZE_T n, Text *outputs, Text *body)
{
    for (SIZE_T i = 0; i < n; i++)
        if (s[i] == '{')
            return;
    char layout[96] = "";
    char words[10][48];
    int count = 0;
    char array[24] = "";
    for (SIZE_T i = 0; i < n;) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
        } else if (c == '[') {
            SIZE_T k = i;
            while (k < n && s[k] != ']')
                k++;
            SIZE_T len = k + 1 - i;
            if (len < sizeof(array)) {
                memcpy(array, s + i, len);
                array[len] = 0;
            }
            i = k + 1;
        } else if (id_char(c)) {
            SIZE_T k = i;
            while (k < n && id_char(s[k]))
                k++;
            if (k - i == 6 && !memcmp(s + i, "layout", 6)) {
                SIZE_T close = k;
                while (close < n && s[close] != ')')
                    close++;
                SIZE_T len = close + 1 - i;
                if (len < sizeof(layout)) {
                    memcpy(layout, s + i, len);
                    layout[len] = 0;
                }
                i = close + 1;
                continue;
            }
            if (count < 10 && k - i < sizeof(words[0])) {
                memcpy(words[count], s + i, k - i);
                words[count][k - i] = 0;
                count++;
            }
            i = k;
        } else {
            return;
        }
    }
    /* the in qualifier, then the type and the name are the last words. */
    int in = -1;
    for (int k = 0; k < count; k++)
        if (!strcmp(words[k], "in"))
            in = k;
    if (in < 0 || count - in != 3)
        return;
    const char *type = words[in + 1], *name = words[in + 2];
    if (layout[0]) {
        text_str(outputs, layout);
        text_str(outputs, " ");
    }
    for (int k = 0; k < in; k++) {
        text_str(outputs, words[k]);
        text_str(outputs, " ");
    }
    text_str(outputs, "out ");
    text_str(outputs, type);
    text_str(outputs, " ");
    text_str(outputs, name);
    text_str(outputs, array);
    text_str(outputs, ";\n");
    if (array[0]) {
        text_str(body, "  for (int i = 0; i < ");
        text_add(body, array + 1, strlen(array) - 2);
        text_str(body, "; i++) ");
        text_str(body, name);
        text_str(body, "[i] = ");
    } else {
        text_str(body, "  ");
        text_str(body, name);
        text_str(body, " = ");
    }
    text_str(body, type);
    text_str(body, "(0);\n");
}

char *gl_standin_vertex(const char *fragment)
{
    Text outputs = {}, body = {};
    SIZE_T n = strlen(fragment);
    int depth = 0;
    SIZE_T start = 0;
    for (SIZE_T i = 0; i < n; i++) {
        char c = fragment[i];
        if (c == '/' && fragment[i + 1] == '/') {
            while (i < n && fragment[i] != '\n')
                i++;
            start = i;
        } else if (c == '/' && fragment[i + 1] == '*') {
            i += 2;
            while (i < n && !(fragment[i] == '*' && fragment[i + 1] == '/'))
                i++;
            i++;
            start = i + 1;
        } else if (c == '#') {
            while (i < n && fragment[i] != '\n')
                i++;
            start = i;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            if (depth)
                depth--;
            if (!depth)
                start = i + 1;
        } else if (c == ';' && !depth) {
            declaration(fragment + start, i - start, &outputs, &body);
            start = i + 1;
        }
    }
    Text out = {};
    text_str(&out, "#version 410 core\n");
    if (outputs.data)
        text_str(&out, outputs.data);
    text_str(&out, "void main() {\n  gl_Position = vec4(0.0);\n");
    if (body.data)
        text_str(&out, body.data);
    text_str(&out, "}\n");
    gl_free(outputs.data);
    gl_free(body.data);
    return out.data;
}
