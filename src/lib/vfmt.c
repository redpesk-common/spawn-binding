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

#include <stdarg.h>

void vfmt(void (*fun)(const char *fmt, va_list args), const char *fmt, ...)
{
	if (fun) {
		va_list args;
		va_start(args, fmt);
		fun(fmt, args);
		va_end(args);
	}
}

void vfmtc(void (*fun)(void *closure, const char *fmt, va_list args), void *closure, const char *fmt, ...)
{
	if (fun) {
		va_list args;
		va_start(args, fmt);
		fun(closure, fmt, args);
		va_end(args);
	}
}

void vfmtcl(void (*fun)(void *closure, int lvl, const char *fmt, va_list args), void *closure, int lvl, const char *fmt,
	    ...)
{
	if (fun) {
		va_list args;
		va_start(args, fmt);
		fun(closure, lvl, fmt, args);
		va_end(args);
	}
}