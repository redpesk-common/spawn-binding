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

#include <afb/afb-binding.h>
#include <rp-utils/rp-jsonc.h>
#include <afb-helpers4/afb-data-utils.h>
#include <rp-utils/rp-expand-vars.h>

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-config.h"
#include "spawn-subtask.h"

/* basic implementation of ping */
static void PingTest (afb_req_t request, unsigned naparam, afb_data_t const params[])
{
	static int count = 0;
	char response[32];
	afb_data_t arg, repl;
	int curcount = ++count;
	afb_req_param_convert(request, 0, AFB_PREDEFINED_TYPE_STRINGZ, &arg);
	AFB_REQ_NOTICE(request, "ping count=%d query=%s", curcount, (const char*)afb_data_ro_pointer(arg));
	snprintf (response, sizeof(response), "\"pong=%d\"", curcount);
	repl = afb_data_json_copy(response, 0);
	afb_req_reply(request, 0, 1, &repl);
}

/* returns the array of info description for verbs of the sandbox */
static json_object *InfoCmds(sandBoxT *sandbox)
{
        json_object *cmdJ, *commandsJ = json_object_new_array();
        shellCmdT *cmd = sandbox->cmds;
        for (; cmd->uid; cmd++) {
                rp_jsonc_pack (&cmdJ, "{ss ss ss* sO* s{s[ss] sO*}}"
                        , "uid",  cmd->uid
                        , "verb", cmd->apiverb
                        , "info", cmd->info
                        , "sample", cmd->sampleJ
                        , "usage"
                                , "action"
                                        , "start", "stop"
                                , "args", cmd->usageJ
                        );
                json_object_array_add (commandsJ, cmdJ);
        }
        return commandsJ;
}

// build global info page for developper dynamic HTML5 page
static void Infosandbox (afb_req_t request, unsigned naparam, afb_data_t const params[])
{
        afb_data_t repldata;
        json_object *responseJ, *sandboxJ, *sandboxesJ;
        spawnBindingT *spawn = (spawnBindingT*)afb_req_get_vcbdata(request);
        sandBoxT *sandbox = spawn->sandboxes;

        // loop on shell command sandboxes
        sandboxesJ = json_object_new_array();
        for (; sandbox->uid; sandbox++){
                // create sandbox object with sandbox_info and sandbox-cmds
                rp_jsonc_pack (&sandboxJ, "{ss ss* so*}",
                        "uid", sandbox->uid,
                        "info", sandbox->info,
                        "verbs", InfoCmds(sandbox));
                json_object_array_add(sandboxesJ, sandboxJ);
        }

        rp_jsonc_pack (&responseJ, "{so s{ss ss* ss* ss*}}"
                , "groups", sandboxesJ
                , "metadata"
                        , "uid", spawn->metadata.uid
                        , "info", spawn->metadata.info
                        , "version", spawn->metadata.version
                        , "author", spawn->metadata.author
                );
        repldata = afb_data_json_c_hold(responseJ);
        afb_req_reply(request, 0, 1, &repldata);
}

/**
* Main verb entry, extracts the JSON argument object and the associated command.
* Then calls the function spawnTaskVerb that effectively perform the action.
*/
static void cmdApiRequest(afb_req_t request, unsigned naparam, afb_data_t const params[])
{
	afb_data_t arg;
	int rc = afb_req_param_convert(request, 0, AFB_PREDEFINED_TYPE_JSON_C, &arg);
	if (rc < 0)
		afb_req_reply(request, AFB_ERRNO_INVALID_REQUEST, 0, NULL);
	else {
		json_object *query = (json_object*)afb_data_ro_pointer(arg);
		shellCmdT *cmd = (shellCmdT*)afb_req_get_vcbdata(request);
		spawnTaskVerb (request, cmd, query);
	}
}

#if 0



static int AutostartConfig(afb_api_t api, CtlSectionT *section, json_object *onloadJ)
{

    // run autostart section after everything is loaded
    if (!onloadJ) return 0;

    // save autostart to run it after sealing API
    autostartSection = section;


    // delagate autostart parsing to controller builtin OnloadConfig
    return (OnloadConfig (api, section, onloadJ));
}

#endif


static int add_verb(
        spawnBindingT *spawn,
	const char *verb,
	const char *info,
	afb_req_callback_t callback,
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session
) {
        int rc = afb_api_add_verb(spawn->api, verb, info, callback, vcbdata, auth, session, 0);
        if (rc < 0)
                AFB_API_ERROR(spawn->api, "failed to register verb %s", verb);
        return rc;
}


