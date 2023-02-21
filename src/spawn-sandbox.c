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

/* references:
 - https://facebookmicrosites.github.io/cgroup2/docs/cpu-controller.html
 - https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html
*/

#define _GNU_SOURCE

#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/filter.h>

// Fulup OpenSuse two seccomp.h one for libseccomp the other one for syscall seccomp !!!
#include <seccomp.h>
#include <linux/seccomp.h>
#include <cap-ng.h>

#include <rp-utils/rp-jsonc.h>

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include "spawn-utils.h"
#include "spawn-expand.h"
#include "spawn-enums.h"


// try to apply ACLs
int sandboxApplyAcls(confAclT *acls, int isPrivileged) {
    int err;

    // no ACls silenty ignore
    if (!acls) return(0);

    if (isPrivileged) {
        err= setgid(acls->gid);
        if (err) {
            fprintf (stderr, "spawnTaskStart: [fail-to-setgid] error=%s\n", strerror(errno));
            goto OnErrorExit;
        }
    }

    if (isPrivileged) {
        err= setuid(acls->uid);
        if (err) {
            fprintf (stderr, "spawnTaskStart: [fail-to-setuid] error=%s\n", strerror(errno));
            goto OnErrorExit;
        }
    }

    if (acls->chdir) {
        err= chdir(acls->chdir);
        if (err) {
            fprintf (stderr, "spawnTaskStart: [fail-to-chdir] error=%s\n", strerror(errno));
            goto OnErrorExit;
        }
    }

    if (acls->umask) {
        (void)utilsUmaskSetGet(acls->umask);
    }

    if (acls->ldpath) {
        setenv("LD_LIBRARY_PATH", acls->ldpath, 1);
    }

    if (acls->path) {
        setenv("PATH", acls->path, 1);
    }
    return (0);

OnErrorExit:
    return 1;
};


