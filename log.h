#pragma once

#include <stdbool.h>

void logger_init(const char path[]);
void logger_deinit(void);

void logger_add_thread(void);
void logger_remove_thread(bool dump);

void log_error(int line, const char *file, const char *func, int code,
	       const char *format, ...);
void log_perror(int line, const char *file, const char *func,
		const char *source);
void log_pop(int line, const char *file, const char *func);
void log_call(int line, const char *file, const char *func);
void log_ignore(int line, const char *file, const char *func);
void log_throw(int line, const char *file, const char *func);
void log_consume(int line, const char *file, const char *func);

#define LOG(fun, expr) (expr) ? (fun(__LINE__, __FILE__, __func__), 1) : 0

#define LOG_ERRORF(expr, code, format, ...)                            \
	(expr ? (log_error(__LINE__, __FILE__, __func__, code, format, \
			   __VA_ARGS__),                               \
		 1) :                                                  \
		0)

#define LOG_ERROR(expr, code, message)                                         \
	(expr ? (log_error(__LINE__, __FILE__, __func__, code, message, NULL), \
		 1) :                                                          \
		0)

#define LOG_PERROR(expr, source) \
	(expr ? ((log_perror(__LINE__, __FILE__, __func__, source)), 1) : 0)

#define LOG_CALL(expr)                                         \
	(log_call(__LINE__, __FILE__, __func__),               \
	 (expr) ? (log_pop(__LINE__, __FILE__, __func__), 1) : \
		  (log_pop(__LINE__, __FILE__, __func__), 0))

#define LOG_CALLV(expr)                                \
	(log_call(__LINE__, __FILE__, __func__), expr, \
	 log_pop(__LINE__, __FILE__, __func__))

#define LOG_IGNORE(expr) (LOG(log_ignore, expr))
#define LOG_THROW(expr) (LOG(log_throw, expr))
#define LOG_CONSUME(expr) (LOG(log_consume, expr))
