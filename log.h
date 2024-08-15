#pragma once

void set_logfile(const char path[]);

void log_call(int line, const char *file, const char *func);
void log_error(int line, const char *file, const char *func);
void log_throw(int line, const char *file, const char *func);

#define LOG(fun, expr) (expr) ? (fun(__LINE__, __FILE__, __func__), 1) : 0

#define LOG_CALL(expr) LOG(log_call, expr)
#define LOG_ERROR(expr) LOG(log_error, expr)
#define LOG_THROW(expr) LOG(log_throw, expr)
