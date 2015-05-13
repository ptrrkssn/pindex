/*
** html.h
*/

#ifndef PTMS_HTML_H
#define PTMS_HTML_H 1

extern int
html_putc(int c, FILE *fp);

extern int
html_puts(const char *str, FILE *fp);

extern void
html_header(const char *title);

extern void
html_footer(const char *footer);

extern int
html_href(FILE *fp,
	  const char *label,
	  const char *alt,
	  const char *ref,
	  ...);

extern int
html_email(FILE *fp,
	   const char *email);

extern int
html_putbody(const char *str,
	     FILE *fp);

#endif



