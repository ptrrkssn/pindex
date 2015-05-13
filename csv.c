#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <alloca.h>

#include "csv.h"

CSV *
csv_open(const char *path,
	 const char *mode)
{
  CSV *cp;

  
  cp = malloc(sizeof(*cp));
  if (!cp)
    return NULL;

  if (strcmp(mode, "x") == 0)
  {
    int i, rc, fd = -1;
    char *tpath;


    tpath = alloca(strlen(path)+32);
    if (!path)
    {
      free(cp);
      return NULL;
    }
    
    sprintf(tpath, "%s.tmp.%u.%u", path, (unsigned int) getpid(), rand());
    fd = open(tpath, O_CREAT|O_WRONLY|O_EXCL, 0777);
    if (fd < 0)
    {
      free(cp);
      return NULL;
    }
    
    for (i = 0; (rc = link(tpath, path)) < 0 && errno == EEXIST && i < 10; i++)
      sleep(1);
    
    if (rc < 0)
    {
      close(fd);
      free(cp);
      return NULL;
    }

    if (unlink(tpath) < 0)
    {
      unlink(path);
      close(fd);
      free(cp);
      return NULL;
    }
    
    cp->fp = fdopen(fd, "w");
    if (!cp->fp)
    {
      unlink(path);
      close(fd);
      free(cp);
      return NULL;
    }
  }
  else
  {
    cp->fp = fopen(path, mode);
    if (!cp->fp)
    {
      free(cp);
      return NULL;
    }
  }
  
  cp->st = 0;

  return cp;
}

int
csv_close(CSV *cp)
{
  int rc;

  if (cp->st != 0)
    csv_puteol(cp);
  
  rc = fclose(cp->fp);
  free(cp);

  return rc;
}

int
csv_puteol(CSV *cp)
{
  int rc;
  
  cp->st = 0;


#if 1
  rc = putc('\n', cp->fp);
#else
  if ((rc = putc('\r', cp->fp)) != EOF)
    rc = putc('\n', cp->fp);
#endif
  
  return rc;
}


int
csv_puts(const char *s,
	 CSV *cp)
{
  int q = 0;
  int rc = 0;
  int n = 0;


  if (!s)
    s = "";

  if (cp->st != 0)
    putc(';', cp->fp);
  
  if (strcspn(s, ";\"'") != strlen(s))
  {
    if (*s == '"')
      q = '\'';
    else
      q = '"';
    rc = putc(q, cp->fp);
    ++n;
  }

  while (rc != EOF &&*s)
  {
    if (*s == q)
    {
      rc = putc(q, cp->fp);
      ++n;
      if (rc == EOF)
	break;
      
      switch (q)
      {
	case '"':
	  q = '\'';
	  break;
	case '\'':
	  q = '"';
	  break;
      }
      rc = putc(q, cp->fp);
      ++n;
      if (rc == EOF)
	break;
      
    }
    rc = putc(*s, cp->fp);
    ++n;
    ++s;
  }
  
  if (rc != EOF && q)
  {
    rc = putc(q, cp->fp);
    ++n;
  }

  cp->st = 1;
  
  return rc == EOF ? EOF : n;
}

int
csv_putu(unsigned int v,
	 CSV *cp)
{
  int rc = 0;

  
  if (cp->st != 0)
    rc = putc(';', cp->fp);

  if (rc != EOF)
    rc = fprintf(cp->fp, "%u", v);

  cp->st = 1;

  return rc;
}


int
csv_gets(char *buf,
	 int bufsize,
	 CSV *cp)
{
  int buflen = 0;
  int q = 0;
  int c;


  while ((c = getc(cp->fp)) != EOF && !(!q && c == ';') && c != '\n' && c != '\r')
  {
    if (q)
    {
      if (c == q)
      {
	q = 0;
	continue;
      }
      else
      {
	*buf++ = c;
	buflen++;
      }
    }
    else
      switch (c)
      {
	case '"':
	case '\'':
	  q = c;
	  continue;
	  
	default:
	  *buf++ = c;
	  buflen++;
	  break;
      }
  }

  *buf++ = '\0';

  if (buflen > 0)
    switch (c)
    {
      case EOF:
	return buflen;
	
      case '\r':
      case '\n':
	ungetc(c, cp->fp);
	return buflen;
	
      default:
	return buflen;
    }
  else
    switch (c)
    {
      case EOF:
	return -1;
	
      case '\r':
	c = getc(cp->fp);
	if (c != '\n' && c != EOF)
	  ungetc(c, cp->fp);
	return -2;
	
      case '\n':
	c = getc(cp->fp);
	if (c != '\r' && c != EOF)
	  ungetc(c, cp->fp);
	return -2;
	
      default:
	return buflen;
    }
}

int
csv_skipeol(CSV *cp)
{
  int c;


  while ((c = getc(cp->fp)) != EOF && c != '\n' && c != '\r')
    ;
  
  switch (c)
  {
    case '\r':
      c = getc(cp->fp);
      if (c != '\n' && c != EOF)
	ungetc(c, cp->fp);
      return -2;
      
    case '\n':
      c = getc(cp->fp);
      if (c != '\r' && c != EOF)
	ungetc(c, cp->fp);
      return -2;
      
    default:
      return -1;
  }
}


int
csv_skip(CSV *cp)
{
  int len = 0;
  int q = 0;
  int c;


  while ((c = getc(cp->fp)) != EOF && !(!q && c == ';') && c != '\n' && c != '\r')
  {
    if (q)
    {
      if (c == q)
      {
	q = 0;
	continue;
      }
      else
	len++;
    }
    else
      switch (c)
      {
	case '"':
	case '\'':
	  q = c;
	  continue;
	  
	default:
	  len++;
	  break;
      }
  }

  if (len > 0)
    switch (c)
    {
      case EOF:
	return len;
	
      case '\r':
      case '\n':
	ungetc(c, cp->fp);
	return len;
	
      default:
	return len;
    }
  else
    switch (c)
    {
      case EOF:
	return -1;
	
      case '\r':
	c = getc(cp->fp);
	if (c != '\n' && c != EOF)
	  ungetc(c, cp->fp);
	return -2;
	
      case '\n':
	c = getc(cp->fp);
	if (c != '\r' && c != EOF)
	  ungetc(c, cp->fp);
	return -2;
	
      default:
	return len;
    }
}


int
csv_getsdup(char **sp,
	    CSV *cp)
{
  char buf[10240];
  int rc;


  rc = csv_gets(buf, sizeof(buf), cp);
  if (rc < 0)
    return rc;

  if (rc == 0)
    *sp = NULL;
  else
    *sp = strdup(buf);
  
  return rc;
}



int
csv_geti(int *ip,
	 CSV *cp)
{
  char buf[1024];
  int rc;


  rc = csv_gets(buf, sizeof(buf), cp);
  if (rc <= 0)
    return rc;

  return sscanf(buf, "%d", ip);
}



#ifdef DEBUG
int
main(int argc,
     char *argv[])
{
  int i;
  char buf[1024];
  CSV *cp;

  
  if (argc > 1)
  {
    cp = csv_open("/dev/tty", "w");
    
    for (i = 1; i < argc; i++)
    {
      csv_puts(argv[i], cp);
      putchar('\n');
    }

    csv_close(cp);
  }
  else
  {
    cp = csv_open("/dev/tty", "r");
    while ((i = csv_gets(buf, sizeof(buf), cp)) != EOF)
      switch (i)
      {
	case -2:
	  puts("<newline>");
	  break;

	default:
	  printf("[%s]\t", buf);
	  break;
      }

    csv_close(cp);
  }

  return 0;
}
#endif

