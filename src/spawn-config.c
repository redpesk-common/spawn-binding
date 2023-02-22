/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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
#include <pthread.h>
#include <unistd.h>

#include <rp-utils/rp-jsonc.h>

#include "spawn-binding.h"
#include "spawn-config.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include "spawn-utils.h"
#include "spawn-expand.h"


static int read_one_command_config(sandBoxT *sandbox, shellCmdT *cmd, json_object *cmdJ)
{
	int err = 0;
	const char *privilege = NULL;
	json_object *execJ = NULL, *encoderJ = NULL;

	cmd->sandbox = sandbox;
	cmd->magic = MAGIC_SPAWN_CMD;

	// default verbose is sandbox->verbose
	cmd->verbose = -1;

	// parse shell command and lock format+exec object if defined
	err = rp_jsonc_unpack(cmdJ, "{ss,s?s,s?i,s?i,s?s,s?o,s?o,s?o,s?o,s?b !}"
		,"uid", &cmd->uid
		,"info", &cmd->info
		,"timeout", &cmd->timeout
		,"verbose", &cmd->verbose
		,"privilege", &privilege
		,"usage", &cmd->usageJ
		,"encoder", &encoderJ
		,"sample", &cmd->sampleJ
		,"exec", &execJ
		,"single", &cmd->single
		);
	if (err) {
		AFB_ERROR("[parsing-error] sandbox='%s' fail to parse cmd=%s", sandbox->uid, json_object_to_json_string(cmdJ));
		goto OnErrorExit;
	}

	// if verbose undefined
	if (cmd->verbose < 0)
		cmd->verbose = sandbox->verbose;

	// find encode/decode callback
	err = encoderFind(cmd, encoderJ);
	if (err)
		goto OnErrorExit;

	// If not special privilege use sandbox one
	if (!privilege)
		privilege = sandbox->privilege;
	if (privilege) {
		cmd->authent.type = afb_auth_Permission;
		cmd->authent.text = privilege;
	}
        else {
                cmd->authent.type = afb_auth_Yes;
        }

	// use sandbox timeout as default
	if (!cmd->timeout && sandbox->acls)
		cmd->timeout = sandbox->acls->timeout;

	// pre-parse command to boost runtime execution
	err = spawnParse (cmd, execJ);
	if (err)
		goto OnErrorExit;

	// initialize semaphore to protect tids hashtable
	err = pthread_rwlock_init(&cmd->sem, NULL);
	if (err < 0) {
		AFB_ERROR("[fail init semaphore] API sandbox=%s cmd=%s", sandbox->uid, cmd->uid);
		goto OnErrorExit;
	}

	// if prefix not empty add it to verb api
	if (!sandbox->prefix)
		cmd->apiverb = cmd->uid;
	else {
		if (asprintf ((char**) &cmd->apiverb, "%s/%s", sandbox->prefix, cmd->uid)<0)
			goto OnErrorExit;
	}
	return 0;

OnErrorExit:
	return 1;
}

