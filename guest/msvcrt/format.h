/*
 * the printf formatting engine of msvcrt's _output and _woutput,
 * included by printf.c once per character type. it defines FORMAT_NAME
 * over format strings of FCHAR, writing FCHAR characters to a SINK.
 * FORMAT_WIDE is 1 for the wide engine, where %s and %c take wide
 * arguments and %S and %C narrow ones.
 *
 * the parser is msvcrt's state machine: a character that does not fit
 * the state ends the conversion and is printed, so "%y" prints "y".
 */
static int FORMAT_NAME(SINK *out, const FCHAR *fmt, va_list ap)
{
    int state = ST_NORMAL;
    int flags = 0, width = 0, prec = -1, caps = 0, no_output = 0;
    FCHAR c;

    if (!fmt) {
        MSVCRT_INVALID(EINVAL);
        return -1;
    }
    while ((c = *fmt++) && out->count >= 0) {
        int cls = (unsigned)c - ' ' <= 'z' - ' ' ? g_class[c - ' '] : CL_OTHER;
        state = g_next[cls][state];
        switch (state) {
        case ST_NORMAL:
            sink_put(out, c);
            continue;
        case ST_PERCENT:
            flags = 0;
            width = 0;
            prec = -1;
            caps = 0;
            no_output = 0;
            continue;
        case ST_FLAG:
            switch (c) {
            case '+':
                flags |= FL_SIGN;
                break;
            case ' ':
                flags |= FL_SIGNSP;
                break;
            case '-':
                flags |= FL_LEFT;
                break;
            case '0':
                flags |= FL_LEADZERO;
                break;
            case '#':
                flags |= FL_ALTERNATE;
                break;
            }
            continue;
        case ST_WIDTH:
            if (c == '*') {
                width = va_arg(ap, int);
                if (width < 0) {
                    flags |= FL_LEFT;
                    width = -width;
                }
            } else {
                width = width * 10 + (c - '0');
            }
            continue;
        case ST_DOT:
            prec = 0;
            continue;
        case ST_PRECIS:
            if (c == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
            } else {
                prec = prec * 10 + (c - '0');
            }
            continue;
        case ST_SIZE:
            switch (c) {
            case 'l':
                if (*fmt == 'l') {
                    fmt++;
                    flags |= FL_LONGLONG;
                } else {
                    flags |= FL_LONG;
                }
                break;
            case 'h':
                flags |= FL_SHORT;
                break;
            case 'w':
                flags |= FL_WIDECHAR;
                break;
            case 'I':
            case 'j':
            case 'z':
            case 't':
                flags |= FL_I64;
                if (c == 'I') {
                    if (fmt[0] == '6' && fmt[1] == '4') {
                        fmt += 2;
                        break;
                    }
                    if (fmt[0] == '3' && fmt[1] == '2') {
                        fmt += 2;
                        flags &= ~FL_I64;
                        break;
                    }
                } else if (c == 'j') {
                    break;
                }
                if (*fmt == 'X' || *fmt == 'd' || *fmt == 'i' || *fmt == 'o' || *fmt == 'u' || *fmt == 'x')
                    break;
                state = ST_NORMAL;
                sink_put(out, c);
                break;
            }
            continue;
        }

        /* ST_TYPE: the conversion itself. */
        char nbuf[600];
        char *heap = nullptr;
        const void *text = nullptr;
        int text_wide = 0;
        int len = 0;
        char prefix[2];
        int prefix_len = 0;
        unsigned radix = 0;
        int hexadd = 0;
#if FORMAT_WIDE
        FCHAR one[1];
#endif

        switch (c) {
        case 'C':
#if FORMAT_WIDE
            if (!(flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)))
                flags |= FL_SHORT;
#else
            if (!(flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)))
                flags |= FL_WIDECHAR;
#endif
            [[fallthrough]];
        case 'c': {
            int arg = va_arg(ap, int);
#if FORMAT_WIDE
            one[0] = (flags & FL_SHORT) ? (FCHAR)(unsigned char)arg : (FCHAR)arg;
            text = one;
            len = 1;
#else
            if (flags & (FL_LONG | FL_WIDECHAR)) {
                wchar_t wc = (wchar_t)arg;
                if (wc > 0xff)
                    no_output = 1;
                nbuf[0] = (char)wc;
            } else {
                nbuf[0] = (char)arg;
            }
            text = nbuf;
            len = 1;
#endif
            break;
        }
        case 'S':
#if FORMAT_WIDE
            if (!(flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)))
                flags |= FL_SHORT;
#else
            if (!(flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)))
                flags |= FL_WIDECHAR;
