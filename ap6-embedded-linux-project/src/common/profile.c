#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "profile.h"
#include "util.h"

/* Cac nhom tham so theo Bao cao muc 16.3:
 *  - vung quoc gia / kenh tan so
 *  - nhan dang mang va che do Wi-Fi
 *  - hieu nang va QoS
 *  - bao mat va chong flooding
 */
struct key_def {
	const char *key;
	const char *def;
	int is_int;
	long min;
	long max;
	const char *enum_vals;	/* danh sach gia tri hop le, phan cach bang '|' */
};

static const struct key_def k_defs[] = {
	/* key                       default        int  min   max   enum */
	{ "CountryRegion",           "5",            1,   0,   11,   NULL },
	{ "CountryRegionABand",      "7",            1,   0,   16,   NULL },
	{ "CountryCode",             "VN",           0,   0,    0,   NULL },
	{ "Channel",                 "6",            1,   0,  196,   NULL },
	{ "AutoChannelSelect",       "0",            1,   0,    2,   NULL },
	{ "SSID",                    "Viettel_AP6",  0,   0,    0,   NULL },
	{ "BssidNum",                "1",            1,   1,    8,   NULL },
	{ "WirelessMode",            "9",            1,   0,   16,   NULL },
	{ "HideSSID",                "0",            1,   0,    1,   NULL },
	{ "BeaconPeriod",            "100",          1,  20, 1000,   NULL },
	{ "DtimPeriod",              "1",            1,   1,  255,   NULL },
	{ "TxPower",                 "100",          1,   1,  100,   NULL },
	{ "TxBurst",                 "1",            1,   0,    1,   NULL },
	{ "WmmCapable",              "1",            1,   0,    1,   NULL },
	{ "NoForwarding",            "0",            1,   0,    1,   NULL },
	{ "NoForwardingBTNBSSID",    "0",            1,   0,    1,   NULL },
	{ "AuthMode",                "WPA2PSK",      0,   0,    0,
	  "OPEN|WPAPSK|WPA2PSK|WPA3PSK|WPA1PSKWPA2PSK" },
	{ "EncrypType",              "AES",          0,   0,    0,
	  "NONE|TKIP|AES|TKIPAES" },
	{ "WPAPSK",                  "12345678",     0,   0,    0,   NULL },
	{ "PMFMFPC",                 "1",            1,   0,    1,   NULL },
	{ "AuthFloodThreshold",      "32",           1,   0,  255,   NULL },
	{ "AssocReqFloodThreshold",  "32",           1,   0,  255,   NULL },
	{ "DeauthFloodThreshold",    "32",           1,   0,  255,   NULL },
};

static const size_t k_ndefs = sizeof(k_defs) / sizeof(k_defs[0]);

static const struct key_def *find_def(const char *key)
{
	size_t i;

	for (i = 0; i < k_ndefs; i++) {
		if (strcasecmp(k_defs[i].key, key) == 0)
			return &k_defs[i];
	}
	return NULL;
}

static int enum_contains(const char *list, const char *val)
{
	const char *p = list;
	size_t vlen = strlen(val);

	while (*p) {
		const char *sep = strchr(p, '|');
		size_t len = sep ? (size_t)(sep - p) : strlen(p);

		if (len == vlen && strncasecmp(p, val, len) == 0)
			return 1;
		if (!sep)
			break;
		p = sep + 1;
	}
	return 0;
}

static char *trim(char *s)
{
	char *end;

	while (*s && isspace((unsigned char)*s))
		s++;
	if (!*s)
		return s;
	end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		*end-- = '\0';
	return s;
}

void profile_defaults(struct profile *p)
{
	size_t i;

	memset(p, 0, sizeof(*p));
	for (i = 0; i < k_ndefs && i < PROFILE_MAX_KEYS; i++) {
		snprintf(p->ent[i].key, PROFILE_KEY_LEN, "%s", k_defs[i].key);
		snprintf(p->ent[i].val, PROFILE_VAL_LEN, "%s", k_defs[i].def);
	}
	p->n = i;
	p->dirty = 0;
}

