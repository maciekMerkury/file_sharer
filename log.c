#include <errno.h>
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

#define DATETIME_LEN 20

typedef struct error_chain {
	char datetime[DATETIME_LEN];
	vector_t errors;
} error_chain_t;

#define MAX_LOG_SIZE 10

typedef struct log {
	char *filepath;
	bool handling;
	size_t len;
	error_chain_t chains[MAX_LOG_SIZE];
} log_t;

static log_t log;

void log_init(const char *path)
{
	if (path != NULL)
		log.filepath = strdup(path);
	for (size_t i = 0; i < MAX_LOG_SIZE; i++)
		create_vector(&log.chains[i].errors, sizeof(error_t));
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
	fprintf(stderr, "Exception was thrown at %s:\n", chain->datetime);
	print_error(file, vector_get_item(&chain->errors, 0));

	for (size_t i = 1; i < chain->errors.len; i++) {
		fprintf(file, "During the handling of the above exception "
			      "another exception occurred:\n");
		print_error(file, vector_get_item(&chain->errors, i));
	}
}

void log_dump(void)
{
	FILE *logfile;
	if (log.filepath != NULL) {
		logfile = fopen(log.filepath, "a");
		if (logfile == NULL) {
			PERROR("fopen");
			return;
		}
	} else
		logfile = stderr;

	for (size_t i = 0; i < log.len; i++) {
		print_error_chain(logfile, log.chains + i);
		destroy_error_chain(log.chains + i);
	}

	log.len = 0;
	fclose(logfile);
}

void log_call(int line, const char *file, const char *func)
{
	error_chain_t *chain = log.chains + log.len - 1;
	error_t *error = vector_get_item(&chain->errors, chain->errors.len - 1);
	stacktrace_entry_t *entry = vector_add_item(&error->callstack);
	*entry = (stacktrace_entry_t){ line, file, func };
}

void log_error(int line, const char *file, const char *func)
{
	const int err = errno;

	if (!log.handling) {
		if (log.len == MAX_LOG_SIZE)
			log_dump();

		log.len++;

		time_t t = time(NULL);
		struct tm *now = localtime(&t);
		snprintf(log.chains[log.len - 1].datetime, DATETIME_LEN,
			 "%04d-%02d-%02d %02d:%02d:%02d", now->tm_year + 1900,
			 now->tm_mon + 1, now->tm_mday, now->tm_hour,
			 now->tm_min, now->tm_sec);
	}

	error_t *error = vector_add_item(&log.chains[log.len - 1].errors);
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
	log_call(line, file, func);
	print_error_chain(stderr, log.chains + log.len - 1);
	abort();
}

void log_handle(int line, const char *file, const char *func)
{
	log_call(line, file, func);
	log.handling = true;
}

void log_consume(int line, const char *file, const char *func)
{
	destroy_error_chain(&log.chains[log.len - 1]);
	log.len--;

	log.handling = false;
}
