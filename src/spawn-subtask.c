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

#define _GNU_SOURCE

#include "spawn-binding.h"
#include "spawn-utils.h"
#include "spawn-enums.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include <ctl-config.h>

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


static void taskPushResponse (taskIdT *taskId) {
    //AFB_API_DEBUG(taskId->cmd->api, "taskPushResponse: uid='%s' responseJ=%s",  taskId->uid, json_object_get_string(taskId->responseJ));
    int count;

    // push event if not one listen just stop pushing
    if (taskId->responseJ) {
        count= afb_event_push(taskId->event, taskId->responseJ);
        taskId->responseJ=NULL;
        if (!count) AFB_API_DEBUG(taskId->cmd->api, "taskPushResponse: uid='%s' no listener",  taskId->uid);
    }
}

static void spawnFreeTaskId  (afb_api_t api, taskIdT *taskId) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);

    fprintf (stderr, "**** taskPushFinalResponse taskId=0x%p pid=%d\n", taskId, taskId->pid);

    // mark taskId as invalid
    taskId->pid=0;

    // release source to prevent any further notification
    if (taskId->srcout) {
        sd_event_source_set_enabled (taskId->srcout, SD_EVENT_OFF);
        sd_event_source_unref (taskId->srcout);
    }
    if (taskId->srcerr) {
        sd_event_source_set_enabled (taskId->srcerr, SD_EVENT_OFF);
        sd_event_source_unref (taskId->srcerr);
    }

    fprintf (stderr, "**** taskPushFinalResponse taskId=0x%p step=2\n", taskId);
    if (taskId->timer) {
        TimerEvtStop(taskId->timer);
        free (taskId->timer);
    }

    if (taskId->errFD) close (taskId->errFD);
    if (taskId->outFD) close (taskId->outFD);

    fprintf (stderr, "**** taskPushFinalResponse taskId=0x%p step=3\n", taskId);
    if (taskId->uid) free(taskId->uid);
    free(taskId);

    fprintf (stderr, "**** taskPushFinalResponse taskId=0x%p step=4\n", taskId);

}

static void taskPushFinalResponse (taskIdT *taskId) {
    shellCmdT *cmd=taskId->cmd;

    fprintf (stderr, "**** taskPushFinalResponse taskId=0x%p pid=%d\n", taskId, taskId->pid);

    // read any remaining data
    (void)cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDOUT, ENCODER_OPS_CLOSE, cmd->encoder->fmtctx);
    (void)cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDERR, ENCODER_OPS_CLOSE, cmd->encoder->fmtctx);
    (void)cmd->encoder->actionsCB (taskId, ENCODER_TASK_STOP, ENCODER_OPS_CLOSE, cmd->encoder->fmtctx);

    // status should have been updated with signal handler
    AFB_API_NOTICE(cmd->api, "[child taskPushFinalResponse] taskId=0x%p action='stop' uid='%s' status=%s (taskPushFinalResponse)",  taskId, taskId->uid, json_object_get_string(taskId->statusJ));

    // push final response if any and clear taskId
    taskPushResponse (taskId);
    spawnFreeTaskId(cmd->api, taskId);
}

static int spawnPipeFdCB (sd_event_source* source, int fd, uint32_t events, void* context) {
    taskIdT *taskId = (taskIdT*) context;
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    shellCmdT *cmd=taskId->cmd;
    int err;

    // if taskId->pid == 0 then FMT_TASK_STOP was already called once
    if (taskId->pid) {

        if (fd == taskId->outFD && events & EPOLLIN) {
            err= cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDOUT, ENCODER_OPS_STD, cmd->encoder->fmtctx);
            if (err) taskPushResponse (taskId);

        } else if (fd == taskId->errFD && events & EPOLLIN) {
            err= cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDERR, ENCODER_OPS_STD, cmd->encoder->fmtctx);
            if (!err) taskPushResponse (taskId);
        }

        // what ever stdout/err pipe hanghup 1st we close both event sources
        if (events & EPOLLHUP) {
            spawnChildUpdateStatus (cmd->api, cmd->sandbox->binding, taskId);
            taskId->pid=0; // mark taskId as done
        }
    }

    return 0;
}

