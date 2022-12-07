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


#ifndef _SPAWN_SANDBOX_INCLUDE_
#define _SPAWN_SANDBOX_INCLUDE_

#include "spawn-binding.h"
#include "spawn-enums.h"
#include "spawn-encoders.h"

typedef struct {
    uid_t uid;
    gid_t gid;
    nsRunmodFlagE runmod;
    const int timeout;
    const char *umask;
    const char *path;
    const char *ldpath;
    const char *chdir;
} confAclT;

typedef struct {
    const char *source;
    const char *target;
    nsMountFlagE mode;
} confMountT;

typedef struct {
    const char *key;
    const char *value;
    nsEnvFlagE mode;
} confEnvT;

typedef struct {
    int value;
    nsCapFlagE mode;
} confCapT;

typedef struct {
    int pidgroupFd;
} confCgroupT;

typedef struct {
    const char *bwrap;
    const char *hostname;
    int autocreate;
    int selinux;
} nsNamespaceOptsT;

typedef struct {
    int action;
    int syscall;
} confSecRuleT;

typedef struct {
    int dflt;
    int locked;
    const char *rulespath;
    struct sock_fprog *fsock;
    confSecRuleT *rules;
} confSeccompT;

// name space global config
typedef struct {
    nsShareFlagE all;
    nsShareFlagE user;
    nsShareFlagE cgroup;
    nsShareFlagE ipc;
    nsShareFlagE pid;
    nsShareFlagE net;
} confNamespaceTagsT;

typedef struct {
    spawnMagicT magic;
    const char **argv;
    int argc;
    int secompFD;
    nsNamespaceOptsT opts;
    confNamespaceTagsT*shares;
    confEnvT *envs;
    confMountT *mounts;
} confNamespaceT;

struct sandBoxS {
  spawnMagicT magic;
  const char *uid;
  const char *info;
  int verbose;
  const char *privilege;
  const char *prefix;
  confAclT *acls;
  confCapT *caps;
  confEnvT *envs;
  int *filefds;
  confCgroupT *cgroups;
  confSeccompT *seccomp;
  confNamespaceT *namespace;
  void *context;
  spawnBindingT *binding;
  shellCmdT *cmds;
};

struct shellCmdS {
  spawnMagicT magic;
  const char *uid;
  const char *info;
  const char *cli;
  const char *apiverb;
  int verbose;
  json_object *usageJ;
  json_object *sampleJ;
  const char **argv;
  int  argc;
  encoderCbT *encoder;
  struct sandBoxS *sandbox;
  int timeout;
  afb_api_t api;
  void *context;
  void *opts;
  taskIdT *tids;
  pthread_rwlock_t sem;
  int single;
};

// spawn-sandbox.c
confEnvT *sandboxParseEnvs (afb_api_t api, sandBoxT *sandbox, json_object *envsJ);
confAclT *sandboxParseAcls(afb_api_t api, sandBoxT *sandbox, json_object *namespaceJ);
confCapT *sandboxParseCaps(afb_api_t api, sandBoxT *sandbox, json_object *capsJ);
confCgroupT *sandboxParseCgroups (afb_api_t api, sandBoxT *sandbox, json_object *cgroupsJ);
confSeccompT *sandboxParseSecRules(afb_api_t api, sandBoxT *sandbox, json_object *seccompJ);
confNamespaceT *sandboxParseNamespace(afb_api_t api, sandBoxT *sandbox, json_object *namespaceJ);
const char **sandboxBwrapArg (afb_api_t api, sandBoxT *sandbox, confNamespaceT *namespace);

int sandboxApplyAcls(confAclT *acls, int isPrivileged);

#endif /* _SPAWN_SANDBOX_INCLUDE_ */
