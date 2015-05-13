/*
** index.c 
**
** Copyright (c) Peter Eriksson <pen@lysator.liu.se>
*/

#define MAXNODES 2000

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <dirent.h>

#include "strmatch.h"
#include "html.h"
#include "table.h"
#include "form.h"
#include "creole.h"

int debug = 0;
int nowrap = 0;
int raw = 0;
time_t now = 0;

char **our_argv = NULL;

int max_cache_time = 300;

time_t file_dtm = 0;

typedef struct dirnode
{
  char *path;
  char *title;
  int hidden;
  int len;
  int size;
  struct dirnode **node;
} DIRNODE;

char *global_cache_path = NULL;
DIRNODE *global_cache_dnp = NULL;


char *index_title = NULL;
char *index_head = NULL;

char *http_cache_control = NULL;
char *http_host = NULL;
char *path_info = NULL;
char *query_string = NULL;
char *request_uri = NULL;
char *request_url = NULL;
char *document_root = NULL;
char *path_translated = NULL;
char *path_translated_dir = NULL;
char *remote_addr = NULL;
char *server_name = NULL;
char *request_method = NULL;
char *server_protocol = NULL;
char *http_referer = NULL;
char *http_user_agent = NULL;

int skip_header = 0;
int skip_footer = 0;

char *ssi_errmsg = "[An error occurred while processing this directive]";
char *ssi_timefmt = "%Y-%m-%d %H:%M:%S";
char *ssi_sizefmt = "bytes";

void
fail(const char *msg, const char *arg)
{
  if (!arg)
    arg = strerror(errno);

  fprintf(stderr, "*** FAIL: %s: %s\n", msg, arg);
  exit(1);
}


void
ssi_error(const char *a1,
	  const char *a2,
	  FILE *out)
{
  if (debug)
    fprintf(out, "[ERROR: %s: %s]", a1 ? a1 : "null", a2 ? a2 : "null");
  else
    fputs(ssi_errmsg, out);
}


char *
str2html(const char *src)
{
  static char buf[2048];
  char *dst;


  for (dst = buf; *src; ++src)
  {
    if (*src < ' ')
    {
      sprintf(dst, "&#%d;", *src);
    }
    else
      switch (*src)
      {
	case '"':
	  sprintf(dst, "&quot;");
	  break;

	case '&':
	  sprintf(dst, "&amp;");
	  break;

	case '<':
	  sprintf(dst, "&lt;");
	  break;

	case '>':
	  sprintf(dst, "&gt;");
	  break;

	default:
	  *dst++ = *src;
	  continue;
      }
    
      while (*dst)
	++dst;
  }

  *dst = '\0';
  return buf;
}

char *
str2href(const char *src)
{
  static char buf[2048];
  char *dst;


  for (dst = buf; *src; ++src)
  {
    if (*src <= ' ' || !(isalnum(*src) || *src == '.' || *src == '-' || *src == '_'))
    {
      sprintf(dst, "%%%02x", *src);
      while (*dst)
	++dst;
    }
    else
      *dst++ = *src;
  }

  *dst = '\0';
  
  return buf;
}


typedef struct
{
  int dc;
  int ds;
  struct dirlist_entry
  {
    char *name;
    char *href;
  } *dv;
} DIRLIST;



void
dirlist_destroy(DIRLIST *dlp)
{
  int i;

  
  if (!dlp)
    return;
  
  if (dlp->dv)
  {
    for (i = 0; i < dlp->dc; i++)
    {
      if (dlp->dv[i].name)
	free(dlp->dv[i].name);
      if (dlp->dv[i].href)
	free(dlp->dv[i].href);
    }
    free(dlp->dv);
  }
  
  free(dlp);
}

DIRLIST *
dirlist_create(const char *path,
	       const char *match)
{
  DIRLIST *dlp = NULL;
  DIR *dp = NULL;
  struct dirent *dep;
  

  dlp = malloc(sizeof(*dlp));
  if (!dlp)
    return NULL;
  memset(dlp, 0, sizeof(*dlp));
  
  dlp->ds = 128;
  dlp->dv = calloc(sizeof(dlp->dv[0]), dlp->ds);
  if (!dlp->dv)
    goto Fail;
  
  dp = opendir(path);
  if (!dp)
    goto Fail;
  
  while ((dep = readdir(dp)))
  {
    if (dep->d_name[0] == '.')
      continue;
    
    if (match && !strmatch(dep->d_name, match))
      continue;
    
    dlp->dv[dlp->dc].name = strdup(str2html(dep->d_name));
    dlp->dv[dlp->dc].href = strdup(str2href(dep->d_name));

    dlp->dc++;
  }
  
  closedir(dp);
  return dlp;
  
  Fail:
  dirlist_destroy(dlp);
  if (dp)
    closedir(dp);
  return NULL;
}

int
dirlist_foreach(DIRLIST *dlp,
		int (*fun)(const char *name,
			   const char *href,
			   void *xtra),
		void *xtra)
{
  int i, rc;


  for (i = 0; i < dlp->dc; i++)
  {
    rc = (*fun)(dlp->dv[i].name,
		dlp->dv[i].name,
		xtra);
    if (rc != 0)
      return rc;
  }

  return 0;
}

int
dirlist_sortfun(const void *p1,
		const void *p2)
{
  struct dirlist_entry *d1 = (struct dirlist_entry *) p1;
  struct dirlist_entry *d2 = (struct dirlist_entry *) p2;

  return strcmp(d2->name, d1->name);
}
	       
void
dirlist_sort(DIRLIST *dlp)
{
  qsort(dlp->dv, dlp->dc, sizeof(dlp->dv[0]), dirlist_sortfun);
}

typedef struct dirlist_xtra
{
    FILE *fp;
    const char *base;
} DIRLIST_XTRA;

int
print_dirlist(const char *name,
	      const char *href,
	      void *xtra)
{
    DIRLIST_XTRA *xp = (DIRLIST_XTRA *) xtra;
    int rc;

    if (xp->base)
	rc = fprintf(xp->fp,
		     "<li><a href=\"%s/%s\">%s</a>\n",
		     xp->base, href, name);
    else
	rc = fprintf(xp->fp,
		     "<li><a href=\"%s\">%s</a>\n",
		     href, name);
    if (rc < 0)
	return rc;
    
    return 0;
}
     

int
ssi_directory(const char *base,
	      const char *path,
	      const char *match,
	      FILE *out)
{
  DIRLIST *dlp;
  DIRLIST_XTRA xbuf;
  

  xbuf.fp = out;
  xbuf.base = base;
  
  dlp = dirlist_create(path, match);
  if (!dlp)
    return -1;

  dirlist_sort(dlp);
  
  fputs("<ul>\n", out);
  dirlist_foreach(dlp, print_dirlist, (void *) &xbuf);
  fputs("</ul>\n", out);
  
  dirlist_destroy(dlp);
  return 0;
}


static int gallery_idx = 0;
static int gallery_width = 0;

int
print_gallery(const char *name,
	      const char *href,
	      void *xtra)
{
    DIRLIST_XTRA *xp = (DIRLIST_XTRA *) xtra;
    int rc;

    if (gallery_idx > 0 && (gallery_idx % 3) == 0)
	fprintf(xp->fp, "<p style=\"clear: left;\">\n");
    
    fprintf(xp->fp, "<div class=\"galleri\">\n");
    
    if (xp->base)
	rc = fprintf(xp->fp,
		     "<img src=\"%s/",
		     xp->base);
    rc = fprintf(xp->fp,
		 "<%s\"",
		 href);
    if (gallery_width)
	rc = fprintf(xp->fp, "width=%u", gallery_width);
    rc = fprintf(xp->fp, ">\n</div>\n");
    ++gallery_idx;
    
    if (rc < 0)
	return rc;
    
    return 0;
}
     

int
ssi_gallery(const char *base,
	    const char *path,
	    const char *match,
	    FILE *out)
{
  DIRLIST *dlp;
  DIRLIST_XTRA xbuf;
  

  xbuf.fp = out;
  xbuf.base = base;
  
  dlp = dirlist_create(path, match);
  if (!dlp)
    return -1;

  dirlist_sort(dlp);
  
  dirlist_foreach(dlp, print_gallery, (void *) &xbuf);
  
  dirlist_destroy(dlp);
  return 0;
}

char *
concat(const char *s1,
       const char *s2,
       const char *s3)
{
  char *res = malloc(strlen(s1)+strlen(s2 ? s2 : "")+strlen(s3 ? s3 : "")+1);
  if (!res)
    fail("malloc", NULL);

  strcpy(res, s1);
  if (s2)
    strcat(res, s2);
  if (s3)
    strcat(res, s3);

  return res;
}

char *
fconcat(const char *p1,
	const char *p2)
{
  int len;
  char *new;

  
  len = strlen(p1);
  while (len > 0 && p1[len-1] == '/')
    --len;

  new = malloc(len+2+strlen(p2));
  memcpy(new, p1, len);
  new[len++] = '/';
  strcpy(new+len, p2);

  return new;
}


