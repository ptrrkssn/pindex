#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "creole.h"


int
creole_parse(FILE *in,
	     FILE *out)
{
    char buf[1024];
    char *last, *cp;
    int rc = -1;
    int line = 0;
    int lvl, len;
    int np = 0;
    int li_lvl = 0;
    int li_type = 0;
    

    while (fgets(buf, sizeof(buf), in) != NULL)
    {
	++line;

	last = buf+strlen(buf)-1;
	while (last > buf && isspace(*last))
	    --last;

	lvl = 0;
	switch (*buf)
	{
	  case '\n':
	    if (np < 0)
		np = 1;
	    break;
	    
	  case '=':
	    while (li_lvl > 0)
	    {
		if (li_type == '*')
		    fputs("</ul>\n", out);
		else
		    fputs("</ol>\n", out);
		--li_lvl;
	    }
	    
	    np = 0;
	    cp = buf;
	    while (*cp == *buf)
		++cp;
	    lvl = cp-buf;
	    while (isspace(*cp))
		++cp;
	    while (last > cp && *last == *buf)
		--last;
	    while (last > cp && isspace(*last))
		--last;
	    len = last - cp + 1;

	    fprintf(out, "<h%d>%.*s</h%d>\n", lvl, len, cp, lvl);
	    break;

	  case '*':
	  case '#':
	    li_type = *buf;
	    
	    np = 0;
	    cp = buf;
	    while (*cp == *buf)
		++cp;
	    lvl = cp-buf;
	    
	    if (li_lvl != lvl)
	    {
		if (li_lvl < lvl)
		{
		    while (li_lvl < lvl)
		    {
			if (li_type == '*')
			    fputs("<ul>\n", out);
			else
			    fputs("<ol>\n", out);
			++li_lvl;
		    }
		}
		else if (li_lvl > lvl)
		{
		    while (li_lvl > lvl)
		    {
			if (li_type == '*')
			    fputs("</ul>\n", out);
			else
			    fputs("</ol>\n", out);
			--li_lvl;
		    }
		}
	    }
	    while (isspace(*cp))
		++cp;
	    while (last > cp && isspace(*last))
		--last;
	    len = last - cp + 1;

	    fprintf(out, "<li>%.*s</li>\n", len, cp);
	    break;
	    
	  default:
	    while (li_lvl > 0)
	    {
		if (li_type == '*')
		    fputs("</ul>\n", out);
		else
		    fputs("</ol>\n", out);
		--li_lvl;
	    }
	    
	    if (np)
	    {
		fputs("<p>\n", out);
		np = 0;
	    }
	    fputs(buf, out);
	    np = -1;
	}
    }

    while (li_lvl > 0)
    {
	if (li_type == '*')
	    fputs("</ul>\n", out);
	else
	    fputs("</ol>\n", out);
	--li_lvl;
    }
	    
    return rc;
}


#ifdef DEBUG
int
main(int argc,
     char *argv[])
{
    creole_parse(stdin, stdout);
}
#endif
