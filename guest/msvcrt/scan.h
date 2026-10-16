/*
 * the scanf engine of msvcrt's _input and _winput, included by scanf.c
 * once per character type. it defines SCAN_NAME over format strings and
 * input of SCHAR; SCAN_WIDE is 1 for the wide engine, where %s, %c and
 * %[ store wide characters unless h is given, and %S, %C narrow ones.
 */
static int SCAN_NAME(SOURCE *in, const SCHAR *fmt, va_list ap)
{
    int assigned = 0;
    int matched_any = 0;
    int c;

    if (!fmt) {
        MSVCRT_INVALID(EINVAL);
        return EOF;
    }
    while (*fmt) {
        if (SCAN_ISSPACE(*fmt)) {
            while (SCAN_ISSPACE(*fmt))
                fmt++;
            do
                c = src_get(in);
            while (c != EOF && SCAN_ISSPACE(c));
            src_unget(in, c);
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%')
                fmt++;
            c = src_get(in);
            if (c == EOF)
                goto input_failure;
            if (c != (SCHAR_U)*fmt) {
                src_unget(in, c);
                return assigned;
            }
            fmt++;
            continue;
        }
        fmt++;

        int suppress = 0, width = 0, size = 0, widechar = 0;
        if (*fmt == '*') {
            suppress = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        for (int done = 0; !done;) {
            switch (*fmt) {
            case 'h':
                size = SZ_SHORT;
                widechar = -1;
                fmt++;
                break;
            case 'l':
                fmt++;
                if (*fmt == 'l') {
                    fmt++;
                    size = SZ_LONGLONG;
                } else {
                    size = SZ_LONG;
                    widechar = 1;
                }
                break;
            case 'L':
                size = SZ_LONGDOUBLE;
                fmt++;
                break;
            case 'w':
                widechar = 1;
                fmt++;
                break;
            case 'I':
                if (fmt[1] == '6' && fmt[2] == '4') {
                    size = SZ_LONGLONG;
                    fmt += 3;
                } else if (fmt[1] == '3' && fmt[2] == '2') {
                    size = 0;
                    fmt += 3;
                } else {
                    size = SZ_LONGLONG;
                    fmt++;
                }
                break;
            case 'z':
            case 't':
            case 'j':
                size = SZ_LONGLONG;
                fmt++;
                break;
            default:
                done = 1;
                break;
            }
        }
        if (!width)
            width = -1;

        SCHAR conv = *fmt++;
        if (!conv)
            break;
        if (conv != 'c' && conv != 'C' && conv != '[' && conv != 'n') {
            do
                c = src_get(in);
            while (c != EOF && SCAN_ISSPACE(c));
            src_unget(in, c);
        }

        switch (conv) {
        case 'n':
            if (!suppress) {
                if (size == SZ_SHORT)
                    *va_arg(ap, short *) = (short)in->consumed;
                else if (size == SZ_LONGLONG)
                    *va_arg(ap, long long *) = in->consumed;
                else
                    *va_arg(ap, int *) = in->consumed;
            }
            continue;

        case 'C':
            if (!widechar)
                widechar = SCAN_WIDE ? -1 : 1;
            [[fallthrough]];
        case 'c': {
            int wide_out = SCAN_WIDE ? widechar >= 0 : widechar > 0;
            if (width == -1)
                width = 1;
            void *dst = suppress ? nullptr : va_arg(ap, void *);
            void *first = dst;
            size_t room = dst && in->secure ? va_arg(ap, unsigned) : (size_t)-1;
            int got = 0;
            while (got < width) {
                c = src_get(in);
                if (c == EOF)
                    break;
                if (dst && !room--) {
                    store_char(&first, 0, wide_out);
                    MsvcrtSetErrno(ENOMEM);
                    return assigned;
                }
                if (dst)
                    store_char(&dst, c, wide_out);
                got++;
            }
            if (!got)
                goto input_failure;
            matched_any = 1;
            if (dst)
                assigned++;
            continue;
        }

        case 'S':
            if (!widechar)
                widechar = SCAN_WIDE ? -1 : 1;
            [[fallthrough]];
        case 's': {
            int wide_out = SCAN_WIDE ? widechar >= 0 : widechar > 0;
            void *dst = suppress ? nullptr : va_arg(ap, void *);
            void *first = dst;
            size_t room = dst && in->secure ? va_arg(ap, unsigned) : (size_t)-1;
            int got = 0;
            while (width == -1 || got < width) {
                c = src_get(in);
                if (c == EOF || SCAN_ISSPACE(c)) {
                    src_unget(in, c);
                    break;
                }
                if (dst && room-- <= 1) {
                    store_char(&first, 0, wide_out);
                    MsvcrtSetErrno(ENOMEM);
                    return assigned;
                }
                if (dst)
                    store_char(&dst, c, wide_out);
                got++;
            }
            if (!got)
                goto match_failure;
            if (dst) {
                store_char(&dst, 0, wide_out);
                assigned++;
            }
            matched_any = 1;
            continue;
        }

        case '[': {
            int wide_out = SCAN_WIDE ? widechar >= 0 : widechar > 0;
            int invert = 0;
            const SCHAR *set = fmt;
            if (*set == '^') {
                invert = 1;
                set++;
            }
            const SCHAR *start = set;
            if (*set == ']')
                set++;
            while (*set && *set != ']')
                set++;
            const SCHAR *end = set;
            fmt = *set ? set + 1 : set;
            void *dst = suppress ? nullptr : va_arg(ap, void *);
            void *first = dst;
            size_t room = dst && in->secure ? va_arg(ap, unsigned) : (size_t)-1;
            int got = 0;
            while (width == -1 || got < width) {
                c = src_get(in);
                if (c == EOF)
                    break;
                int in_set = 0;
                for (const SCHAR *p = start; p < end; p++) {
                    if (p + 2 < end && p[1] == '-') {
                        SCHAR_U lo = p[0], hi = p[2];
                        if (lo > hi) {
                            SCHAR_U t = lo;
                            lo = hi;
                            hi = t;
                        }
                        if ((SCHAR_U)c >= lo && (SCHAR_U)c <= hi) {
                            in_set = 1;
                            break;
                        }
                        p += 2;
                    } else if ((SCHAR_U)c == (SCHAR_U)*p) {
                        in_set = 1;
                        break;
                    }
                }
                if (in_set == invert) {
                    src_unget(in, c);
                    break;
                }
                if (dst && room-- <= 1) {
                    store_char(&first, 0, wide_out);
                    MsvcrtSetErrno(ENOMEM);
                    return assigned;
                }
                if (dst)
                    store_char(&dst, c, wide_out);
                got++;
            }
            if (!got)
                goto match_failure;
            if (dst) {
                store_char(&dst, 0, wide_out);
                assigned++;
            }
            matched_any = 1;
            continue;
        }

        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X':
        case 'p': {
            int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : conv == 'i' ? 0 : 10;
            if (conv == 'p')
                size = SZ_LONGLONG;
            uint64_t value = 0;
            int neg = 0, digits = 0;
            c = src_get(in);
            if (c == EOF)
                goto input_failure;
            if ((c == '-' || c == '+') && width != 0) {
                neg = c == '-';
                if (width > 0)
                    width--;
                c = width ? src_get(in) : EOF;
            }
            if (c == '0' && width != 0) {
                digits = 1;
                if (width > 0)
                    width--;
                int c2 = width ? src_get(in) : EOF;
                if ((c2 == 'x' || c2 == 'X') && (base == 0 || base == 16)) {
                    base = 16;
                    if (width > 0)
                        width--;
                    c = width ? src_get(in) : EOF;
                    digits = 0;
                    /* "0x" alone still converts the zero. */
                    if (c == EOF || hexval(c) < 0) {
                        digits = 1;
                    }
                } else {
                    if (base == 0)
                        base = 8;
                    c = c2;
                }
            } else if (base == 0) {
                base = 10;
            }
            while (c != EOF && width != 0) {
                int d = hexval(c);
                if (d < 0 || d >= base)
                    break;
                value = value * base + d;
                digits++;
                if (width > 0)
                    width--;
                c = width ? src_get(in) : EOF;
            }
            src_unget(in, c);
            if (!digits)
                goto match_failure;
            if (neg)
                value = 0 - value;
            if (!suppress) {
                if (size == SZ_LONGLONG)
                    *va_arg(ap, uint64_t *) = value;
                else if (size == SZ_SHORT)
                    *va_arg(ap, unsigned short *) = (unsigned short)value;
                else
                    *va_arg(ap, unsigned int *) = (unsigned int)value;
                assigned++;
            }
            matched_any = 1;
            continue;
        }

        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            char num[400];
            int n = 0, seen_digit = 0;
            c = src_get(in);
            if (c == EOF)
                goto input_failure;
            if ((c == '-' || c == '+') && width != 0) {
                num[n++] = (char)c;
                if (width > 0)
                    width--;
                c = width ? src_get(in) : EOF;
            }
            while (c != EOF && width != 0 && c >= '0' && c <= '9') {
                if (n < (int)sizeof(num) - 8)
                    num[n++] = (char)c;
                seen_digit = 1;
                if (width > 0)
                    width--;
                c = width ? src_get(in) : EOF;
            }
            if (c == '.' && width != 0) {
                num[n++] = '.';
                if (width > 0)
                    width--;
                c = width ? src_get(in) : EOF;
                while (c != EOF && width != 0 && c >= '0' && c <= '9') {
                    if (n < (int)sizeof(num) - 8)
                        num[n++] = (char)c;
                    seen_digit = 1;
                    if (width > 0)
                        width--;
                    c = width ? src_get(in) : EOF;
                }
            }
            if (seen_digit && (c == 'e' || c == 'E') && width != 0) {
                num[n++] = 'e';
                if (width > 0)
                    width--;
                c = width ? src_get(in) : EOF;
                if ((c == '-' || c == '+') && width != 0) {
                    num[n++] = (char)c;
                    if (width > 0)
                        width--;
                    c = width ? src_get(in) : EOF;
                }
                while (c != EOF && width != 0 && c >= '0' && c <= '9') {
                    if (n < (int)sizeof(num) - 2)
                        num[n++] = (char)c;
                    if (width > 0)
                        width--;
                    c = width ? src_get(in) : EOF;
                }
            }
            src_unget(in, c);
            if (!seen_digit)
                goto match_failure;
            num[n] = 0;
            int range;
            double v = MsvcrtStrtod(num, nullptr, &range);
            if (!suppress) {
                if (size == SZ_LONG || size == SZ_LONGDOUBLE)
                    *va_arg(ap, double *) = v;
                else
                    *va_arg(ap, float *) = (float)v;
                assigned++;
            }
            matched_any = 1;
            continue;
        }

        default:
            /* an unknown conversion matches its own character, like msvcrt. */
            c = src_get(in);
            if (c != (SCHAR_U)conv) {
                src_unget(in, c);
                return assigned;
            }
            continue;
        }
    }
    return assigned;

input_failure:
    return assigned || matched_any ? assigned : EOF;

match_failure:
    return assigned;
}
