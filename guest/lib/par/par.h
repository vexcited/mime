#ifndef MIME_LIB_PAR_H
#define MIME_LIB_PAR_H

/* does rows first to end, which the caller may split as it likes. several threads call it at once. */
typedef void (*MimeParFn)(void *ctx, int first, int end);

/*
 * runs fn over first to end in pieces of grain rows, on the calling thread and on idle
 * processors. returns when every row is done. a call made while another one runs does
 * its rows alone.
 */
void mime_par_rows(int first, int end, int grain, MimeParFn fn, void *ctx);

#endif
