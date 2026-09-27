/* hello.c - chuong trinh dung de quan sat 4 giai doan bien dich (muc 2.1) */
#include <stdio.h>

#include "mathlib.h"

#define GREETING "Xin chao tu Embedded Linux"

int main(void)
{
	int a = 7, b = 5;

	printf("%s\n", GREETING);
	printf("cong(%d, %d)   = %d\n", a, b, cong(a, b));
	printf("nhan(%d, %d)   = %d\n", a, b, nhan(a, b));
	printf("thu vien       = %s\n", mathlib_version());
	return 0;
}
