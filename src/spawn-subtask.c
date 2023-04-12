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

#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>

#include <afb/afb-binding.h>
#include <rp-utils/rp-jsonc.h>
#include <afb-helpers4/afb-data-utils.h>
#include <afb-helpers4/afb-req-utils.h>

#include "spawn-binding.h"
#include "spawn-utils.h"
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

/** global running tasks */
taskIdT *globtids = NULL;

/** globtids' access protection */
pthread_rwlock_t globtidsem = PTHREAD_RWLOCK_INITIALIZER;

void taskPushResponse (taskIdT *taskId) {

    // push event if not one listen just stop pushing
    json_object *resp = taskId->responseJ;
    taskId->responseJ = NULL;
    if (resp) {
	int count = 1;
        afb_data_t data = afb_data_json_c_hold(resp);
        if (taskId->synchronous)
            afb_req_reply(taskId->request, 0, 1, &data);
        else
	    count = afb_event_push(taskId->event, 1, &data);
        taskId->statusJ = NULL;
        taskId->errorJ = NULL;
        if (!count && taskId->verbose > 4)
	    AFB_REQ_NOTICE(taskId->request, "uid='%s' no client listening",  taskId->uid);
    }
}

extern void end_timeout_monitor(taskIdT *taskId);

void spawnFreeTaskId  (taskIdT *taskId) {

    taskIdT *t;
    shellCmdT *cmd = taskId->cmd;
    spawnApiT *binding = cmd ? cmd->sandbox->binding : NULL;

    if (binding && !pthread_rwlock_wrlock(&globtidsem)) {
        HASH_FIND(gtidsHash, globtids, &taskId->pid, sizeof(int), t);
        if (t == taskId)
            HASH_DELETE(gtidsHash, globtids, taskId);
        pthread_rwlock_unlock(&globtidsem);
    }

    if (cmd && !pthread_rwlock_wrlock(&cmd->sem)) {
        HASH_FIND(tidsHash, cmd->tids, &taskId->pid, sizeof(int), t);
        if (t == taskId)
            HASH_DELETE(tidsHash, cmd->tids, taskId);
        pthread_rwlock_unlock(&cmd->sem);
    }

    // mark taskId as invalid
    taskId->pid = 0;

    // release source to prevent any further notification
    if (taskId->srcout)
        afb_evfd_unref(taskId->srcout);
    if (taskId->srcerr)
        afb_evfd_unref(taskId->srcerr);

    // TimerEvtStop stop+free timer handle
    end_timeout_monitor(taskId);

    if (taskId->request)
        afb_req_unref(taskId->request);

    if (taskId->uid)
        free(taskId->uid);
    free(taskId);
}

static void taskPushFinalResponse (taskIdT *taskId)
{
    shellCmdT *cmd=taskId->cmd;

    // try to read any remaining data before building exit status
    if (taskId->verbose > 2)
        AFB_REQ_INFO (taskId->request, "taskPushFinalResponse: uid=%s pid=%d [step-1: collect remaining data]", taskId->uid, taskId->pid);

    encoderClose(cmd->encoder, taskId);

    if (taskId->verbose > 2)
        AFB_REQ_INFO (taskId->request, "taskPushFinalResponse: uid=%s pid=%d [step-2: collect child status=%s]", taskId->uid, taskId->pid, json_object_get_string(taskId->statusJ));

    taskPushResponse (taskId);
    spawnFreeTaskId(taskId);
}


