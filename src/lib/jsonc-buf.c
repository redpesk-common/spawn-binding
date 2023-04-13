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

#include <unistd.h>
#include <errno.h>

#include "jsonc-buf.h"


void jsonc_buf_process(json_tokener *tokener, const char *buffer, size_t count, jsonc_buf_cb push, void *closure, jsonc_buf_error_cb onerror)
{
	size_t used, pos;
	enum json_tokener_error jerr;
	json_object *obj;

	// scan
	for (pos = 0 ; pos < count ; pos += used) {
		obj = json_tokener_parse_ex(tokener, &buffer[pos], count - pos);
		jerr = json_tokener_get_error(tokener);
		used = json_tokener_get_parse_end(tokener);
		switch (jerr) {
		case json_tokener_continue:
			break;
		case json_tokener_success:
			push(closure, obj);
			break;
		default:
			if (onerror)
				onerror(closure, json_tokener_error_desc(jerr));
			used += !used;
			break;
		}
	}
}

void jsonc_buf_read(json_tokener *tokener, int fd, jsonc_buf_cb push, void *closure, jsonc_buf_error_cb onerror)
{
	char buffer[4096];

	for (;;) {
		// read
		ssize_t sts = read(fd, buffer, sizeof buffer);
		if (sts > 0)
			jsonc_buf_process(tokener, buffer, (size_t)sts, push, closure, onerror);
		else if (sts == 0 || errno != EINTR)
			break;
	}
}

void jsonc_buf_end(json_tokener *tokener, jsonc_buf_cb push, void *closure, jsonc_buf_error_cb onerror)
{
	jsonc_buf_process(tokener, "", 1, push, closure, onerror);
}