static int nsParseOneMount (afb_api_t api, sandBoxT *sandbox, json_object *mountJ, confMountT *mount)  {
    const char *keymode= NULL;
    int err;

    err= rp_jsonc_unpack (mountJ, "{ss s?s s?s !}"
    ,"target", &mount->target
    ,"source", &mount->source
    ,"mode", &keymode
    );
    if (err) goto OnErrorExit;

    // if source is defined try to expand $KEY
    if (mount->source) {
        mount->source= utilsExpandKeyCtx(mount->source, (void*)sandbox);
        if (!mount->source) goto OnErrorExit;
    }

    if (keymode) {
        mount->mode= enumMapValue(mountMode, keymode);
        if (mount->mode <0) goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

static int nsParseOneEnv (afb_api_t api, json_object *envJ, confEnvT *env)  {
    int err;
    const char *keymode= NULL;

    err= rp_jsonc_unpack (envJ, "{ss s?s s?s !}"
    ,"name", &env->key
    ,"value", &env->value
    ,"mode", &keymode
    );
    if (err) goto OnErrorExit;

        if (keymode) {
        env->mode= enumMapValue(envMode, keymode);
        if (env->mode <0) goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

// reference https://github.com/stevegrubb/libcap-ng
static int nsParseOneCap (afb_api_t api, sandBoxT *sandbox, json_object *capJ, confCapT *cap)  {
    int err;
    const char *capflag= NULL;
    const char *capName= NULL;

    err= rp_jsonc_unpack (capJ, "{ss ss !}"
    ,"cap",  &capName
    ,"mode", &capflag
    );
    if (err) {
        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' cap='%s'", sandbox->uid, json_object_to_json_string(capJ));
        goto OnErrorExit;
    }

    cap->mode= enumMapValue(capMode, capflag);
    if (cap->mode <0) {
        AFB_API_ERROR(api, "[capability-invalid-mode] sandbox='%s' cap='%s[%s]'", sandbox->uid, capName, capflag);
        goto OnErrorExit;
    }

    cap->value = enumMapValue(capLabel, capName);
    if (cap->value < 0) {
        AFB_API_ERROR(api, "[capability-value-unknown] sandbox='%s' cap='%s[%s]'", sandbox->uid,  capName, capflag);
        goto OnErrorExit;
    }

    // check if capability is avaliable for current process
    if (cap->mode == NS_CAP_SET && !capng_have_capability (CAPNG_PERMITTED, cap->value)  && !utilsTaskPrivileged()) {
        AFB_API_NOTICE(api, "[capability-ignored] sandbox='%s' capability='%s[%s]' (sandboxParseOneCap)", sandbox->uid, capName, capflag);
        cap->mode= NS_CAP_UNKNOWN;
    }

    return 0;

OnErrorExit:
    return 1;
} // end onecap

static int nsParseOneSecRule (afb_api_t api, sandBoxT *sandbox, json_object *ruleJ, confSecRuleT *rule)  {
    int err;
    const char *action= NULL;
    const char *syscall= NULL;

    err= rp_jsonc_unpack (ruleJ, "{ss ss !}"
    ,"action", &action
    ,"syscall",&syscall
    );
    if (err) {
        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' rule='%s'", sandbox->uid, json_object_to_json_string(ruleJ));
        goto OnErrorExit;
    }

    rule->syscall= seccomp_syscall_resolve_name(syscall);
    if (rule->syscall <0) {
        AFB_API_ERROR(api, "[scmp-invalid-syscall] sandbox='%s' rule='%s[%s]'", sandbox->uid, action, syscall);
        goto OnErrorExit;
    }

    rule->action = enumMapValue(nsScmpAction, action);
    if (rule->action < 0) {
        AFB_API_ERROR(api, "[scmp-value-unknown] sandbox='%s' rule='%s[%s]'", sandbox->uid,  action, syscall);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return 1;
} // end nsParseOneSecRule


confCgroupT *sandboxParseCgroups (afb_api_t api, sandBoxT *sandbox, json_object *cgroupsJ) {
    json_object *cgMemJ=NULL, *cgCpuJ=NULL, *cgIoJ=NULL;
    confCgroupT *cgroups = calloc(1, sizeof(confCgroupT));
    int err, cgRootFd, subgroupFd;
    json_object_get(cgroupsJ);
    const char *cgCset, *mntpath=CGROUPS_MOUNT_POINT;

    // uppack cgroup namespace configuration
    err= rp_jsonc_unpack(cgroupsJ, "{s?s s?s s?o s?o s?o !}"
    ,"mount", &mntpath
    ,"cset",&cgCset
    ,"mem", &cgMemJ
    ,"cpu", &cgCpuJ
    ,"io" , &cgIoJ
    );
    if (err) {
        AFB_API_ERROR(api, "[cgroups-parsing-fail] sandbox='%s' cgroups='%s'", sandbox->uid, json_object_to_json_string(cgroupsJ));
        goto OnErrorExit;
    }

    // try to create a cgroup node
    cgRootFd= open(mntpath, O_DIRECTORY);
    if (cgRootFd <= 0) {
        AFB_API_ERROR(api, "[cgroups-not-found] sandbox='%s' cgroups='%s' error=%s", sandbox->uid, CGROUPS_MOUNT_POINT, strerror(errno));
        goto OnErrorExit;
    }

    // if subgroup does not exit create one within system root cgroup
    subgroupFd= openat(cgRootFd, sandbox->uid, O_DIRECTORY);
    if (subgroupFd <0) {
        err = mkdirat(cgRootFd, sandbox->uid, 0);
        if (err) {
            AFB_API_ERROR(api, "[cgroups-create-fail] sandbox='%s' cgroups='%s/%s' error=%s", sandbox->uid, CGROUPS_MOUNT_POINT, sandbox->uid, strerror(errno));
            goto OnErrorExit;
        }

        // open newly create subgroup to activate group values
        subgroupFd= openat(cgRootFd, sandbox->uid, O_DIRECTORY);
        if (subgroupFd <0) {
            AFB_API_ERROR(api, "[cgroups-open-fail] sandbox='%s' cgroups='%s/%s' error=%s", sandbox->uid, CGROUPS_MOUNT_POINT, sandbox->uid, strerror(errno));
            goto OnErrorExit;
        }
    }

    if (cgCset) {
        (void) utilsFileAddControl (api, sandbox->uid, cgRootFd, "cgroup.subtree_control", "+cpuset");
        err= utilsFileAddControl (api, sandbox->uid, subgroupFd, "cgroup.subtree_control", "+cpuset");
        err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "cpuset.cpus", cgCset);
        if (err) goto OnErrorExit;

    }

    if (cgCpuJ) {
        const char* cpuWeight=NULL, *cpuMax=NULL;
        err= rp_jsonc_unpack(cgCpuJ, "{s?s s?s!}"
            ,"weight", &cpuWeight
            ,"max"   , &cpuMax
        );
        if (err) {
            AFB_API_ERROR(api, "[cgroups-cpu-parsing-fail] sandbox='%s' cgroup.cpu='%s'", sandbox->uid, json_object_to_json_string(cgCpuJ));
            goto OnErrorExit;
        }

        // https://facebookmicrosites.github.io/cgroup2/docs/cpu-controller.html
        (void) utilsFileAddControl (api, sandbox->uid, cgRootFd, "cgroup.subtree_control", "+cpu");
        err= utilsFileAddControl (api, sandbox->uid, subgroupFd, "cgroup.subtree_control", "+cpu");
        if (cpuWeight)  err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "cpu.weight", cpuWeight);
        if (cpuMax)  err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "cpu.max", cpuMax);
        if (err) goto OnErrorExit;

    }

    if (cgMemJ) {
        const char* memMin=NULL, *memMax=NULL, *memSwap=NULL, *memHight=NULL;
        err= rp_jsonc_unpack(cgMemJ, "{s?s s?s s?s s?s !}"
            ,"min" , &memMin
            ,"max" , &memMax
            ,"hight",&memHight
            ,"swap", &memSwap
        );
        if (err) {
            AFB_API_ERROR(api, "[cgroups-mem-parsing-fail] sandbox='%s' cgroup.mem='%s'", sandbox->uid, json_object_to_json_string(cgMemJ));
            goto OnErrorExit;
        }
        // https://facebookmicrosites.github.io/cgroup2/docs/cpu-controller.html
        (void)utilsFileAddControl (api, sandbox->uid, cgRootFd, "cgroup.subtree_control", "+memory");
        err= utilsFileAddControl (api, sandbox->uid, subgroupFd, "cgroup.subtree_control", "+memory");
        if (memMin)   err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "memory.min", memMin);
        if (memMax)   err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "memory.max", memMax);
        if (memHight) err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "memory.high", memHight);
        if (memSwap)  err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "memory.swap.max", memSwap);
        if (err) goto OnErrorExit;
    }

    if (cgIoJ) {
        if (! json_object_is_type (cgIoJ, json_type_string)) {
            AFB_API_ERROR(api, "[cgroups-io-parsing-fail] sandbox='%s' cgroup.io='%s'", sandbox->uid, json_object_to_json_string(cgIoJ));
            goto OnErrorExit;
        }

        (void) utilsFileAddControl (api, sandbox->uid, cgRootFd, "cgroup.subtree_control", "+io");
        err= utilsFileAddControl (api, sandbox->uid, subgroupFd, "cgroup.subtree_control", "+io");
        err =+ utilsFileAddControl (api, sandbox->uid, subgroupFd, "io.max", json_object_get_string (cgIoJ));
        if (err) goto OnErrorExit;
    }

    close(cgRootFd);

      // create an empty cgroup controller to host sonpids reference: https://lwn.net/Articles/679786/
    cgroups->pidgroupFd = openat(subgroupFd, "taskid", O_DIRECTORY);
    if (cgroups->pidgroupFd <0) {
        err = mkdirat(subgroupFd, "taskid", 0);
        if (err) {
            AFB_API_ERROR(api, "[cgroups-create-fail] sandbox='%s' cgroups='%s/%s/taskid' error=%s", sandbox->uid, CGROUPS_MOUNT_POINT, sandbox->uid, strerror(errno));
            goto OnErrorExit;
        }

        // open newly create subgroup to activate group values
        cgroups->pidgroupFd= openat(subgroupFd, "taskid", O_DIRECTORY);
        if (cgroups->pidgroupFd <0) {
            AFB_API_ERROR(api, "[cgroups-open-fail] sandbox='%s' cgroups='%s/%s/taskid' error=%s", sandbox->uid, CGROUPS_MOUNT_POINT, sandbox->uid, strerror(errno));
            goto OnErrorExit;
        }
    }

    // default needed capabilities
    (void) utilsFileAddControl (api, sandbox->uid, subgroupFd, "cgroup.subtree_control", "+pids");

    return cgroups;

