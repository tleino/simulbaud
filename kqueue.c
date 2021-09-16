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

#include "evsrc.h"
#include "event.h"

#include <sys/types.h>

#include <sys/event.h>

#include <sys/time.h>

#include <stdlib.h>
#include <assert.h>

#include <unistd.h>

#define QUEUE_DEPTH	64

struct event
{
	int		 kq;
};

struct event *
event_create()
{
	struct event *ev;

	ev = calloc(1, sizeof(struct event));
	if (ev != NULL) {
		ev->kq = kqueue();
		if (ev->kq == -1) {
			event_free(ev);
			ev = NULL;
		}
	}

	return ev;
}

void
event_free(struct event *ev)
{
	free(ev);
}

int
event_add_evsrc(struct event *ev, struct evsrc *evsrc)
{
	struct kevent changelist;

	evsrc->ev = ev;

	switch (evsrc->type) {
	case EVSRC_FD:
		EV_SET(&changelist, evsrc->value, EVFILT_READ,
		    EV_ADD | EV_ENABLE, 0, 0, evsrc);
		break;
	case EVSRC_WRITE_FD:
		EV_SET(&changelist, evsrc->value, EVFILT_WRITE,
		    EV_ADD | EV_ENABLE | EV_DISPATCH, 0, 0, evsrc);
		break;
	case EVSRC_TIMER:
		EV_SET(&changelist, 1, EVFILT_TIMER,
		    EV_ADD | EV_ENABLE, 0, evsrc->value, evsrc);
		break;
	default:
		assert(0);
		break;
	}
	if (kevent(ev->kq, &changelist, 1, NULL, 0, NULL) != 0)
		return -1;

	return 0;
}

int
event_dispatch(struct event *ev)
{
	struct kevent event[QUEUE_DEPTH];
	struct kevent *evp;
	int i, nevents;
	struct evsrc *evsrc;

	nevents = kevent(ev->kq, NULL, 0, event, QUEUE_DEPTH, NULL);
	if (nevents == -1)
		return -1;

	for (i = 0; i < nevents; i++) {
		evp = &event[i];
		evsrc = (struct evsrc *) evp->udata;
		if (evsrc->readcb(evsrc, evsrc->data) == -1) {
			if (evsrc->type == EVSRC_FD)
				close(evsrc->value);
		}
	}

	return 0;
}
