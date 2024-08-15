#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core.h"
#include "log.h"
#include "stream.h"

typedef struct stacktrace_entry {
	int line;
	const char *file;
	const char *func;
} stacktrace_entry_t;

typedef struct error {
	char *message;
	int err;
	vector_t callstack;
} error_t;

#define DATETIME_SIZE 20

typedef struct error_chain {
	char datetime[DATETIME_SIZE];
	vector_t errors;
} error_chain_t;

#define MAX_LOG_SIZE 10

typedef struct log {
	bool taken;
	pthread_t tid;

	bool handling;
	size_t len;
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
	for (size_t i = 0; i < MAX_LOG_SIZE; i++)
		create_vector(&log->chains[i].errors, sizeof(error_t));
}

void logger_remove_thread(void)
{
	log_t *log = pthread_getspecific(logger_key);

	for (size_t i = 0; i < MAX_LOG_SIZE; i++)
		destroy_vector(&log->chains[i].errors);

	pthread_mutex_lock(&logger.logs_mutex);

	log->taken = false;
	logger.free_cnt++;
	pthread_cond_signal(&logger.free_cond);

	pthread_mutex_unlock(&logger.logs_mutex);

	pthread_setspecific(logger_key, NULL);
}

void print_error(FILE *file, error_t *error)
{
	for (size_t i = 0; i < error->callstack.len; i++) {
		stacktrace_entry_t *entry =
			vector_get_item(&error->callstack, i);
		fprintf(file, "%s:%d: %s\n", entry->file, entry->line,
			entry->func);
	}
	fprintf(file, "(errno: %d): %s\n\n", error->err, error->message);
}

void destroy_error_chain(error_chain_t *chain)
{
	free(chain->datetime);
	for (size_t i = 0; i < chain->errors.len; i++)
		free(((error_t *)vector_get_item(&chain->errors, i))->message);
	destroy_vector(&chain->errors);
}

void print_error_chain(FILE *file, error_chain_t *chain)
{
	fprintf(stderr, "Exception was thrown at %s in thread %lu:\n",
		chain->datetime, pthread_self());
	print_error(file, vector_get_item(&chain->errors, 0));

	for (size_t i = 1; i < chain->errors.len; i++) {
		fprintf(file, "During the handling of the above exception "
			      "another exception occurred:\n");
		print_error(file, vector_get_item(&chain->errors, i));
	}
}

void log_dump(void)
{
	log_t *log = pthread_getspecific(logger_key);

	pthread_mutex_lock(&logger.file_mutex);
	FILE *logfile = logger.filepath == NULL ? stderr :
						  fopen(logger.filepath, "a");

	if (logfile == NULL) {
		pthread_mutex_unlock(&logger.file_mutex);
		PERROR("fopen");
		return;
	}

	for (size_t i = 0; i < log->len; i++)
		print_error_chain(logfile, log->chains + i);

	pthread_mutex_unlock(&logger.file_mutex);

	for (size_t i = 0; i < log->len; i++)
		destroy_error_chain(log->chains + i);

	log->len = 0;
	fclose(logfile);
}

void log_call(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);
	error_chain_t *chain = log->chains + log->len - 1;
	error_t *error = vector_get_item(&chain->errors, chain->errors.len - 1);
	stacktrace_entry_t *entry = vector_add_item(&error->callstack);
	*entry = (stacktrace_entry_t){ line, file, func };
}

void log_error(int line, const char *file, const char *func)
{
	const int err = errno;

	log_t *log = pthread_getspecific(logger_key);
	if (!log->handling) {
		if (log->len == MAX_LOG_SIZE)
			log_dump();

		log->len++;

		time_t t = time(NULL);
		struct tm *now = localtime(&t);
		snprintf(log->chains[log->len - 1].datetime, DATETIME_SIZE,
			 "%04d-%02d-%02d %02d:%02d:%02d", now->tm_year + 1900,
			 now->tm_mon + 1, now->tm_mday, now->tm_hour,
			 now->tm_min, now->tm_sec);
	}

	error_t *error = vector_add_item(&log->chains[log->len - 1].errors);
	*error = (error_t){ .message = strdup(strerror(errno)),
			    .err = err,
			    .callstack = { 0 } };
	create_vector(&error->callstack, sizeof(stacktrace_entry_t));

	log_call(line, file, func);
}

void log_ignore(int line, const char *file, const char *func)
{
	log_call(line, file, func);
}

void log_throw(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);
	log_call(line, file, func);

	pthread_mutex_lock(&logger.file_mutex);
	print_error_chain(stderr, log->chains + log->len - 1);
	fprintf(stderr, "Thread %ld exited due to the above throw\n\n\n",
		pthread_self());
	pthread_mutex_unlock(&logger.file_mutex);

	logger_remove_thread();
	pthread_exit(NULL);
}

void log_handle(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);
	log_call(line, file, func);
	log->handling = true;
}

void log_consume(int line, const char *file, const char *func)
{
	log_t *log = pthread_getspecific(logger_key);
	destroy_error_chain(&log->chains[log->len - 1]);
	log->len--;

	log->handling = false;
}
