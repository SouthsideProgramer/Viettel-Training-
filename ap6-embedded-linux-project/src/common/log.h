/* log.h - logging toi thieu cho daemon nhung (Bao cao muc 7.5, 10.5) */
#ifndef AP6_LOG_H
#define AP6_LOG_H

enum log_level { LOG_L_ERR = 0, LOG_L_WARN, LOG_L_INFO, LOG_L_DEBUG };

/* use_syslog != 0: ghi ra syslog (daemon khong con terminal) */
void log_init(const char *ident, enum log_level level, int use_syslog);
void log_set_level(enum log_level level);
void log_msg(enum log_level level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void log_close(void);

#define log_err(...)   log_msg(LOG_L_ERR, __VA_ARGS__)
#define log_warn(...)  log_msg(LOG_L_WARN, __VA_ARGS__)
#define log_info(...)  log_msg(LOG_L_INFO, __VA_ARGS__)
#define log_dbg(...)   log_msg(LOG_L_DEBUG, __VA_ARGS__)

#endif /* AP6_LOG_H */
