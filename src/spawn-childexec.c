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
#include <pthread.h>
#include <fcntl.h>
#include <seccomp.h>        // high level api
#include <assert.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/epoll.h>

#include <cap-ng.h>

#include <afb/afb-binding.h>
#include <rp-utils/rp-jsonc.h>
#include <afb-helpers4/afb-data-utils.h>
#include <afb-helpers4/afb-req-utils.h>

#include "spawn-binding.h"
#include "spawn-utils.h"
#include "spawn-enums.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include "spawn-expand.h"

#include "spawn-subtask-internal.h"



/************************************************************************/
/* MANAGE TIMEOUTS */
/************************************************************************/

static pthread_mutex_t timeout_mutex = PTHREAD_MUTEX_INITIALIZER;


struct timeout_data
{
	taskIdT *taskId;
	int jobid;
};

static void on_timeout_expired(int signum, void *arg)
{
	struct timeout_data *data = arg;
	pthread_mutex_lock(&timeout_mutex);
	data->jobid = 0;
	if (signum == 0) {
		pid_t pid = 0;
		taskIdT *taskId;
		taskId = data->taskId;
		if (taskId != NULL) {
			data->taskId = NULL;
			taskId->timeout = NULL;
			pid = taskId->pid;
			if (pid != 0) {
				AFB_REQ_NOTICE(taskId->request, "Terminating task uid=%s", taskId->uid);
				taskId->cmd->encoder->actionsCB (taskId, ENCODER_TASK_KILL, ENCODER_OPS_STD, taskId->cmd->encoder->fmtctx);
				kill(-pid, SIGKILL);
			}
		}
	}
	pthread_mutex_unlock(&timeout_mutex);
	free(arg);
}

static int make_timeout_monitor(taskIdT *taskId, int timeout)
{
	struct timeout_data *data = malloc(sizeof *data);
	if (data == NULL)
		return -1;
	pthread_mutex_lock(&timeout_mutex);
	data->taskId = taskId;
	data->jobid = afb_job_post(timeout * 1000, 0, on_timeout_expired, data, NULL);
	if (data->jobid < 0) {
		free(data);
		pthread_mutex_unlock(&timeout_mutex);
		return -1;
	}
	taskId->timeout = data;
	pthread_mutex_unlock(&timeout_mutex);
	return 0;
}

void end_timeout_monitor(taskIdT *taskId)
{
	struct timeout_data *data;
	pthread_mutex_lock(&timeout_mutex);
	data = taskId->timeout;
	taskId->timeout = NULL;
	if (data != NULL && data->jobid)
		afb_job_abort(data->jobid);
	pthread_mutex_unlock(&timeout_mutex);
}


/************************************************************************/
/*  */
/************************************************************************/

static void on_pipe(afb_evfd_t efd, int fd, uint32_t revents, taskIdT *taskId, int out)
{
	shellCmdT *cmd=taskId->cmd;
	int err;

	// if taskId->pid == 0 then FMT_TASK_STOP was already called once
	if (taskId->pid == 0)
		return;


	if (revents & EPOLLIN) {
		if (taskId->verbose >2)
			AFB_REQ_INFO (taskId->request, "uid=%s pid=%d [EPOLLIN std%s=%d]", taskId->uid, taskId->pid, out?"out":"err", fd);
		err = cmd->encoder->actionsCB (taskId, out ? ENCODER_TASK_STDOUT : ENCODER_TASK_STDERR, ENCODER_OPS_STD, cmd->encoder->fmtctx);
		if (err) {
			rp_jsonc_pack (&taskId->responseJ, "{ss so* so*}"
				, "fail" ,"ENCODER_TASK_STDOUT"
				, "error" , taskId->errorJ
				, "status", taskId->statusJ
				);
			taskPushResponse (taskId);
			taskId->errorJ=NULL;
			taskId->statusJ=NULL;
		}
	}

	// what ever stdout/err pipe hanghup 1st we close both event sources
	if (revents & EPOLLHUP) {
		spawnChildUpdateStatus(taskId);
	}
}

static void on_pipe_out(afb_evfd_t efd, int fd, uint32_t revents, void *closure)
{
	taskIdT *taskId = closure;
	on_pipe(efd, fd, revents, taskId, 1);
}