time_t
str2time(const char *str)
{
  struct tm tmb;
  int year, month, day, hour, min, sec;

  year = month = day = hour = min = sec = 0;
  
  memset(&tmb, 0, sizeof(tmb));
  if (sscanf(str, "%u-%u-%u %u:%u:%u", &year, &month, &day, &hour, &min, &sec) < 1)
    return (time_t) -1;

  tmb.tm_year = year-1900;
  tmb.tm_mon = month-1;
  tmb.tm_mday = day;
  tmb.tm_hour = hour;
  tmb.tm_min = min;
  tmb.tm_sec = sec;
  tmb.tm_isdst = -1;

  return mktime(&tmb);
}

int
weekday(time_t bt)
{
  struct tm *tp;

  tp = localtime(&bt);
  return tp->tm_wday;
}


void
do_accesslog(void)
{
  char buf[1024];
  FILE *fp;
  time_t bt;
  struct tm *tp;


  sprintf(buf, "%s/access.log", document_root);
  fp = fopen(buf, "a");
  if (!fp)
    return;

  /*
  **  212.105.91.194 - - [11/Apr/2001:21:49:07 +0200] "GET /en/iroot-logo.jpg HTTP/1.0" 200 1659 "http://www.instant-root.se/en/" "Mozilla/4.76 [en] (X11; U; Linux"
  */

  time(&bt);
  tp = localtime(&bt);
  strftime(buf, sizeof(buf), "%d/%b/%Y:%H:%M:%S %z", tp);

  fprintf(fp, "%s - - [%s] \"%s %s %s\" 200 - \"%s\" \"%s\"\n",
	  remote_addr ? remote_addr : "-",
	  buf,
	  request_method ? request_method : "-",
	  request_uri ? request_uri : "-",
	  server_protocol ? server_protocol : "-",
	  http_referer ? http_referer : "-",
	  http_user_agent ? http_user_agent : "-");
  fclose(fp);
}


char *
memmem(const char *buf,
       size_t buflen,
       const char *pattern,
       size_t len)
{
  char *bf = (char *)buf, *pt = (char *)pattern, *p = bf;
  
  while (len <= (buflen - (p - bf)))
  {
    if (NULL != (p = memchr(p, (int)(*pt), buflen - (p - bf))))
    {
      if (memcmp(p, pattern, len) == 0)
	return p;
      else  ++p;
    }
    else  break;
  }
  return NULL;
}



void
strucase(char *str)
{
  while (*str)
  {
    *str = toupper(*str);
    ++str;
  }
}

char *
file_get_section(const char *path,
		 const char *section)
{
  char *buf, *eob, *head, *sbuf, *start, *stop;
  int fd;
  struct stat sb;


  sbuf = malloc(strlen(section+3));
  if (!sbuf)
    return NULL;
  
  head = NULL;
  
  fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    fprintf(stderr, "file_get_section: open(%s) failed: %s\n",
	    path, strerror(errno));
    return NULL;
  }

  if (fstat(fd, &sb) != 0)
  {
    fprintf(stderr, "file_get_section: fstat(%s) failed: %s\n",
	    path, strerror(errno));
    close(fd);
    return NULL;
  }

  buf = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED)
  {
    fprintf(stderr, "file_get_section: mmap(%s) failed: %s\n",
	    path, strerror(errno));
    close(fd);
    return NULL;
  }

  eob = buf+sb.st_size;

  sprintf(sbuf, "<%s", section);
  start = memmem(buf, sb.st_size, sbuf, strlen(sbuf));
  if (!start)
  {
    strucase(sbuf);
    start = memmem(buf, sb.st_size, sbuf, strlen(sbuf));
  }

  if (!start)
  {
    fprintf(stderr, "file_get_section: could not locate start\n");
    goto End;
  }
    
  start += strlen(sbuf);
  while (start < eob && *start != '>')
    ++start;
  
  if (start >= eob)
  {
    fprintf(stderr, "file_get_section: missing closing start marker\n");
    goto End;
  }

  ++start;
  
  sprintf(sbuf, "</%s", section);
  stop = memmem(buf, sb.st_size, sbuf, strlen(sbuf));
  if (!stop)
  {
    strucase(sbuf);
    stop = memmem(buf, sb.st_size, sbuf, strlen(sbuf));
  }
  
  if (!stop || stop <= start)
  {
    fprintf(stderr, "file_get_section: missing end token, or end before start\n");
    goto End;
  }
  
  head = malloc(stop-start+1);
  if (!head)
  {
      fprintf(stderr, "file_get_section: could not allocate %u bytes\n", (unsigned int) (stop-start+1));
    goto End;
  }
  
  memcpy(head, start, stop-start);
  head[stop-start] = '\0';

  End:
  munmap(buf, sb.st_size);
  close(fd);
  
  return head;
}

void
dirtree_free(DIRNODE *dnp)
{
  int i;

  if (dnp == global_cache_dnp)
    return;
  
  for (i = 0; i < dnp->len; i++)
    dirtree_free(dnp->node[i]);

  if (dnp->path)
    free(dnp->path);

  if (dnp->title)
    free(dnp->title);

  free(dnp);
}


void
dirnode_save(DIRNODE *dnp,
	     FILE *out)
{
  int i;
  static int count = 0;
  
  if (!dnp)
  {
    fprintf(out, "# Got NULL node\n");
    return;
  }
  if (count >= MAXNODES)
  {
    fprintf(out, "# Got too many nodes: %d\n", count);
    return;
  }

  fprintf(out, "# Node %u\n", count++);
  fprintf(out, "%s\n", dnp->path);
  fprintf(out, "%s\n", dnp->title);
  fprintf(out, "%u %u\n", dnp->hidden, dnp->len);
  
  for (i = 0; i < dnp->len; i++)
    dirnode_save(dnp->node[i], out);
}


DIRNODE *
dirnode_load(FILE *in)
{
  int i, c;
  char buf[2048];

  
  DIRNODE *dnp;


  Again:
  c = getc(in);
  if (c == EOF)
    return NULL;
  
  if (c == '#')
  {
    while ((c = getc(in)) != EOF && c != '\n')
      ;
    goto Again;
  }
  else
    ungetc(c, in);
  
  dnp = malloc(sizeof(DIRNODE));
  if (!dnp)
    abort();
  
  memset(dnp, 0, sizeof(dnp));

  buf[0] = '\0';
  if (fscanf(in, "%[^\n]\n", buf) < 1)
  {
    free(dnp);
    return NULL;
  }
  
  dnp->path = strdup(buf);
  if (!dnp->path)
    abort();
  
  if (fscanf(in, "%[^\n]\n", buf) < 1)
  {
    free(dnp->path);
    free(dnp);
    return NULL;
  }
  
  dnp->title = strdup(buf);
  if (!dnp->title)
    abort();
  
  if (fscanf(in, "%u %u\n", &dnp->hidden, &dnp->len) < 2)
  {
    free(dnp->path);
    free(dnp->title);
    free(dnp);
    return NULL;
  }
  
  dnp->node = calloc(sizeof(dnp->node[0]), dnp->len);
  if (!dnp->node)
    abort();
  
  for (i = 0; i < dnp->len; i++)
    dnp->node[i] = dirnode_load(in);

  return dnp;
}

int
dirnode_compare_title(const void *n1,
		      const void *n2)
{
  const DIRNODE *d1, *d2;

  d1 = * (DIRNODE **) n1;
  d2 = * (DIRNODE **) n2;

  return strcmp(d1->title, d2->title);
}

void
dirtree_sort_title(DIRNODE *dnp)
{
  int i;

  for (i = 0; i < dnp->len; i++)
    dirtree_sort_title(dnp->node[i]);
  
  qsort((void *) &dnp->node[0], dnp->len, sizeof(dnp->node[0]), dirnode_compare_title);

}


DIRNODE *
dirnode_parse(const char *path,
	      int descend)
{
  char *npath, *tpath, *ipath, *cp;
  char *title = NULL;
  DIRNODE *dnp = NULL;
  struct stat sb;
  struct dirent *dep;
  DIR *dp = NULL;

  
  if (!path || !*path)
    return NULL;

  if (debug > 2)
    fprintf(stderr, "dirnode_parse: path=%s\n", path);
  
  npath = strdup(path);
  for (cp = npath+strlen(npath)-1; cp > npath && *cp == '/'; --cp)
    ;
  cp[1] = '\0';

  dp = opendir(npath);
  if (!dp)
  {
    free(npath);
    return NULL;
  }

  ipath = fconcat(npath, "index.html");
  title = file_get_section(ipath, "title");
  free(ipath);
  if (!title)
    goto Fail;
  
  dnp = malloc(sizeof(*dnp));
  if (!dnp)
    goto Fail;
  
  dnp->path = npath;
  dnp->title = title;
  dnp->len = 0;
  dnp->size = 32;
  dnp->node = malloc(dnp->size * sizeof(DIRNODE *));
  
  ipath = fconcat(dnp->path, ".hidden");
  dnp->hidden = (access(ipath, R_OK) == 0);
  free(ipath);
  
  if (descend)
    while ((dep = readdir(dp)) != NULL)
    {
      if (strcmp(dep->d_name, ".") == 0 ||
	  strcmp(dep->d_name, "..") == 0)
	continue;
      
      tpath = fconcat(dnp->path, dep->d_name);
      if (lstat(tpath, &sb) == 0 && S_ISDIR(sb.st_mode))   /* lstat? Don't follow symlinks...? */
      {
	DIRNODE *node = dirnode_parse(tpath, descend-1);
	
	if (node)
	{
	  if (dnp->len + 1 >= dnp->size)
	  {
	    dnp->size += 32;
	    dnp->node = realloc(dnp->node, dnp->size * sizeof(DIRNODE *));
	  }
	  dnp->node[dnp->len++] = node;
	}
      }
      
      if (tpath)
	free(tpath);
    }
  
  dirtree_sort_title(dnp);
  
  closedir(dp);
  return dnp;

  Fail:
  if (title)
    free(title);

  if (npath)
    free(npath);
  
  if (dnp)
    dirtree_free(dnp);
  
  closedir(dp);
  return NULL;
}