static int taskCtrlOne (afb_req_t request, taskIdT *taskId, taskActionE action, int signal, json_object **responseJ) {
    int err;

    if (taskId->verbose>1) AFB_REQ_INFO (request, "taskCtrlOne: sandbox=%s cmd=%s pid=%d action=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid, action);
    switch (action) {
        case SPAWN_ACTION_STOP:
            if (taskId->pid)
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
    afb_data_t data;
    int err=0;
    int taskPid=0;
    int signal= SIGINT;
    json_object *signalJ=NULL;

    // if not argument kill all task attache to this cmd->cli
    if (argsJ) {
        err = rp_jsonc_unpack (argsJ, "{s?i s?o !}", "pid", &taskPid, "signal", &signalJ);
        if (err || (!taskPid && !signalJ)) {
            afb_req_reply(request, AFB_ERRNO_INVALID_REQUEST, 0, NULL);
            return 1;
	}
    }

    if (signalJ) {
        if (json_object_is_type (signalJ, json_type_int)) {
            signal = json_object_get_int(signalJ);
        } else {
            signal = enumMapValue(shSignals, json_object_get_string(signalJ));
            if (!signal) {
                afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "invalid signal");
                return 1;
	    }
        }
    }

    // if no task pid provided loop on every cmd forked task
    if (!taskPid) {
        responseJ = json_object_new_array();
        json_object *statusJ;

        // scan tids hashtable and stop every task
        err=pthread_rwlock_rdlock(&cmd->sem);
        if (err) goto InternalError;
        HASH_ITER(tidsHash, cmd->tids, taskId, tidNext) {
            err= taskCtrlOne (request, taskId, action, signal, &statusJ);
            if (!err) {
                json_object_array_add (responseJ, statusJ);
            }
        }
        pthread_rwlock_unlock(&cmd->sem);

    } else {

        err=pthread_rwlock_rdlock(&cmd->sem);
        if (err) goto InternalError;
        HASH_FIND(tidsHash, cmd->tids, &taskPid, sizeof(int), taskId);
        pthread_rwlock_unlock(&cmd->sem);
        if (taskId) {
            taskCtrlOne (request, taskId, action, signal, &responseJ);
        }else {
            afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "invalid pid");
            return 1;
        }
    }
    data = afb_data_json_c_hold(responseJ);
    afb_req_reply(request, 0, 1, &data);
    return 0;

InternalError:
    afb_req_reply(request, AFB_ERRNO_INTERNAL_ERROR, 0, NULL);
    return 1;
} // end spawnTaskStop


void spawnTaskVerb (afb_req_t request, shellCmdT *cmd, json_object *queryJ) {
    assert (cmd);
    const char *action="start";
    json_object *argsJ=NULL;
    int err, verbose=-1;

    // if not a valid formating then everything is args and action==start
    if (queryJ) {
	err= rp_jsonc_unpack(queryJ, "{s?s s?o s?i !}", "action", &action, "args", &argsJ, "verbose", &verbose);
	if (err) argsJ=queryJ;
    }

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
        afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "invalid action");
        goto OnErrorExit;
    }

    // afb_req_success already set
    return;

OnErrorExit:
    // afb_req_fail already set
    return;
}

// search taskId from child pid within global binding gtids
static taskIdT *spawnChildGetTaskId (int childPid) {
    taskIdT *taskId = NULL;

    // search if PID is present within binding list
    if (! pthread_rwlock_rdlock(&globtidsem)) {
        HASH_FIND (gtidsHash, globtids, &childPid, sizeof(int), taskId);
        pthread_rwlock_unlock(&globtidsem);
    }
    return taskId;
}

void spawnChildUpdateStatus (taskIdT *taskId) {
    int childPid, childStatus;
    int expectPid=0; // wait for any child whose process group ID is equal to calling process

    // taskId status was already collected
    if (taskId && !taskId->pid) return;

    // we known what we're looking for
    if (taskId) {
        if (taskId->verbose > 2)
            AFB_REQ_INFO(taskId->request, "spawnChildUpdateStatus: uid=%s pid=%d [step-1 wait sigchild]", taskId->uid, taskId->pid);
        expectPid = taskId->pid;
    }

    // retreive every child status as many child may share the same signal
    while ((childPid = waitpid (expectPid, &childStatus, 0 /* Fulup removed WNOHANG ??? */)) > 0) {

        // anonymous childs signal should be check against global binding taskid
        if (!taskId)
		taskId= spawnChildGetTaskId (childPid);
        if (!taskId) {
            AFB_REQ_NOTICE(taskId->request, "[sigchild-unknown] igoring childPid=%d exit status=%d (spawnChildUpdateStatus)", childPid, childStatus);
            continue;
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
        if (taskId->verbose > 2)
            AFB_REQ_INFO(taskId->request, "spawnChildUpdateStatus: uid=%s pid=%d [step-2 got sigchild status=%d]", taskId->uid, taskId->pid, childStatus);
        taskPushFinalResponse (taskId);

    } // end while
}