static void on_pipe_err(afb_evfd_t efd, int fd, uint32_t revents, void *closure)
{
	taskIdT *taskId = closure;
	on_pipe(efd, fd, revents, taskId, 0);
}

static void childDumpArgv (shellCmdT *cmd, const char **params)
{
	int argcount;

	if (cmd->sandbox->namespace)
		fprintf(stderr, "child(%s)=> bwrap", cmd->uid);
	else
		fprintf(stderr, "command(%s)=> %s", cmd->uid, cmd->cli);

	for (argcount=1; params[argcount]; argcount++)
		fprintf (stderr, " %s", params[argcount]);
	fprintf (stderr, "\n");
}

// Build Child execv argument list. Argument list is compose of expanded argument from config + namespace specific one.
// Note that argument expansion is done after fork(). Modifiing config RAM structure has no impact on afb-binder/binding
static char* const* childBuildArgv (shellCmdT *cmd, json_object * argsJ, int verbose)
{
    const char **params;
    int argcount, argsize;

    // if no argument to expand and not namespace use directly the config argument list.
    if (!argsJ && !cmd->sandbox->namespace) {
        params= cmd->argv;
    }
    else {

        // total arguments list is namespace+cmd argv
        if (cmd->sandbox->namespace) {
            argsize= cmd->argc + cmd->sandbox->namespace->argc + 2;
            if (verbose >4) fprintf (stderr, "arguments size=%d cmd=%d sandbox=%d\n",argsize,  cmd->argc , cmd->sandbox->namespace->argc);
        } else {
            argsize= cmd->argc+2;
            if (verbose >4) fprintf (stderr, "arguments size=%d cmd=%d\n",argsize,  cmd->argc);
        }

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
            params[argcount++] = utilsExpandJson (cmd->argv[idx], argsJ);
            if (!params[argcount-1]) {
                fprintf (stderr, "[fail expanding] sandbox=%s cmd='%s' config:args='%s' query:args=%s\n", cmd->sandbox->uid, cmd->uid, cmd->argv[idx], json_object_get_string(argsJ));
                exit (1);
            }
            if (verbose > 4) fprintf (stderr, "cmd args[%d] params[%d] config=%s expanded=%s\n", idx, argcount-1, cmd->argv[idx], params[argcount-1]);
        }
        params[argcount]=NULL;
    }
    if (verbose > 2)
    	childDumpArgv (cmd, params);

    return (char* const*)params;
} // end childBuildArgv

