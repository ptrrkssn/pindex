/*
** table.h
*/

#ifndef PTMS_TABLE_H
#define PTMS_TABLE_H

typedef struct
{
    char **head;
    
    int rows;
    int maxrows;
    char ***cell;
} TABLE;


extern TABLE *
table_create(void);

extern int
table_load(TABLE *tp,
	   const char *path,
	   int header);

extern int
table_sort(TABLE *tp,
	   int col,
	   int type);

extern int
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
		 int skip_header);

extern int
table_date_filter(TABLE *tblp,
		  int date_field,
		  int date_range);

extern void
table_free(TABLE *tp);

#endif