OnErrorExit:
    return NULL;
} // end cgroupsJ


confCapT *sandboxParseCaps (afb_api_t api, sandBoxT *sandbox, json_object *capsJ) {
    confCapT *caps;
    int err, count;
    json_object_get(capsJ);

    switch (json_object_get_type (capsJ)) {
        case json_type_array:
            count= (int)json_object_array_length(capsJ);
            caps = calloc (count+1, sizeof(confCapT));

            for (int idx=0; idx < count; idx ++) {
                json_object *capJ= json_object_array_get_idx(capsJ, idx);
                err= nsParseOneCap (api, sandbox, capJ, &caps[idx]);
                if (err) goto OnErrorExit;
            }
            break;

        case json_type_object:
            caps = calloc (2, sizeof(confCapT));
            err= nsParseOneCap (api, sandbox, capsJ, &caps[0]);
            if (err) goto OnErrorExit;
            break;

        default:
            AFB_API_ERROR(api, "[parsing-error] group sandbox='%s' cap='%s'", sandbox->uid, json_object_to_json_string(capsJ));
            goto OnErrorExit;
    }

    return caps;

OnErrorExit:
    return NULL;
} // end capsJ

confEnvT *sandboxParseEnvs (afb_api_t api, sandBoxT *sandbox, json_object *envsJ) {
    confEnvT *envs=NULL;
    int err, count;
    json_object_get(envsJ);

    switch (json_object_get_type (envsJ)) {
        case json_type_array:
            count= (int)json_object_array_length (envsJ);
            envs = calloc (count+1, sizeof(confEnvT));
            for (int idx=0; idx < count; idx ++) {
                json_object *envJ= json_object_array_get_idx(envsJ, idx);
                err= nsParseOneEnv (api, envJ, &envs[idx]);
                if (err) {
                    AFB_API_ERROR(api, "[parsing-error] sandbox='%s' setenv='%s'", sandbox->uid, json_object_to_json_string(envJ));
                    goto OnErrorExit;
                }
            }
            break;

        case json_type_object:
            envs = calloc (2, sizeof(confEnvT));
            err= nsParseOneEnv (api, envsJ, &envs[0]);
            if (err) {
                AFB_API_ERROR(api, "[parsing-error] sandbox='%s' setenv='%s'", sandbox->uid, json_object_to_json_string(envsJ));
                goto OnErrorExit;
            }
            break;

        default:
            AFB_API_ERROR(api, "[parsing-error] group sandbox='%s' setenv='%s'", sandbox->uid, json_object_to_json_string(envsJ));
            goto OnErrorExit;
    }
    return envs;

OnErrorExit:
    if (envs) free(envs);
    return NULL;
} // end envsJ