int
nocache(void)
{
  if (!http_cache_control)
    return 0;

  return (strcmp(http_cache_control, "no-cache") == 0);
}


DIRNODE *
dirtree_load(const char *path,
	     int descend)
{
  char *cpath;
  DIRNODE *dnp = NULL, *tdnp;
  FILE *fp;
  struct stat sb;
  

  cpath = fconcat(path, ".cache");

  if (debug)
    fprintf(stderr, "dirtree_load: path=%s\n", path);


  if (!nocache() &&
      stat(cpath, &sb) == 0 &&
      (sb.st_mtime + max_cache_time >= now) &&
      (fp = fopen(cpath, "r")) != NULL)
  {
    if (debug)
      fprintf(stderr, "dirtree_load: Using cache (%lu+%u=%lu >= %lu): %s\n",
	      sb.st_mtime, max_cache_time, sb.st_mtime+max_cache_time,
	      now, cpath);
  
    dnp = dirnode_load(fp);
    fclose(fp);
    
    if (dnp != NULL)
    {
      if (global_cache_dnp)
      {
	tdnp = global_cache_dnp;
	global_cache_dnp = NULL;
	dirtree_free(tdnp);
      }
      free(global_cache_path);
      
      global_cache_dnp = dnp;
      global_cache_path = strdup(path);
      
      return dnp;
    }
  }

  if (global_cache_dnp && strcmp(path, global_cache_path) == 0)
      return global_cache_dnp;
  
  dnp = dirnode_parse(path, descend);

  /* XXX: Make temp filename, write, rename to cpath */
  fp = fopen(cpath, "w");
  if (fp)
  {
    if (debug)
      fprintf(stderr, "dirtree_load: creating new cache file: %s\n", cpath);
    
    fprintf(fp, "# Cache path: %s\n", cpath);
    dirnode_save(dnp, fp);
    fclose(fp);

    if (global_cache_dnp)
    {
      tdnp = global_cache_dnp;
      global_cache_dnp = NULL;
      dirtree_free(tdnp);
      free(global_cache_path);
    }
    
    global_cache_dnp = dnp;
    global_cache_path = strdup(path);
  }

  return dnp;
}







void
dirtree_menu_print(DIRNODE *dnp,
		   const char *curpath,
		   int level,
		   FILE *out,
		   const char *type,
		   const char *style)
{
  int i, len, rlen, isopen;
  char *url;
  
  if (!dnp)
    return;

  rlen = strlen(document_root);
  len = strlen(dnp->path);
  url = dnp->path+rlen;
  if (!*url)
    url = "/";

  isopen = (curpath &&
	    (strncmp(curpath, dnp->path, len) == 0) &&
	    (curpath[len] == '/' || curpath[len] == '\0'));

  if (dnp->hidden && !isopen && level != 0)
    return;


  if (level == 0)
  {
    if (strcmp(type, "ol") == 0 || strcmp(type, "ul") == 0)
    {
      if (style)
	fprintf(out, "<%s style=\"%s;\">\n", type, style);
      else
	fprintf(out, "<%s>\n", type);
    }
  }
  
  if (level >= 0)
    fprintf(out, "<li class=\"level%d\"><a%s href=\"%s\">%s</a>",
	    level,
	    isopen ? " class=\"selected\"" : "",
	    url,
	    dnp->title);
  
  if (dnp->len && (isopen || !curpath))
  {
      putc('\n', out);
      if (strcmp(type, "ol") == 0 || strcmp(type, "ul") == 0)
      {
	  if (style)
	      fprintf(out, "<%s style=\"%s;\">\n", type, style);
	  else
	      fprintf(out, "<%s>\n", type);
      }
      
      for (i = 0; i < dnp->len; i++)
	  dirtree_menu_print(dnp->node[i], curpath, (level < 0 ? 1 : level+1), out, type, style);
      
      if (strcmp(type, "ol") == 0 || strcmp(type, "ul") == 0)
	  fprintf(out, "</%s>\n", type);
  }
  fputs("</li>\n", out);
  
  if (level == 0 && (strcmp(type, "ol") == 0 || strcmp(type, "ul") == 0))
    fprintf(out, "</%s>\n", type);
}


int
dirtree_last_get(DIRNODE *dnp,
		 const char *curpath,
		 int level,
		 char **last_url)
{
  int i, len, rlen, rc, isopen;
  char *url;
  
  
  if (!dnp)
    return 0;

  rlen = strlen(document_root);
  len = strlen(dnp->path);
  url = dnp->path+rlen;
  if (!*url)
    url = "/";

  isopen = (curpath && (strncmp(curpath, dnp->path, len) == 0));
  
  if (dnp->hidden && !isopen)
    return 0;

  if (0 && strcmp(curpath, dnp->path) == 0)
      return 1;

  if (*last_url)
      free(*last_url);
  *last_url = strdup(url);

  if (dnp->len && (1 || isopen || !curpath))
  {
      for (i = 0; i < dnp->len; i++)
      {
	  rc = dirtree_last_get(dnp->node[i], curpath, (level < 0 ? 1 : level+1), last_url);
	  if (rc)
	      return rc;
      }
  }

  return 0;
}

int
dirtree_up_get(DIRNODE *dnp,
	       const char *curpath,
	       int level,
	       char **up_url)
{
  int i, len, rlen, rc, isopen;
  char *url;
  
  
  if (!dnp)
    return 0;

  rlen = strlen(document_root);
  len = strlen(dnp->path);
  url = dnp->path+rlen;
  if (!*url)
    url = "/";

  isopen = (curpath && (strncmp(curpath, dnp->path, len) == 0));
  
  if (!isopen)
    return 0;

  if (strcmp(curpath, dnp->path) == 0)
      return 1;

  for (i = 0; i < dnp->len; i++)
  {
      rc = dirtree_up_get(dnp->node[i], curpath, (level < 0 ? 1 : level+1), up_url);
      if (rc == 1)
      {
	  if (*up_url)
	      free(*up_url);
	  *up_url = strdup(url);
	  return 2;
      }
  }

  return 0;
}

int
dirtree_prev_get(DIRNODE *dnp,
		 const char *curpath,
		 int level,
		 char **prev_url)
{
  int i, len, rlen, rc, isopen;
  char *url;
  
  
  if (!dnp)
    return 0;

  rlen = strlen(document_root);
  len = strlen(dnp->path);
  url = dnp->path+rlen;
  if (!*url)
    url = "/";

  isopen = (curpath && (strncmp(curpath, dnp->path, len) == 0));
  
  if (dnp->hidden && !isopen)
    return 0;

  if (strcmp(curpath, dnp->path) == 0)
      return 1;

  if (*prev_url)
      free(*prev_url);
  *prev_url = strdup(url);

  if (dnp->len && (1 || isopen || !curpath))
  {
      for (i = 0; i < dnp->len; i++)
      {
	  rc = dirtree_prev_get(dnp->node[i], curpath, (level < 0 ? 1 : level+1), prev_url);
	  if (rc)
	      return rc;
      }
  }

  return 0;
}

int
dirtree_next_get(DIRNODE *dnp,
		 const char *curpath,
		 int level,
		 int *nextflag,
		 char **next_url)
{
  int i, len, rlen, rc, isopen;
  char *url;
  
  
  if (!dnp)
    return 0;

  rlen = strlen(document_root);
  len = strlen(dnp->path);
  url = dnp->path+rlen;
  if (!*url)
    url = "/";

  isopen = (curpath && (strncmp(curpath, dnp->path, len) == 0));
  
  if (dnp->hidden && !isopen)
    return 0;

  if (*nextflag)
  {
      *next_url = strdup(url);
      return 1;
  }
  
  if (strcmp(curpath, dnp->path) == 0)
      *nextflag = 1;

  if (dnp->len && (isopen || !curpath))
  {
      for (i = 0; i < dnp->len; i++)
      {
	  rc = dirtree_next_get(dnp->node[i], curpath, (level < 0 ? 1 : level+1), nextflag, next_url);
	  if (rc)
	      return rc;
      }
  }

  return 0;
}


