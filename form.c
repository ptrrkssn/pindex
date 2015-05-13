#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "form.h"

static int nvar = 0;
static FORMVAR fvar[MAXVARS];

int
hextochr(char *str)
{
    return ((isdigit((unsigned char) (str[0])) ?
	     str[0]-'0' : toupper(str[0])-'A'+10) << 4) +
	(isdigit((unsigned char) (str[1])) ?
	 str[1]-'0' : toupper(str[1])-'A'+10);
}


void
http_print(FILE *fp, const char *str)
{
  int c;

  while ((c = (unsigned char) *str++) != '\0')
    {
      if (c == ' ')
	putc('+', fp);
      else if (c < ' ' || !isascii(c) || c == '=' || c == '&' || c == '?')
	fprintf(fp, "%%%02X", c);
      else
	putc(c, fp);
    }
}


char *
http_strip(char *str)
{
    char *dst, *src;

    dst = src = str;
    while (*src)
    {
	if (*src == '+')
	{
	    *dst++ = ' ';
	    ++src;
	    continue;
	}

	if (*src == '%')
	{
	    if (src[1] == '%')
	    {
		*dst++ = '%';
		src += 2;
		continue;
	    }

	    if (isxdigit((unsigned char) (src[1])) &&
		isxdigit((unsigned char) (src[2])))
	    {
		*dst++ = hextochr(src+1);
		src += 3;
		continue;
	    }
	}
	    
	*dst++ = *src++;
    }

    *dst = '\0';

    return str;
}


int
form_init(FILE *fp)
{
    char var[256];
    char val[10000];
    char *cp, *vp;
    int c;
    
    char *ep = getenv("QUERY_STRING");
    char *rm = getenv("REQUEST_METHOD");

    nvar = 0;

    if (ep && *ep && (rm && strcmp(rm, "POST") != 0))
    {
	cp = strtok(ep, "&");
	while (cp)
	{
	    vp = strchr(cp, '=');
	    if (vp)
	    {
		*vp++ = '\0';
		http_strip(vp);
	    }
	    
	    http_strip(cp);
	    
	    fvar[nvar].name = strdup(cp);
	    fvar[nvar].value = (vp && vp[0] ? strdup(vp) : NULL);
	    ++nvar;

	    cp = strtok(NULL, "&");
	}
    }
    else if (fp != NULL)
    {
	while (fscanf(fp, "%255[^=]=", var) == 1)
	{
	    val[0] = '\0';
	    
	    http_strip(var);
	    
	    c = getc(fp);
	    if (c == EOF)
		break;
	    
	    ungetc(c, fp);
	    if (c != '&' && fscanf(fp, "%9999[^&\n\r]", val) == 1)
		http_strip(val);
	    
	    fvar[nvar].name = strdup(var);
	    fvar[nvar].value = (val[0] ? strdup(val) : NULL);
	    ++nvar;
	    
	    c = getc(fp);
	    if (c != '&')
		ungetc(c, fp);
	}
    }
    
    return nvar;
}



char *
form_get(const char *var)
{
    int i;
    
    for (i = 0; i < nvar && strcmp(var, fvar[i].name) != 0; i++)
	;

    if (i >= nvar)
	return NULL;

    return fvar[i].value ? strdup(fvar[i].value) : NULL;
}


int
form_foreach(int (*fun)(const char *var, const char *val, void *x), void *x)
{
  int i, rv;


  rv = 0;
  for (i = 0; i < nvar && rv == 0; i++)
    rv = fun(fvar[i].name, fvar[i].value, x);

  return rv;
}

void
form_cgi_post(FILE *fp)
{
  int i;

  for (i = 0; i < nvar; i++)
    {
      http_print(fp, fvar[i].name);
      putc('=', fp);
      http_print(fp, fvar[i].value);
    }
}
