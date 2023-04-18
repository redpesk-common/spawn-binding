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

#ifndef _SPAWN_SUBTASK_INCLUDE_
#define _SPAWN_SUBTASK_INCLUDE_

#include <stdarg.h>
#include "spawn-binding.h"

// spawn-subtask.c
void spawnTaskVerb(afb_req_t request, shellCmdT *cmd, json_object *queryJ);
void spawnChildUpdateStatus(taskIdT *taskId);
void spawnFreeTaskId(taskIdT *taskId);

// spawn-childexec.c
int spawnTaskStart(afb_req_t request, shellCmdT *cmd, json_object *argsJ, int verbose);

//
void spawnTaskPushInitialStatus(taskIdT *taskId, json_object *object);
void spawnTaskPushFinalStatus(taskIdT *taskId, json_object *object);
void spawnTaskPushEventJSON(taskIdT *taskId, json_object *object);
void spawnTaskReplyJSON(taskIdT *taskId, int status, json_object *object);
void spawnTaskLog(taskIdT *taskId, int lvl, const char *fmt, va_list args);

#endif /* _SPAWN_SUBTASK_INCLUDE_ */