// Timer base cmd pooling tic send event if cmd value changed
static int spawnTimerCB (TimerHandleT *handle) {
    assert(handle);
    taskIdT *taskId= (taskIdT*) handle->context;
    assert (taskId->magic == MAGIC_SPAWN_TASKID);

    // if process still run terminate it
    if (getpgid(taskId->pid) >= 0) {
        AFB_NOTICE("spawnTimerCB: Terminating task uid=%s", taskId->uid);
        taskId->cmd->encoder->actionsCB (taskId, ENCODER_TASK_KILL, ENCODER_OPS_STD, taskId->cmd->encoder->fmtctx);
        kill(taskId->pid, SIGKILL);
    }
    return 0; // OK count will stop timer
}

static int taskCtrlOne (afb_req_t request, taskIdT *taskId, taskActionE action, int signal, json_object **responseJ) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    int err;

    switch (action) {
        case SPAWN_ACTION_STOP:
            kill (taskId->pid, signal);
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

static char* const* taskBuildArgv (shellCmdT *cmd, json_object * argsJ) {
    const char **params;

    // argument expansion, when no arguments use directly argv (no issue with malloc/free as we are in son process)
    if (!argsJ) {
        params= cmd->argv;
    } else {
        params= calloc (cmd->argc, sizeof (char*));
        params[0]= cmd->uid;

        // if needed expand arguments replacing all $UPERCASE by json field
        for (int idx=1; cmd->argv[idx]; idx++) {

            params[idx]= utilsExpandJson (cmd->argv[idx], argsJ);
            if (!params[idx]) {
                fprintf (stderr, "[fail expanding] sandbox=%s cmd='%s' args='%s' expects=%s\n", cmd->sandbox->uid, cmd->uid, cmd->argv[idx], json_object_get_string(argsJ));
                exit (1);
            }
        }
    }
    return (char* const*)params;
} // end taskBuildArgv

static int spawnTaskStart (afb_req_t request, shellCmdT *cmd, json_object *argsJ) {
    assert(cmd);
    json_object *responseJ;
    pid_t sonPid;
    char* const* params;
    afb_api_t api= cmd->api;
    int   stdoutP[2];
    int   stderrP[2];
    int   fdFlags, err;

    // create pipes FD to retreive son stdout/stderr
    pipe (stdoutP);
    pipe (stderrP);
    // fork son process
    sonPid= fork();
    if (sonPid < 0) goto OnErrorExit;

    if (sonPid == 0) {
        // forked son process attach stdout/stderr to father pipes
        close (STDIN_FILENO);
        close (stdoutP[0]);
        close (stderrP[0]);
        int isPrivileged= utilsTaskPrivileged();
        confAclT *acls= cmd->sandbox->acls;
        confCapT *caps= cmd->sandbox->caps;

        // redirect stdout/stderr on binding pipes
        err= dup2(stdoutP[1], STDOUT_FILENO);
        if (err > 0) dup2(stderrP[1], STDERR_FILENO);
        if (err < 0) {
            fprintf (stderr, "spawnTaskStart: [fail to dup stdout/err] sandbox=%s cmd=%s\n", cmd->sandbox->uid, cmd->uid);
            exit (1);
        }

        // when privileged set cgroup
        if (isPrivileged) {
            setenv("LD_LIBRARY_PATH", "/xxx", 0);
            setenv("PATH", "/xxx", 0);

            // char pidstr[32];
            // snprintf (pidstr, sizeof (pidstr), "%d", sonPid);
            err= utilsFileAddControl (NULL, cmd->sandbox->uid, cmd->sandbox->cgroups->pidgroupFd, "cgroup.procs", "0");
            if (err) {
                fprintf (stderr, "spawnTaskStart: [capabilities is privileged]\n");
                exit(1);
            }
        }

        // capabilities apply only to son process reference https://people.redhat.com/sgrubb/libcap-ng/
        if (caps && 0) {
            // in privilege mode we drop every capabilities except the one listed in config
            if (isPrivileged && capng_have_capability(CAPNG_EFFECTIVE, CAP_SETPCAP)) {

                capng_clear(CAPNG_SELECT_BOTH); // start dropping all capabilities
                for (int idx=0; caps[idx].value; idx++) {
                    confCapT *cap= &caps[idx];
                    switch (cap->mode) {
                        case NS_CAP_SET:
                            err= capng_update(CAPNG_ADD, CAPNG_INHERITABLE, cap->value);
                            if (err < 0) {
                                fprintf (stderr, "spawnTaskStart: [capabilities set fail] sandbox=%s cmd=%s caps=%s error=%s\n", cmd->sandbox->uid, cmd->uid, capng_capability_to_name(cap->value),strerror(errno));
                            }
                            break;
                        default:
                            fprintf (stderr, "spawnTaskStart: [capabilities ignored unset=%s]\n", capng_capability_to_name(cap->value));
                            // all capacities where previously removed
                            break;
                    }
                }

                // change uid/gid while keeping capabilities
                err = capng_change_id(acls->uid, acls->gid, CAPNG_DROP_SUPP_GRP | CAPNG_CLEAR_BOUNDING);
                if (err) {
                    fprintf (stderr, "spawnTaskStart: [capabilities set fail] sandbox=%s cmd=%s\n", cmd->sandbox->uid, cmd->uid);
                    exit (1);
                }
                isPrivileged=0;

            } else {
                // we do not change user and only update capability list
                for (int idx=0; cmd->sandbox->caps[idx].value; idx++) {
                    confCapT *cap= &cmd->sandbox->caps[idx];
                    switch (cap->mode) {
                       case NS_CAP_SET:
                            capng_update(CAPNG_ADD, CAPNG_INHERITABLE, cap->value);
                            break;
                        case NS_CAP_UNSET:
                            capng_update(CAPNG_DROP, CAPNG_INHERITABLE, cap->value);
                            break;
                        default:
                            // capacity was remove ignore at config parsing
                            break;
                    }
                }
            }
            // Fulup not sure is this is needed or not !!!
            capng_apply(CAPNG_SELECT_BOTH) ;

        } // end if caps

        // apply DAC Acls even when running not privileged
        if (cmd->sandbox->acls) {
            err= sandboxApplyAcls (cmd->sandbox->acls, isPrivileged);
        }

        // build command arguments
        params= taskBuildArgv (cmd, argsJ);

        // finish by seccomp syscall filter as potentially this may prevent previous action to appen. (seccomp do not require privilege)
        if (cmd->sandbox->seccomp) {
            // reference https://blog.yadutaf.fr/2014/05/29/introduction-to-seccomp-bpf-linux-syscall-filter/
            confSeccompT *seccomp= cmd->sandbox->seccomp;
            // if we have a seccomp file apply it now otherwise check for individual rules
            if (seccomp->fsock) {
                err= prctl (PR_SET_SECCOMP, SECCOMP_MODE_FILTER, seccomp->fsock);
                if (err) {
                    fprintf (stderr, "[invalid seccomp] sandbox='%s' cmd='%s' seccomp='%s' err=%s\n", cmd->sandbox->uid, cmd->uid, seccomp->rulespath, strerror(errno));
                    exit(1);
                }
            } else {
                if (seccomp->locked) {
                    prctl(PR_SET_NO_NEW_PRIVS, 1); // never get any extra permission
                    prctl(PR_SET_DUMPABLE, 0);     // no ptrace escape
                }

                if (seccomp->rules) {
                    scmp_filter_ctx ctx= seccomp_init(seccomp->dflt); // set default filtering action
                    if (!ctx) {
                        fprintf (stderr, "[fail  seccomp_init] sandbox='%s' cmd='%s' err=%s\n", cmd->sandbox->uid, cmd->uid, strerror(errno));
                        exit(1);
                    }
                    for (int jdx=0; seccomp->rules[jdx].action; jdx++) {
                        seccomp_rule_add(ctx, seccomp->rules[jdx].action, seccomp->rules[jdx].syscall, 0);
                    }
                    seccomp_load(ctx);
                }
            }
        }

        // if cmd->cli is not executable try /bin/sh
        err = execv(cmd->cli,params);
        fprintf (stderr, "HOOPS: spawnTaskStart execve return cmd->cli=%s error=%s\n", cmd->cli, strerror(errno));
        exit(1);

    } else {
        close (stderrP[1]);
        close (stdoutP[1]);

        // create task context
        taskIdT *taskId= taskId= calloc (1, sizeof(taskIdT));
        taskId->magic = MAGIC_SPAWN_TASKID;
        taskId->pid= sonPid;
        taskId->cmd= cmd;
        taskId->outFD= stdoutP[0];
        taskId->errFD= stderrP[0];
        (void)asprintf (&taskId->uid, "%s/%s@%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
        AFB_API_NOTICE (api, "[taskid created] taskId=0x%p action='start' uid='%s' pid=%d (spawnTaskStart)", taskId, taskId->uid, sonPid);

        // create task event
        taskId->event = afb_api_make_event(api, taskId->uid);
        if (!taskId->event) goto OnErrorExit;

        err=afb_req_subscribe(request, taskId->event);
        if (err) {
            afb_req_fail_f (request, "subscribe-error","spawnTaskVerb: fail to subscribe sandbox=%s cmd=%s", cmd->sandbox->uid, cmd->uid);
            goto OnErrorExit;
        }

        // main process register son stdout/err into event mainloop
        if (cmd->timeout > 0) {
            taskId->timer = (TimerHandleT*)calloc (1, sizeof(TimerHandleT));
            taskId->timer->delay = cmd->timeout * 1000; // move to ms
            taskId->timer->count = 1; // run only once
            taskId->timer->uid = taskId->uid;
            TimerEvtStart (api, taskId->timer, spawnTimerCB, (void*)taskId);
        }

        // initilise cmd->cli corresponding output formater buffer
        err= cmd->encoder->actionsCB (taskId, ENCODER_TASK_START, ENCODER_OPS_STD, cmd->encoder->fmtctx);
        if (err) goto OnErrorExit;

        // set pipe fd into noblock mode
        fdFlags = fcntl(taskId->outFD, F_GETFL);
        fdFlags &= ~O_NONBLOCK;
        err= fcntl (taskId->outFD, F_SETFL, fdFlags);
        if (err) goto OnErrorExit;

        fdFlags = fcntl(taskId->errFD, F_GETFL);
        fdFlags &= ~O_NONBLOCK;
        err= fcntl (taskId->errFD, F_SETFL, fdFlags);
        if (err) goto OnErrorExit;

        // register stdout/err piped FD within mainloop
        err=sd_event_add_io(afb_api_get_event_loop(api), &taskId->srcout, taskId->outFD, EPOLLIN|EPOLLHUP, spawnPipeFdCB, taskId);
        if (err) goto OnErrorExit;
        err=sd_event_add_io(afb_api_get_event_loop(api), &taskId->srcerr, taskId->errFD, EPOLLIN|EPOLLHUP, spawnPipeFdCB, taskId);
        if (err) goto OnErrorExit;

        // update command anf binding global tids hashtable
        if (! pthread_rwlock_wrlock(&cmd->sem)) {
            HASH_ADD(tidsHash, cmd->tids, pid, sizeof(int), taskId);
            pthread_rwlock_unlock(&cmd->sem);
        }

        spawnBindingT *binding= cmd->sandbox->binding;
        if (! pthread_rwlock_wrlock(&binding->sem)) {
            HASH_ADD(gtidsHash, binding->gtids, pid, sizeof(int), taskId);
            pthread_rwlock_unlock(&binding->sem);
        }

        // build responseJ & update call tid
        wrap_json_pack (&responseJ, "{ss ss ss si}", "api", api->apiname, "sandbox", taskId->cmd->sandbox->uid, "command", taskId->cmd->uid, "pid", taskId->pid);
        afb_req_success_f(request, responseJ, NULL);
        return 0;

    OnErrorExit:
        spawnFreeTaskId (api, taskId);
        AFB_API_ERROR (api, "spawnTaskStart [Fail to launch] uid=%s cmd=%s pid=%d error=%s", cmd->uid, cmd->cli, sonPid, strerror(errno));
        afb_req_fail_f (request, "start-error", "spawnTaskStart: fail to start sandbox=%s cmd=%s", cmd->sandbox->uid, cmd->uid);

        if (sonPid>0) kill(sonPid, SIGTERM);
        return 1;
    }
} //end spawnTaskStart

static int spawnTaskControl (afb_req_t request, shellCmdT *cmd, taskActionE action, json_object *argsJ) {
    taskIdT *taskId, *tidNext;
    json_object *responseJ;
    int err=0;
    int taskPid=0;
    int signal= SIGINT;
    json_object *signalJ=NULL;

    // if not argument kill all task attache to this cmd->cli
    if (argsJ) {
        err= wrap_json_unpack (argsJ, "{s?i s?o !}", "pid", &taskPid, "signal", &signalJ);
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
    int err;

    if (json_object_is_type (queryJ, json_type_object)) {
        err= wrap_json_unpack(queryJ, "{s?s s?o !}", "action", &action, "args", &argsJ);
        if (err) {
            afb_req_fail_f(request, "query-error", "spawnTaskVerb: invalid 'json' sandbox=%s cmd=%s query=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(queryJ));
            goto OnErrorExit;
        }
    }

    if (!strcasecmp (action, "start")) {
        err = spawnTaskStart (request, cmd, argsJ);
        if (err) goto OnErrorExit;

    } else if (!strcasecmp (action, "stop")) {
        err = spawnTaskControl (request, cmd, SPAWN_ACTION_STOP, argsJ);
        if (err) goto OnErrorExit;

    } else if (!strcasecmp (action, "subscribe")) {
        err = spawnTaskControl (request, cmd, SPAWN_ACTION_SUBSCRIBE, argsJ);
        if (err) goto OnErrorExit;

    } else if (!strcasecmp (action, "unsubscribe")) {
        err = spawnTaskControl (request, cmd,  SPAWN_ACTION_UNSUBSCRIBE, argsJ);
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

int spawnParse (shellCmdT *cmd, json_object *execJ) {
    int err;
    json_object *argsJ=NULL;
    struct stat statbuf;

    err= wrap_json_unpack(execJ, "{ss s?o !}", "cmdpath", &cmd->cli, "args", &argsJ);
    if (err) {
        AFB_API_ERROR(cmd->api, "spawnParse: fail parse cmdpath sandbox=%s cmd=%s exec=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // check if cmdpath exists and is executable
    err= stat(cmd->cli, &statbuf);
    if (err) {
       AFB_API_ERROR(cmd->api, "spawnParse: file not found sandbox=%s cmd=%s cmdpath=%s error=%s", cmd->sandbox->uid, cmd->uid, cmd->cli, strerror(errno));
       goto OnErrorExit;
    }

    cmd->cli= utilsExpandKey(cmd->cli);  // expand env $keys
    if (! (statbuf.st_mode&S_IXUSR)) {
       AFB_API_ERROR(cmd->api, "spawnParse: file not executable sandbox=%s cmd=%s exec=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(execJ));
       goto OnErrorExit;
    }

    // prepare arguments list, they will still need to be expanded before execution
    if (!argsJ) {
        cmd->argc=2;
        cmd->argv=calloc (cmd->argc, sizeof (char*));
        cmd->argv[0]=  utilsExpandKey(cmd->cli);
        if (!cmd->argv[0]) {
            AFB_API_ERROR(cmd->api, "spawnParse: [unknow $ENV key] sandbox=%s cmd=%s cmdpath=%s", cmd->sandbox->uid, cmd->uid, cmd->cli);
            goto OnErrorExit;
        }
    } else {
        switch (json_object_get_type(argsJ)) {
            const char*param;
            case json_type_array:
                cmd->argc= (int)json_object_array_length(argsJ)+2;
                cmd->argv=calloc (cmd->argc, sizeof (char*));
                cmd->argv[0]= cmd->uid;
                for (int idx=1; idx < cmd->argc-1; idx ++) {
                    param= json_object_get_string (json_object_array_get_idx(argsJ, idx-1));
                    cmd->argv[idx] = utilsExpandKey(param);
                    if (!cmd->argv[idx]) {
                        AFB_API_ERROR(cmd->api, "spawnParse: [unknow $ENV key] sandbox=%s cmd=%s args=%s", cmd->sandbox->uid, cmd->uid, param);
                        goto OnErrorExit;
                    }
                }
                break;

            default:
                param=json_object_get_string (argsJ);
                cmd->argc=3;
                cmd->argv=calloc (cmd->argc, sizeof (char*));
                cmd->argv[0]= cmd->uid;
                cmd->argv[1] = utilsExpandKey(param);
                if (!cmd->argv[1]) {
                    AFB_API_ERROR(cmd->api, "spawnParse: [unknow $ENV key] uid=%s cmdpath=%s arg=%s", cmd->uid, cmd->cli, param);
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
    taskIdT *taskId;

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
    int expectPid=-1;
    shellCmdT *cmd;

    // taskId status was already collected
    if (taskId && !taskId->pid) return;

    fprintf (stderr, "**** spawnChildUpdateStatus taskId=0x%p pid=%d\n", taskId, taskId->pid);

    // we known what we're looking for
    if (taskId) expectPid=taskId->pid;

    // retreive every child status as many child may share the same signal
    while ((childPid = waitpid (expectPid, &childStatus, WNOHANG)) > 0) {

        // anonymous childs signal should be check agains global binding taskid
        if (!taskId) taskId= spawnChildGetTaskId (binding, childPid);

        if (!taskId) {
            AFB_API_NOTICE(api, "[sigchild unknown] igoring childPid=%d exit status=%d (spawnChildUpdateStatus)", childPid, childStatus);
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
            wrap_json_pack (&taskId->statusJ, "{si so*}", "exit", WEXITSTATUS(childStatus), "info", taskId->errorJ);
        } else if(WIFSIGNALED (childStatus)) {
            wrap_json_pack (&taskId->statusJ, "{ss so*}", "signal", strsignal(WTERMSIG(childStatus)), "info", taskId->errorJ);
        } else {
            wrap_json_pack (&taskId->statusJ, "{si so*}", "unknown", 255, "info", taskId->errorJ);
        }

        // push final respond to every taskId subscriber
        taskPushFinalResponse (taskId);

    } // end while
}

int spawnChildSignalCB (sd_event_source* source, int fd, uint32_t events, void* context) {
    spawnBindingT *binding= (spawnBindingT*)context;
    assert(binding->magic == MAGIC_SPAWN_BINDING);

    // Ignore termination child signal outside of stdout pipe handup
    AFB_API_NOTICE(binding->api, "[child gtids signal] (spawnChildSignalCB)");
    spawnChildUpdateStatus (binding->api, binding, NULL);

    return 0;
}

// register a callback to monitor sigchild events
int spawnChildMonitor (afb_api_t api, sd_event_io_handler_t callback, spawnBindingT *binding) {
    sigset_t sigMask;

    // init signal mask
    sigemptyset (&sigMask);
    sigaddset (&sigMask, SIGCHLD);

    // create global hashtable semaphore
    if (pthread_rwlock_init (&binding->sem, NULL)) {
        AFB_API_NOTICE(api, "[semaphore init fail] error=%s (spawnChildMonitor)", strerror(errno));
        goto OnErrorExit;
    }

/* Fulup do not monitor global sigchild as we may get a signal both from global and from pipe hangup
    int sigFd;

    sigFd = signalfd (-1, &sigMask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sigFd < 0) {
        AFB_API_NOTICE(api, "[signalfd SIGCHLD fail] error=%s (spawnChildMonitor)", strerror(errno));
        goto OnErrorExit;
    }
    (void)sd_event_add_io(afb_api_get_event_loop(api), NULL, sigFd, EPOLLIN, callback, (void*)binding);
*/

    return 0;

 OnErrorExit:
    return 1;
}