#endif
            [[fallthrough]];
        case 's': {
            const void *s = va_arg(ap, const void *);
            int max = prec == -1 ? INT_MAX : prec;
#if FORMAT_WIDE
            int wide_arg = !(flags & FL_SHORT);
#else
            int wide_arg = (flags & (FL_LONG | FL_WIDECHAR)) != 0;
#endif
            if (wide_arg) {
                const wchar_t *w = s ? s : L"(null)";
                while (len < max && w[len])
                    len++;
                text = w;
                text_wide = 1;
            } else {
                const char *a = s ? s : "(null)";
                while (len < max && a[len])
                    len++;
                text = a;
            }
            break;
        }
        case 'Z': {
            const ANSI_STRING *str = va_arg(ap, const ANSI_STRING *);
            if (!str || !str->Buffer) {
                text = "(null)";
                len = 6;
            } else if (flags & FL_WIDECHAR) {
                text = str->Buffer;
                text_wide = 1;
                len = str->Length / 2;
            } else {
                text = str->Buffer;
                len = str->Length;
            }
            break;
        }
        case 'n': {
            void *p = va_arg(ap, void *);
            if (!g_count_output) {
                MSVCRT_INVALID(EINVAL);
                return -1;
            }
            if (flags & FL_SHORT)
                *(short *)p = (short)out->count;
            else
                *(int *)p = out->count;
            no_output = 1;
            break;
        }
        case 'E':
        case 'G':
        case 'A':
            caps = 1;
            c += 'a' - 'A';
            [[fallthrough]];
        case 'e':
        case 'f':
        case 'g':
        case 'a': {
            double v = va_arg(ap, double);
            char *buf = nbuf;
            if (prec < 0)
                prec = 6;
            else if (prec == 0 && c == 'g')
                prec = 1;
            if (prec > 512)
                prec = 512;
            if (prec > 163) {
                heap = malloc(prec + 350);
                if (heap)
                    buf = heap;
                else
                    prec = 163;
            }
            MsvcrtFormatDouble(v, (char)c, prec, caps, (flags & FL_ALTERNATE) != 0, buf);
            if (*buf == '-') {
                flags |= FL_NEGATIVE;
                buf++;
            }
            flags |= FL_SIGNED;
            text = buf;
            len = (int)strlen(buf);
            break;
        }
        case 'd':
        case 'i':
            flags |= FL_SIGNED;
            radix = 10;
            break;
        case 'u':
            radix = 10;
            break;
        case 'o':
            radix = 8;
            if (flags & FL_ALTERNATE)
                flags |= FL_FORCEOCTAL;
            break;
        case 'p':
            prec = 16;
            flags |= FL_I64;
            hexadd = 'A' - '9' - 1;
            radix = 16;
            break;
        case 'X':
            hexadd = 'A' - '9' - 1;
            radix = 16;
            break;
        case 'x':
            hexadd = 'a' - '9' - 1;
            radix = 16;
            break;
        }

        if (radix) {
            if (radix == 16 && (flags & FL_ALTERNATE)) {
                prefix[0] = '0';
                prefix[1] = (char)('9' + 1 + hexadd + 'X' - 'A');
                prefix_len = 2;
            }
            uint64_t v;
            if (flags & (FL_I64 | FL_LONGLONG)) {
                v = va_arg(ap, uint64_t);
            } else if (flags & FL_SHORT) {
                int a = va_arg(ap, int);
                v = (flags & FL_SIGNED) ? (uint64_t)(int64_t)(short)a : (uint64_t)(unsigned short)a;
            } else {
                unsigned a = va_arg(ap, unsigned);
                v = (flags & FL_SIGNED) ? (uint64_t)(int64_t)(int)a : (uint64_t)a;
            }
            if ((flags & FL_SIGNED) && (int64_t)v < 0) {
                v = 0 - v;
                flags |= FL_NEGATIVE;
            }
            if (!(flags & (FL_I64 | FL_LONGLONG)))
                v &= 0xffffffffu;
            if (prec >= 0) {
                flags &= ~FL_LEADZERO;
                if (prec > 512)
                    prec = 512;
            } else {
                prec = 1;
            }
            if (!v)
                prefix_len = 0;
            char *end = nbuf + sizeof(nbuf) - 1;
            char *p = end;
            while (prec-- > 0 || v) {
                int d = (int)(v % radix) + '0';
                v /= radix;
                if (d > '9')
                    d += hexadd;
                *--p = (char)d;
            }
            len = (int)(end - p);
            if ((flags & FL_FORCEOCTAL) && (!len || *p != '0')) {
                *--p = '0';
                len++;
            }
            text = p;
        }

        if (!no_output && text) {
            if (flags & FL_SIGNED) {
                if (flags & FL_NEGATIVE) {
                    prefix[0] = '-';
                    prefix_len = 1;
                } else if (flags & FL_SIGN) {
                    prefix[0] = '+';
                    prefix_len = 1;
                } else if (flags & FL_SIGNSP) {
                    prefix[0] = ' ';
                    prefix_len = 1;
                }
            }
            int pad = width - len - prefix_len;
            if (!(flags & (FL_LEFT | FL_LEADZERO)))
                sink_repeat(out, ' ', pad);
            for (int i = 0; i < prefix_len; i++)
                sink_put(out, (FCHAR)(unsigned char)prefix[i]);
            if ((flags & (FL_LEFT | FL_LEADZERO)) == FL_LEADZERO)
                sink_repeat(out, '0', pad);
            if (text_wide) {
                const wchar_t *w = text;
                for (int i = 0; i < len && out->count >= 0; i++) {
#if FORMAT_WIDE
                    sink_put(out, w[i]);
#else
                    if (w[i] > 0xff) {
                        MsvcrtSetErrno(EILSEQ);
                        out->count = -1;
                        break;
                    }
                    sink_put(out, (char)w[i]);
#endif
                }
            } else {
                const unsigned char *a = text;
                for (int i = 0; i < len && out->count >= 0; i++)
                    sink_put(out, (FCHAR)a[i]);
            }
            if (out->count >= 0 && (flags & FL_LEFT))
                sink_repeat(out, ' ', pad);
        }
        free(heap);
    }
    return out->count;
}
