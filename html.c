/*
** html.c
*/

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "html.h"

int
html_putc(int c,
	  FILE *fp)
{
  int rc;

  
  switch (c)
  {
    case '<':
      rc = fputs("&lt;", fp);
      break;
      
    case '>':
      rc = fputs("&gt;", fp);
      break;
      
    case '&':
      rc = fputs("&amp;", fp);
      break;
      
    case '"':
      rc = fputs("&quot;", fp);
      break;

    default:
      if (!iscntrl(c) || c == '\t' || c == '\r' || c == '\n')
	rc = putc(c, fp);
      else
	rc = putc('?', fp);
  }

  return rc;
}


int
html_puts(const char *str,
	  FILE *fp)
{
  int rc = 0;
  
  if (!str)
    return 0;
  
  while (*str && rc != EOF)
    rc = html_putc(*str++, fp);

  return rc;
}

int
html_putbody(const char *str,
	     FILE *fp)
{
  int rc = 0;
  
  if (!str)
    return 0;
  
  while (*str && rc != EOF)
  {
    switch (*str)
    {
      case '\n':
	rc = fputs("<br>", fp);
	break;

      default:
	rc = html_putc(*str, fp);
    }
    ++str;
  }

  return rc;
}

void
filesend(const char *path,
	 FILE *out)
{
  FILE *in;
  int c;


  in = fopen(path, "r");
  if (!in)
    return;

  while ((c = getc(in)) != EOF)
    putc(c, out);

  fclose(in);
}


void
html_header(const char *title)
{
  char *path = getenv("PTMS_HEADER");

  
  if (path)
    filesend(path, stdout);
  else
  {
    puts("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">");
    puts("<html>");
    puts("<head>");
#if 0
    puts("<link rel=\"Stylesheet\" type=\"text/css\" href=\"/atk.css\">");
#endif
    printf("<title>");
    html_puts(title, stdout);
    puts("</title>");
    puts("</head>");
  }
  
  puts("<body>");
  printf("<h2>");
  html_puts(title, stdout);
  puts("</h2>");
}

void
html_footer(const char *footer)
{
  char *path = getenv("PTMS_FOOTER");

  if (footer)
  {
    puts("<hr noshade size=1>");
    html_puts(footer, stdout);
  }

  if (path)
    filesend(path, stdout);
  else
  {
    puts("</body>");
    puts("</html>");
  }
}


int
html_href(FILE *fp,
	  const char *label,
	  const char *alt,
	  const char *ref,
	  ...)
{
  int rc;
  char buf[10240];
  va_list ap;
  

  va_start(ap, ref);
  vsprintf(buf, ref, ap);
  va_end(ap);

  rc = fputs("<a", fp);
  if (rc < 0)
    return rc;

  if (alt)
  {
  rc = fputs(" title=\"", fp);
  if (rc < 0)
    return rc;
  
  html_puts(alt, fp);
  
  rc = fputs("\"", fp);
  if (rc < 0)
    return rc;

  }
  
  rc = fputs(" href=\"", fp);
  if (rc < 0)
    return rc;
  
  html_puts(buf, fp);
  
  rc = fputs("\">", fp);
  if (rc < 0)
    return rc;

  if (label)
  {
    rc = html_puts(label, fp);
    fputs("</a>", fp);
  }

  return rc;
}


int
html_email(FILE *fp,
	   const char *email)
{
  const char *cp;
  int rc = 0;
  

  for (cp = email; rc >= 0 && *cp; ++cp)
    switch (*cp)
    {
      case '.':
	rc = fputs(" . ", fp);
	break;

      case '@':
	rc = fputs(" (vid) ", fp);
	break;

      default:
	rc = putc(*cp, fp);
    }

  return rc;
}
