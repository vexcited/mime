#include "internal.h"

static void begin(Hlsl *h, const HlslOptions *options)
{
    h->options = options;
    h->include = options->include;
    h->warnings_are_errors = (options->flags & HLSL_WARNINGS_ARE_ERRORS) != 0;
    if (options->name)
        h->source_name = hl_strndup(h, options->name, __builtin_strlen(options->name));
    else
        h->source_name = hl_sprintf(h, "Shader@0x%p", (const void *)options->source);
}

int hlsl_preprocess(Hlsl *h, const HlslOptions *options)
{
    begin(h, options);
    if (hl_preprocess(h))
        return -1;
    hl_print_tokens(h, &h->output);
    return 0;
}

int hlsl_compile(Hlsl *h, const HlslOptions *options)
{
    begin(h, options);
    if (hl_preprocess(h))
        return -1;
    return hl_compile(h);
}