const char *
dirtree_locate(DIRNODE *dnp,
	       const char *title)
{
    int i;
    const char *url = NULL;
    

    if (!dnp)
	return NULL;

    if (debug)
	fprintf(stdout, "<!-- dirnode_locate: checking node title=%s -->\n", dnp->title);
    
    if (strcasecmp(dnp->title, title) == 0)
	return dnp->path;
    
    for (i = 0; i < dnp->len; i++)
    {
	url = dirtree_locate(dnp->node[i], title);
	if (url)
	    break;
    }

    return url;
}


void
dirtree_submenu_print(DIRNODE *dnp,
		   const char *curpath,
		   FILE *out,
		   const char *type,
		   const char *style)
{
  int i, len, rlen, isopen;
  char *url;
  DIRNODE *subnode = NULL;
  
  if (!dnp)
    return;

  rlen = strlen(document_root);
  
  if (strcmp(type, "ol") == 0 || strcmp(type, "ul") == 0)
  {
    if (style)
      fprintf(out, "<%s style=\"%s;\">\n", type, style);
    else
      fprintf(out, "<%s>\n", type);
  }
  
  for (i = 0; i < dnp->len; i++)
  {
    len = strlen(dnp->node[i]->path);
    url = dnp->node[i]->path+rlen;
    if (!*url)
      url = "/";
    
    isopen = (curpath && (strncmp(curpath, dnp->node[i]->path, len) == 0));
    
    if (dnp->node[i]->hidden && !isopen)
      continue;

    fprintf(out, "<li><a%s href=\"%s\">%s</a></li>\n",
	    isopen ? " class=\"selected\"" : "",
	    url,
	    dnp->node[i]->title);

    if (!subnode && isopen)
      subnode = dnp->node[i];
  }
  
  if (strcmp(type, "ol") == 0 || strcmp(type, "ul") == 0)
    fprintf(out, "</%s>\n", type);
  

  if (subnode && subnode->len)
  {
#if 0
    fputs("<div style=\"border-bottom: dashed 1px gray;\"></div>\n", out);
#else
    fputs("<hr>\n", out);
    
#endif
    dirtree_submenu_print(subnode, curpath, out, type, style);
  }
}



char *
bar_create(char *inpath,
	   char *baseurl,
	   int navbar_f)
{
  char *path = strdup(inpath);
  char *title, *tmp, *tmp2;
  char *banner = NULL;
  char *start = path + strlen(document_root);

  
  tmp = path + strlen(path);
  while (tmp > path && *--tmp == '/')
    *tmp = '\0';
  
  Loop:
  if (baseurl && strncmp(path, baseurl, strlen(baseurl)) != 0)
    goto End;
  
  tmp = concat(path, "/", "index.html");
  title = file_get_section(tmp, "title");
  free(tmp);

  if (!title)
    goto End;

  if (navbar_f)
  {
    tmp = concat("<a href=\"", *start ? start : "/", "\">");
    tmp2 = concat(tmp, title, "</a>");
    free(tmp);
    free(title);
    title = tmp2;
  }
  
  if (!banner)
    banner = title;
  else
  {
    tmp = concat(title, " / ", banner);
    free(banner);
    free(title);
    banner = tmp;
  }
  tmp = strrchr(path, '/');
  if (!tmp)
    goto End;

  *tmp = '\0';
  goto Loop;
  
  End:
  return banner;
}

char *
navbar_create(char *inpath, char *baseurl)
{
  return bar_create(inpath, baseurl, 1);
}

char *
titlebar_create(char *inpath, char *baseurl)
{
  return bar_create(inpath, baseurl, 0);
}




void 
env_get(void)
{
  http_cache_control = getenv("HTTP_CACHE_CONTROL");
  path_info = getenv("PATH_INFO");
  query_string = getenv("QUERY_STRING");
  document_root = getenv("DOCUMENT_ROOT");
  path_translated = getenv("PATH_TRANSLATED");
  server_name = getenv("SERVER_NAME");
  remote_addr = getenv("REMOTE_ADDR");
  request_method = getenv("REQUEST_METHOD");
  server_protocol = getenv("SERVER_PROTOCOL");
  http_host = getenv("HTTP_HOST");
  http_referer = getenv("HTTP_REFERER");
  http_user_agent = getenv("HTTP_USER_AGENT");
  
  request_uri = getenv("REQUEST_URI");
  if (request_uri)
  {
      char *cp;
      request_url = strdup(request_uri);
      cp = strchr(request_url, '?');
      if (cp)
	  *cp = 0;
  }
}

char *
xstrtok(char *buf,
	char *delim,
	char **tokp)
{
  char *end;
  int len;


  if (buf)
    *tokp = buf;
  else
    buf = *tokp;

  if (!buf || !*buf)
    return NULL;

  while (isspace(*buf))
    ++buf;

  if (*buf == '"')
    {
      ++buf;
      end = strchr(buf, '"');
      if (!end)
	end = buf+strlen(buf);
    }
  else
    {
      len = strcspn(buf, delim);
      end = buf+len;
    }
  
  if (!*end)
    *tokp = NULL;
  else
    {
      *end = '\0';
      *tokp = end+1;
    }

  return buf;
}


char *
ssi_make_path(char *type,
	      char *file)
{
  if (strcmp(type, "file") == 0)
  {
    if (*file == '/' || strstr(file, "../") != NULL)
      return NULL;
    
    return fconcat(path_translated_dir, file);
  }
  else if (strcmp(type, "virtual") == 0)
  {
    if (strstr(file, "../") != NULL)
      return NULL;

    if (*file == '/')
      return fconcat(document_root, file);
    else
      return fconcat(path_translated_dir, file);
  }

  return NULL;
}

void
fsend(FILE *in, FILE *out)
{
  int c;

  while ((c = getc(in)) != EOF)
    putc(c, out);
}


void
file_write(const char *path, FILE *out)
{
  FILE *fp;
  

  fp = fopen(path, "r");
  
  if (!fp)
    return;

  fsend(fp, out);
  
  fclose(fp);
}

char *
xtime(time_t *bt, char *fmt)
{
  static char buf[256];

  if (strftime(buf, sizeof(buf), fmt, localtime(bt)) <= 0)
    return ssi_errmsg;
  
  return buf;
}

void
icon_print(FILE *out,
	   const char *alt,
	   const char *icon,
	   const char *title)
{
  fprintf(out, "<img alt=\"%s\" title=\"%s\" src=\"/img/19x19/%s.gif\" width=\"19\" height=\"19\" border=\"0\" align=\"middle\" hspace=\"0\" vspace=\"0\">",
	  alt, title, icon);
}

void
folderview_print_structure(FILE *out,
			const char *navstr)
{
  int c;

  
  while ((c = *navstr++) != 0)
    switch (c)
    {
      case ' ':
	icon_print(out, " ", "space", "");
	break;

      case '+':
	icon_print(out, "+", "plus", "Open Folder");
	break;

      case 'x':
	icon_print(out, "+", "pluslast", "Open Folder");
	break;
	
      case '-':
	icon_print(out, "x", "minus", "Close Folder");
	break;

      case '/':
	icon_print(out, "x", "minuslast", "Close Folder");
	break;

      case '>':
	icon_print(out, "-", "empty", "");
	break;
	
      case '\\':
	icon_print(out, "-", "emptylast", "");
	break;
	
      case '|':
	icon_print(out, "|", "vertical", "");
	break;
    }
}

void
folderview_print_item(FILE *out,
		   const char *prev,
		   const char *url,
		   const char *label)
{
  folderview_print_structure(out, prev);
  fprintf(out, "<a href=\"%s\">", url);
  icon_print(out, "", "folder", "Goto Folder");
  fprintf(out, "</a>%s<a href=\"%s\">%s</a><br>\n",
	  *prev ? "&nbsp;" : "", url, label);
}

void
folderview_print(const char *path,
	      FILE *out)
{
  folderview_print_item(out, "",  "/",          "Grebo IK");
  folderview_print_item(out, "-",  "/fotboll",   "Fotboll");
  folderview_print_item(out, "|/",  "/fotboll/ungdom", "Ungdom");
  folderview_print_item(out, "| \\",  "/fotboll/ungdom/boll-lek", "Boll-lek");
  folderview_print_item(out, "+",  "/tennis",    "Tennis");
  folderview_print_item(out, "+",  "/innebandy", "Innebandy");
  folderview_print_item(out, "x", "/isbanan",   "Isbanan");
}


#if 0
void
html_putc(int c,
	  FILE *fp)
{
  switch (c)
  {
    case '<':
      fputs("&lt;", fp);
      break;
    
    case '>':
      fputs("&gt;", fp);
      break;
    
    case '&':
      fputs("&amp;", fp);
      break;

    default:
      putc(c, fp);
  }
}


void
html_puts(const char *str,
	  FILE *fp)
{
  while (*str)
    html_putc(*str++, fp);
}
#endif


