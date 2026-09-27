/* profile.h - doc/ghi profile SoftAP kieu MediaTek (Bao cao muc 16.2, 16.3)
 *
 * Profile la file text dang Key=Value, giong file mt7615.dat / RT2860AP.dat
 * cua driver MediaTek. Daemon doc profile luc khoi dong, validate tung tham so
 * roi moi ap dung xuong driver.
 */
#ifndef AP6_PROFILE_H
#define AP6_PROFILE_H

#include <stddef.h>

#define PROFILE_MAX_KEYS	64
#define PROFILE_KEY_LEN		48
#define PROFILE_VAL_LEN		64

struct profile_entry {
	char key[PROFILE_KEY_LEN];
	char val[PROFILE_VAL_LEN];
};

struct profile {
	struct profile_entry ent[PROFILE_MAX_KEYS];
	size_t n;
	int dirty;			/* co thay doi chua SAVE */
};

/* Nap gia tri mac dinh (dung khi chua co file profile tren flash). */
void profile_defaults(struct profile *p);

/* Doc profile tu file. Tra ve 0 neu OK, -1 neu loi (errno duoc giu). */
int profile_load(struct profile *p, const char *path);

/* Ghi profile xuong file theo kieu nguyen tu (tmp + fsync + rename). */
int profile_save(struct profile *p, const char *path);

const char *profile_get(const struct profile *p, const char *key);

/* Dat gia tri sau khi validate.
 * Tra ve 0 neu OK; -1 neu sai, err chua mo ta loi de tra ve cho client. */
int profile_set(struct profile *p, const char *key, const char *val,
		char *err, size_t errlen);

/* Kiem tra mot cap key/value co hop le khong (khong ghi vao profile). */
int profile_validate(const char *key, const char *val, char *err, size_t errlen);

/* Serialize toan bo profile ra buffer dang "Key=Value\n". */
size_t profile_dump(const struct profile *p, char *buf, size_t buflen);

#endif /* AP6_PROFILE_H */
