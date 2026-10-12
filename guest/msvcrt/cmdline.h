/*
 * command line splitting of msvcrt's parse_cmdline, included by
 * startup.c once per character type with CHAR_T and PARSE_NAME defined.
 * the program name ends at the first blank outside quotes and keeps its
 * backslashes. arguments follow the 2n backslashes rule; inside quotes a
 * doubled quote gives a literal quote and ends the quoted part. with argv
 * NULL it only counts arguments and characters. quoted[i] tells whether
 * argument i contained quotes, which keeps it from wildcard expansion.
 */
static void PARSE_NAME(const CHAR_T *p, CHAR_T **argv, CHAR_T *args, int *quoted, int *argc, size_t *chars)
{
    int n = 0;
    size_t count = 0;
    int inquote = 0;
    int was_quoted = 0;
    CHAR_T c;

    if (argv)
        argv[0] = args;
    do {
        if (*p == '"') {
            inquote = !inquote;
            was_quoted = 1;
            c = '"';
            p++;
            continue;
        }
        count++;
        if (args)
            *args++ = *p;
        c = *p++;
        if (!c) {
            p--;
            break;
        }
    } while (inquote || (c != ' ' && c != '\t'));
    if (c && args)
        args[-1] = 0;
    if (quoted)
        quoted[0] = was_quoted;
    n = 1;

    inquote = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (argv)
            argv[n] = args;
        was_quoted = 0;
        for (;;) {
            int copy = 1;
            unsigned slashes = 0;
            while (*p == '\\') {
                p++;
                slashes++;
            }
            if (*p == '"') {
                if (!(slashes & 1)) {
                    if (inquote) {
                        copy = p[1] == '"';
                        if (copy)
                            p++;
                    } else {
                        copy = 0;
                    }
                    inquote = !inquote;
                    was_quoted = 1;
                }
                slashes >>= 1;
            }
            while (slashes--) {
                count++;
                if (args)
                    *args++ = '\\';
            }
            if (!*p || (!inquote && (*p == ' ' || *p == '\t'))) {
                count++;
                if (args)
                    *args++ = 0;
                break;
            }
            if (copy) {
                count++;
                if (args)
                    *args++ = *p;
            }
            p++;
        }
        if (quoted)
            quoted[n] = was_quoted;
        n++;
    }
    if (argv)
        argv[n] = NULL;
    *argc = n;
    *chars = count;
}