void
table_print_csv(const char *path,
		const char *opts,
		FILE *out,
		int variant,
		const char *width)
{
  FILE *in;
  char buf[16384], *cp, *tokp;
  int line = 0;
  int col = 0;
  

  in = fopen(path, "r");
  if (!in)
  {
    ssi_error("table_print_csv:fopen", path, out);
    return;
  }

  fprintf(out, "<table %s>\n", opts ? opts : "");
  while (fgets(buf, sizeof(buf), in))
  {
    ++line;
    col = 0;
    
    if (variant == 0 && line == 1)
      fputs("<tr bgcolor=\"#C0C0C0\">\n", out);
    else
      fputs("<tr>\n", out);
    
    cp = xstrtok(buf, ";\n", &tokp);
    while (cp)
    {
      ++col;

      
      if (variant == 0 && line == 1)
      {
	fprintf(out, "<th nowrap class=\"header\" align=left %s%s>", width ? "width=" : "", width ? width : "");
	html_puts(cp, out);
	fputs("</th>\n", out);
      }
      else if (variant == 1 && col == 1)
      {
	fprintf(out, "<th nowrap align=right %s%s>", width ? "width=" : "", width ? width : "");
	html_puts(cp, out);
	fputs(":</td>\n", out);
      }
      else
      {
	fprintf(out, "<td nowrap align=left %s%s>",
		(width && variant != 1) ? "width=" : "",
		(width && variant != 1) ? width : "");
	html_puts(cp, out);
	fputs("</td>\n", out);
      }

      cp = xstrtok(NULL, ";\n", &tokp);
    }
    fputs("</tr>\n", out);
  }
  fputs("</table>\n", out);
  fclose(in);
}

int
leap_year(int year)
{
  return !(year % 4) - !(year % 100) + !(year % 400);
}

int
month_days(int year,
	   int month) /* 0 is January */
{
      const int month_days[12] =
      {
	31, 28, 31, 30, 31, 30,
	31, 31, 30, 31, 30, 31
      };

      return month_days[month-1] + (month == 2 && leap_year(year));
}



void
calendar_print_csv(const char *path,
		   int sel_year,
		   int sel_month,
		   int cols,
		   char *opts,
		   FILE *out)
{
  FILE *in;
  char buf[16384], *cp, *tokp;
  int line = 0, ncol = 0;
  int mdays, curday, lastday;
  int year, month, day;
  int cur_year, cur_month;
  time_t bt;
  int wday;

  
  in = fopen(path, "r");
  if (!in)
  {
    ssi_error("calendar_print_csv:fopen", path, out);
    return;
  }

  curday = lastday = 0;
  cur_year = 0; cur_month = 0;
  
  fprintf(out, "<table style=\"border: solid 1px black;\" %s>\n", opts ? opts : "");

  if (fgets(buf, sizeof(buf), in))
  {
    ++line;
    ncol = 0;

    fputs("<tr bgcolor=\"#C0C0C0\">\n", out);
    
    cp = xstrtok(buf, ";\n", &tokp);
    while (cp)
    {
      if (ncol < 2)
	fputs("<th width=\"10%\" align=\"left\" class=\"header\">", out);
      else
	fputs("<th align=\"left\" class=\"header\">", out);
      
      html_puts(cp, out);
      fputs("&nbsp;</th>\n", out);

      ++ncol;
      
      cp = xstrtok(NULL, ";\n", &tokp);
    }
    fputs("</tr>\n", out);
  }

  if (ncol > cols)
    cols = ncol;


  bt = 0;
  wday = 0;
  mdays = 0;
  
  if (sel_year && sel_month)
    mdays = month_days(sel_year, sel_month);
  
  while (fgets(buf, sizeof(buf), in))
  {
    ++line;
    ncol = 0;
    
    cp = xstrtok(buf, ";\n", &tokp);
    if (!cp)
      continue;

    if (*cp)
    {
      if (sscanf(cp, "%u-%u-%u", &year, &month, &day) != 3)
      {
	ssi_error("parsing date", cp, out);
	goto End;
      }

      bt = str2time(cp);
      wday = weekday(bt);
      
      if (debug)
	fprintf(stderr, "calendar_print_csv: Got date: %s == %04u-%02u-%02u, weekday = %u\n",
		cp, year, month, day, wday);
      
      if (sel_year && year != sel_year)
	continue;

      if (sel_month && month != sel_month)
	continue;

      if (cur_year != year)
	cur_year = year;

      if (cur_month != month)
      {
	cur_month = month;
	curday = 0;
      }
    }

    while (curday+1 < day)
    {
      ++curday;
#if 1
      fprintf(out, "<tr bgcolor=\"%s\">\n", (curday & 1) ? "#FFFFFF" : "#E0E0E0");
#else
      fputs("<tr>\n", out);
#endif
      
      fprintf(out, "<td nowrap style=\"border-top: solid 1px black;\">%04u-%02u-%02u&nbsp;</td>\n", year, month, curday);
      while (++ncol < cols)
	fputs("<td style=\"border-top: solid 1px black;\">&nbsp;</td>\n", out);
      
      fputs("</tr>\n", out);
      ncol = 0;
    }

#if 1
    fprintf(out, "<tr bgcolor=\"%s\">\n", (day & 1) ? "#FFFFFF" : "#E0E0E0");
#else
      fputs("<tr>\n", out);
#endif
      
    if (lastday != day)
      fprintf(out, "<td nowrap style=\"border-top: solid 1px black;\">%04u-%02u-%02u&nbsp;</td>\n", year, month, day);
    else
      fprintf(out, "<td>&nbsp;</td>\n");

    ++ncol;
    curday = day;
    
    while ((cp = xstrtok(NULL, ";\n", &tokp)) != NULL)
    {
      if (lastday != day)
	fputs("<td nowrap style=\"border-top: solid 1px black;\">", out);
      else
	fputs("<td>", out);
      html_puts(cp, out);
      fputs("</td>\n", out);
      ++ncol;
    }
    
    while (++ncol < cols)
    {
      if (lastday != day)
	fputs("<td style=\"border-top: solid 1px black;\">&nbsp;</td>\n", out);
      else
	fputs("<td>&nbsp;</td>\n", out);
    }
    
    lastday = day;
    fputs("</tr>\n", out);
  }

  End:
  while (curday < mdays+1)
  {
    ncol = 0;
    ++curday;
#if 1
    fprintf(out, "<tr bgcolor=\"%s\">\n", (curday & 1) ? "#FFFFFF" : "#E0E0E0");
#else
      fputs("<tr>\n", out);
#endif
    fprintf(out, "<td nowrap style=\"border-top: solid 1px black;\">%04u-%02u-%02u&nbsp;</td>\n", year, month, curday);
    while (++ncol < cols)
      fputs("<td style=\"border-top: solid 1px black;\">&nbsp;</td>\n", out);
    
    fputs("</tr>\n", out);
  }

  fputs("</table>\n", out);
  fclose(in);
}



int
strcncmp(const char *s1,
	 const char *s2,
	 int n)
{
  int d = 0;


  while (n > 0 && (d = tolower(*s1) - tolower(*s2)) == 0)
  {
    --n;
    ++s1;
    ++s2;
  }

  return d;
}


void
filter_title(char *start)
{
    char *cp;

    
    if ((cp = strstr(start, "<title")) ||
	(cp = strstr(start, "<TITLE")))
    {
	start = cp;
	
	if ((cp = strstr(start+6, "</title>")) ||
	    (cp = strstr(start+6, "</TITLE>")))
	{
	    memset(start, ' ', cp-start+8);
	}
    }
}

	

