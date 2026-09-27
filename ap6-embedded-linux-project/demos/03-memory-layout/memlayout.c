/*
 * memlayout.c - in dia chi cac vung nho cua process (Bao cao muc 3.1).
 *
 * Chuong trinh khong doc RAM vat ly ma in dia chi AO. Chay hai lan se thay
 * dia chi thay doi neu he thong bat ASLR - bang chung cho thay day la khong
 * gian dia chi ao rieng cua tung process.
 *
 * Bien dich: gcc -Wall -o memlayout memlayout.c
 * Doi chieu:  cat /proc/<pid>/maps   hoac   pmap <pid>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;		/* bang bien moi truong, nam gan dinh stack */

/* .data: bien toan cuc DA khoi tao */
int  g_data_init = 42;
char g_data_str[16] = "data";

/* .bss: bien toan cuc CHUA khoi tao (khong chiem cho trong file ELF) */
int  g_bss_uninit;
char g_bss_buf[4096];

/* .rodata: hang so chi doc */
const char *const g_rodata = "chuoi trong vung chi doc";

static int s_static_init = 7;	/* static co khoi tao -> .data */
static int s_static_bss;	/* static chua khoi tao -> .bss */

static void ham_vi_du(void) { }

static void in_maps(void)
{
	char line[256];
	FILE *f = fopen("/proc/self/maps", "re");

	if (!f)
		return;
	printf("\n--- /proc/self/maps (cac vung chinh) ---\n");
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "[heap]") || strstr(line, "[stack]") ||
		    strstr(line, "memlayout") || strstr(line, "libc"))
			fputs(line, stdout);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	int stack_var = 1;
	int *heap_small = malloc(64);
	/* malloc lon: glibc dung mmap thay vi brk -> nam o vung mmap,
	 * khong lien tuc voi heap (Bao cao muc 3.1). */
	int *heap_large = malloc(4 * 1024 * 1024);
	char *env0 = environ[0];

	printf("PID = %ld\n", (long)getpid());
	printf("Dia chi tang dan tu duoi len (text -> data -> heap -> stack)\n\n");

	printf("%-34s %p\n", "text  : ham main()", (void *)main);
	printf("%-34s %p\n", "text  : ham ham_vi_du()", (void *)ham_vi_du);
	printf("%-34s %p\n", "rodata: chuoi hang", (void *)g_rodata);
	printf("%-34s %p (= %d)\n", "data  : g_data_init",
	       (void *)&g_data_init, g_data_init);
	printf("%-34s %p\n", "data  : s_static_init", (void *)&s_static_init);
	printf("%-34s %p (= %d)\n", "bss   : g_bss_uninit",
	       (void *)&g_bss_uninit, g_bss_uninit);
	printf("%-34s %p\n", "bss   : s_static_bss", (void *)&s_static_bss);
	printf("%-34s %p\n", "bss   : g_bss_buf[4096]", (void *)g_bss_buf);
	printf("%-34s %p\n", "heap  : malloc(64)", (void *)heap_small);
	printf("%-34s %p\n", "mmap  : malloc(4MB)", (void *)heap_large);
	printf("%-34s %p\n", "stack : bien cuc bo", (void *)&stack_var);
	printf("%-34s %p\n", "stack : tham so argv", (void *)argv);
	printf("%-34s %p\n", "stack : bien moi truong", (void *)env0);
	(void)argc;

	printf("\nNhan xet:\n");
	printf("  - g_bss_uninit = %d: kernel dam bao BSS bang 0 khi nap.\n",
	       g_bss_uninit);
	printf("  - malloc 4MB nam xa heap vi glibc chuyen sang mmap.\n");
	printf("  - Ghi vao g_rodata se gay SIGSEGV (vung chi doc).\n");

	in_maps();

	/* Giai phong day du: tren thiet bi chay lien tuc nhieu thang, mot
	 * memory leak nho cung tich luy dan (Bao cao muc 3.1, 6.5). */
	free(heap_small);
	free(heap_large);
	return 0;
}
