#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include "log.h"

static enum log_level g_level = LOG_L_INFO;
static int g_syslog;
static char g_ident[32] = "ap6";

static const char *level_name(enum log_level l)
{
	switch (l) {
	case LOG_L_ERR:   return "ERR ";
	case LOG_L_WARN:  return "WARN";
	case LOG_L_INFO:  return "INFO";
	default:          return "DBG ";
	}
}

static int level_to_syslog(enum log_level l)
{
	switch (l) {
	case LOG_L_ERR:   return LOG_ERR;
	case LOG_L_WARN:  return LOG_WARNING;
	case LOG_L_INFO:  return LOG_INFO;
	default:          return LOG_DEBUG;
	}
}

void log_init(const char *ident, enum log_level level, int use_syslog)
{
	if (ident) {
		strncpy(g_ident, ident, sizeof(g_ident) - 1);
		g_ident[sizeof(g_ident) - 1] = '\0';
	}
	g_level = level;
	g_syslog = use_syslog;
	if (g_syslog)
		openlog(g_ident, LOG_PID, LOG_DAEMON);
}

void log_set_level(enum log_level level)
{
	g_level = level;
}

void log_msg(enum log_level level, const char *fmt, ...)
{
	va_list ap;
	char buf[512];

	if (level > g_level)
		return;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (g_syslog) {
		syslog(level_to_syslog(level), "%s", buf);
	} else {
		char ts[32];
		time_t now = time(NULL);
		struct tm tm;

		localtime_r(&now, &tm);
		strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
		fprintf(stderr, "[%s] %s %s: %s\n", ts, level_name(level),
			g_ident, buf);
		fflush(stderr);
	}
}

void log_close(void)
{
	if (g_syslog)
		closelog();
}
