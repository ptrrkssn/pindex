/*
** table.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "html.h"
#include "table.h"
#include "csv.h"

TABLE *
table_create(void)
{
    TABLE *tp;

    tp = malloc(sizeof(*tp));
    if (!tp)
	return NULL;

    memset(tp, 0, sizeof(*tp));
    return tp;
}

void
table_free(TABLE *tp)
{
    int r, c;

    if (!tp)
	return;
    
    if (tp->head)
	for (c = 0; tp->head[c]; ++c)
	    free(tp->head);

    if (tp->cell)
    {
	for (r = 0; tp->cell[r]; ++r)
	{
	    for (c = 0; tp->cell[r][c]; ++c)
		free(tp->cell[r][c]);
	    free(tp->cell[r]);
	}
	free(tp->cell);
    }
    free(tp);
}

static char **
getrow(CSV *cp, int row)
{
    char **ncv, **cv = NULL, *cell;
    int maxcols = 0;
    int col = 0;
    int rc = -1;
    int i;
    char buf[256];
    

    cv = calloc(sizeof(char *), maxcols = 64);
    if (!cv)
	return NULL;

    sprintf(buf, "%u", row);
    cell = strdup(buf);
    rc = 1;
    
    while (rc >= 0)
    {
	if (!cell)
	    cell = strdup("");
	
	if (col+1 >= maxcols)
	{
	    ncv = calloc(sizeof(char *), maxcols += 16);
	    if (!ncv)
	    {
		if (cv)
		    free(cv);
		return NULL;
	    }
	    
	    if (cv)
	    {
		for (i = 0; i < col; i++)
		    ncv[i] = cv[i];
		free(cv);
	    }
	    cv = ncv;
	}
	cv[col++] = cell;
	
	rc = csv_getsdup(&cell, cp);
    }

    cv[col] = NULL;
    
    if (rc < 0 && rc != -2)
    {
	if (cv)
	    free(cv);
	return NULL;
    }
    
    return cv;
}

int
table_load(TABLE *tp,
	   const char *path,
	   int header)
{
    CSV *cp;
    char **cv, ***nrv;
    int i;
    int row = 0;
    
    
    cp = csv_open(path, "r");

    if (header)
	tp->head = getrow(cp, row);

    ++row;
    
    if (!tp->cell)
    {
	tp->cell = calloc(sizeof(char **), tp->maxrows = 128);
	tp->rows = 0;
    }

    while ((cv = getrow(cp, row++)) != NULL)
    {
	if (tp->rows+1 >= tp->maxrows)
	{
	    nrv = calloc(sizeof(char **), tp->maxrows += 128);
	    if (!nrv)
		return -1;
	    
	    for (i = 0; i < tp->rows; i++)
		nrv[i] = tp->cell[i];
	    free(tp->cell);
	    tp->cell = nrv;
	}
	tp->cell[tp->rows++] = cv;
    }
    tp->cell[tp->rows] = NULL;

    csv_close(cp);
    return tp->rows;
}

int
table_print_html(TABLE *tp,
		 FILE *fp,
		 const char *opts,
		 const char *width,
		 const char *filter,
		 int field,
		 int count,
		 int striped,
		 int rows,
		 int cols,
		 int skip_header)
{
    int r, c, n = 0, nc;


    if (!tp)
	return -1;
    
    fprintf(fp, "<table");
    if (opts)
	fputs(opts, fp);
    fputs(">\n", fp);
    
    if (tp->head && !skip_header)
    {
	n = 1;
	
	fprintf(fp, "<tr class=\"header\">\n");
	if (count)
	{
	    fprintf(fp, "<th nowrap class=\"header\" align=right>");
	    fprintf(fp, "<a href=\"?field=%u&amp;sort=1\">", 0);
	    fputs("#", fp);
	    fputs("</a>", fp);
	    fputs("</th>\n", fp);
	}
	    
	for (c = 1; tp->head[c] && (!cols || c < cols); ++c)
	{
	    fprintf(fp, "<th nowrap class=\"header\" align=left %s%s>",
		    (c == 1 && width) ? "width=" : "", (c == 1 && width) ? width : "");

	    fprintf(fp, "<a href=\"?field=%u&amp;sort=1\">", c);
	    html_puts(tp->head[c], fp);
	    fprintf(fp, "</a>");
	    fprintf(fp, "</th>\n");	    
	}
	fprintf(fp, "</tr>\n");
    }
    
    if (tp->cell)
    {
	for (r = 0; r < tp->rows && tp->cell[r] && (!rows || n < rows); ++r)
	{
	    if (filter)
	    {
		if (field >= 0)
		{
		    int i;
		    
		    for (i = 0; field != i && tp->cell[r][i] != NULL; i++)
			;
		    if (tp->cell[r][i] && strstr(tp->cell[r][i], filter) == NULL)
			continue;
		}
		else
		{
		    int i;
		    
		    for (i = 0; tp->cell[r][i] != NULL &&
			     strstr(tp->cell[r][i], filter) == NULL; i++)
			;
		    if (!tp->cell[r][i])
			continue;
		}
	    }

	    /* fputs("<tr bgcolor=\"#D8D8D8\">\n", fp); */
	    if (striped && (n & 1))
		fputs("<tr class=\"odd\">\n", fp);
	    else
		fputs("<tr class=\"even\">\n", fp);
	    ++n;
	    
