/*
 * Copyright (C) 2015-2021 IoT.bzh Company
 * Author "Fulup Ar Foll"
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

#ifndef _SPAWN_ENUMS_INCLUDE_
#define _SPAWN_ENUMS_INCLUDE_

typedef enum {
	NS_MOUNT_UNKNOWN = 0,
	NS_MOUNT_DIR,
	NS_MOUNT_RO,
	NS_MOUNT_RW,
	NS_MOUNT_SYMLINK,
	NS_MOUNT_EXECFD,
	NS_MOUNT_DEFLT,
	NS_MOUNT_DEVFS,
	NS_MOUNT_TMPFS,
	NS_MOUNT_MQUEFS,
	NS_MOUNT_PROCFS,
	NS_MOUNT_LOCK,
} nsMountFlagE;

typedef enum {
	NS_CAP_UNKNOWN = 0,
	NS_CAP_SET,
	NS_CAP_UNSET,
} nsCapFlagE;

typedef enum {
	NS_ENV_UNKNOWN = 0,
	NS_ENV_SET,
	NS_ENV_UNSET,
} nsEnvFlagE;

typedef enum {
	RUNM_DEFAULT = 0,
	RUNM_USER,
	RUNM_ADMIN,
} nsRunmodFlagE;

typedef enum {
	NS_SHARE_DEFAULT = 0,
	NS_SHARE_ENABLE,
	NS_SHARE_DISABLE,
} nsShareFlagE;

typedef enum {
	LSM_LABEL_NONE = 0,
	LSM_LABEL_SBOX,
	LSM_LABEL_API,
	LSM_LABEL_CMD,
} nsLSMFlagE;

typedef struct {
	const char *label;
	const int value;
} nsKeyEnumT;

extern const nsKeyEnumT mountMode[];
extern const nsKeyEnumT envMode[];
extern const nsKeyEnumT capMode[];
extern const nsKeyEnumT capLabel[];
extern const nsKeyEnumT nsScmpFilter[];
extern const nsKeyEnumT nsScmpAction[];
extern const nsKeyEnumT nsShareMode[];
extern const nsKeyEnumT nsRunmodMode[];

int enumMapValue(const nsKeyEnumT *keyvals, const char *label);

#endif