static int start_in_parent (afb_req_t request, shellCmdT *cmd, json_object *argsJ, int verbose, pid_t sonPid, int outfd, int errfd) {

    json_object *responseJ;
    afb_api_t api = cmd->sandbox->binding->api;
    int   err;
    taskIdT *taskId = NULL;

        // create task context
        taskId = (taskIdT*) calloc (1, sizeof(taskIdT));
        taskId->magic = MAGIC_SPAWN_TASKID;
        taskId->pid= sonPid;
        taskId->cmd= cmd;
        taskId->verbose= verbose;
        taskId->outfd= outfd;
        taskId->errfd= errfd;
	taskId->request= afb_req_addref(request); // save request for later logging and response
	taskId->synchronous = taskId->cmd->encoder->synchronous;

        if(asprintf (&taskId->uid, "%s/%s@%d", cmd->sandbox->uid, cmd->uid, taskId->pid)<0)
            goto InternalError;

        if (verbose)
            AFB_REQ_INFO (request, "[taskid-created] uid='%s' pid=%d (spawnTaskStart)", taskId->uid, sonPid);

        // create task event
        err = afb_api_new_event(api, taskId->cmd->apiverb, &taskId->event);
        if (err < 0) goto InternalError;

        err = afb_req_subscribe(request, taskId->event);
        if (err) goto InternalError;

        // initilise cmd->cli corresponding output formater buffer
        err= cmd->encoder->actionsCB (taskId, ENCODER_TASK_START, ENCODER_OPS_STD, cmd->encoder->fmtctx);
        if (err) goto InternalError;

        // set pipe fd into noblock mode
        err= fcntl (taskId->outfd, F_SETFL, O_NONBLOCK);
        if (err) goto InternalError;

        err= fcntl (taskId->errfd, F_SETFL, O_NONBLOCK);
        if (err) goto InternalError;

        // register stdout/err piped FD within mainloop
        err = afb_evfd_create(&taskId->srcout, taskId->outfd, EPOLLIN|EPOLLHUP, on_pipe_out, taskId, 0, 1);
        if (err) goto InternalError;
        err = afb_evfd_create(&taskId->srcerr, taskId->errfd, EPOLLIN|EPOLLHUP, on_pipe_err, taskId, 0, 1);
        if (err) goto InternalError;

        // update command and binding global tids hashtable
        if (! pthread_rwlock_wrlock(&cmd->sem)) {
            HASH_ADD(tidsHash, cmd->tids, pid, sizeof(pid_t), taskId);
            pthread_rwlock_unlock(&cmd->sem);
        }

        if (! pthread_rwlock_wrlock(&globtidsem)) {
            HASH_ADD(gtidsHash, globtids, pid, sizeof(pid_t), taskId);
            pthread_rwlock_unlock(&globtidsem);
        }

        // build responseJ & update call tid
        if (!taskId->synchronous) {
            rp_jsonc_pack (&responseJ, "{ss ss* ss si}", "api", afb_api_name(api), "sandbox", taskId->cmd->sandbox->uid, "command", taskId->cmd->uid, "pid", taskId->pid);
            afb_data_t data = afb_data_json_c_hold(responseJ);
            afb_req_reply(taskId->request, 0, 1, &data);
        }

        // main process register son stdout/err into event mainloop
        if (cmd->timeout > 0)
		make_timeout_monitor(taskId, cmd->timeout);

        return 0;

InternalError:
    AFB_REQ_ERROR(request, "spawnTaskStart [Fail-to-launch] uid=%s cmd=%s pid=%d error=%s", cmd->uid, cmd->cli, sonPid, strerror(errno));
    afb_req_reply(request, AFB_ERRNO_INTERNAL_ERROR, 0, NULL);
    kill(-sonPid, SIGTERM);
    spawnFreeTaskId (taskId);
    return 1;
}

static void child_exit(int code)
{
	fflush(stderr);
	fflush(stdout);
	_exit(code);
}

