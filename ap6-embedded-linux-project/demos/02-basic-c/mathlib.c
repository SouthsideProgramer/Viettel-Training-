/* mathlib.c - duoc dong goi thanh ca thu vien tinh (.a) va dong (.so)
 * de so sanh static linking va dynamic linking (muc 2.2). */
#include "mathlib.h"

int cong(int a, int b)
{
	return a + b;
}

int nhan(int a, int b)
{
	return a * b;
}

const char *mathlib_version(void)
{
	return "mathlib 1.0";
}
