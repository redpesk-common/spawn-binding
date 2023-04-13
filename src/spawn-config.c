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
#include "spawn-expand.h"
#include "spawn-utils.h"

struct jsonc_optarray_process_s
{
        int (*callback)(void*,void*,struct json_object*);
        void *closure;
        size_t szelem;
        void *array;
        void *iter;
};

static int jsonc_optarray_process_cb(void *closure, struct json_object *object)
{
        struct jsonc_optarray_process_s *jop = closure;
        void *ptr = jop->iter;
        jop->iter = ptr + jop->szelem;
        return jop->callback(jop->closure, ptr, object);
}

int jsonc_optarray_process(
                void **result,
                struct json_object *object,
                int (*callback)(void*,void*,struct json_object*),
                void *closure,
                size_t szelem
) {
        int rc;
        struct jsonc_optarray_process_s jop;
        size_t count;

        count = json_object_is_type(object, json_type_array) ? (size_t)json_object_array_length(object) : object != NULL;
        jop.array = calloc(1 + count, szelem);
        if (jop.array == NULL) {
		AFB_ERROR("out of memory");
                rc = -1;
        }
        else if (object != NULL) {
                jop.iter = jop.array;
                jop.callback = callback;
                jop.closure = closure;
                jop.szelem = szelem;
                rc = rp_jsonc_optarray_until(object, jsonc_optarray_process_cb, &jop);
                if (rc < 0) {
                        free(jop.array);
                        jop.array = NULL;
                }
        }
        *result = jop.array;
        return rc;
}

/**
*/
static int parse_prepare_command (shellCmdT *cmd, json_object *execJ)
{
	int idx, err;
	json_object *argsJ = NULL;
	const char*param;

	/* get the command and its arguments */
	err = rp_jsonc_unpack(execJ, "{ss s?o !}", "cmdpath", &cmd->command, "args", &argsJ);
	if (err) {
		AFB_API_ERROR(cmd->sandbox->binding->api, "[fail-parsing] cmdpath sandbox=%s cmd=%s exec=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(argsJ));
		goto OnErrorExit;
	}

	cmd->command = utilsExpandKeyCmd(cmd->command, cmd);  // expand env $keys
	if (access(cmd->command, X_OK|R_OK)) {
		AFB_API_ERROR(cmd->sandbox->binding->api, "[file-not-executable] sandbox=%s cmd=%s exec=%s", cmd->sandbox->uid, cmd->uid, cmd->command);
		goto OnErrorExit;
	}

	// prepare arguments list, they will still need to be expanded before execution
	if (!argsJ) {
		cmd->argc = 2;
		cmd->argv = calloc (cmd->argc, sizeof (char*));
		cmd->argv[0] = utilsExpandKeyCmd(cmd->command, cmd);
		if (!cmd->argv[0]) {
			AFB_API_ERROR(cmd->sandbox->binding->api, "[unknow-$ENV-key] sandbox=%s cmd=%s cmdpath=%s", cmd->sandbox->uid, cmd->uid, cmd->command);
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
				cmd->argv[idx] = utilsExpandKeyCmd(param, cmd);
				if (!cmd->argv[idx]) {
					AFB_API_ERROR(cmd->sandbox->binding->api, "[unknow-$ENV-key] sandbox=%s cmd=%s args=%s", cmd->sandbox->uid, cmd->uid, param);
					goto OnErrorExit;
				}
			}
			break;

		default:
			param = json_object_get_string (argsJ);
			cmd->argc = 3;
			cmd->argv = calloc(cmd->argc, sizeof (char*));
			cmd->argv[0] = cmd->uid;
			cmd->argv[1] = utilsExpandKeyCmd(param, cmd);
			if (!cmd->argv[1]) {
				AFB_API_ERROR(cmd->sandbox->binding->api, "[unknow-$ENV-key] uid=%s cmdpath=%s arg=%s", cmd->uid, cmd->command, param);
				goto OnErrorExit;
			}
			break;
		}
	}
	return 0;

OnErrorExit:
	return -1;
}

int spawn_config_read_one_command(sandBoxT *sandbox, shellCmdT *cmd, json_object *cmdJ)
{
	int err = 0;
	const char *privilege = NULL;
	json_object *execJ = NULL, *encoderJ = NULL;

	cmd->sandbox = sandbox;

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
	err = encoder_generator_get_JSON(encoderJ, &cmd->encoder.generator, &cmd->encoder.options);
	if (err == ENCODER_NO_ERROR)
		err = encoder_generator_check_options(cmd->encoder.generator, cmd->encoder.options);
	if (err != ENCODER_NO_ERROR) {
		AFB_ERROR("[encoder-error] sandbox='%s' fail to get encoder: %s for %s",
					sandbox->uid,
					encoder_error_text(err),
					json_object_to_json_string(encoderJ));
		goto OnErrorExit;
	}

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
	err = parse_prepare_command (cmd, execJ);
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
	return -1;
}

struct read_one_sandbox_config_closure_s
{
        spawnApiT *spawn;
        int forceprefix;
};

static int read_one_sandbox_config(struct read_one_sandbox_config_closure_s *roscc, sandBoxT *sandbox, json_object *sandboxJ)
{
	int err = 0;
	json_object *cmdsJ, *namespaceJ=NULL, *capsJ=NULL, *aclsJ=NULL, *cgroupsJ=NULL, *envsJ=NULL, *seccompJ=NULL;

	sandbox->binding = roscc->spawn;
	sandbox->namespace = NULL;
	sandbox->caps = NULL;
	sandbox->acls = NULL;

	// user 'O' to force json objects not to be released
	err = rp_jsonc_unpack(sandboxJ, "{ss,s?s,s?s,s?s,s?i,s?o,s?o,s?o,s?o,s?o,s?o,s?o}"
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

        // force prefix if required
        if (roscc->forceprefix && sandbox->prefix == NULL)
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

        /* read commands */
        err = jsonc_optarray_process(
                (void **)&sandbox->cmds,
                cmdsJ,
                (int (*)(void*,void*,json_object*))spawn_config_read_one_command,
                sandbox,
                sizeof(*sandbox->cmds));
        if (err)
                goto OnErrorExit;
	return 0;

OnErrorExit:
	free(sandbox->namespace);
	free(sandbox->caps);
	free(sandbox->acls);
	return -1;
}

int spawn_config_read(spawnApiT *spawn, json_object *configJ)
{
	int rc;
        struct read_one_sandbox_config_closure_s roscc;
	json_object *sandboxesJ;
        unsigned count = 1;

	/* get the count of sandboxes and the first item */
	sandboxesJ = json_object_object_get(configJ, "sandboxes");
	switch(json_object_get_type(sandboxesJ)) {
	case json_type_array:
                count = (unsigned)json_object_array_length(sandboxesJ);
                /*@fallthrough@*/
	case json_type_object:
		roscc.spawn = spawn;
		roscc.spawn->config = configJ;
                roscc.forceprefix = count > 1;
                rc = jsonc_optarray_process(
                        (void **)&roscc.spawn->sandboxes,
                        sandboxesJ,
                        (int (*)(void*,void*,json_object*))read_one_sandbox_config,
                        &roscc,
                        sizeof(*roscc.spawn->sandboxes));
		break;
	default:
		AFB_NOTICE("no sandbox");
		rc = 0;
		break;
	}

	return rc;
}

