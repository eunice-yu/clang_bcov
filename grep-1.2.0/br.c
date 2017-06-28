#include "stdio.h"

int _br(int cond, int addr, int line, int col)
{
	FILE *fp;
	fp =fopen("trace", "a");
	fprintf(fp,"%x\t%d\t%d\t%d\n", addr, line, col, cond);
	fclose(fp);	

    return cond;
}
