#ifndef PTMS_FORM_H
#define PTMS_FORM_H

typedef struct
{
    char *name;
    char *value;
} FORMVAR;

#define MAXVARS 1024

extern int
form_init(FILE *fp);

extern char *
form_get(const char *var);

extern void
form_cgi_post(FILE *fp);

extern int
form_foreach(int (*fun)(const char *var, const char *val, void *x), void *x);

#endif
