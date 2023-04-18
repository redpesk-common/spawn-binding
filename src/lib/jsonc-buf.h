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

#include <json-c/json.h>

typedef void (*jsonc_buf_cb)(void *closure, json_object *object);
typedef void (*jsonc_buf_error_cb)(void *closure, const char *message);

extern void jsonc_buf_process(json_tokener *tokener, const char *buffer, size_t offset, jsonc_buf_cb push,
			      void *closure, jsonc_buf_error_cb onerror);

extern void jsonc_buf_read(json_tokener *tokener, int fd, jsonc_buf_cb push, void *closure, jsonc_buf_error_cb onerror);

extern void jsonc_buf_end(json_tokener *tokener, jsonc_buf_cb push, void *closure, jsonc_buf_error_cb onerror);
