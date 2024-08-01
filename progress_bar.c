#include "progress_bar.h"
#include "size_info.h"
#include <bits/time.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

size_info calculate_speed(const progress_bar_t *const bar, const size_t curr,
			  const struct timespec dtt)
{
	const double dt = dtt.tv_sec + dtt.tv_nsec * 1.0e-9;

	if (dtt.tv_sec == 0 && dtt.tv_nsec <= 1)
		return (size_info){ 0 };

	const size_t d_size = curr - bar->last_val;

	return bytes_to_size(round(d_size / dt));
}

int print_bar(const progress_bar_t *const bar, const size_t curr,
	      const size_info speed)
{
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	const size_t static_len = bar->title_len + 2 + 2 + 9 + 13;
	if (w.ws_col < static_len)
		return -1;

	const size_t bar_len = w.ws_col - static_len;

	const double state = (double)curr / bar->max_val;
	const unsigned done = round(state * bar_len);

	for (unsigned i = 0; i < w.ws_col; ++i) {
		putchar('\b');
	}

	//putchar('\r');
	printf("%s: [", bar->title);

	size_t i = 0;

	for (; i < done; ++i) {
		putchar('#');
	}
	for (; i < bar_len; ++i) {
		putchar(' ');
	}
	printf("] %5.1lf%% %6.1lf %3s/s", state * 100.0, speed.size,
	       unit(speed));

	fflush(stdout);
	return 0;
}

void prog_bar_init(progress_bar_t *bar, const char *const title,
		   const size_t max, const struct timespec minimum_dt)
{
	*bar = (progress_bar_t){
		.title = title,
		.title_len = strlen(title),
		.max_val = max,
		.minimum_dt = minimum_dt,
	};
}

int prog_bar_start(progress_bar_t *prog)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		perror("gettime");
		exit(1);
	}
	prog->prev_ts = ts;
	return print_bar(prog, 0, (size_info){ 0 });
}

int prog_bar_advance(progress_bar_t *prog, const size_t curr_val)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		perror("gettime");
		exit(1);
	}

	const struct timespec dt = {
		.tv_sec = ts.tv_sec - prog->prev_ts.tv_sec,
		.tv_nsec = ts.tv_nsec - prog->prev_ts.tv_nsec,
	};

	if (dt.tv_sec <= prog->minimum_dt.tv_sec &&
	    dt.tv_nsec < prog->minimum_dt.tv_nsec) {
		return 0;
	}

	if (print_bar(prog, curr_val, calculate_speed(prog, curr_val, dt)) < 0)
		return -1;

	prog->prev_ts = ts;
	prog->last_val = curr_val;

	return 0;
}

int prog_bar_finish(progress_bar_t *prog)
{
	const struct timespec old = prog->minimum_dt;
	prog->minimum_dt = (struct timespec){ 0 };
	prog_bar_advance(prog, prog->max_val);
	prog->minimum_dt = old;

	return putchar('\n');
}
