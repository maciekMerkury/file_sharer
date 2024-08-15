#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define LOG_SIZE 10

typedef struct log {
	char *filepath;
	size_t len;
	error_t errors[LOG_SIZE];
} log_t;

static log_t log;

void set_logfile(const char *path)
{
	log.filepath = strdup(path);
}

void write_error(FILE *file, error_t *error)
{
	for (size_t i = 0; i < error->callstack.len; i++) {
		stacktrace_entry_t *entry =
			vector_get_item(&error->callstack, i);
		printf("%s:%d: %s\n", entry->file, entry->line, entry->func);
	}
	fprintf(file, "(errno: %d): %s\n", error->err, error->message);
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
		error_t *trace = log.errors + i;
		write_error(logfile, trace);
		free(trace->message);
	}

	log.len = 0;
	fclose(logfile);
}

void log_call(int line, const char *file, const char *func)
{
	stacktrace_entry_t *entry =
		vector_add_item(&log.errors[log.len - 1].callstack);
	*entry = (stacktrace_entry_t){ line, file, func };
}

void log_error(int line, const char *file, const char *func)
{
	const int err = errno;

	if (log.len == LOG_SIZE)
		log_dump();

	log.errors[log.len] = (error_t){ .message = strdup(strerror(errno)),
					 .err = err,
					 .callstack = { 0 } };
	create_vector(&log.errors[log.len].callstack,
		      sizeof(stacktrace_entry_t));
	log.len++;

	log_call(line, file, func);
}

void log_throw(int line, const char *file, const char *func)
{
	log_call(line, file, func);
	printf("Exception was thrown:\n");
	write_error(stderr, log.errors + log.len - 1);
	exit(EXIT_FAILURE);
}
