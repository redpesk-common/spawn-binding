/*
 * Copyright (C) 2015-2021 IoT.bzh Company
 * Author José Bollo
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
*/

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "stream-buf.h"


// free the memory used by the stream buffer
void stream_buf_clear(stream_buf_t *sbuf)
{
	sbuf->capacity = 0;
	sbuf->length = 0;
	free(sbuf->data);
	sbuf->data = NULL;
}

// realloc and reset the stream buffer
stream_buf_t *stream_buf_init(stream_buf_t *sbuf, size_t capacity)
{
	sbuf->length = 0;
	sbuf->data = malloc(capacity);
	sbuf->capacity = sbuf->data == NULL ? 0 : capacity;
	return sbuf->data == NULL ? NULL : sbuf;
}

// realloc and reset the stream buffer
stream_buf_t *stream_buf_resize(stream_buf_t *sbuf, size_t capacity)
{
	char *buffer = realloc(sbuf->data, capacity);
	if (buffer == NULL)
		return NULL;
	sbuf->data = buffer;
	sbuf->capacity = capacity;
	if (sbuf->length > capacity)
		sbuf->length = capacity;
	return sbuf;
}

// ensure at least the size of available data in the stream buffer
stream_buf_t *stream_buf_ensure(stream_buf_t *sbuf, size_t size)
{
        size_t avail = sbuf->capacity - sbuf->length;
	return avail >= size ? sbuf : stream_buf_resize(sbuf, sbuf->capacity + size);
}

// realloc and reset the stream buffer
stream_buf_t *stream_buf_create(size_t capacity)
{
	stream_buf_t *sbuf = malloc(sizeof *sbuf);
	if (sbuf != NULL) {
		if (stream_buf_init(sbuf, capacity) == NULL) {
			free(sbuf);
			sbuf = NULL;
		}
	}
	return sbuf;
}

// free memory used by the new stream buffer
void stream_buf_free(stream_buf_t *sbuf)
{
	free(sbuf->data);
	free(sbuf);
}

// read data from fd in the stream buffer
// returns a negative on error, zero if nothing is read, or a positive if something was read
int stream_buf_read_fd(stream_buf_t *sbuf, int fd)
{
	int rc = 0;
	for (;; rc = 1) {
		size_t avail = sbuf->capacity - sbuf->length;
		ssize_t sts = avail ? read(fd, &sbuf->data[sbuf->length], avail) : 0;
		if (sts < 0) {
			if (errno != EINTR)
				return -1;
		}
		else {
			sbuf->length += (size_t)sts;
			if (sts == 0 || avail == (size_t)sts)
				return rc;
		}
	}
}

// removes the bytes at the beginning of the stream buffer
void stream_buf_consume(stream_buf_t *sbuf, size_t size)
{
	if (size >= sbuf->length)
		sbuf->length = 0;
	else {
		sbuf->length -= size;
		memmove(sbuf->data, &sbuf->data[size], sbuf->length);
	}
}

