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

#include "spawn-binding.h"
#include "spawn-utils.h"
#include "spawn-enums.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include <ctl-config.h>

#include <errno.h>
#include <systemd/sd-event.h>
#include <pthread.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <seccomp.h>        // high level api
#include <linux/seccomp.h>  // low level api
#include <linux/filter.h>
#include <sys/prctl.h>
#include <assert.h>
#include <strings.h>
#include <stdio.h>

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

static int spawnPipeFdCB (sd_event_source* source, int fd, uint32_t events, void* context) {
    taskIdT *taskId = (taskIdT*) context;
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    shellCmdT *cmd=taskId->cmd;
    int err;

    // if taskId->pid == 0 then FMT_TASK_STOP was already called once
    if (taskId->pid) {

        if (fd == taskId->outfd && events & EPOLLIN) {
            if (taskId->verbose >2) AFB_API_INFO (cmd->api, "spawnPipeFdCB: uid=%s pid=%d [EPOLLIN stdout=%d]", taskId->uid, taskId->pid, taskId->outfd );
            err= cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDOUT, ENCODER_OPS_STD, cmd->encoder->fmtctx);
            if (err) {
                wrap_json_pack (&taskId->responseJ, "{ss so* so*}"
                   , "fail" ,"ENCODER_TASK_STDOUT"
                   , "error" , taskId->errorJ
                   , "status", taskId->statusJ
                );
                taskPushResponse (taskId);
                taskId->errorJ=NULL;
                taskId->statusJ=NULL;
            }

        } else if (fd == taskId->errfd && events & EPOLLIN) {
            if (taskId->verbose >2) AFB_API_INFO (cmd->api, "spawnPipeFdCB: uid=%s pid=%d [EPOLLIN stderr=%d]", taskId->uid, taskId->pid, taskId->outfd );
            err= cmd->encoder->actionsCB (taskId, ENCODER_TASK_STDERR, ENCODER_OPS_STD, cmd->encoder->fmtctx);
            if (err) {
                wrap_json_pack (&taskId->responseJ, "{ss so* so*}"
                   , "fail" , "ENCODER_TASK_STDERR"
                   , "error" , taskId->errorJ
                   , "status", taskId->statusJ
                );
                taskPushResponse (taskId);
                taskId->errorJ=NULL;
                taskId->statusJ=NULL;
            }
        }
        // what ever stdout/err pipe hanghup 1st we close both event sources
        if (events & EPOLLHUP) {
            spawnChildUpdateStatus (cmd->api, cmd->sandbox->binding, taskId);
        }
    }
    return 0;
}

static void childDumpArgv (shellCmdT *cmd, const char **params) {
    int argcount;

    fprintf (stderr, "bwrap ");

    for (argcount=1; params[argcount]; argcount++) {
        fprintf (stderr, "%s ", params[argcount]);
    }
    fprintf (stderr, "\n");
}

// Build Child execv argument list. Argument list is compose of expanded argument from config + namespace specific one.
// Note that argument expansion is done after fork(). Modifiing config RAM structure has no impact on afb-binder/binding
static char* const* childBuildArgv (shellCmdT *cmd, json_object * argsJ, int verbose) {
    const char **params;
    int argcount, argsize;

    // if no argument to expand and not namespace use directly the config argument list.
    if (!argsJ && !cmd->sandbox->namespace) {
        params= cmd->argv;
    } else {

        // total arguments list is namespace+cmd argv
        if (!cmd->sandbox->namespace->argc) argsize= cmd->argc+2;
        else argsize= cmd->argc + cmd->sandbox->namespace->argc+2;

        if (verbose >4) fprintf (stderr, "childBuildArgv: arguments size=%d cmd=%d sandbox=%d\n",argsize,  cmd->argc , cmd->sandbox->namespace->argc);

        // allocate execv arguments value
        params= calloc (argsize, sizeof (char*));  // add NULL terminator and command line name
        argcount=0;
        params[argcount++]= cmd->uid;

        // if namespace exit insert its options before cmd arguments
        if (cmd->sandbox->namespace) {
            for (int idx=1; cmd->sandbox->namespace->argv[idx]; idx++) {
                params[argcount++]= cmd->sandbox->namespace->argv[idx];
                if (verbose > 4) fprintf (stderr, "sbox args[%d] params[%d] config=%s\n", idx, argcount-1, cmd->sandbox->namespace->argv[idx]);
            }
            // add cmd execv command as bwrap 1st parameter
            params[argcount++]=cmd->cli;
        }

        for (int idx=1; cmd->argv[idx]; idx++) {

            // if needed expand arguments replacing all $UPERCASE by json field
            params[argcount++]= utilsExpandJson (cmd->argv[idx], argsJ);
            if (!params[argcount-1]) {
                fprintf (stderr, "[fail expanding] sandbox=%s cmd='%s' config:args='%s' query:args=%s\n", cmd->sandbox->uid, cmd->uid, cmd->argv[idx], json_object_get_string(argsJ));
                exit (1);
            }
            if (verbose > 4) fprintf (stderr, "cmd args[%d] params[%d] config=%s expanded=%s\n", idx, argcount-1, cmd->argv[idx], params[argcount-1]);
        }
        params[argcount]=NULL;
    }
    if (verbose > 2) childDumpArgv (cmd, params);

    return (char* const*)params;
} // end childBuildArgv

