/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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


#include "spawn-binding.h"
#include <afb-timer.h>
#include <uthash.h>


struct taskIdS {
  int magic;
  int pid; // hashtable key
  char *uid;
  int stdout;
  int stderr;
  shellCmdT *cmd;
  void *context;
  TimerHandleT *timer;
  sd_event_source *srcout;
  sd_event_source *srcerr;
  afb_event_t event;
  json_object *responseJ;
  json_object *errorJ;
  json_object *statusJ;
  UT_hash_handle tidsHash, gtidsHash;    /* makes this structure hashable */
};

void spawnTaskVerb (afb_req_t request, shellCmdT *cmd, json_object *queryJ);
int spawnParse (shellCmdT *cmd, json_object *execJ);
int spawnChildSignalCB (sd_event_source* source, int fd, uint32_t events, void* context);
int spawnChildMonitor (afb_api_t api, sd_event_io_handler_t callback, spawnBindingT *binding);
void spawnChildUpdateStatus (afb_api_t api,  spawnBindingT *binding, taskIdT *taskId);

#endif /* _SPAWN_SUBTASK_INCLUDE_ */
