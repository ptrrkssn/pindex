/*
** csv.h
*/

#ifndef PTMS_CSV_H
#define PTMS_CSV_H

typedef struct
{
    FILE *fp;
    int st;
} CSV;


extern CSV *
csv_open(const char *path,
	 const char *mode);

extern int
csv_close(CSV *cp);

extern int
csv_puteol(CSV *cp);

     
extern int
csv_puts(const char *s,
	 CSV *cp);

extern int
csv_putu(unsigned int v,
	 CSV *cp);

extern int
csv_skip(CSV *cp);

extern int
csv_skipeol(CSV *cp);

extern int
csv_gets(char *buf,
	 int bufsize,
	 CSV *cp);

extern int
csv_getsdup(char **sp,
	    CSV *cp);

extern int
csv_geti(int *ip,
	 CSV *cp);

#endif