void
file_parse(const char *path,
	   FILE *out,
	   int raw,
	   int *gottitle)
{
    FILE *fp;
    char buf[16384], *cp, *arg, *val, *end, *start, *tokp;
    int stop = 0;
    int got_body = 0;
    int iscgi;
    int local_skip_header = skip_header;
    struct stat sb;
  

    if (debug)
	fprintf(out, "<!-- file_parse:\n\tpath=%s\n\traw=%d\n\tgottitle=%d\n\tskip_header=%d\n\tskip_footer=%d\n-->\n",
		path, raw, *gottitle, skip_header, skip_footer);
    
    if (stat(path, &sb) != 0)
    {
	fputs(ssi_errmsg, out);
	return;
    }

    if (sb.st_mtime > file_dtm)
	file_dtm = sb.st_mtime;
    
    cp = strrchr(path, '.');
  
    iscgi = ((S_IXUSR & sb.st_mode) && cp && strcmp(cp, ".cgi") == 0);
    if (iscgi)
    {
	char pbuf[2048];
    
	setenv("SCRIPT_FILENAME", path, 1);

	strcpy(pbuf, path);
	if (our_argv[1])
	{
	    strcat(pbuf, " \"");
	    strcat(pbuf, our_argv[1]);
	    strcat(pbuf, "\"");
	}

	file_dtm = now;
	
	fp = popen(pbuf, "r");
	if (!fp)
	{
	    fputs(ssi_errmsg, out);
	    return;
	}

	while (fgets(buf, sizeof(buf), fp) &&
	       strcncmp(buf, "Content-Type:", 13) != 0)
	    ;
    }
    else
    {
	fp = fopen(path, "r");
	if (!fp)
	{
	    fputs(ssi_errmsg, out);
	    return;
	}
    }

    while (!stop && fgets(buf, sizeof(buf), fp) != NULL) {
	start = buf;

	if (raw < 2)
	{
	    if (local_skip_header && !got_body)
	    {
		if ((cp = strstr(start, "<body")) || 
		    (cp = strstr(start, "<BODY")))
		{
		    got_body = 1;
		    start = cp+5;
		    while (*start && *start != '>')
			++start;
		    if (*start)
			++start;
		    
		}
		else if (local_skip_header == 1)
		    {
			if (debug)
			    fprintf(out, "<!-- skip_header, checking for heading tags -->\n");
			
			if (strstr(start, "<!DOCTYPE") ||
			    strstr(start, "<!doctype") ||
			    strstr(start, "<html") ||
			    strstr(start, "<HTML"))
			{
			    if (debug)
				fprintf(out, "<!-- skip_header, found doctype or html -->\n");
			
			    local_skip_header = 2;
			    continue;
			}
			else
			{
			    if (strstr(start, "<"))
				local_skip_header = 0;
			}
		    }
		else
		    continue;
	    }
	
	    if (skip_footer)
	    {
		if ((cp = strstr(start, "</body>")) || (cp = strstr(start, "</BODY>")))
		{
		    stop = 1;
		    *cp = '\0';
		}
	    }

	    if ((cp = strstr(start, "<title")) ||
		(cp = strstr(start, "<TITLE")))
	    {
		if (!*gottitle)
		    *gottitle = 1;
		else
		{
		    char *tp = cp+6;
		    if ((cp = strstr(tp, "</title>")) ||
			(cp = strstr(tp, "</TITLE>")))
			start = cp+8;
		}
	    }
	}
	
	while ((cp = strstr(start, "<!--#")) != NULL) {
	    end = strstr(cp, "-->");
	    if (!end)
		break;

	    *cp = '\0';
	    fputs(start, out);

	    *end = '\0';
	    start = end+3;

	    cp  = xstrtok(cp+5, " \t", &tokp);
	    arg = xstrtok(NULL, " \t=", &tokp);
	    val = xstrtok(NULL, " \t", &tokp);

	    if (strcmp(cp, "config") == 0 && arg && val) {
		if (strcmp(arg, "errmsg") == 0)
		    ssi_errmsg = strdup(val);
		else if (strcmp(arg, "timefmt") == 0)
		    ssi_timefmt = strdup(val);
		else if (strcmp(arg, "sizefmt") == 0)
		    ssi_sizefmt = strdup(val);
		else
		    fputs(ssi_errmsg, out);
	    }

	    else if (strcmp(cp, "echo") == 0) {
		fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "exec") == 0) {
		fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "fsize") == 0) {
		struct stat sb;
		char *p;

		if (arg && val)
		    p = ssi_make_path(arg, val);
		else
		    p = strdup(path);

		if (p && stat(p, &sb) == 0)
		    fprintf(out, "%lu", sb.st_size);
		else
		    fputs(ssi_errmsg, out);
		free(p);
	    }
      
	    else if (strcmp(cp, "flastmod") == 0) {
		time_t dtm = 0;
		struct stat sb;
		char *p;

		if (arg && val)
		{
		    p = ssi_make_path(arg, val);
		    if (p)
		    {
			if (stat(p, &sb) == 0)
			    dtm = sb.st_mtime;
			free(p);
		    }
		}
		else
		    dtm = file_dtm;
		
		if (dtm)
		    fputs(xtime(&dtm, ssi_timefmt), out);
		else
		    fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "include") == 0 && arg && val) {
		char *p;
	
		p = ssi_make_path(arg, val);
		if (p)
		    file_parse(p, out, 1, gottitle);  /* FIXME! 2 or 1 */
		else
		    fputs(ssi_errmsg, out);
		free(p);
	    }
      
	    else if (strcmp(cp, "printenv") == 0) {
		fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "set") == 0) {
		fputs(ssi_errmsg, out);
	    }

	    else if (strcmp(cp, "x-creole") == 0 && arg && val) {
		char *p;
		FILE *fp;
	
		p = ssi_make_path(arg, val);
		if (p && (fp = fopen(p, "r")) != NULL)
		{
		    creole_parse(fp, out);
		    fclose(fp);
		}
		else
		    fputs(ssi_errmsg, out);
		free(p);
	    }
      
	    else if (strcmp(cp, "x-href") == 0) {
		DIRNODE *dnp = NULL;
		char *baseurl = path_translated_dir;
		char *title = NULL;
		char *target = NULL;
		const char *href = NULL;
		int rlen = strlen(document_root);
		
		
		while (arg)
		{
		    if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "title") == 0 && val)
		    {
			title = strdup(val);
		    }

		    else if (strcmp(arg, "target") == 0 && val)
		    {
			target = strdup(val);
		    }

		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}

		if (!target ||
		    (dnp = dirtree_load(baseurl, -1)) == NULL ||
		    (href = dirtree_locate(dnp, target)) == NULL)
		{
		    fputs(ssi_errmsg, out);
		}
		else
		{
		    if (!title)
			html_puts(href ? href+rlen : "/unknown", out);
		    else
		    {
			fprintf(out, "<a href=\"");
			html_puts(href ? href+rlen : "/unknown", out);
			fprintf(out, "\">");
			html_puts(title, out);
			fprintf(out, "</a>");
		    }
		}

		dirtree_free(dnp);
	    }

	    else if (strcmp(cp, "x-gallery") == 0) {
		struct stat sb;
		char *base = NULL;
		char *dir = ".";
		char *match = NULL;

	
		while (arg)
		{
		    if (strcmp(arg, "path") == 0 && val)
		    {
			if (*val == '/')
			    dir = concat(document_root, NULL, val);
			else
			    dir = concat(path_translated_dir, "/", val);
			base = val;
		    }

		    else if (strcmp(arg, "width") == 0)
		    {
			if (val && sscanf(val, "%u", &gallery_width) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }
		    else if (strcmp(arg, "match") == 0 && val)
		    {
			match = strdup(val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}

		if (!dir || stat(dir, &sb) < 0 || !S_ISDIR(sb.st_mode) ||
		    ssi_gallery(base, dir, match, out) < 0)
		{
		    fputs(ssi_errmsg, out);
		}
		else
		{
		    if (sb.st_mtime > file_dtm)
			file_dtm = sb.st_mtime;
		}
	    }
	    
	    else if (strcmp(cp, "x-directory") == 0) {
		struct stat sb;
		char *base = NULL;
		char *dir = ".";
		char *match = NULL;

	
		while (arg)
		{
		    if (strcmp(arg, "path") == 0 && val)
		    {
			if (*val == '/')
			    dir = concat(document_root, NULL, val);
			else
			    dir = concat(path_translated_dir, "/", val);
			base = val;
		    }

		    else if (strcmp(arg, "match") == 0 && val)
		    {
			match = strdup(val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}

		if (!dir || stat(dir, &sb) < 0 || !S_ISDIR(sb.st_mode) ||
		    ssi_directory(base, dir, match, out) < 0)
		{
		    fputs(ssi_errmsg, out);
		}
		else
		{
		    if (sb.st_mtime > file_dtm)
			file_dtm = sb.st_mtime;
		}
	    }

	    else if (strcmp(cp, "x-uri") == 0)
	    {
		fputs(request_uri, out);
	    }
      
	    else if (strcmp(cp, "x-uri-print") == 0)
	    {
		fputs("http://", out);
		fputs(http_host, out);
		fputs(request_url, out);
		fputs("/", out);
		if (query_string && *query_string)
		{
		    fputs("?", out);
		    fputs(query_string, out);
		}
		
	    }
      
	    else if (strcmp(cp, "x-title") == 0)
	    {
		if (index_title)
		    fputs(index_title, out);
	    }
      
	    else if (strcmp(cp, "x-navbar") == 0)
	    {
		char *baseurl = document_root;
		char *navbar;
	
		while (arg)
		{
		    if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		navbar = navbar_create(path_translated_dir, baseurl);
		if (navbar)
		    fputs(navbar, out);
	    }
      
	    else if (strcmp(cp, "x-titlebar") == 0)
	    {
		char *baseurl = document_root;
		char *titlebar;
	
		while (arg)
		{
		    if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		titlebar = titlebar_create(path_translated_dir, baseurl);
		if (titlebar)
		    fputs(titlebar, out);
	    }

	    else if (strcmp(cp, "x-oldtable") == 0 && arg && val)
	    {
		char *tpath = NULL, *xp, *yp;
		char *opts = strdup("");
		int variant = 0;
		char *width=NULL;
	
		while (arg && *arg)
		{
		    xp = ssi_make_path(arg, val);
		    if (xp)
		    {
			if (tpath)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}

			tpath = xp;
		    }
		    else if (strcmp(arg, "variant") == 0)
		    {
			if (val && sscanf(val, "%u", &variant) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }
		    else if (strcmp(arg, "cellwidth") == 0)
		    {
			if (!val)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
			width=strdup(val);
		    }
		    else
		    {
			if (val && *val)
			{
			    yp = concat(arg, "=", val);
			    xp = concat(opts, " ", yp);
			    free(yp);
			}
			else
			    xp = concat(opts, " ", arg);
	    
			free(opts);
			opts = xp;
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		if (tpath)
		{
		    table_print_csv(tpath, opts, out, variant, width);
		    free(tpath);
		}
		else
		    fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "x-table") == 0 && arg && val)
	    {
		char *tpath = NULL, *xp, *yp;
		char *opts = strdup("");
		int header = 0;
		int field = -1;
		int count = 0;
		int sorttype = 0;
		int date_field = -1;
		int date_range = 0;
		int striped = 0;
		int rows = 0;
		int cols = 0;
		char *width=NULL;
		char *filter=NULL;
	
		while (arg && *arg)
		{
		    xp = ssi_make_path(arg, val);
		    if (xp)
		    {
			if (tpath)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}

			tpath = xp;
		    }
	  
		    else if (strcmp(arg, "header") == 0)
		    {
			if (val && sscanf(val, "%d", &header) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "field") == 0)
		    {
			if (val && sscanf(val, "%u", &field) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "striped") == 0)
		    {
			if (val && sscanf(val, "%u", &striped) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "range") == 0 || strcmp(arg, "rows") == 0)
		    {
			if (val && sscanf(val, "%u", &rows) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "cols") == 0)
		    {
			if (val && sscanf(val, "%u", &cols) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "count") == 0)
		    {
			if (val && sscanf(val, "%u", &count) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "date-field") == 0)
		    {
			if (val && sscanf(val, "%u", &date_field) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "date-range") == 0)
		    {
			if (val && sscanf(val, "%u", &date_range) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "sort") == 0)
		    {
			if (val && sscanf(val, "%u", &sorttype) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }

		    else if (strcmp(arg, "filter") == 0)
		    {
			if (val)
			    filter = strdup(val);
			else
			    filter = NULL;
		    }

		    else if (strcmp(arg, "cellwidth") == 0)
		    {
			if (!val)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
			width=strdup(val);
		    }
		    else
		    {
			if (val && *val)
			{
			    char *tmp;

			    tmp = concat(val, "", "\"");
			    yp = concat(arg, "=\"", tmp);
			    xp = concat(opts, " ", yp);
			    free(yp);
			}
			else
			    xp = concat(opts, " ", arg);
	    
			free(opts);
			opts = xp;
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		if (tpath)
		{
		    int n = -1;
		    char *ss;

	    
		    form_init(NULL);
		    ss = form_get("sort");
		    if (ss)
			sscanf(ss, "%d", &n);
		    if (n != -1)
			sorttype = n;

		    n = -1;
		    ss = form_get("field");
		    if (ss)
			sscanf(ss, "%d", &n);
		    if (n != -1)
			field = n;
	    
		    ss = form_get("filter");
		    if (ss)
			filter = strdup(ss);
	    
		    TABLE *tblp = table_create();
		    table_load(tblp, tpath, header ? 1 : 0);
		    if (date_field != -1)
			table_date_filter(tblp, date_field, date_range);

#if 0
		    if (rows > 0 && tblp->rows > rows)
			tblp->rows = rows; /* XXX: Hack, should free rows... */
#endif
		    
		    if (sorttype != 0)
			table_sort(tblp, field, sorttype);
		    
		    table_print_html(tblp, out, opts, width, filter, field, count, striped, rows, cols,
				     (header < 0 ? 1 : 0));
		    /*	    table_free(tblp); */
		    free(tpath);
		}
		else
		    fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "x-calendar") == 0 && arg && val)
	    {
		char *tpath = NULL, *xp, *yp;
		char *opts = strdup("");
		int year, month, cols;
		struct tm *tp;

		tp = localtime(&now);
		year = tp->tm_year;
		month = tp->tm_mon;
		cols = 0;
	
		while (arg)
		{
		    xp = ssi_make_path(arg, val);
		    if (xp)
		    {
			if (tpath)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}

			tpath = xp;
		    }
		    else if (strcmp(arg, "year") == 0)
		    {
			if (val && sscanf(val, "%u", &year) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }
		    else if (strcmp(arg, "month") == 0)
		    {
			if (val && sscanf(val, "%u", &month) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }
		    else if (strcmp(arg, "cols") == 0)
		    {
			if (val && sscanf(val, "%u", &cols) != 1)
			{
			    fputs(ssi_errmsg, out);
			    break;
			}
		    }
		    else
		    {
			yp = concat(arg, "=", val);
			xp = concat(opts, " ", yp);
			free(yp);
			free(opts);
			opts = xp;
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		if (tpath)
		{
		    calendar_print_csv(tpath, year, month, cols, opts, out);
		    free(tpath);
		}
		else
		    fputs(ssi_errmsg, out);
	    }
      
	    else if (strcmp(cp, "x-folderview") == 0)
		folderview_print(path_translated_dir, out);

	    else if (strcmp(cp, "x-submenu") == 0)
	    {
		DIRNODE *dnp;
		char *baseurl = path_translated_dir;
		char *openurl = path_translated_dir;
		char *type = "ol";
		char *style = NULL;

	
		while (arg)
		{
		    if (strcmp(arg, "open") == 0)
		    {
			if (!val || !*val || strcmp(val, "ALL") == 0)
			    openurl = NULL;
			else if (*val == '/')
			    openurl = concat(document_root, NULL, val);
			else
			    openurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "type") == 0 && val)
			type = strdup(val);
	  
		    else if (strcmp(arg, "style") == 0 && val)
			style = strdup(val);

		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		dnp = dirtree_load(baseurl, -1);

		if (dnp)
		{
		    dirtree_submenu_print(dnp, openurl, out, type, style);
		    dirtree_free(dnp);
		}
	    }
      
	    else if (strcmp(cp, "x-menu") == 0)
	    {
		DIRNODE *dnp;
		char *openurl = path_translated_dir;
		char *baseurl = document_root;
		char *type = "ol";
		char *style = NULL;
	
		while (arg)
		{
		    if (strcmp(arg, "open") == 0)
		    {
			if (!val || !*val || strcmp(val, "ALL") == 0)
			    openurl = NULL;
			else if (*val == '/')
			    openurl = concat(document_root, NULL, val);
			else
			    openurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "type") == 0 && val)
			type = strdup(val);
	  
		    else if (strcmp(arg, "style") == 0 && val)
			style = strdup(val);

		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		dnp = dirtree_load(baseurl, -1);

		if (dnp)
		{
		    dirtree_menu_print(dnp, openurl, 0, out, type, style);
		    dirtree_free(dnp);
		}
	    }
      
	    else if (strcmp(cp, "x-up") == 0)
	    {
		DIRNODE *dnp;
		char *openurl = path_translated_dir;
		char *baseurl = document_root;
	
		while (arg)
		{
		    if (strcmp(arg, "open") == 0)
		    {
			if (!val || !*val || strcmp(val, "ALL") == 0)
			    openurl = NULL;
			else if (*val == '/')
			    openurl = concat(document_root, NULL, val);
			else
			    openurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		dnp = dirtree_load(baseurl, -1);

		if (dnp)
		{
		    char *url = NULL;
		    
		    dirtree_up_get(dnp, openurl, 0, &url);
		    dirtree_free(dnp);

		    if (url)
		    {
			html_puts(url, out);
			free(url);
		    }
		}
	    }
      
	    else if (strcmp(cp, "x-last") == 0)
	    {
		DIRNODE *dnp;
		char *openurl = path_translated_dir;
		char *baseurl = document_root;
	
		while (arg)
		{
		    if (strcmp(arg, "open") == 0)
		    {
			if (!val || !*val || strcmp(val, "ALL") == 0)
			    openurl = NULL;
			else if (*val == '/')
			    openurl = concat(document_root, NULL, val);
			else
			    openurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		dnp = dirtree_load(baseurl, -1);

		if (dnp)
		{
		    char *url = NULL;
		    
		    dirtree_last_get(dnp, openurl, 0, &url);
		    dirtree_free(dnp);

		    if (url)
		    {
			html_puts(url, out);
			free(url);
		    }
		}
	    }
      
	    else if (strcmp(cp, "x-prev") == 0)
	    {
		DIRNODE *dnp;
		char *openurl = path_translated_dir;
		char *baseurl = document_root;
	
		while (arg)
		{
		    if (strcmp(arg, "open") == 0)
		    {
			if (!val || !*val || strcmp(val, "ALL") == 0)
			    openurl = NULL;
			else if (*val == '/')
			    openurl = concat(document_root, NULL, val);
			else
			    openurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		dnp = dirtree_load(baseurl, -1);

		if (dnp)
		{
		    char *url = NULL;
		    
		    dirtree_prev_get(dnp, openurl, 0, &url);
		    dirtree_free(dnp);

		    if (url)
		    {
			html_puts(url, out);
			free(url);
		    }
		}
	    }
      
	    else if (strcmp(cp, "x-next") == 0)
	    {
		DIRNODE *dnp;
		char *openurl = path_translated_dir;
		char *baseurl = document_root;
	
		while (arg)
		{
		    if (strcmp(arg, "open") == 0)
		    {
			if (!val || !*val || strcmp(val, "ALL") == 0)
			    openurl = NULL;
			else if (*val == '/')
			    openurl = concat(document_root, NULL, val);
			else
			    openurl = concat(path_translated_dir, "/", val);
		    }
	  
		    else if (strcmp(arg, "base") == 0 && val)
		    {
			if (*val == '/')
			    baseurl = concat(document_root, NULL, val);
			else
			    baseurl = concat(path_translated_dir, "/", val);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}
	
		dnp = dirtree_load(baseurl, -1);

		if (dnp)
		{
		    char *url = NULL;
		    int nflag = 0;
		    
		    dirtree_next_get(dnp, openurl, 0, &nflag, &url);
		    dirtree_free(dnp);

		    if (url)
		    {
			html_puts(url, out);
			free(url);
		    }
		}
	    }
      
	    else if (strcmp(cp, "x-date") == 0) {
		fputs(xtime(&now, "%Y-%m-%d"), out);
	    }

	    else if (strcmp(cp, "x-time") == 0) {
		fputs(xtime(&now, "%H:%M:%S"), out);
	    }
      
	    else if (strcmp(cp, "x-random") == 0)
	    {
		int start = 0;
		unsigned int length = 100;
		unsigned int zeros = 0;
		int v;
		
		
		while (arg)
		{
		    if (strcmp(arg, "start") == 0)
		    {
			if (val)
			    sscanf(val, "%d", &start);
		    }
		    
		    else if (strcmp(arg, "length") == 0)
		    {
			if (val)
			    sscanf(val, "%u", &length);
		    }
	  
		    else if (strcmp(arg, "zeros") == 0)
		    {
			if (val)
			    sscanf(val, "%u", &zeros);
		    }
	  
		    arg = xstrtok(NULL, " \t=", &tokp);
		    val = xstrtok(NULL, " \t", &tokp);
		}

		v = start+(rand()%length);

		if (zeros)
		{
		    char fmt[256];
		    sprintf(fmt, "%%0%ud", zeros);
		    
		    fprintf(out, fmt, v);
		}
		else
		    fprintf(out, "%d", v);
	    }
	
	    else if (strcmp(cp, "x-parse") == 0 && arg && val) {
		char *p;
	
		p = ssi_make_path(arg, val);
		if (p)
		    file_parse(p, out, 0, gottitle);
		else
		    fputs(ssi_errmsg, out);
		free(p);
	    }
      
	    else if (strcmp(cp, "x-head") == 0) {
		char *p, *head;

		if (arg && val)
		{
		    p = ssi_make_path(arg, val);
		    if (p)
		    {
			head = file_get_section(p, "head");
			if (head)
			{
			    fputs(head, out);
			    free(head);
			}
		    }
		    else
			fputs(ssi_errmsg, out);
		    free(p);
		}
		else
		{
		    if (index_head)
		    {
			if (*gottitle)
			    filter_title(index_head);
			fputs(index_head, out);
		    }
		}
	    }
      
	    else if (strcmp(cp, "x-write") == 0 && arg && val) {
		char *p;
	
		p = ssi_make_path(arg, val);
		if (p)
		    file_write(p, out);
		else
		    fputs(ssi_errmsg, out);

		free(p);
	    }

	    else
		fputs(ssi_errmsg, out);
	}
    
	fputs(start, out);
    }

    if (iscgi)
	pclose(fp);
    else
	fclose(fp);
}


void
sigalrm_handler(int sig)
{
    write(1, "[TIMEOUT]\n", 10);
    _exit(0);
}

char *
locate_file(const char *file)
{
  char *path, *dpath, *cp;
  
  dpath = strdup(path_translated_dir);


  while (*dpath)
  {
    path = fconcat(dpath, file);
    if (access(path, R_OK) == 0)
    {
      free(dpath);
      return path;
    }

    free(path);
    cp = strrchr(dpath, '/');
    if (!cp)
      break;

    *cp = '\0';
   }
  
  free(dpath);
  return NULL;
}

  
unsigned long
file_size(const char *path)
{
  struct stat sb;

  stat(path, &sb);
  return sb.st_size;
}


int
is_doubleslash(const char *uri)
{
    const char *cp;


    if (!uri)
	return 0;
    
    cp = strchr(uri, '?');
    if (!cp)
	cp = uri+strlen(uri);
    cp -= 2;
    if (cp >= uri && cp[0] == '/' && cp[1] == '/')
	return 1;

    return 0;
}



int
main(int argc, char *argv[])
{
  char *path, *header_path, *index_path, *footer_path, *cp;
  int i, j;
  int got_title = 0;
  FILE *fp;
  struct stat sb;
  time_t end;
  

  signal(SIGALRM, sigalrm_handler);
  signal(SIGPIPE, SIG_IGN);
  
  alarm(60);

  time(&now);
  srand(now*getpid());

  header_path = footer_path = NULL;
  
  our_argv = argv;
  
  for (i = 1; i < argc; i++)
  {
    if (strcmp(argv[i], "x-debug") == 0)
      ++debug;
    
    else if (strcmp(argv[i], "x-nowrap") == 0)
      ++nowrap;
    
    else if (strcmp(argv[i], "x-raw") == 0)
      ++raw;

    else
      continue;

    for (j = i; j < argc-1; j++)
      argv[j] = argv[j+1];
    argv[j] = NULL;

    
    --argc;
    --i;
  }
  
  env_get();

#if 0
  form_init(NULL);
#endif
  
  if (!path_translated)
  {
    fprintf(stderr, "%s: missing environment PATH_TRANSLATED\n", argv[0]);
    exit(1);
  }

  if (!path_info)
  {
    fprintf(stderr, "%s: missing environment PATH_INFO\n", argv[0]);
    exit(1);
  }

  if (debug)
  {
    path = fconcat(document_root, "debug.log");
    freopen(path, "a", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    free(path);

    fprintf(stderr, "*** Index: Start at %s", ctime(&now));
    fflush(stderr);

    fprintf(stderr, "PATH_TRANSLATED=%s\n", path_translated);
    fprintf(stderr, "PATH_INFO=%s\n", path_info);
    fprintf(stderr, "DOCUMENT_ROOT=%s\n", document_root);
  }
  
  if (strncmp(path_translated, "redirect:/cgi-bin/index.cgi/", 28) == 0)
  {
      char *lp;
      path_translated = concat(document_root, "/", path_translated+28);
      lp = path_translated+strlen(path_translated);
      if (lp > path_translated && lp[-1] == '/')
	  path_translated = concat(path_translated, "/", "index.html");
  }
  
  path_translated_dir = strdup(path_translated);
  if (stat(path_translated, &sb) == 0 &&
      !S_ISDIR(sb.st_mode))
  {
    cp = strrchr(path_translated_dir, '/');
    *cp = '\0';
  }
  
  chdir(path_translated_dir);
  
  if (debug)
    fprintf(stderr,
	    "path_translated = %s\n, path_translated_dir = %s\n, path_info = %s\nnowrap = %d, raw = %d\n",
	    path_translated, path_translated_dir, path_info, nowrap, raw);

  puts("Content-Type: text/html\n");
  fflush(stdout);

  j = strlen(path_info);
  if (raw ||
      (0 && j > 5 && strcmp(path_info+j-5, ".html") == 0) ||
      is_doubleslash(request_uri))
  {
      nowrap = 1;
  }
  
  index_path = strdup(path_translated);
  
  if (!nowrap && !raw)
  {
    header_path = locate_file("header.html");
    footer_path = locate_file("footer.html");

    if (debug)
      fprintf(stderr, "header_path = %s\nfooter_path = %s\n",
	      header_path, footer_path);
	       
  }
  
  index_title = file_get_section(index_path, "title");
  index_head = file_get_section(index_path, "head");
  
  if (header_path && access(header_path, R_OK) == 0) {
      file_parse(header_path, stdout, 0, &got_title);
      free(header_path);
      skip_header = 1;
  }
  
  if (footer_path && access(footer_path, R_OK) == 0)
    skip_footer = 1;

  if (!raw)
    file_parse(index_path, stdout, 0, &got_title);
  else
    file_write(index_path, stdout);
  
  free(index_path);

  if (footer_path)
  {
      skip_footer = 0;
      file_parse(footer_path, stdout, 0, &got_title);
      free(footer_path);
  }
  
  if (debug > 1)
  {
    fp = popen("/usr/bin/uptime", "r");
    fsend(fp, stderr);
    pclose(fp);
    fp = popen("/bin/ps auxw", "r");
    fsend(fp, stderr);
    pclose(fp);
  }

  time(&end);
  if (debug)
      fprintf(stderr, "*** Index: Done at %s", ctime(&now));

  do_accesslog();
#if 0  
  printf("<!-- Exec time: %u seconds -->\n", end-now);
#endif
  return 0;
}