confAclT *sandboxParseAcls(afb_api_t api, sandBoxT *sandbox, json_object *aclsJ) {
    confAclT *acls= calloc (1, sizeof(confAclT));
    json_object *uidJ=NULL, *gidJ=NULL;
    const char *runMod=NULL;
    int err;
    json_object_get(aclsJ);

    err = rp_jsonc_unpack(aclsJ, "{s?s s?o s?o s?i s?s s?s s?s s?s !}"
        ,"umask" , &acls->umask
        ,"user", &uidJ
        ,"group" , &gidJ
        ,"timeout", &acls->timeout
        ,"runmod", &runMod
        ,"path" , &acls->path
        ,"ldpath" , &acls->ldpath
        ,"chdir" , &acls->chdir
        );
    if (err) {
        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' acls='%s'", sandbox->uid, json_object_to_json_string(aclsJ));
        goto OnErrorExit;
    }

    if (runMod) {
        acls->runmod= enumMapValue(nsRunmodMode,runMod);
        if (acls->runmod < 0) {
            AFB_API_ERROR(api, "[runmod-error] sandbox='%s' should be [default,admin,user] acls->runmod='%s'", sandbox->uid, runMod);
            goto OnErrorExit;
        }
    }

    if (uidJ) {
        struct passwd *user;

        switch (json_object_get_type(uidJ)) {

            case json_type_int:
                acls->uid=json_object_get_int(uidJ);
                break;

            case json_type_string:
                user= getpwnam(json_object_get_string(uidJ));
                if (!user) {
                    AFB_API_ERROR(api, "[user-not-found] sandbox='%s' user='%s' missing", sandbox->uid, json_object_get_string(uidJ));
                    goto OnErrorExit;
                }
                acls->uid= user->pw_uid;
                break;
        default:
            AFB_API_ERROR(api, "[invalid-format] sandbox='%s' user='%s' should be string|integer", sandbox->uid, json_object_get_string(uidJ));
            goto OnErrorExit;

        }
    } else {
        AFB_API_ERROR(api, "[security-error] ### privileged mode sandbox='%s' requirer users=xxx ### acls='%s'", sandbox->uid, json_object_to_json_string(aclsJ));
        goto OnErrorExit;
    }

    if (gidJ) {
        struct group *group;

        switch (json_object_get_type(gidJ)) {

            case json_type_int:
                acls->gid=json_object_get_int(gidJ);
                break;

            case json_type_string:
                group= getgrnam(json_object_get_string(gidJ));
                if (!group) {
                    AFB_API_ERROR(api, "[group-not-found] sandbox='%s' group='%s' missing", sandbox->uid, json_object_get_string(uidJ));
                    goto OnErrorExit;
                }
                acls->gid= group->gr_gid;
                break;
        default:
            AFB_API_ERROR(api, "[invalid-format] sandbox='%s' should be string|integer group='%s'", sandbox->uid, json_object_get_string(gidJ));
            goto OnErrorExit;

        }
    }

        // check seteuid privilege with capabilities
    if (utilsTaskPrivileged()) {
        if (acls->runmod == RUNM_USER) {
            AFB_API_ERROR(api, "[privilege-mode-forbiden] sandbox='%s' should be 'admin' acls->mod=%s euid=%d", sandbox->uid, runMod, geteuid());
            goto OnErrorExit;
        }
        if (!uidJ || !gidJ) {
            AFB_API_ERROR(api, "[require-user/group-acls] sandbox='%s' running with privileges require acls with user=xxx group=xxx acls=%s", sandbox->uid, json_object_to_json_string(aclsJ));
            goto OnErrorExit;
        }
    } else {
        if (acls->runmod == RUNM_ADMIN) {
            AFB_API_ERROR(api, "[user-mode-forbiden] sandbox='%s' should be 'admin' acls->mod=%s euid=%d", sandbox->uid, runMod, geteuid());
            goto OnErrorExit;
        }
        if (uidJ || gidJ) {
            AFB_API_NOTICE(api, "[ignoring-user/group-acls] sandbox='%s' no uid/gid privileges ignoring user='%s' group='%s'", sandbox->uid, json_object_to_json_string(uidJ),  json_object_to_json_string(gidJ));
            acls->uid=0;
            acls->gid=0;
        }
    }
    return acls;

OnErrorExit:
    free(acls);
    return NULL;

} // end alcsJ