int spawnTaskStart (afb_req_t request, shellCmdT *cmd, json_object *argsJ, int verbose) {
    assert(cmd);
    json_object *responseJ;
    pid_t sonPid = -1;
    char* const* params=NULL;
    afb_api_t api= cmd->api;
    int   stdoutP[2];
    int   stderrP[2];
    int   fdFlags, err;
    taskIdT *taskId = NULL;

    // create pipes FD to retreive son stdout/stderr
    if(pipe (stdoutP)<0){
        goto OnErrorExit;
    }
    if(pipe (stderrP)<0){
        goto OnErrorExit;
    }

    // if verbose > 8 try to build argument within main process to enable gdb
    if (verbose > 8) params= childBuildArgv (cmd, argsJ, verbose);

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

        // if we have some fileexec reset them to start
        if (cmd->sandbox->filefds) {
            int *filefds=cmd->sandbox->filefds;
            for (int idx=0; filefds[idx]; idx++) {
                int offset= lseek (filefds[idx], 0, SEEK_SET);
                if (offset <0) {
                    fprintf (stderr, "spawnTaskStart: [fail lseek execfd=%d error=%s\n",filefds[idx], strerror(errno));  
                    exit(1);
                }
            }
        }

        // redirect stdout/stderr on binding pipes
        err= dup2(stdoutP[1], STDOUT_FILENO);
        if (verbose < 3) err=+dup2(stderrP[1], STDERR_FILENO);
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
        if (!params) params= childBuildArgv (cmd, argsJ, verbose);

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
        if (cmd->sandbox->namespace) err= execv(cmd->sandbox->namespace->opts.bwrap, params);
        else err= execv(cmd->cli,params);
        fprintf (stderr, "HOOPS: spawnTaskStart execve return cmd->cli=%s error=%s\n", cmd->cli, strerror(errno));
        exit(1);

    } else {
        close (stderrP[1]);
        close (stdoutP[1]);

        // create task context
        taskId = (taskIdT*) calloc (1, sizeof(taskIdT));
        taskId->magic = MAGIC_SPAWN_TASKID;
        taskId->pid= sonPid;
        taskId->cmd= cmd;
        taskId->verbose= verbose + cmd->verbose;
        taskId->outfd= stdoutP[0];
        taskId->errfd= stderrP[0];
        if(asprintf (&taskId->uid, "%s/%s@%d", cmd->sandbox->uid, cmd->uid, taskId->pid)<0){
            goto OnErrorExit;
        }
        if (verbose) AFB_API_NOTICE (api, "[taskid created] uid='%s' pid=%d (spawnTaskStart)", taskId->uid, sonPid);

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
        fdFlags = fcntl(taskId->outfd, F_GETFL);
        fdFlags &= ~O_NONBLOCK;
        err= fcntl (taskId->outfd, F_SETFL, fdFlags);
        if (err) goto OnErrorExit;

        fdFlags = fcntl(taskId->errfd, F_GETFL);
        fdFlags &= ~O_NONBLOCK;
        err= fcntl (taskId->errfd, F_SETFL, fdFlags);
        if (err) goto OnErrorExit;

        // register stdout/err piped FD within mainloop
        err=sd_event_add_io(afb_api_get_event_loop(api), &taskId->srcout, taskId->outfd, EPOLLIN|EPOLLHUP, spawnPipeFdCB, taskId);
        if (err) goto OnErrorExit;
        err=sd_event_add_io(afb_api_get_event_loop(api), &taskId->srcerr, taskId->errfd, EPOLLIN|EPOLLHUP, spawnPipeFdCB, taskId);
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

