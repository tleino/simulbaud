/*
 * Copyright (c) 2021, Tommi Leino <namhas@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "event.h"
#include "evsrc.h"

#include <locale.h>
#include <termios.h>
#include <util.h>
#include <err.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int				 pty_fd;
static char				*inbuf;
static size_t				 inbuf_alloc;
static size_t				 inbuf_sz;
static size_t				 inbuf_pos;
static size_t				 line_col;

int
read_stdin(struct evsrc *es, void *data)
{
	char				 c;
	ssize_t				 n;

	n = read(0, &c, 1);
	if (n <= 0)
		return -1;

	if (c == '~' && line_col == 0)
		exit(0);
	else if (c == '\n' || c == '\r')
		line_col = 0;
	else
		line_col++;

	write(pty_fd, &c, 1);
	return 0;
}

int
read_pty(struct evsrc *es, void *data)
{
	char				 buf[4096];
	ssize_t				 n;

	n = read(pty_fd, buf, sizeof(buf)-1);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	while (inbuf_sz + n + 1 >= inbuf_alloc) {
		if (inbuf_alloc == 0)
			inbuf_alloc = 4096;
		else
			inbuf_alloc *= 2;
		inbuf = realloc(inbuf, inbuf_alloc * sizeof(char));
		if (inbuf == NULL)
			return -1;
	}

	strcpy(&inbuf[inbuf_sz], buf);
	inbuf_sz += n;
	inbuf[inbuf_sz] = '\0';
	return 0;
}

int
read_timer(struct evsrc *es, void *data)
{
	size_t				 i;
	int				 *per_frame = (int *) data;

	i = *per_frame;
	while (i-- && inbuf_pos < inbuf_sz)
		putchar(inbuf[inbuf_pos++]);
	fflush(stdout);

	if (inbuf_pos == inbuf_sz) {
		inbuf_sz = 0;
		inbuf_pos = 0;
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	pid_t				 pid;
	struct event			*ev;
	struct evsrc			*es_stdin, *es_pty, *es_timer;
	double				 bits_per_s;
	int				 bits_in_byte;
	double				 ms;
	double				 framerate_ms;
	int				 per_frame;
	int				 timer_ms;

	if (argc < 2) {
		fprintf(stderr, "usage: %s baudrate\n", argv[0]);
		return 1;
	}

	bits_per_s = atoi(argv[1]);
	if (bits_per_s < 150 || bits_per_s > 200000) {
		fprintf(stderr,
		    "Minimum baudrate is 150 and maximum is 200000.\n");
		return 1;
	}
	bits_in_byte = 10;
	framerate_ms = (1000 / 60.0);

	ms = 1000 / (bits_per_s / bits_in_byte);
	timer_ms = ms > framerate_ms ? ms : framerate_ms;
	per_frame = (int) (framerate_ms/ms);
	if (per_frame == 0)
		per_frame = 1;
	printf("Escape character is: ~\r\n");
	printf("Writing %d characters every %d ms.\r\n", per_frame, timer_ms);

	setlocale(LC_ALL, "");

	pid = forkpty(&pty_fd, NULL, NULL, NULL);
	if (pid < 0)
		err(1, "forkpty");
	if (pid == 0)
		execl("/bin/sh", "/bin/sh", NULL);

	ev = event_create();
	if (ev == NULL)
		err(1, "event_create");

	es_stdin = evsrc_create_fd(0, read_stdin, NULL);
	if (es_stdin == NULL)
		err(1, "evsrc_create");

	es_pty = evsrc_create_fd(pty_fd, read_pty, NULL);
	if (es_pty == NULL)
		err(1, "evsrc_create");

	es_timer = evsrc_create_timer(timer_ms, read_timer, &per_frame);
	if (es_timer == NULL)
		err(1, "evsrc_create");

	if (event_add_evsrc(ev, es_stdin) != 0)
		err(1, "event_add_evsrc");

	if (event_add_evsrc(ev, es_pty) != 0)
		err(1, "event_add_evsrc");

	if (event_add_evsrc(ev, es_timer) != 0)
		err(1, "event_add_evsrc");

	for (;;)
		if (event_dispatch(ev) == -1)
			err(1, "event_dispatch");
	
	event_free(ev);
	return 0;
}