confSeccompT *sandboxParseSecRules(afb_api_t api, sandBoxT *sandbox, json_object *seccompJ) {
    confSeccompT *seccomp= calloc (1, sizeof(confSeccompT));
    json_object *rulesJ=NULL;
    int err;
    json_object_get(seccompJ);
    const char *dfltAction=NULL;

    err = rp_jsonc_unpack(seccompJ, "{s?s s?s s?b s?o!}"
        ,"default", &dfltAction
        ,"rulespath" , &seccomp->rulespath
        ,"locked" , &seccomp->locked
        ,"rules", rulesJ
        );
    if (err) {
        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' seccomp='%s'", sandbox->uid, json_object_to_json_string(seccompJ));
        goto OnErrorExit;
    }

    if (seccomp->rulespath && rulesJ) {
        AFB_API_ERROR(api, "[rulepath/rules-exclusive] sandbox='%s' seccomp='%s'", sandbox->uid, json_object_to_json_string(seccompJ));
        goto OnErrorExit;

    }

    if (!dfltAction) seccomp->dflt=SCMP_ACT_KILL;
    else {
        seccomp->dflt= enumMapValue(nsScmpAction, dfltAction);
        if (seccomp->dflt <0) {
            AFB_API_ERROR(api, "[unknown-default-action] sandbox='%s' action='%s'", sandbox->uid, dfltAction);
            goto OnErrorExit;
        }
    }

    if (seccomp->rulespath) {
        const char*rulespath= utilsExpandKeyCtx(seccomp->rulespath, (void*)sandbox); // expand keys
        if (!rulespath) {
            AFB_API_ERROR(api, "[expand-fail-$ENV-key] sandbox='%s' rulepath='%s'", sandbox->uid, seccomp->rulespath);
            goto OnErrorExit;
        }
        struct sock_fprog *fsock=calloc(1, sizeof(struct sock_fprog));
        char *buffer;
        ssize_t count= utilsFileLoad(rulespath, &buffer);

        fsock->len= (short unsigned int)count/8;
        fsock->filter=(struct sock_filter *)buffer;
        seccomp->fsock= fsock;
        seccomp->rulespath=rulespath;
    } else if (rulesJ) {
        int count;
        switch (json_object_get_type (rulesJ)) {
            case json_type_array:
                count= (int)json_object_array_length (rulesJ);
                seccomp->rules = calloc (count+1, sizeof(confEnvT));

                for (int idx=0; idx < count; idx ++) {
                    json_object *ruleJ= json_object_array_get_idx(rulesJ, idx);
                    err= nsParseOneSecRule (api, sandbox, ruleJ, &seccomp->rules[idx]);
                    if (err) {
                        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' rules='%s'", sandbox->uid, json_object_to_json_string(ruleJ));
                        goto OnErrorExit;
                    }
                }
                break;

            case json_type_object:
                seccomp->rules = calloc (2, sizeof(confEnvT));
                err= nsParseOneSecRule (api, sandbox, rulesJ, &seccomp->rules[0]);
                if (err) {
                    AFB_API_ERROR(api, "[parsing-error] sandbox='%s' rules='%s'", sandbox->uid, json_object_to_json_string(rulesJ));
                    goto OnErrorExit;
                }
                break;

            default:
                AFB_API_ERROR(api, "[parsing-error] group sandbox='%s' rules='%s'", sandbox->uid, json_object_to_json_string(rulesJ));
                goto OnErrorExit;
        }
    }
    return seccomp;

OnErrorExit:
    free(seccomp);
    return NULL;
} // end seccomp}