#if 0
	    if (count)
	    {
		fprintf(fp, "<td nowrap align=right>");
		printf("<b>%u</b>", count++);
		fprintf(fp, "</td>\n");	    
	    }
#endif

	    nc = 0;
	    for (c = (count ? 0 : 1); tp->cell[r][c] && (!cols || nc < cols); ++c)
	    {
		fprintf(fp, "<td nowrap align=\"%s\" %s%s>",
			(count && c == 0) ? "right" : "left",
			(c == 0 && width) ? "width=" : "",
			(c == 0 && width) ? width : "");
		html_puts(tp->cell[r][c], fp);
		fprintf(fp, "</td>\n");
		++nc;
	    }
	    fprintf(fp, "</tr>\n");
	}
    }
    fprintf(fp, "</table>\n");
    
    return 0;
}



static int sort_col = 0;
static int sort_dir = 1;


static const char *
get_cell(const char **row,
	 int col)
{
    int c;

    
    for (c = 0; row[c] != NULL && c < col; ++c)
	;

    return row[c];
}

static int
i_sort_auto(const void *e1,
	    const void *e2,
	    int sort_col)
{
    const char ***r1 = (const char ***) e1;
    const char ***r2 = (const char ***) e2;
    const char *c1, *c2;
    int i1, i2;
    double d1, d2;

    
    c1 = get_cell(*r1, sort_col);
    c2 = get_cell(*r2, sort_col);

    if (!c1 && !c2)
	return 0;

    if (!c1)
	return -1;
    if (!c2)
	return +1;

    if (sscanf(c1, "%lf", &d1) == 1 &&
	sscanf(c2, "%lf", &d2) == 1)
    {
	if (d1 - d2 < 0)
	    return -1;
	else if (d1 - d2 > 0)
	    return +1;
	else
	    return 0;
    }

    if (sscanf(c1, "%d", &i1) == 1 &&
	sscanf(c2, "%d", &i2) == 1)
	return i1 - i2;

    return strcmp(c1, c2);
}


static int
sort_auto(const void *e1,
	  const void *e2)
{
    int d;
    
    d = i_sort_auto(e1, e2, sort_col);
    if (d)
	return d * sort_dir;

    if (sort_col != 0)
	return i_sort_auto(e1, e2, 0) * sort_dir;

    return 0;
}

static int
sort_str(const void *e1,
	 const void *e2)
{
    const char ***r1 = (const char ***) e1;
    const char ***r2 = (const char ***) e2;
    const char *c1, *c2;
    int d;
    

    c1 = get_cell(*r1, sort_col);
    c2 = get_cell(*r2, sort_col);

    if (!c1 && !c2)
	return 0;

    if (!c1)
	return -1 * sort_dir;
    if (!c2)
	return +1 * sort_dir;

    d = strcmp(c1, c2);
    if (d)
	return d * sort_dir;

    if (sort_col != 0)
	return i_sort_auto(e1, e2, 0) * sort_dir;
    return 0;
}

static int
sort_int(const void *e1,
	 const void *e2)
{
    const char ***r1 = (const char ***) e1;
    const char ***r2 = (const char ***) e2;
    const char *c1, *c2;
    int v1, v2, d;

    
    c1 = get_cell(*r1, sort_col);
    c2 = get_cell(*r2, sort_col);

    if (!c1 && !c2)
	return 0;

    if (!c1)
	return -1 * sort_dir;
    if (!c2)
	return +1 * sort_dir;

    if (sscanf(c1, "%d", &v1) != 1)
	return -1 * sort_dir;
    
    if (sscanf(c2, "%d", &v2) != 1)
	return +1 * sort_dir;

    d = v1 - v2;
    if (d)
	return d * sort_dir;

    if (sort_col != 0)
	return i_sort_auto(e1, e2, 0) * sort_dir;
    else
	return 0;
}

