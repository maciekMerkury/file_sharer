#pragma once

void log_init(const char path[]);

void log_call(int line, const char *file, const char *func);
void log_error(int line, const char *file, const char *func);
void log_ignore(int line, const char *file, const char *func);
void log_throw(int line, const char *file, const char *func);
void log_handle(int line, const char *file, const char *func);
void log_consume(int line, const char *file, const char *func);

#define LOG(fun, expr) (expr) ? (fun(__LINE__, __FILE__, __func__), 1) : 0

#define LOG_CALL(expr) LOG(log_call, expr)
#define LOG_ERROR(expr) LOG(log_error, expr)
#define LOG_IGNORE(expr) LOG(log_ignore, expr)
#define LOG_THROW(expr) LOG(log_throw, expr)
#define LOG_HANDLE(expr) LOG(log_handle, expr)
#define LOG_CONSUME(expr) LOG(log_consume, expr)