confNamespaceT *sandboxParseNamespace (afb_api_t api, sandBoxT *sandbox, json_object *namespaceJ) {
    json_object * mountsJ=NULL, *optsJ=NULL, *sharesJ=NULL;
    confNamespaceT *namespace=calloc(1, sizeof(confNamespaceT));
    namespace->magic= MAGIC_SPAWN_NSPACE;
    int err, count;


    // parse namespace and lock json object with 'O'
    err = rp_jsonc_unpack(namespaceJ, "{s?o s?o s?o !}"
        ,"opts"   , &optsJ
        ,"mounts" , &mountsJ
        ,"shares" , &sharesJ
        );
    if (err) {
        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' namespace='%s'", sandbox->uid, json_object_to_json_string(namespaceJ));
        goto OnErrorExit;
    }

    if (optsJ) {
        err = rp_jsonc_unpack(optsJ, "{s?s s?s s?b s?b !}"
            ,"hostname"  , &namespace->opts.hostname
            ,"bwrap"     , &namespace->opts.bwrap
            ,"selinux"     , &namespace->opts.selinux
            ,"autocreate", &namespace->opts.autocreate
        );
        if (err) {
            AFB_API_ERROR(api, "[parsing-error] sandbox='%s' opts='%s'", sandbox->uid, json_object_to_json_string(optsJ));
            goto OnErrorExit;
        }
    } // end optsJ

    // provide default value for bwrap cli and assert it is executable
    if (!namespace->opts.bwrap) namespace->opts.bwrap= BWRAP_EXE_PATH;
    if (!utilsFileModeIs(namespace->opts.bwrap, S_IXUSR)) {
        AFB_API_ERROR(api, "[bwrap not executable] sandbox='%s' bwrap='%s'", sandbox->uid, namespace->opts.bwrap);
        goto OnErrorExit;
    }

    if (mountsJ) {
        switch (json_object_get_type (mountsJ)) {
            case json_type_array:
                count= (int)json_object_array_length(mountsJ);
                namespace->mounts = calloc (count+1, sizeof(confMountT));

                for (int idx=0; idx < count; idx ++) {
                    json_object *mountJ= json_object_array_get_idx(mountsJ, idx);
                    err= nsParseOneMount (api, sandbox, mountJ, &namespace->mounts[idx]);
                    if (err) {
                        AFB_API_ERROR(api, "[parsing-error] sandbox='%s' mounts='%s'", sandbox->uid, json_object_to_json_string(mountJ));
                        goto OnErrorExit;
                    }
                }
                break;

            case json_type_object:
                namespace->mounts = calloc (2, sizeof(confMountT));
                err= nsParseOneMount (api, sandbox, mountsJ, &namespace->mounts[0]);
                if (err) {
                    AFB_API_ERROR(api, "[parsing-error] sandbox='%s' mounts='%s'", sandbox->uid, json_object_to_json_string(mountsJ));
                    goto OnErrorExit;
                }
                break;

            default:
                AFB_API_ERROR(api, "[parsing-error] sandbox='%s' mounts='%s'", sandbox->uid, json_object_to_json_string(mountsJ));
                goto OnErrorExit;
        }
    } // end mountJ

    if (sharesJ) {
        namespace->shares = calloc (1, sizeof (confNamespaceTagsT));
        const char *shareall=NULL, *shareuser=NULL, *sharecgroup=NULL, *sharenet=NULL, *shareipc=NULL;

        err = rp_jsonc_unpack(sharesJ, "{s?s s?s s?s s?s s?s !}"
            ,"all" , &shareall
            ,"users", &shareuser
            ,"cgroups" , &sharecgroup
            ,"net" , &sharenet
            ,"ipc" , &shareipc
            );
        if (err) {
            AFB_API_ERROR(api, "[parsing-share] sandbox=%s share='%s' invalid json", sandbox->uid, json_object_to_json_string(sharesJ));
            goto OnErrorExit;
        }

        namespace->shares->all = enumMapValue(nsShareMode, shareall);
        if (namespace->shares->all < 0) {
            AFB_API_ERROR(api, "[share-flag-unknow] sandbox=%s shareall='%s'", sandbox->uid, shareall);
            goto OnErrorExit;
        }
        namespace->shares->user= enumMapValue(nsShareMode, shareuser);
        if (namespace->shares->user < 0) {
            AFB_API_ERROR(api, "[share-flag-unknow] sandbox=%s shareuser='%s'", sandbox->uid, shareuser);
            goto OnErrorExit;
        }
        namespace->shares->cgroup = enumMapValue(nsShareMode, sharecgroup);
        if (namespace->shares->cgroup < 0) {
            AFB_API_ERROR(api, "[share-flag-unknow] sandbox=%s sharecgroup='%s'", sandbox->uid, sharecgroup);
            goto OnErrorExit;
        }
        namespace->shares->net = enumMapValue(nsShareMode, sharenet);
        if (namespace->shares->net < 0) {
            AFB_API_ERROR(api, "[share-flag-unknow] sandbox=%s sharenet='%s'", sandbox->uid, sharenet);
            goto OnErrorExit;
        }
        namespace->shares->ipc = enumMapValue(nsShareMode, shareipc);
        if (namespace->shares->ipc < 0) {
            AFB_API_ERROR(api, "[share-flag-unknow] sandbox=%s shareipc='%s'", sandbox->uid, shareipc);
            goto OnErrorExit;
        }

        // pretest namespace config and retreive number of used arguments.
        namespace->argv = sandboxBwrapArg(api, sandbox, namespace);
        if (!namespace->argv) {
            AFB_API_ERROR(api, "[parsing-mounts] sandbox=%s namespace='%s' invalid config", sandbox->uid, json_object_to_json_string(mountsJ));
            goto OnErrorExit;
        }

    } // end shareJ

    return namespace;

OnErrorExit:
    if (namespace) free(namespace);
    return NULL;
}

