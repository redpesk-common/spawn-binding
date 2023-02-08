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

#include <errno.h>
#include <systemd/sd-event.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <seccomp.h>        // high level api
#include <linux/seccomp.h>  // low level api
#include <linux/filter.h>
#include <sys/prctl.h>
#include <assert.h>
#include <strings.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>

#include <rp-utils/rp-jsonc.h>
#include <afb-helpers4/afb-data-utils.h>
#include <afb-helpers4/afb-req-utils.h>

#include "spawn-binding.h"
#include "spawn-expand.h"
#include "spawn-utils.h"
#include "spawn-enums.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include "spawn-subtask-internal.h"


const nsKeyEnumT shSignals[] = {
    {"SIGTERM", SIGTERM},
    {"SIGINT" , SIGINT},
    {"SIGKILL", SIGKILL},
    {"SIGUSR1", SIGUSR1},
    {"SIGUSR2", SIGUSR2},

    {NULL} // terminator
};

typedef enum {
    SPAWN_ACTION_START,
    SPAWN_ACTION_STOP,
    SPAWN_ACTION_SUBSCRIBE,
    SPAWN_ACTION_UNSUBSCRIBE,
} taskActionE;


void taskPushResponse (taskIdT *taskId) {
    int count=0;

    // push event if not one listen just stop pushing
    if (taskId->responseJ) {
        if (taskId->request) {
            afb_req_success(taskId->request, taskId->responseJ, NULL);
            afb_req_unref(taskId->request);
        }
        else {
	    afb_data_t data = afb_data_json_c_hold(taskId->responseJ);
	    count= afb_event_push(taskId->event, 1, &data);
	}
        taskId->responseJ=NULL;
        taskId->statusJ=NULL;
        taskId->errorJ=NULL;
        if (!count && taskId->verbose >4) AFB_API_NOTICE(taskId->cmd->api, "uid='%s' no client listening",  taskId->uid);
    }
}

extern void end_timeout_monitor(taskIdT *taskId);

void spawnFreeTaskId  (afb_api_t api, taskIdT *taskId) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);

    // mark taskId as invalid
    taskId->pid=0;

    // release source to prevent any further notification
    if (taskId->srcout)
        afb_evfd_unref(taskId->srcout);
    if (taskId->srcerr)
        afb_evfd_unref(taskId->srcerr);

    // TimerEvtStop stop+free timer handle
    end_timeout_monitor(taskId);

    if (taskId->uid) free(taskId->uid);
    free(taskId);
}

static void taskPushFinalResponse (taskIdT *taskId) {
    shellCmdT *cmd=taskId->cmd;

    // try to read any remaining data before building exit status
    if (taskId->verbose >2) AFB_API_INFO (taskId->cmd->api, "taskPushFinalResponse: uid=%s pid=%d [step-1: collect remaining data]", taskId->uid, taskId->pid);
    (void)cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDOUT, ENCODER_OPS_CLOSE, cmd->encoder->fmtctx);
    (void)cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDERR, ENCODER_OPS_CLOSE, cmd->encoder->fmtctx);

    (void)cmd->encoder->actionsCB (taskId, ENCODER_TASK_STOP, ENCODER_OPS_CLOSE, cmd->encoder->fmtctx);
    if (taskId->verbose >2) AFB_API_INFO (taskId->cmd->api, "taskPushFinalResponse: uid=%s pid=%d [step-2: collect child status=%s]", taskId->uid, taskId->pid, json_object_get_string(taskId->statusJ));

    taskPushResponse (taskId);
    spawnFreeTaskId(cmd->api, taskId);
}


static int taskCtrlOne (afb_req_t request, taskIdT *taskId, taskActionE action, int signal, json_object **responseJ) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    int err;

    if (taskId->verbose>1) AFB_REQ_INFO (request, "taskCtrlOne: sandbox=%s cmd=%s pid=%d action=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid, action);
    switch (action) {
        case SPAWN_ACTION_STOP:
            kill (-taskId->pid, signal);
            break;
        case SPAWN_ACTION_SUBSCRIBE:
            err=afb_req_subscribe(request, taskId->event);
            if (err) goto OnErrorExit;
            break;
        case SPAWN_ACTION_UNSUBSCRIBE:
            err=afb_req_unsubscribe(request, taskId->event);
            if (err) goto OnErrorExit;
            break;
        default:
            AFB_REQ_ERROR (request, "taskCtrlOne: [unknown action] action=%d", action);
            goto OnErrorExit;
    }

   *responseJ= json_object_new_string(taskId->uid);
    return 0;

OnErrorExit:
    return 1;
}

