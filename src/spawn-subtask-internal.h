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

#ifndef _SPAWN_SUBTASK_INTERNAL_INCLUDE_
#define _SPAWN_SUBTASK_INTERNAL_INCLUDE_

#include <uthash.h>
#include <afb/afb-binding.h>

/**
* Structure holding data of a command execution
*/
struct taskIdS
{
	/** pid of the task */
	pid_t pid;

	/** uid of the task */
	char *uid;

	/** related command */
	shellCmdT *cmd;

	/** verbosity of the task */
	int verbose;

	/** flag if synchronous */
	int synchronous;

	/** input pipe from task stdout */
	int outfd;

	/** input pipe from task stderr */
	int errfd;

	/** event handlers for pipe from task stdout */
	afb_evfd_t srcout;

	/** event handlers for pipe from task stderr */
	afb_evfd_t srcerr;

	/** encoder */
	encoder_t *encoder;

	/** timeout management data */
	struct timeout_data *timeout;

	/** request attached to the task */
	afb_req_t request;

	/** event attached to the task */
	afb_event_t event;

	void *context;

	json_object *responseJ;
	json_object *errorJ;
	json_object *statusJ;

	/** hash of tasks per command */
	UT_hash_handle tidsHash;

	/** global hash of tasks */
	UT_hash_handle gtidsHash;
};

/** global running tasks */
extern taskIdT *globtids;

/** globtids' access protection */
extern pthread_rwlock_t globtidsem;

#endif /* _SPAWN_SUBTASK_INTERNAL_INCLUDE_ */