static int init_spawn_api(spawnBindingT *spawn)
{
        shellCmdT *cmd;
	sandBoxT *sandbox;
       	afb_auth_t *authent;


        // add static controls verbs
        int rc = add_verb(spawn, "ping", "ping test", PingTest, NULL, NULL, 0);
        if (rc >= 0)
                rc = add_verb(spawn, "info", "info about sandboxes", Infosandbox, spawn, NULL, 0);

        // dynamic verbs
        for (sandbox = spawn->sandboxes ; rc >= 0 && sandbox->uid != NULL ; sandbox++) {
                for (cmd = sandbox->cmds ; rc >= 0 && cmd->uid != NULL ; cmd++) {
                        cmd->api = spawn->api;
                        authent = cmd->authent.type == afb_auth_Yes ? NULL : &cmd->authent;
                        rc = add_verb(spawn, cmd->apiverb, cmd->info, cmdApiRequest, cmd, authent, 0);
                }
        }
        return rc;
}

static int main_api_ctl(
		afb_api_t api,
		afb_ctlid_t ctlid,
		afb_ctlarg_t ctlarg,
		void *userdata
) {
	spawnBindingT *spawn = userdata;

	switch (ctlid)
	{
	/** called on root entries, the entries not linked to an API */
	case afb_ctlid_Root_Entry:
		break;

	/** called for the preinit of an API, has a companion argument */
	case afb_ctlid_Pre_Init:
        	spawn->api = api;
		return init_spawn_api(spawn);

	/** called for init */
	case afb_ctlid_Init:
		break;

	/** called when required classes are ready */
	case afb_ctlid_Class_Ready:
		break;

	/** called when an event is not handled */
	case afb_ctlid_Orphan_Event:
		break;

	/** called when shuting down */
	case afb_ctlid_Exiting:
		break;
	}
	return 0;
}

int apply_spawn_config(spawnBindingT *spawn)
{
        afb_api_t api;
	return afb_create_api(&api, spawn->metadata.api, spawn->metadata.info, 1, main_api_ctl, spawn);
}

/* create one API per config file object */
static int apply_one_config(void *closure, json_object *rootdesc)
{
	int rc;
	spawnBindingT *spawn;
	afb_api_t api;


        rc = read_spawn_config(&spawn, rootdesc);
	if (rc == 0) {
		rc = afb_create_api(&api, spawn->metadata.api, spawn->metadata.info, 1, main_api_ctl, spawn);
		if (rc < 0)
			AFB_ERROR("creation of api %s failed: %d", spawn->metadata.api, rc);
	}
	return rc;
}


/* Reads the JSON file of 'path' as a JSON-C object
 * and call the callback with it. */
static int call_for_json_path(
	void *closure,
	int (*callback)(void*,json_object*),
	const char *path
) {
	int rc;
	char *expath = rp_expand_vars_env_only(path, 0);
	json_object *json = json_object_from_file(expath ?: path);
	if (json) {
		rc = callback(closure, json);
		json_object_put(json);
	}
	else {
		AFB_ERROR("reading JSON file %s returned NULL", expath ?: path);
		rc = -1;
	}
	free(expath);
	return rc;
}

/* Using the given binding's path
*/
static int iter_root_configs(
	afb_api_t rootapi,
	const char *path,
	const char *uid,
	json_object *config,
	int (*callback)(void*,json_object*),
	void *closure
) {
	char *configpath;
	int rc;

	rc = config ? callback(closure, config) : 0;
	configpath = getenv("AFB_SPAWN_CONFIG");
	if (!rc && configpath)
		rc = call_for_json_path(closure, callback, configpath);

	/* TODO */
	return rc;
}

static int initialize_binding(
	afb_api_t rootapi,
	const char *path,
	const char *uid,
	json_object *config
) {
	int rc;

	AFB_API_NOTICE(rootapi, "Initialisation of Spawn Binding");
	AFB_API_INFO(rootapi, "Initialisation of Spawn Binding uid=%s path=%s config=%s",uid?:"",path,json_object_to_json_string(config));

	// register builtin encoders before plugin get load
	rc = encoderInit();
	if (rc == 0)
		rc = iter_root_configs(rootapi, path, uid, config, apply_one_config, NULL);
	return rc;
}


int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
	switch (ctlid)
	{
	/** called on root entries, the entries not linked to an API */
	case afb_ctlid_Root_Entry:
		return initialize_binding(rootapi, ctlarg->root_entry.path,
				ctlarg->root_entry.uid, ctlarg->root_entry.config);

	/** called for the preinit of an API, has a companion argument */
	case afb_ctlid_Pre_Init:
		break;

	/** called for init */
	case afb_ctlid_Init:
		break;

	/** called when required classes are ready */
	case afb_ctlid_Class_Ready:
		break;

	/** called when an event is not handled */
	case afb_ctlid_Orphan_Event:
		break;

	/** called when shuting down */
	case afb_ctlid_Exiting:
		break;
	}
	return 0;
}


