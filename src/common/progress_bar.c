#include <assert.h>
#include <bits/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "core.h"
#include "entry.h"
#include "notification.h"
#include "progress_bar.h"

#define UNIT_LEN 4
static const char size_units[UNIT_LEN][4] = { "B", "KiB", "MiB", "GiB" };

size_info bytes_to_size(size_t byte_size)
{
	double size = byte_size;
	unsigned int idx = 0;

	while (size >= 1024.0) {
		idx++;
		size /= 1024.0;
	}

	return (size_info){
		.size = size,
		.unit_idx = idx,
	};
}

const char *const unit(size_info info)
{
	return (info.unit_idx < UNIT_LEN) ? size_units[info.unit_idx] : NULL;
}

void prog_bar_init(prog_bar_t *bar, prog_bar_type type, stream_t *entries,
		   size_t max_val)
{
	*bar = (prog_bar_t){ .entries = entries,
			     .type = type,
			     .max_val = max_val };
	stream_iter_init(&bar->it, entries);
}

size_t calculate_speed(unsigned dsec, unsigned dnsec, size_t step)
{
	const float ts = dsec + dnsec * 1e-9f;
	return step / ts;
}

void print_bar(const char title[], size_t size, size_t speed, float perc)
{
	assert(perc <= 1);
	assert(perc >= 0);
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	const int max_title_len = w.ws_col / 3;
	const int title_len = strlen(title);

	const char *tl = title_len < max_title_len ?
				 title :
				 title + title_len - max_title_len;

	const size_info info = bytes_to_size(size);
	const size_info speed_info = bytes_to_size(speed);
	const int size_width = 4;
	const int precision = speed_info.size >= 100 ? 0 :
			      speed_info.size >= 10  ? 1 :
						       2;
	char title_format[64];
	snprintf(title_format, 64, "%%-%ds  %%7.2lf %%-%ds  %%%d.%dlf %%%ds/s ",
		 max_title_len, UNIT_LEN - 1, size_width, precision,
		 UNIT_LEN - 1);

	printf(title_format, tl, info.size, unit(info), speed_info.size,
	       unit(speed_info));

	const unsigned time = (size * (1 - perc)) / speed;
	const unsigned time_sec = time % 60;
	const unsigned time_mins = (time / 60) % 60;
	const unsigned time_hrs = time / 360;

	char time_str[8] = ">100h";
	if (time_hrs <= 100) {
		if (time_hrs > 0)
			snprintf(time_str, 6, "%2dh", time_hrs);
		else
			snprintf(time_str, 6, "%02d:%02d", time_mins, time_sec);
	}
	printf("%s ", time_str);

	const int bar_width = w.ws_col - max_title_len - (UNIT_LEN - 1) -
			      size_width - (UNIT_LEN - 1) - 29;

	const int done = bar_width * perc;
	putchar('[');
	for (int i = 0; i < done; i++)
		putchar('#');
	for (int i = 0; i < bar_width - done; i++)
		putchar('-');
	putchar(']');

	printf(" %3.0f%%\n", perc * 100);
}

void show_bar(const char filename[], float perc)
{
	const static int max_title_len = 30;
	const int title_len = strlen(filename);
	const char *tl = title_len < max_title_len ?
				 filename :
				 filename + title_len - max_title_len;

	char title[64];
	snprintf(title, 64, "Receiving: %s", tl);

	transfer_in_progress_notification(title, perc);
}

void prog_bar_next(prog_bar_t *bar)
{
	bar->curr = stream_iter_next(&bar->it);
	assert(bar->curr != NULL);

	bar->val = 0;

	clock_gettime(CLOCK_REALTIME, &bar->ts);

	if (bar->start_ts.tv_sec == 0 && bar->start_ts.tv_nsec == 0)
		bar->start_ts = bar->ts;
}

void prog_bar_advance(prog_bar_t *bar, size_t step)
{
	const static struct timespec min_dt = { 0, 1e8 };

	const entry_t *entry = bar->curr;
	assert(entry->type != et_dir);

	bar->total_val += step;
	bar->val += step;

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	size_t total_speed = 0;
	size_t speed = 0;
	const struct timespec dt = {
		.tv_sec = ts.tv_sec - bar->ts.tv_sec,
		.tv_nsec = ts.tv_nsec - bar->ts.tv_nsec,
	};

	if (bar->val != step && bar->val != entry->size)
		if (dt.tv_sec <= min_dt.tv_sec && dt.tv_nsec < min_dt.tv_nsec)
			return;

	speed = calculate_speed(dt.tv_sec, dt.tv_nsec, bar->val);
	total_speed = calculate_speed(ts.tv_sec - bar->start_ts.tv_sec,
				      ts.tv_nsec - bar->start_ts.tv_nsec,
				      bar->total_val);

	if (bar->type == pb_console) {
		printf("\x1B[A");
		if (bar->val != step)
			printf("\x1B[A");

		print_bar(get_entry_rel_path(entry->data), entry->size, speed,
			  (float)bar->val / entry->size);

		char total_title[64];
		sprintf(total_title, "Total(%ld/%ld)", bar->it.i,
			bar->entries->metadata.len);
		print_bar(total_title, bar->max_val, total_speed,
			  (float)bar->total_val / bar->max_val);
	} else
		show_bar(get_entry_rel_path(entry->data),
			 (float)bar->total_val / bar->max_val);
}