static const char *sandboxSetShare (nsShareFlagE mode, const char * enable, char *disable) {
    switch (mode) {
        case NS_SHARE_ENABLE:
            return enable;
            break;

        case NS_SHARE_DISABLE:
            return disable;
            break;
        default:
            break;
    }
    return NULL;
}

// build bwrap argv argument list
const char **sandboxBwrapArg (afb_api_t api, sandBoxT *sandbox, confNamespaceT *namespace) {
    const char **argval= NULL;
    int *filefds=NULL;
    int argcount=0, fdcount=0;
    int err;

    assert(namespace);
    confNamespaceTagsT *shares= namespace->shares;
    confMountT *mounts = namespace->mounts;

    argval= calloc(BWRAP_ARGC_MAX, sizeof(char*));

    // set default process name to command sandbox->uid
    argval[argcount++]= namespace->opts.bwrap;

    // mandatory settings
    argval[argcount++]="--die-with-parent"; // kill bweap if binder dial
    argval[argcount++]="--new-session"; // disconnect from ttys

    if (namespace->opts.selinux) {
        argval[argcount++]="--exec-label"; argval[argcount++]=sandbox->uid; // used as seLinux Label
    }

    if (namespace->opts.hostname) {
        argval[argcount++]="--unshare-uts";
        argval[argcount++]="--hostname";
        argval[argcount++]= namespace->opts.hostname;
    }

    if (shares->all   != NS_SHARE_DEFAULT) argval[argcount++]= sandboxSetShare (shares->all, "--share-all", "--unshare-all");
    if (shares->user  != NS_SHARE_DEFAULT) argval[argcount++]= sandboxSetShare (shares->user, "--share-user", "--unshare-user");
    if (shares->cgroup!= NS_SHARE_DEFAULT) argval[argcount++]= sandboxSetShare (shares->cgroup, "--share-cgroup", "--unshare-cgroup");
    if (shares->ipc   != NS_SHARE_DEFAULT) argval[argcount++]= sandboxSetShare (shares->ipc, "--share-ipc", "--unshare-ipc");
    if (shares->pid   != NS_SHARE_DEFAULT) argval[argcount++]= sandboxSetShare (shares->pid, "--share-pid", "--unshare-pid");
    if (shares->net   != NS_SHARE_DEFAULT) argval[argcount++]= sandboxSetShare (shares->net, "--share-net", "--unshare-net");

    // apply mounts
    for (int idx=0; mounts[idx].target; idx++) {
        nsMountFlagE mode= mounts[idx].mode;
        const char* source= mounts[idx].source;
        const char* target=mounts[idx].target;
        struct stat status;

        // if target not defined use mount source path as default
        if (!source) source=target;

        // check source path exit
        switch (mode) {
            case  NS_MOUNT_RO:
            case  NS_MOUNT_RW:
            //case  NS_MOUNT_SYMLINK:
                // check if mounting path exist, if not try to create it now
                err = stat(source, &status);
                if (err && namespace->opts.autocreate) {
                    // mkdir ACL do not work as documented, need chmod to install proper acl
                    mode_t mask=utilsUmaskSetGet(NULL);
               		mask= 07777 & ~mask;
                    mkdir(source, 0);
                    chmod(source, mask);
                    err = stat(source, &status);
                }
                if (err < 0) {
                    AFB_API_ERROR(api, "[mount-path-invalid] sandbox=%s source=%s not accessible", sandbox->uid, source);
                    goto OnErrorExit;
                }
                break;
        default:
            break;
        }

        switch (mode) {

        case NS_MOUNT_RW:
            argval[argcount++]="--bind";
            argval[argcount++]=source;
            argval[argcount++]=target;
            break;

        case NS_MOUNT_RO:
            argval[argcount++]="--ro-bind";
            argval[argcount++]=source;
            argval[argcount++]=target;
            break;

        case NS_MOUNT_SYMLINK:
            argval[argcount++]="--symlink";
            argval[argcount++]=source;
            argval[argcount++]=target;
            break;

        case NS_MOUNT_EXECFD:
            if (!filefds) filefds= calloc (BWRAP_ARGC_MAX, sizeof(int));
            if (fdcount == BWRAP_ARGC_MAX) {
                AFB_API_ERROR(api, "[execfd-too-many] sandbox=%s source=%s execfd count >%d", sandbox->uid, source, BWRAP_ARGC_MAX);
                goto OnErrorExit;
            }
            argval[argcount++]="--file";
            argval[argcount++]=utilsExecCmd (api, target, source, &filefds[fdcount]);
            if (filefds[fdcount++] <0) goto OnErrorExit;
            argval[argcount++]=target;
            break;

        case NS_MOUNT_DIR:
            argval[argcount++]="--dir";
            argval[argcount++]=target;
            break;

        case NS_MOUNT_TMPFS:
            argval[argcount++]="--tmpfs";
            argval[argcount++]=target;
            break;

        case NS_MOUNT_DEVFS:
            argval[argcount++]="--dev";
            argval[argcount++]=target;
            break;

        case NS_MOUNT_PROCFS:
            argval[argcount++]="--proc";
            argval[argcount++]= target;
            break;

        case NS_MOUNT_MQUEFS:
            argval[argcount++]="--mqueue";
            argval[argcount++]= target;
            break;

        case NS_MOUNT_LOCK:
            argval[argcount++]="--lock-file";
            argval[argcount++]= target;
            break;

        default:
            break;
        }
    }
    argval[argcount++]=NULL; // close argument list
    namespace->argc=argcount; // store effective bwrap arguments count
    sandbox->filefds= realloc (filefds , fdcount*sizeof(int)); // save some ram
    argval=  realloc(argval, argcount*sizeof(char*));
    return (argval);

OnErrorExit:
if (argval) free (argval);
return NULL;
}