static int spawnTaskControl (afb_req_t request, shellCmdT *cmd, taskActionE action, json_object *argsJ, int verbose) {
    taskIdT *taskId, *tidNext;
    json_object *responseJ;
    int err=0;
    int taskPid=0;
    int signal= SIGINT;
    json_object *signalJ=NULL;

    // if not argument kill all task attache to this cmd->cli
    if (argsJ) {
        err= rp_jsonc_unpack (argsJ, "{s?i s?o !}", "pid", &taskPid, "signal", &signalJ);
        if (err || (!taskPid && !signalJ)) {
            afb_req_fail_f (request, "task-ctrl-fail","[missing pid|signal] sandbox=%s cmd=%s args=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
        }
    }

    if (signalJ) {
        if (json_object_is_type (signalJ, json_type_int)) {
            signal= json_object_get_int(signalJ);
        } else {
            signal= enumMapValue(shSignals, json_object_get_string(signalJ));
            if (!signal) {
                afb_req_fail_f (request, "task-ctrl-fail","[invalid signal] sandbox=%s cmd=%s signal=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(signalJ));
                goto OnErrorExit;
            }
        }
    }

    // if no task pid provided loop on every cmd forked task
    if (!taskPid) {
        responseJ = json_object_new_array();
        json_object *statusJ;

        // scan tids hashtable and stop every task
        err=pthread_rwlock_rdlock(&cmd->sem);
        if (err) goto OnErrorExit;
        HASH_ITER(tidsHash, cmd->tids, taskId, tidNext) {
            err= taskCtrlOne (request, taskId, action, signal, &statusJ);
            if (!err) {
                json_object_array_add (responseJ, statusJ);
            }
        }
        pthread_rwlock_unlock(&cmd->sem);

    } else {

        err=pthread_rwlock_rdlock(&cmd->sem);
        if (err) goto OnErrorExit;
        HASH_FIND(tidsHash, cmd->tids, &taskPid, sizeof(int), taskId);
        pthread_rwlock_unlock(&cmd->sem);
        if (taskId) {
            taskCtrlOne (request, taskId, action, signal, &responseJ);
        }else {
            goto OnErrorExit;
        }
    }
    afb_req_success_f(request, responseJ, NULL);
    return 0;

OnErrorExit:
    afb_req_fail_f (request, "task-ctrl-fail","[unknown pid signal] sandbox=%s cmd=%s args=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(argsJ));
    return 1;
} // end spawnTaskStop


void spawnTaskVerb (afb_req_t request, shellCmdT *cmd, json_object *queryJ) {
    assert (cmd);
    const char *action="start";
    json_object *argsJ=NULL;
    int err, verbose=-1;

    // if not a valid formating then everything is args and action==start
    err= rp_jsonc_unpack(queryJ, "{s?s s?o s?i !}", "action", &action, "args", &argsJ, "verbose", &verbose);
    if (err) argsJ=queryJ;

    // default is not null but cmd->verbose and query can not set verbosity to more than 4
    if (verbose < 0 || verbose >4) verbose=cmd->sandbox->verbose;

    if (!strcasecmp (action, "start")) {
        err = spawnTaskStart (request, cmd, argsJ, verbose);
        if (err) goto OnErrorExit;

    } else if (!strcasecmp (action, "stop")) {
        err = spawnTaskControl (request, cmd, SPAWN_ACTION_STOP, argsJ, verbose);
        if (err) goto OnErrorExit;

    } else if (!strcasecmp (action, "subscribe")) {
        err = spawnTaskControl (request, cmd, SPAWN_ACTION_SUBSCRIBE, argsJ, verbose);
        if (err) goto OnErrorExit;

    } else if (!strcasecmp (action, "unsubscribe")) {
        err = spawnTaskControl (request, cmd,  SPAWN_ACTION_UNSUBSCRIBE, argsJ, verbose);
        if (err) goto OnErrorExit;

    } else {
        afb_req_fail_f (request, "syntax-error", "spawnTaskVerb: unknown action='%s' sandbox=%s cmd=%s", action, cmd->sandbox->uid, cmd->uid);
        goto OnErrorExit;
    }

    // afb_req_success already set
    return;

OnErrorExit:
    // afb_req_fail already set
    return;
}

/**
*/
int spawnParse (shellCmdT *cmd, json_object *execJ)
{
	int idx, err;
	json_object *argsJ = NULL;
	const char*param;

	/* get the command and its arguments */
	err = rp_jsonc_unpack(execJ, "{ss s?o !}", "cmdpath", &cmd->cli, "args", &argsJ);
	if (err) {
		AFB_API_ERROR(cmd->api, "[fail-parsing] cmdpath sandbox=%s cmd=%s exec=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(argsJ));
		goto OnErrorExit;
	}

	cmd->cli = utilsExpandKeyCtx(cmd->cli, (void*)cmd);  // expand env $keys
	if (!utilsFileModeIs(cmd->cli, S_IXUSR)) {
		AFB_API_ERROR(cmd->api, "[file-not-executable] sandbox=%s cmd=%s exec=%s", cmd->sandbox->uid, cmd->uid, cmd->cli);
		goto OnErrorExit;
	}

	// prepare arguments list, they will still need to be expanded before execution
	if (!argsJ) {
		cmd->argc = 2;
		cmd->argv = calloc (cmd->argc, sizeof (char*));
		cmd->argv[0] = utilsExpandKeyCtx(cmd->cli, (void*)cmd);
		if (!cmd->argv[0]) {
			AFB_API_ERROR(cmd->api, "[unknow-$ENV-key] sandbox=%s cmd=%s cmdpath=%s", cmd->sandbox->uid, cmd->uid, cmd->cli);
			goto OnErrorExit;
		}
	}
	else {
		switch (json_object_get_type(argsJ)) {
		case json_type_array:
			cmd->argc = (int)json_object_array_length(argsJ) + 2;
			cmd->argv = calloc (cmd->argc, sizeof (char*));
			cmd->argv[0] = cmd->uid;
			for (idx = 1; idx < cmd->argc-1; idx++) {
				param = json_object_get_string (json_object_array_get_idx(argsJ, idx - 1));
				cmd->argv[idx] = utilsExpandKeyCtx(param, (void*)cmd);
				if (!cmd->argv[idx]) {
					AFB_API_ERROR(cmd->api, "[unknow-$ENV-key] sandbox=%s cmd=%s args=%s", cmd->sandbox->uid, cmd->uid, param);
					goto OnErrorExit;
				}
			}
			break;

		default:
			param = json_object_get_string (argsJ);
			cmd->argc = 3;
			cmd->argv = calloc(cmd->argc, sizeof (char*));
			cmd->argv[0] = cmd->uid;
			cmd->argv[1] = utilsExpandKeyCtx(param, (void*)cmd);
			if (!cmd->argv[1]) {
				AFB_API_ERROR(cmd->api, "[unknow-$ENV-key] uid=%s cmdpath=%s arg=%s", cmd->uid, cmd->cli, param);
				goto OnErrorExit;
			}
			break;
		}
	}
	return 0;

OnErrorExit:
	return 1;
}

// search taskId from child pid within global binding gtids
static taskIdT *spawnChildGetTaskId (spawnBindingT *binding, int childPid) {
    taskIdT *taskId = NULL;

    // search if PID is present within binding list
    if (! pthread_rwlock_rdlock(&binding->sem)) {
        HASH_FIND (gtidsHash, binding->gtids, &childPid, sizeof(int), taskId);
        pthread_rwlock_unlock(&binding->sem);
    }
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    return taskId;
}

void spawnChildUpdateStatus (afb_api_t api,  spawnBindingT *binding, taskIdT *taskId) {
    int childPid, childStatus;
    int expectPid=0; // wait for any child whose process group ID is equal to calling process
    shellCmdT *cmd;

    // taskId status was already collected
    if (taskId && !taskId->pid) return;

    // we known what we're looking for
    if (taskId) {
        if (taskId->verbose >2) AFB_API_INFO (api, "spawnChildUpdateStatus: uid=%s pid=%d [step-1 wait sigchild]", taskId->uid, taskId->pid);
        expectPid=taskId->pid;
    }

    // retreive every child status as many child may share the same signal
    while ((childPid = waitpid (expectPid, &childStatus, 0 /* Fulup removed WNOHANG ??? */)) > 0) {

        // anonymous childs signal should be check agains global binding taskid
        if (!taskId) taskId= spawnChildGetTaskId (binding, childPid);

        if (!taskId) {
            AFB_API_NOTICE(api, "[sigchild-unknown] igoring childPid=%d exit status=%d (spawnChildUpdateStatus)", childPid, childStatus);
            continue;
        }
        // remove task from both global & cmd tids
        cmd=taskId->cmd;
        if (! pthread_rwlock_wrlock(&cmd->sem)) {
            HASH_DELETE(tidsHash, cmd->tids, taskId);
            pthread_rwlock_unlock(&cmd->sem);
        }

        if (! pthread_rwlock_wrlock(&binding->sem)) {
            HASH_DELETE(gtidsHash, binding->gtids, taskId);
            pthread_rwlock_unlock(&binding->sem);
        }

        // update child taskId status
        if (WIFEXITED (childStatus)) {
            rp_jsonc_pack (&taskId->statusJ, "{si so*}", "exit", WEXITSTATUS(childStatus), "info", taskId->errorJ);
        } else if(WIFSIGNALED (childStatus)) {
            rp_jsonc_pack (&taskId->statusJ, "{ss so*}", "signal", strsignal(WTERMSIG(childStatus)), "info", taskId->errorJ);
        } else {
            rp_jsonc_pack (&taskId->statusJ, "{si so*}", "unknown", 255, "info", taskId->errorJ);
        }

        // push final respond to every taskId subscriber
        if (taskId->verbose >2) AFB_API_INFO (api, "spawnChildUpdateStatus: uid=%s pid=%d [step-2 got sigchild status=%d]", taskId->uid, taskId->pid, childStatus);
        taskPushFinalResponse (taskId);

    } // end while
}

#if 0
int spawnChildSignalCB (sd_event_source* source, int fd, uint32_t events, void* context) {
    spawnBindingT *binding= (spawnBindingT*)context;
    assert(binding->magic == MAGIC_SPAWN_BDING);

    // Ignore termination child signal outside of stdout pipe handup
    /* if (binding->verbose >2) */ AFB_API_NOTICE(binding->api, "[child-gtids-signal] (spawnChildSignalCB)");
    spawnChildUpdateStatus (binding->api, binding, NULL);

    return 0;
}

// register a callback to monitor sigchild events
int spawnChildMonitor (afb_api_t api, sd_event_io_handler_t callback, spawnBindingT *binding) {
    sigset_t sigMask;
    afb_evfd_t efd;

    // init signal mask
    sigemptyset (&sigMask);
    sigaddset (&sigMask, SIGCHLD);

    // create global hashtable semaphore
    if (pthread_rwlock_init (&binding->sem, NULL)) {
        AFB_API_NOTICE(api, "[semaphore-init-fail] error=%s (spawnChildMonitor)", strerror(errno));
        goto OnErrorExit;
    }

    int sigFd = signalfd (-1, &sigMask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sigFd < 0) {
        AFB_API_NOTICE(api, "[signalfd-SIGCHLD-fail] error=%s (spawnChildMonitor)", strerror(errno));
        goto OnErrorExit;
    }
    (void)sd_event_add_io(afb_api_get_event_loop(api), NULL, sigFd, EPOLLIN, callback, (void*)binding);
    afb_evfd_create(&efd, sigFd, EPOLLIN,
	int fd,
	uint32_t events,
	afb_evfd_handler_t handler,
	void *closure,
	int autounref,
	int autoclose
);
    return 0;

 OnErrorExit:
    return 1;
}

#endif