static int start_in_child (shellCmdT *cmd, json_object *argsJ, int verbose, char* const* params)
{
    int   err;


        setpgid(0,0);

        // detach from afb_binder signal handler
        utilsResetSigals();

        int isPrivileged= utilsTaskPrivileged();
        confAclT *acls= cmd->sandbox->acls;
        confCapT *caps= cmd->sandbox->caps;

        if (verbose > 8)
           fprintf (stderr, "**** [child start] sandbox=%s cmd=%s pid=%d\n", cmd->sandbox->uid, cmd->uid, getpid());

        // if we have some fileexec reset them to start
        if (cmd->sandbox->filefds) {
            int *filefds=cmd->sandbox->filefds;
            for (int idx=0; filefds[idx]; idx++) {
                off_t offset= lseek (filefds[idx], 0, SEEK_SET);
                if (offset <0) {
                    fprintf (stderr, "[fail lseek execfd=%d error=%s\n",filefds[idx], strerror(errno));
                    child_exit(1);
                }
            }
        }

        // when privileged set cgroup
        if (isPrivileged  && cmd->sandbox->cgroups) {
            err= utilsFileAddControl (NULL, cmd->sandbox->uid, cmd->sandbox->cgroups->pidgroupFd, "cgroup.procs", "0");
            if (err) {
                fprintf (stderr, "[capabilities is privileged]\n");
                child_exit(1);
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
                                fprintf (stderr, "[capabilities set fail] sandbox=%s cmd=%s caps=%s error=%s\n", cmd->sandbox->uid, cmd->uid, capng_capability_to_name(cap->value),strerror(errno));
                            }
                            break;
                        default:
                            fprintf (stderr, "[capabilities ignored unset=%s]\n", capng_capability_to_name(cap->value));
                            // all capacities where previously removed
                            break;
                    }
                }

                // change uid/gid while keeping capabilities
                err = capng_change_id(acls->uid, acls->gid, CAPNG_DROP_SUPP_GRP | CAPNG_CLEAR_BOUNDING);
                if (err) {
                    fprintf (stderr, "[capabilities set fail] sandbox=%s cmd=%s\n", cmd->sandbox->uid, cmd->uid);
                    child_exit(1);
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
        if (!params)
		params = childBuildArgv (cmd, argsJ, verbose);

        // finish by seccomp syscall filter as potentially this may prevent previous action to appen. (seccomp do not require privilege)
        if (cmd->sandbox->seccomp) {
            // reference https://blog.yadutaf.fr/2014/05/29/introduction-to-seccomp-bpf-linux-syscall-filter/
            confSeccompT *seccomp= cmd->sandbox->seccomp;
            // if we have a seccomp file apply it now otherwise check for individual rules
            if (seccomp->fsock) {
                err= prctl (PR_SET_SECCOMP, SECCOMP_MODE_FILTER, seccomp->fsock);
                if (err) {
                    fprintf (stderr, "[invalid seccomp] sandbox='%s' cmd='%s' seccomp='%s' err=%s\n", cmd->sandbox->uid, cmd->uid, seccomp->rulespath, strerror(errno));
                    child_exit(1);
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
        if (cmd->sandbox->namespace)
            err = execv(cmd->sandbox->namespace->opts.bwrap, params);
        else
            err = execv(cmd->cli, params);

        // not reached upon success
        fprintf (stderr, "HOOPS: spawnTaskStart execve return cmd->cli=%s error=%s\n", cmd->cli, strerror(errno));
	child_exit(1);
	return 1;
}

int spawnTaskStart (afb_req_t request, shellCmdT *cmd, json_object *argsJ, int verbose)
{
    pid_t sonPid = -1;
    char* const* params = NULL;
    int   stdoutP[2];
    int   stderrP[2];
    int   err;
    char* reasonE = "Internal error";


    if (cmd->single) {
	pthread_rwlock_rdlock(&cmd->sem);
        if (HASH_CNT(tidsHash, cmd->tids) > 0) {
            pthread_rwlock_unlock(&cmd->sem);
            reasonE = "Can only spawn one instance of this command";
            AFB_REQ_ERROR (request, "%s", reasonE);
            goto OnErrorExit;
        }
        pthread_rwlock_unlock(&cmd->sem);
    }

    // create pipes FD to retreive son stdout/stderr
    if(pipe (stdoutP) < 0)
        goto OnErrorExit;
    if(pipe (stderrP) < 0)
        goto OnErrorExit2;

    // if verbose > 8 try to build argument within main process to enable gdb
    if (verbose > 8)
	params = childBuildArgv (cmd, argsJ, verbose);

    // fork son process
    sonPid = fork();
    if (sonPid < 0)
	goto OnErrorExit3;

    if (sonPid == 0) {

	// setup input, output and error files
        close (STDIN_FILENO);
        close (stdoutP[0]);
        close (stderrP[0]);
	if (stdoutP[1] != STDOUT_FILENO) {
        	err = dup2(stdoutP[1], STDOUT_FILENO);
                if (err < 0) {
                    fprintf (stderr, "[fail to dup stdout] sandbox=%s cmd=%s\n", cmd->sandbox->uid, cmd->uid);
                    exit (1);
                }
		close(stdoutP[1]);
	}
	if (stderrP[1] != STDERR_FILENO) {
        	err = dup2(stderrP[1], STDERR_FILENO);
                if (err < 0) {
                    fprintf (stderr, "[fail to dup stdout] sandbox=%s cmd=%s\n", cmd->sandbox->uid, cmd->uid);
                    exit (1);
                }
		close(stderrP[1]);
	}

	// run the child
	return start_in_child (cmd, argsJ, verbose, params);
    } else {
	// close unused pipes
        close (stderrP[1]);
        close (stdoutP[1]);
	return start_in_parent (request, cmd, argsJ, verbose, sonPid, stdoutP[0], stderrP[0]);
    }

OnErrorExit3:
    close(stderrP[0]);
    close(stderrP[1]);
OnErrorExit2:
    close(stdoutP[0]);
    close(stdoutP[1]);
OnErrorExit:
    AFB_REQ_ERROR(request, "spawnTaskStart [Fail-to-launch] uid=%s cmd=%s pid=%d reason=%s error=%s", cmd->uid, cmd->cli, sonPid, reasonE, strerror(errno));
    afb_req_reply(request, AFB_ERRNO_INTERNAL_ERROR, 0, NULL);
    return -1;
} //end start_command

