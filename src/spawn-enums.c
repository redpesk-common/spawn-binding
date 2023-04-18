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

#define _GNU_SOURCE

#include <seccomp.h>
#include <linux/capability.h>
#include "spawn-enums.h"
#include <stdio.h>
#include <string.h>

// for seccomp syscall use seccomp_syscall_resolve_name("syscallname")
const nsKeyEnumT nsScmpFilter[] = {
	{ "SCMP_FLTATR_ACT_DEFAULT", SCMP_FLTATR_ACT_DEFAULT },
	{ "SCMP_FLTATR_ACT_BADARCH", SCMP_FLTATR_ACT_BADARCH },
	{ "SCMP_FLTATR_CTL_NNP", SCMP_FLTATR_CTL_NNP },
	{ "SCMP_FLTATR_CTL_TSYNC", SCMP_FLTATR_CTL_TSYNC },
	{ "SCMP_FLTATR_API_TSKIP", SCMP_FLTATR_API_TSKIP },
	{ "SCMP_FLTATR_CTL_LOG", SCMP_FLTATR_CTL_LOG },
#ifdef SCMP_FLTATR_CTL_OPTIMIZE
	{ "SCMP_FLTATR_CTL_OPTIMIZE", SCMP_FLTATR_CTL_OPTIMIZE },
#endif
#ifdef SCMP_FLTATR_API_SYSRAWRC
	{ "SCMP_FLTATR_API_SYSRAWRC", SCMP_FLTATR_API_SYSRAWRC },
#endif
#ifdef SCMP_FLTATR_CTL_SSB
	{ "SCMP_FLTATR_CTL_SSB", SCMP_FLTATR_CTL_SSB },
#endif

	{ NULL } // terminator
};

const nsKeyEnumT nsScmpAction[] = {
	{ "SCMP_ACT_KILL_PROCESS", SCMP_ACT_KILL_PROCESS },
	{ "SCMP_ACT_KILL_THREAD", SCMP_ACT_KILL_THREAD },
	{ "SCMP_ACT_KILL", SCMP_ACT_KILL },
	{ "SCMP_ACT_TRAP", SCMP_ACT_TRAP },
	{ "SCMP_ACT_LOG", SCMP_ACT_LOG },
	{ "SCMP_ACT_ALLOW", SCMP_ACT_ALLOW },
#ifdef SCMP_ACT_NOTIFY
	{ "SCMP_ACT_NOTIFY", SCMP_ACT_NOTIFY },
#endif

	{ NULL } // terminator
};

const nsKeyEnumT mountMode[] = {
	{ "ro", NS_MOUNT_RO },
	{ "read", NS_MOUNT_RO },
	{ "rw", NS_MOUNT_RW },
	{ "write", NS_MOUNT_RW },
	{ "anonymous", NS_MOUNT_DIR },
	{ "symlink", NS_MOUNT_SYMLINK },
	{ "execfd", NS_MOUNT_EXECFD },
	{ "Tmpfs", NS_MOUNT_TMPFS },
	{ "Procfs", NS_MOUNT_PROCFS },
	{ "Mqueue", NS_MOUNT_MQUEFS },
	{ "Devfs", NS_MOUNT_DEVFS },
	{ "Lock", NS_MOUNT_LOCK },

	{ NULL } // terminator
};

const nsKeyEnumT nsShareMode[] = {
	{ "default", NS_SHARE_DEFAULT },
	{ "enable", NS_SHARE_ENABLE },
	{ "disable", NS_SHARE_DISABLE },

	{ NULL } // terminator
};

const nsKeyEnumT nsRunmodMode[] = {
	{ "default", RUNM_DEFAULT },
	{ "user", RUNM_USER },
	{ "admin", RUNM_ADMIN },

	{ NULL } // terminator
};

const nsKeyEnumT envMode[] = {
	{ "set", NS_ENV_SET },
	{ "unset", NS_ENV_UNSET },

	{ NULL } // terminator
};

const nsKeyEnumT capMode[] = {
	{ "set", NS_CAP_SET },
	{ "unset", NS_CAP_UNSET },

	{ NULL } // terminator
};

// unfortunately capng_name_to_capability may miss few labels
const nsKeyEnumT capLabel[] = {
	{ "CHOWN", CAP_CHOWN },
	{ "DAC_OVERRIDE", CAP_DAC_OVERRIDE },
	{ "DAC_READ_SEARCH", CAP_DAC_READ_SEARCH },
	{ "FOWNER", CAP_FOWNER },
	{ "FSETID", CAP_FSETID },
	{ "KILL", CAP_KILL },
	{ "SETGID", CAP_SETGID },
	{ "SETUID", CAP_SETUID },
	{ "SETPCAP", CAP_SETPCAP },
	{ "LINUX_IMMUTABLE", CAP_LINUX_IMMUTABLE },
	{ "NET_BIND_SERVICE", CAP_NET_BIND_SERVICE },
	{ "NET_BROADCAST", CAP_NET_BROADCAST },
	{ "NET_ADMIN", CAP_NET_ADMIN },
	{ "NET_RAW", CAP_NET_RAW },
	{ "IPC_LOCK", CAP_IPC_LOCK },
	{ "IPC_OWNER", CAP_IPC_OWNER },
	{ "SYS_MODULE", CAP_SYS_MODULE },
	{ "SYS_RAWIO", CAP_SYS_RAWIO },
	{ "SYS_CHROOT", CAP_SYS_CHROOT },
	{ "SYS_PTRACE", CAP_SYS_PTRACE },
	{ "SYS_PACCT", CAP_SYS_PACCT },
	{ "SYS_ADMIN", CAP_SYS_ADMIN },
	{ "SYS_BOOT", CAP_SYS_BOOT },
	{ "SYS_NICE", CAP_SYS_NICE },
	{ "SYS_RESOURCE", CAP_SYS_RESOURCE },
	{ "SYS_TIME", CAP_SYS_TIME },
	{ "SYS_TTY_CONFIG", CAP_SYS_TTY_CONFIG },
	{ "MKNOD", CAP_MKNOD },
	{ "LEASE", CAP_LEASE },
	{ "AUDIT_WRITE", CAP_AUDIT_WRITE },
	{ "AUDIT_CONTROL", CAP_AUDIT_CONTROL },
	{ "MAC_OVERRIDE", CAP_MAC_OVERRIDE },
	{ "MAC_ADMIN", CAP_MAC_ADMIN },
	{ "SYSLOG", CAP_SYSLOG },
	{ "WAKE_ALARM", CAP_WAKE_ALARM },
	{ "BLOCK_SUSPEND", CAP_BLOCK_SUSPEND },
	{ "AUDIT_READ", CAP_AUDIT_READ },
	{ "LAST_CAP", CAP_LAST_CAP },

	{ NULL } // terminator
};

// search for key label within key/value array
int enumMapValue(const nsKeyEnumT *keyvals, const char *label)
{
	int value = -1;
	if (!label)
		goto OnDefaultExit;

	for (int idx = 0; keyvals[idx].label; idx++) {
		if (!strcasecmp(label, keyvals[idx].label)) {
			value = keyvals[idx].value;
			break;
		}
	}
	return value;

OnDefaultExit:
	return keyvals[0].value;
}