static int read_one_sandbox_config(spawnBindingT *spawn, sandBoxT *sandbox, json_object *sandboxJ, int forceprefix)
{
	int err = 0;
	json_object *cmdsJ, *namespaceJ=NULL, *capsJ=NULL, *aclsJ=NULL, *cgroupsJ=NULL, *envsJ=NULL, *seccompJ=NULL;

	sandbox->magic = MAGIC_SPAWN_SBOX;
	sandbox->binding = spawn;
	sandbox->namespace = NULL;
	sandbox->caps = NULL;
	sandbox->acls = NULL;

	// user 'O' to force json objects not to be released
	err = rp_jsonc_unpack(sandboxJ, "{ss,s?s,s?s,s?s,s?i,s?o,s?o,s?o,s?o,s?o,s?o,so !}"
			,"uid", &sandbox->uid
			,"info", &sandbox->info
			,"privilege", &sandbox->privilege
			,"prefix", &sandbox->prefix
			,"verbose", &sandbox->verbose
			,"envs", &envsJ
			,"acls", &aclsJ
			,"caps", &capsJ
			,"cgroups", &cgroupsJ
			,"seccomp", &seccompJ
			,"namespace", &namespaceJ
			,"commands", &cmdsJ
		);
	if (err) {
		AFB_ERROR("[Fail-to-parse] sandbox config JSON='%s'", json_object_to_json_string(sandboxJ));
		goto OnErrorExit;
	}

	// if verbose is 5 then check string expansion works
	if (sandbox->verbose)
		utilsExpandJsonDebug(); // in verbose test parsing before forking a child

        // force prefix if required
        if (forceprefix && sandbox->prefix == NULL)
                sandbox->prefix = sandbox->uid;

	/* get environment */
	if (envsJ) {
		sandbox->envs = sandboxParseEnvs(sandbox, envsJ);
		if (!sandbox->envs)
			goto OnErrorExit;
	}

	/* ? */
	if (aclsJ) {
		sandbox->acls = sandboxParseAcls(sandbox, aclsJ);
		if (!sandbox->acls)
			goto OnErrorExit;
	}
	else {
		if (utilsTaskPrivileged()) {
			AFB_ERROR("[security-error] ### privilege mode uid='%s' requirer acls->user=xxxx ###", sandbox->uid);
			goto OnErrorExit;
		}
	}

	if (capsJ) {
		sandbox->caps = sandboxParseCaps(sandbox, capsJ);
		if (!sandbox->caps)
			goto OnErrorExit;
	}

	if (seccompJ) {
		sandbox->seccomp = sandboxParseSecRules(sandbox, seccompJ);
		if (!sandbox->seccomp)
			goto OnErrorExit;
	}

	if (cgroupsJ) {
		if (!utilsTaskPrivileged()) {
			AFB_NOTICE("[cgroups ignored] sandbox=%s user=%d not privileged (sandboxLoadOne)", sandbox->uid, getuid());
		}
		else {
			sandbox->cgroups = sandboxParseCgroups(sandbox, cgroupsJ);
			if (!sandbox->cgroups)
				goto OnErrorExit;
		}
	}

	// if namespace defined parse try to parse it
	if (namespaceJ) {
		sandbox->namespace = sandboxParseNamespace (sandbox, namespaceJ);
		if (!sandbox->namespace)
			goto OnErrorExit;
	}

	// loop on cmds
	if (json_object_is_type(cmdsJ, json_type_array)) {
		int count = (int)json_object_array_length(cmdsJ);
		sandbox->cmds= (shellCmdT*)calloc(count+1, sizeof (shellCmdT));

		for (int idx = 0; idx < count; idx++) {
			json_object *cmdJ = json_object_array_get_idx(cmdsJ, idx);
			err = read_one_command_config(sandbox, &sandbox->cmds[idx], cmdJ);
			if (err)
				goto OnErrorExit;
		}
	}
	else {
		sandbox->cmds= (shellCmdT*) calloc(2, sizeof(shellCmdT));
		err = read_one_command_config(sandbox, &sandbox->cmds[0], cmdsJ);
		if (err)
			goto OnErrorExit;
	}

	return 0;

OnErrorExit:
	free(sandbox->namespace);
	free(sandbox->caps);
	free(sandbox->acls);
	return -1;
}

static int read_sandboxes_config(spawnBindingT *spawn, json_object *configJ)
{
	int rc;
	unsigned sbidx, sbcnt, nncnt;
	sandBoxT *sandboxes;
	json_object *sandboxJ, *sandboxesJ;

	/* get the count of sandboxes and the first item */
	sandboxesJ = json_object_object_get(configJ, "sandboxes");
	switch(json_object_get_type(sandboxesJ)) {
	case json_type_array:
		sbcnt = (unsigned)json_object_array_length(sandboxesJ);
		sandboxJ = sbcnt ? json_object_array_get_idx(sandboxesJ, 0) : NULL;
		break;
	case json_type_object:
		sbcnt = 1;
		sandboxJ = sandboxesJ;
		break;
	default:
		AFB_NOTICE("no sandbox");
		return 0;
	}

	/* allocates the sandboxes */
       	sandboxes = (sandBoxT*) calloc(sbcnt + 1, sizeof (sandBoxT));
	if (sandboxes == NULL) {
		AFB_ERROR("out of memory");
		return -1;
	}

	/* load the sandboxes */
	for (nncnt = sbidx = rc = 0 ;;) {
		if (sandboxJ != NULL) {
			rc = read_one_sandbox_config(spawn, &sandboxes[nncnt], sandboxJ, sbcnt > 1);
			nncnt += rc >= 0;
		}
		if (rc < 0 || ++sbidx >= sbcnt)
			break;
		sandboxJ = json_object_array_get_idx(sandboxesJ, sbidx);
	}
	if (rc >= 0) {
               	spawn->sandboxes = sandboxes;
		return 0;
	}

	free(sandboxes);
	return -1;
}

int read_spawn_config(spawnBindingT **result, json_object *configJ)
{
	int rc;
	spawnBindingT *spawn;

	/* allocates */
 	spawn = calloc(1, sizeof *spawn);
	if (spawn == NULL) {
		AFB_ERROR("out of memory");
		rc = -1;
        }
	else {
        	/* read metadata */
		rc = ctl_metadata_read_json(&spawn->metadata, configJ);
                if (rc >= 0)
                        /* read sandboxes */
                        rc = read_sandboxes_config(spawn, configJ);
	}

	/* clean or freeze */
	if (rc == 0)
		spawn->config = json_object_get(configJ);
	else {
		free(spawn);
                spawn = NULL;
        }
        *result = spawn;

	return rc;
}

