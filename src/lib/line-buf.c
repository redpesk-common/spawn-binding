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


#include "line-buf.h"


void line_buf_process(stream_buf_t *sbuf, size_t offset, line_buf_cb push, void *closure)
{
	size_t base, last, pos, pz;
	char *data;

	// scan
	data = stream_buf_data(sbuf);
	last = stream_buf_length(sbuf);
	base = 0;
	pos = offset;
	while (pos < last) {
		// search a newline character
		while (pos < last && data[pos] != '\n')
			pos++;

		// check if found
		if (pos < last) {
			// yes, drop 
			pz = pos - (pos && data[pos - 1] == '\r');
			data[pz] = 0;
			push(closure, &data[base], pz - base);
			base = ++pos;
		}
		else if (base == 0 && pos == stream_buf_capacity(sbuf)) {
			pz = --pos;
			base = --pos;
			char a = data[pz];
			char b = data[base];
			data[pz] = 0;
			data[base] = '\\';
			push(closure, data, pz);
			data[pz] = a;
			data[base] = b;
		}
	}
	if (base)
		stream_buf_consume(sbuf, base);
}

void line_buf_read(stream_buf_t *sbuf, int fd, line_buf_cb push, void *closure)
{
	for (;;) {
		size_t offset = stream_buf_length(sbuf);
		int sts = stream_buf_read_fd(sbuf, fd);
		if (sts <= 0)
			return;
		line_buf_process(sbuf, offset, push, closure);
	}
}

void line_buf_end(stream_buf_t *sbuf, line_buf_cb push, void *closure)
{
	size_t length;
	length = stream_buf_length(sbuf);
	if (length) {
		line_buf_process(sbuf, 0, push, closure);
		length = stream_buf_length(sbuf);
		if (length) {
			char *data = stream_buf_data(sbuf);
			data[length] = 0;
			push(closure, data, length);
		}
	}
}
