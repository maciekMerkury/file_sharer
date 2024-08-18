#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"
#include "stream.h"

typedef struct stacktrace_entry {
	int line;
	const char *file;
	const char *func;
} stacktrace_entry_t;

typedef struct error {
	char message[128];
	int err;
	vector_t callstack;
} error_t;

#define DATETIME_SIZE 20

typedef struct error_chain {
	char datetime[DATETIME_SIZE];
	vector_t errors;
	stacktrace_entry_t last;
} error_chain_t;

#define MAX_LOG_SIZE 10

typedef struct log {
	bool taken;
	pthread_t tid;

	vector_t callstack;

	size_t curr;
	error_chain_t chains[MAX_LOG_SIZE];
} log_t;

#define MAX_THREAD_COUNT 10

pthread_key_t logger_key;

typedef struct logger {
	char *filepath;
	size_t free_cnt;
	log_t logs[MAX_THREAD_COUNT];
	pthread_mutex_t logs_mutex;
	pthread_cond_t free_cond;
	pthread_mutex_t file_mutex;
} logger_t;

static logger_t logger;

void logger_init(const char *path)
{
	pthread_key_create(&logger_key, NULL);
	pthread_mutex_init(&logger.logs_mutex, NULL);
	pthread_cond_init(&logger.free_cond, NULL);
	pthread_mutex_init(&logger.file_mutex, NULL);
	logger.free_cnt = MAX_THREAD_COUNT;
	if (path != NULL)
		logger.filepath = strdup(path);
}

void logger_deinit(void)
{
	free(logger.filepath);
	pthread_mutex_destroy(&logger.logs_mutex);
	pthread_cond_destroy(&logger.free_cond);
	pthread_mutex_destroy(&logger.file_mutex);
	pthread_key_delete(logger_key);
}

void logger_add_thread(void)
{
	pthread_mutex_lock(&logger.logs_mutex);
	while (logger.free_cnt == 0)
		pthread_cond_wait(&logger.free_cond, &logger.logs_mutex);

	log_t *log = NULL;
	for (size_t i = 0; i < MAX_THREAD_COUNT; i++)
		if (!logger.logs[i].taken) {
			log = logger.logs + i;
			log->taken = true;
			logger.free_cnt--;
			break;
		}

	pthread_mutex_unlock(&logger.logs_mutex);

	pthread_setspecific(logger_key, log);

	log->tid = pthread_self();
	log->curr = 0;
	create_vector(&log->callstack, sizeof(stacktrace_entry_t));
	for (size_t i = 0; i < MAX_LOG_SIZE; i++)
		create_vector(&log->chains[i].errors, sizeof(error_t));
}

void destroy_error_chain(error_chain_t *chain)
{
	error_t *errors = chain->errors.data;
	for (size_t i = 0; i < chain->errors.len; i++)
		destroy_vector(&errors[i].callstack);
	destroy_vector(&chain->errors);
}

void print_error(FILE *file, const error_t *error)
{
	const stacktrace_entry_t *entries = error->callstack.data;
	for (size_t i = 0; i < error->callstack.len; i++) {
		const stacktrace_entry_t *entry = entries + i;
		fprintf(file, "%s:%d: %s\n", entry->file, entry->line,
			entry->func);
	}
	fprintf(file, "(code: %d): %s\n\n", error->err, error->message);
}

void print_error_chain(FILE *file, error_chain_t *chain)
{
	fprintf(file, "Exception was raised at %s in thread %lu:\n",
		chain->datetime, pthread_self());
	const error_t *errors = chain->errors.data;
	print_error(file, errors);

	for (size_t i = 1; i < chain->errors.len; i++) {
		fprintf(file, "During the handling of the above exception "
			      "another exception occurred:\n");
		print_error(file, errors + i);
	}

	if (chain->last.file != NULL)
		fprintf(file,
			"The above exception was ignored at %s:%d: %s\n\n\n",
			chain->last.file, chain->last.line, chain->last.func);
}