int profile_validate(const char *key, const char *val, char *err, size_t errlen)
{
	const struct key_def *d = find_def(key);

	if (!d) {
		snprintf(err, errlen, "khong ho tro tham so '%s'", key);
		return -1;
	}
	if (val[0] == '\0') {
		snprintf(err, errlen, "gia tri rong cho '%s'", d->key);
		return -1;
	}
	if (strlen(val) >= PROFILE_VAL_LEN) {
		snprintf(err, errlen, "gia tri qua dai (toi da %d ky tu)",
			 PROFILE_VAL_LEN - 1);
		return -1;
	}

	if (d->is_int) {
		char *endp;
		long v;

		errno = 0;
		v = strtol(val, &endp, 10);
		if (errno != 0 || *endp != '\0') {
			snprintf(err, errlen, "'%s' phai la so nguyen", d->key);
			return -1;
		}
		if (v < d->min || v > d->max) {
			snprintf(err, errlen, "'%s' phai trong [%ld..%ld]",
				 d->key, d->min, d->max);
			return -1;
		}
		return 0;
	}

	if (d->enum_vals && !enum_contains(d->enum_vals, val)) {
		snprintf(err, errlen, "'%s' phai thuoc {%s}", d->key,
			 d->enum_vals);
		return -1;
	}

	/* Rang buoc rieng cho cac truong dac biet. */
	if (strcasecmp(d->key, "SSID") == 0) {
		if (strlen(val) > 32) {
			snprintf(err, errlen, "SSID toi da 32 ky tu (802.11)");
			return -1;
		}
	} else if (strcasecmp(d->key, "CountryCode") == 0) {
		if (strlen(val) != 2 || !isalpha((unsigned char)val[0]) ||
		    !isalpha((unsigned char)val[1])) {
			snprintf(err, errlen,
				 "CountryCode phai gom 2 chu cai, vi du VN");
			return -1;
		}
	} else if (strcasecmp(d->key, "WPAPSK") == 0) {
		size_t len = strlen(val);

		if (len < 8 || len > 63) {
			snprintf(err, errlen,
				 "WPAPSK phai dai 8..63 ky tu (WPA passphrase)");
			return -1;
		}
	}
	return 0;
}

const char *profile_get(const struct profile *p, const char *key)
{
	size_t i;

	for (i = 0; i < p->n; i++) {
		if (strcasecmp(p->ent[i].key, key) == 0)
			return p->ent[i].val;
	}
	return NULL;
}

int profile_set(struct profile *p, const char *key, const char *val,
		char *err, size_t errlen)
{
	const struct key_def *d;
	size_t i;

	if (profile_validate(key, val, err, errlen) < 0)
		return -1;

	d = find_def(key);
	for (i = 0; i < p->n; i++) {
		if (strcasecmp(p->ent[i].key, d->key) == 0) {
			if (strcmp(p->ent[i].val, val) != 0) {
				snprintf(p->ent[i].val, PROFILE_VAL_LEN, "%s",
					 val);
				p->dirty = 1;
			}
			return 0;
		}
	}

	if (p->n >= PROFILE_MAX_KEYS) {
		snprintf(err, errlen, "profile day (%d tham so)",
			 PROFILE_MAX_KEYS);
		return -1;
	}
	snprintf(p->ent[p->n].key, PROFILE_KEY_LEN, "%s", d->key);
	snprintf(p->ent[p->n].val, PROFILE_VAL_LEN, "%s", val);
	p->n++;
	p->dirty = 1;
	return 0;
}

int profile_load(struct profile *p, const char *path)
{
	FILE *f = fopen(path, "re");
	char line[256];
	int lineno = 0;

	if (!f)
		return -1;

	profile_defaults(p);
	while (fgets(line, sizeof(line), f)) {
		char err[128];
		char *eq, *key, *val;

		lineno++;
		/* Bo comment kieu '#' va dong trong (giong file .dat). */
		eq = strchr(line, '#');
		if (eq)
			*eq = '\0';
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		key = trim(line);
		val = trim(eq + 1);
		if (key[0] == '\0')
			continue;
		if (profile_set(p, key, val, err, sizeof(err)) < 0)
			fprintf(stderr, "profile %s:%d: bo qua (%s)\n",
				path, lineno, err);
	}
	fclose(f);
	p->dirty = 0;
	return 0;
}

size_t profile_dump(const struct profile *p, char *buf, size_t buflen)
{
	size_t off = 0;
	size_t i;

	for (i = 0; i < p->n && off < buflen; i++) {
		int n = snprintf(buf + off, buflen - off, "%s=%s\n",
				 p->ent[i].key, p->ent[i].val);
		if (n < 0)
			break;
		if ((size_t)n >= buflen - off) {
			off = buflen - 1;
			break;
		}
		off += (size_t)n;
	}
	return off;
}

int profile_save(struct profile *p, const char *path)
{
	char buf[PROFILE_MAX_KEYS * (PROFILE_KEY_LEN + PROFILE_VAL_LEN + 2)];
	size_t len;

	len = profile_dump(p, buf, sizeof(buf));
	if (write_file_atomic(path, buf, len) < 0)
		return -1;
	p->dirty = 0;
	return 0;
}
