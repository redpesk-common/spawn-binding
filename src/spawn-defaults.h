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


#ifndef _SPAWN_DEFAULTS_INCLUDE_
#define _SPAWN_DEFAULTS_INCLUDE_

#ifndef MAX_DOC_LINE_SIZE
#define MAX_DOC_LINE_SIZE 512
#endif

#ifndef MAX_DOC_LINE_COUNT
#define MAX_DOC_LINE_COUNT 128
#endif

#ifndef CGROUPS_MOUNT_POINT
#define CGROUPS_MOUNT_POINT "/sys/fs/cgroup"
#endif

#ifndef BWRAP_EXE_PATH
#define BWRAP_EXE_PATH "/usr/bin/bwrap"
#endif

#ifndef BWRAP_ARGC_MAX
#define BWRAP_ARGC_MAX 512
#endif

#ifndef SPAWN_MAX_ARG_LEN
#define SPAWN_MAX_ARG_LEN 256
#endif

#ifndef SPAWN_MAX_ARG_LABEL
#define SPAWN_MAX_ARG_LABEL 64
#endif

#ifndef ERROR
#define ERROR -1
#endif

#ifndef DONE
#define DONE 1
#endif

typedef char*(*spawnGetDefaultCbT)(const char *label, void *ctx, void *userdata);
typedef struct {
    const char *label;
    spawnGetDefaultCbT callback;
    void *ctx;
} spawnDefaultsT;
extern spawnDefaultsT spawnVarDefaults[];

#endif /* _SPAWN_DEFAULTS_INCLUDE_ */