static int
sort_dbl(const void *e1,
	 const void *e2)
{
    const char ***r1 = (const char ***) e1;
    const char ***r2 = (const char ***) e2;
    const char *c1, *c2;
    double d1, d2, d;

    
    c1 = get_cell(*r1, sort_col);
    c2 = get_cell(*r2, sort_col);

    if (!c1 && !c2)
	return 0;

    if (!c1)
	return -1 * sort_dir;
    if (!c2)
	return +1 * sort_dir;

    if (sscanf(c1, "%lf", &d1) != 1)
	return -1 * sort_dir;
    
    if (sscanf(c2, "%lf", &d2) != 1)
	return +1 * sort_dir;

    d = d1 - d2;
    if (d < 0)
	return -1 * sort_dir;

    if (d > 0)
	return +1 * sort_dir;

    if (sort_col != 0)
	return i_sort_auto(e1, e2, 0) * sort_dir;
    else
	return 0;
}

	 
int
table_sort(TABLE *tp,
	   int col,
	   int type)
{
    sort_col = col;
    sort_dir = (type < 0 ? -1 : 1);
    
    switch (type)
    {
      case 1:
	qsort(&tp->cell[0], tp->rows, sizeof(tp->cell[0]), sort_auto);
	break;
	
      case 2:
	qsort(&tp->cell[0], tp->rows, sizeof(tp->cell[0]), sort_str);
	break;
	
      case 3:
	qsort(&tp->cell[0], tp->rows, sizeof(tp->cell[0]), sort_int);
	break;

      case 4:
	qsort(&tp->cell[0], tp->rows, sizeof(tp->cell[0]), sort_dbl);
	break;

      default:
	return -1;
    }

    return 0;
}


int
str2time2(const char *str,
	  time_t *start,
	  time_t *stop)
{
  struct tm tmb;
  int y1, m1, d1, y2, m2, d2;

  
  y1 = m1 = d1 = y2 = m2 = d2 = 0;
  

  *start = *stop = (time_t) -1;
  
  switch (sscanf(str, "%u-%u-%u - %u-%u-%u", &y1, &m1, &d1, &y2, &m2, &d2))
  {
    case 4:
    case 5:
    case 6:
      memset(&tmb, 0, sizeof(tmb));
      tmb.tm_year = y2-1900;
      tmb.tm_mon = m2 ? m2-1 : 0;
      tmb.tm_mday = d2 ? d2 : 1;
      tmb.tm_hour = 0;
      tmb.tm_min = 0;
      tmb.tm_sec = 0;
      tmb.tm_isdst = -1;
      *stop = mktime(&tmb);

    case 1:
    case 2:
    case 3:
      memset(&tmb, 0, sizeof(tmb));
      tmb.tm_year = y1-1900;
      tmb.tm_mon = m1 ? m1-1 : 0;
      tmb.tm_mday = d1 ? d1 : 1;
      tmb.tm_hour = 0;
      tmb.tm_min = 0;
      tmb.tm_sec = 0;
      tmb.tm_isdst = -1;
      *start = mktime(&tmb);

      if (*stop == (time_t) -1)
      {
	  memset(&tmb, 0, sizeof(tmb));
	  tmb.tm_year = y1-1900;
	  tmb.tm_mon = m1 ? m1-1 : 0;
	  tmb.tm_mday = d1 ? d1 : 1;
	  tmb.tm_hour = 23;
	  tmb.tm_min = 59;
	  tmb.tm_sec = 59;
	  tmb.tm_isdst = -1;
	  *stop = mktime(&tmb);
      }
      break;

    default:
      return -1;
  }

  return 0;
}


int
table_date_filter(TABLE *tp,
		  int date_field,
		  int date_range)
{
    int r, c, i;
    time_t now;
    time_t start, stop;
    

    if (!tp)
	return -1;
    
    time(&now);
    for (r = 0; tp->cell[r]; ++r)
    {
	for (c = 0; tp->cell[r][c] && c < date_field; ++c)
	    ;

	if (c != date_field || !tp->cell[r][c])
	    continue;

	str2time2(tp->cell[r][c], &start, &stop);

	if (stop < now ||
	    (date_range && start != (time_t) -1 &&
	     (start > now+date_range*24*60*60)))
	{
	    for (i = r; tp->cell[i+1]; ++i)
		tp->cell[i] = tp->cell[i+1];
	    tp->cell[i] = NULL;
	    --r;
	    tp->rows--;
	}
    }

    return r;
}


#if 0
int
main(int argc,
     char *argv[])
{
    TABLE *tp;
    int scol, type;
    

    scol = atoi(argv[2]);
    type = atoi(argv[3]);
    
    tp = table_create();
    table_load(tp, argv[1], 1);

    table_sort(tp, scol, type);
    
    table_print_html(tp, scol, stdout);

    return 0;
}
#endif