void log_dump(void)
{
	log_t *log = pthread_getspecific(logger_key);

	pthread_mutex_lock(&logger.file_mutex);
	FILE *logfile = logger.filepath == NULL ? stderr :
						  fopen(logger.filepath, "a");

	if (logfile == NULL) {
		fprintf(stderr,
			"could not open logfile `%s`\nLogging to stderr\n",
			logger.filepath);
		logfile = stderr;
	}

	for (size_t i = 0; i <= log->curr; i++)
		print_error_chain(logfile, log->chains + i);

	pthread_mutex_unlock(&logger.file_mutex);

	for (size_t i = 0; i <= log->curr; i++) {
		destroy_error_chain(log->chains + i);
		create_vector(&log->chains[i].errors, sizeof(error_t));
	}

	log->curr = 0;

	if (logfile != stderr)
		fclose(logfile);
}

void logger_remove_thread(bool dump)
{
	log_t *log = pthread_getspecific(logger_key);

	if (dump && log->curr > 0) {
		log->curr--;
		log_dump();
	}

	destroy_vector(&log->callstack);
	for (size_t i = 0; i <= log->curr; i++)
		destroy_error_chain(log->chains + i);

	pthread_mutex_lock(&logger.logs_mutex);

	log->taken = false;
	logger.free_cnt++;
	pthread_cond_signal(&logger.free_cond);

	pthread_mutex_unlock(&logger.logs_mutex);

	pthread_setspecific(logger_key, NULL);
}

void log_call(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);
	stacktrace_entry_t *entry = vector_add_item(&log->callstack);
	*entry = (stacktrace_entry_t){ line, file, func };
}

void log_pop(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);
	log->callstack.len--;
}

void log_perror(int line, const char *file, const char *func,
		const char *source)
{
	log_call(line, file, func);

	log_t *log = pthread_getspecific(logger_key);
	error_t *error = vector_add_item(&log->chains[log->curr].errors);
	*error = (error_t){ .err = errno };

	const size_t source_len = strlen(source);
	memcpy(error->message, source, source_len);
	error->message[source_len] = ':';
	error->message[source_len + 1] = ' ';
	strerror_r(errno, error->message + source_len + 2,
		   sizeof(error->message) - source_len - 2);

	vector_copy(&error->callstack, &log->callstack);

	log_pop(line, file, func);
}

void log_error(int line, const char *file, const char *func, int code,
	       const char *format, ...)
{
	log_call(line, file, func);

	log_t *log = pthread_getspecific(logger_key);

	error_t *error = vector_add_item(&log->chains[log->curr].errors);
	*error = (error_t){ .err = code };

	va_list args;
	va_start(args, format);
	vsnprintf(error->message, sizeof(error->message), format, args);
	va_end(args);

	vector_copy(&error->callstack, &log->callstack);

	log_pop(line, file, func);
}

bool has_active_error(log_t *log)
{
	return log->chains[log->curr].errors.len > 0;
}

void log_ignore(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);

	// there is no error to ignore
	// all raised errors have been ignored or consumed
	assert(has_active_error(log));

	log->chains[log->curr].last = (stacktrace_entry_t){ line, file, func };
	time_t t = time(NULL);
	struct tm *now = localtime(&t);
	snprintf(log->chains[log->curr].datetime, DATETIME_SIZE,
		 "%04d-%02d-%02d %02d:%02d:%02d", now->tm_year + 1900,
		 now->tm_mon + 1, now->tm_mday, now->tm_hour, now->tm_min,
		 now->tm_sec);

	if (log->curr + 1 == MAX_LOG_SIZE)
		log_dump();
	else
		log->curr++;
}

void log_throw(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);

	// there is no error to throw
	// all raised errors have been ignored or consumed
	assert(has_active_error(log));

	pthread_mutex_lock(&logger.file_mutex);
	print_error_chain(stderr, log->chains + log->curr);
	fprintf(stderr, "Thread %ld exited due to the above throw\n\n\n",
		pthread_self());
	pthread_mutex_unlock(&logger.file_mutex);

	logger_remove_thread(0);
	pthread_exit(NULL);
}

void log_consume(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);

	// there is no error to consume
	// all raised errors have been ignored or consumed
	assert(has_active_error(log));

	vector_t *errors = &log->chains[log->curr].errors;

	error_t *errs = errors->data;
	for (size_t i = 0; i < errors->len; i++)
		destroy_vector(&errs[i].callstack);

	log->chains[log->curr].errors.len = 0;
}
