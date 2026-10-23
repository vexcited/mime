/*
 * printf with positional parameters. the format is read once to learn each numbered argument's type, the arguments are
 * taken from the va_list in number order, and the format is rewritten without the numbers, along with a table of the
 * arguments in the order the engine takes them. included once per character type, with POSITIONAL_NAME and FCHAR
 * defined.
 *
 * numbers go from 1 to 99 and every number up to the highest must be used, a gap is an invalid parameter. a conversion
 * without a number takes the next argument after those taken without a number, a guess for formats that mix the two.
 */
static int POSITIONAL_NAME(const FCHAR *fmt, va_list ap, FCHAR **rewritten, PARG **sequence)
{
    POSITIONS pos = {};
    size_t length = 0;
    while (fmt[length])
        length++;
    FCHAR *out = malloc((length + 1) * sizeof(FCHAR));
    PARG *seq = malloc((length + 2) * sizeof(PARG));
    PARG table[ARGMAX + 1];
    *rewritten = out;
    *sequence = seq;
    if (!out || !seq)
        return -1;
    size_t used = 0, at = 0;
    int unnumbered = 0;

    /* first pass: kinds by number; second pass: writes the format and the order. */
    for (int pass = 0; pass < 2; pass++) {
        const FCHAR *f = fmt;
        unnumbered = 0;
        used = 0;
        at = 0;
        while (*f) {
            if (*f != '%') {
                out[at++] = *f++;
                continue;
            }
            out[at++] = *f++;
            if (*f == '%') {
                out[at++] = *f++;
                continue;
            }
            /* the conversion's number, when digits and a dollar follow. */
            int number = 0;
            const FCHAR *p = f;
            int value = 0;
            while (*p >= '0' && *p <= '9' && value < 1000)
                value = value * 10 + (*p++ - '0');
            if (p != f && *p == '$') {
                number = value;
                f = p + 1;
                if (number < 1 || number > ARGMAX)
                    return -1;
            }
            int width_star = 0, prec_star = 0, wide64 = 0, size_short = 0, kind = PK_NONE;
            while (*f == '-' || *f == '+' || *f == ' ' || *f == '#' || *f == '0')
                out[at++] = *f++;
            /* width. */
            if (*f == '*') {
                f++;
                const FCHAR *q = f;
                int star = 0;
                while (*q >= '0' && *q <= '9' && star < 1000)
                    star = star * 10 + (*q - '0'), q++;
                if (q != f && *q == '$') {
                    f = q + 1;
                    width_star = star;
                } else {
                    width_star = -1;
                }
                if (width_star > ARGMAX)
                    return -1;
                out[at++] = '*';
            } else {
                while (*f >= '0' && *f <= '9')
                    out[at++] = *f++;
            }
            if (*f == '.') {
                out[at++] = *f++;
                if (*f == '*') {
                    f++;
                    const FCHAR *q = f;
                    int star = 0;
                    while (*q >= '0' && *q <= '9' && star < 1000)
                        star = star * 10 + (*q - '0'), q++;
                    if (q != f && *q == '$') {
                        f = q + 1;
                        prec_star = star;
                    } else {
                        prec_star = -1;
                    }
                    if (prec_star > ARGMAX)
                        return -1;
                    out[at++] = '*';
                } else {
                    while (*f >= '0' && *f <= '9')
                        out[at++] = *f++;
                }
            }
            /* size prefix. */
            for (;;) {
                FCHAR c = *f;
                if (c == 'l' && f[1] == 'l') {
                    wide64 = 1;
                    out[at++] = *f++;
                    out[at++] = *f++;
                } else if (c == 'l' || c == 'w') {
                    out[at++] = *f++;
                } else if (c == 'h') {
                    size_short = 1;
                    out[at++] = *f++;
                } else if (c == 'j') {
                    wide64 = 1;
                    out[at++] = *f++;
                } else if (c == 'z' || c == 't' || c == 'I') {
                    if (c == 'I' && f[1] == '6' && f[2] == '4') {
                        wide64 = 1;
                        out[at++] = *f++;
                        out[at++] = *f++;
                        out[at++] = *f++;
                        continue;
                    }
                    if (c == 'I' && f[1] == '3' && f[2] == '2') {
                        wide64 = 0;
                        out[at++] = *f++;
                        out[at++] = *f++;
                        out[at++] = *f++;
                        continue;
                    }
                    if (sizeof(void *) == 8)
                        wide64 = 1;
                    out[at++] = *f++;
                } else {
                    break;
                }
            }
            FCHAR type = *f;
            switch (type) {
            case 'd':
            case 'i':
            case 'o':
            case 'u':
            case 'x':
            case 'X':
                kind = wide64 && !size_short ? PK_I64 : PK_INT;
                break;
            case 'c':
            case 'C':
                kind = PK_INT;
                break;
            case 's':
            case 'S':
            case 'Z':
            case 'p':
            case 'n':
                kind = PK_PTR;
                break;
            case 'e':
            case 'E':
            case 'f':
            case 'F':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
                kind = PK_DOUBLE;
                break;
            }
            if (type)
                out[at++] = *f++;
            /* the arguments this conversion takes, in the order the engine will ask for them. */
            int wanted[3], wanted_kind[3], count = 0;
            if (width_star) {
                wanted[count] = width_star > 0 ? width_star : ++unnumbered;
                wanted_kind[count++] = PK_INT;
            }
            if (prec_star) {
                wanted[count] = prec_star > 0 ? prec_star : ++unnumbered;
                wanted_kind[count++] = PK_INT;
            }
            if (kind != PK_NONE) {
                wanted[count] = number ? number : ++unnumbered;
                wanted_kind[count++] = kind;
            }
            for (int i = 0; i < count; i++) {
                int n = wanted[i];
                if (n < 1 || n > ARGMAX)
                    return -1;
                if (pass == 0) {
                    if (pos.kind[n] == PK_NONE)
                        pos.kind[n] = wanted_kind[i];
                    if (n > pos.highest)
                        pos.highest = n;
                } else {
                    seq[used++] = table[n];
                }
            }
        }
        out[at] = 0;
        if (pass == 0) {
            for (int n = 1; n <= pos.highest; n++) {
                if (pos.kind[n] == PK_NONE)
                    return -1;
                switch (pos.kind[n]) {
                case PK_INT:
                    table[n].u = (unsigned)va_arg(ap, int);
                    break;
                case PK_I64:
                    table[n].u = va_arg(ap, uint64_t);
                    break;
                case PK_PTR:
                    table[n].p = va_arg(ap, void *);
                    break;
                default:
                    table[n].d = va_arg(ap, double);
                    break;
                }
            }
        }
    }
    return 0;
}
