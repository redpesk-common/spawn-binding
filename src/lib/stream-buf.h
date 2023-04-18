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

#pragma once

#include <stddef.h>
#include <stdbool.h>

typedef struct stream_buf_s stream_buf_t;

struct stream_buf_s {
	size_t length;
	size_t capacity;
	char *data;
};

// free the memory used by the stream buffer
extern void stream_buf_clear(stream_buf_t *sbuf);

// realloc and reset the stream buffer
extern stream_buf_t *stream_buf_init(stream_buf_t *sbuf, size_t capacity);

// realloc and reset the stream buffer
extern stream_buf_t *stream_buf_resize(stream_buf_t *sbuf, size_t capacity);

// ensure at least the size of available data in the stream buffer
extern stream_buf_t *stream_buf_ensure(stream_buf_t *sbuf, size_t size);

// realloc and reset the stream buffer
extern stream_buf_t *stream_buf_create(size_t capacity);

// free memory used by the new stream buffer and the stream buffer itself
extern void stream_buf_free(stream_buf_t *sbuf);

// read data from fd in the stream buffer
// returns a negative on error, zero if nothing is read, or a positive if something was read
extern int stream_buf_read_fd(stream_buf_t *sbuf, int fd);

// removes the bytes at the beginning of the stream buffer
extern void stream_buf_consume(stream_buf_t *sbuf, size_t size);

static inline size_t stream_buf_length(stream_buf_t *sbuf)
{
	return sbuf->length;
}

static inline char *stream_buf_data(stream_buf_t *sbuf)
{
	return sbuf->data;
}

static inline size_t stream_buf_capacity(stream_buf_t *sbuf)
{
	return sbuf->capacity;
}

static inline bool stream_buf_is_full(stream_buf_t *sbuf)
{
	return sbuf->length == sbuf->capacity;
}
