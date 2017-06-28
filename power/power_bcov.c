#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _br(int e, char* l)
{
	printf("%s\n", l);
	return e;
}

int power(int base, int exponent)
{
	int r = 1 ;
	int i ;

	if (_br(exponent == 0,"/home/yonhee.yu/llvm-handson/clang/power/power.c:10:2"))
		return 1 ;

	if (_br(exponent == 1,"/home/yonhee.yu/llvm-handson/clang/power/power.c:13:2")) {
		return base ; 
	}

	for (i = 0 ; _br(i < exponent,"/home/yonhee.yu/llvm-handson/clang/power/power.c:17:2") ; i++)
		r = r * base ;

	return r ; 
}

int getNum(char *s) {
	return atoi(s) ;
}


int main(int argc, char ** argv) {
	if (_br(argc == 3,"/home/yonhee.yu/llvm-handson/clang/power/power.c:29:2")) {
		int b, e ;
		char * t ;
		b = getNum(argv[1]) ;
		e = getNum(t = strdup(argv[2])) ;
		//free(t) ;

		if (_br(b > 0,"/home/yonhee.yu/llvm-handson/clang/power/power.c:36:3")) {
			if (_br(e > 0,"/home/yonhee.yu/llvm-handson/clang/power/power.c:37:4")) {
				printf("%d\n", power(b, e)) ;
			}
		}

	}
	return 1 ;
}
