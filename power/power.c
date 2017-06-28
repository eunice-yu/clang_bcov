#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int power(int base, int exponent)
{
	int r = 1 ;
	int i ;

	if (exponent == 0)
		return 1 ;

	if (exponent == 1) {
		return base ; 
	}

	for (i = 0 ; i < exponent ; i++)
		r = r * base ;

	return r ; 
}

int getNum(char *s) {
	return atoi(s) ;
}


int main(int argc, char ** argv) {
	if (argc == 3) {
		int b, e ;
		char * t ;
		b = getNum(argv[1]) ;
		e = getNum(t = strdup(argv[2])) ;
		//free(t) ;

		if (b > 0) {
			if (e > 0) {
				printf("%d\n", power(b, e)) ;
			}
		}

	}
	return 1 ;
}
