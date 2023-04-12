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
#include <strings.h>

#include <afb/afb-binding.h>
#include <rp-utils/rp-jsonc.h>
#include <rp-utils/rp-expand-vars.h>
#include <afb-helpers4/afb-data-utils.h>
#include <afb-helpers4/afb-req-utils.h>

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-config.h"
#include "spawn-subtask.h"

/* plugins */
static plugin_store_t  plugins = PLUGIN_STORE_INITIAL;

/*
* predeclaration of the function that initialize the spawn binding
*/
static int initialiaze_spawn_binding(
	afb_api_t rootapi,
	const char *path,
	const char *uid,
	json_object *config
	);


/**
* This is the main entry of the spawn binding
* it only calls call the function initialiaze_spawn_binding
*/
int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
	switch (ctlid)
	{
	/** called on root entries, the entries not linked to an API */
	case afb_ctlid_Root_Entry:
		return initialiaze_spawn_binding(rootapi, ctlarg->root_entry.path,
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
        spawnApiT *spawn = (spawnApiT*)afb_req_get_vcbdata(request);
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

/**
* structure holding dynamic commands during their
* life.
*/
struct dynexec
{
	/** the parameters */
	json_object *params;

	/** the command object */
	shellCmdT cmd;
};

/**
* free the content of a command
*/
static void free_cmd_content(shellCmdT *cmd)
{
	int idx = cmd->argc;
	if (cmd->apiverb != cmd->uid)
		free((void*)cmd->apiverb);
	if (idx) /* don't free the first */
		while (--idx)
			free((void*)cmd->argv[idx]);
	free(cmd->argv);
}

/**
* free the dynexec value
*/
static void free_dynexec(struct dynexec *dynex)
{
	free_cmd_content(&dynex->cmd);
	json_object_put(dynex->params);
	free(dynex);
}

/**
* Launch the dynexec command
*/
static void dynexec_process(struct dynexec *dynex, afb_req_t request)
{
	spawnTaskVerb (request, &dynex->cmd, NULL);
}

#if defined(SPAWN_EXEC_PERMISSION)
/**
* callback of checking EXEC permission
*/
static void dynexec_check_exec_permission_cb(void *closure, int status, afb_req_t request)
{
	if (status < 0)
		afb_req_reply(request, AFB_ERRNO_INSUFFICIENT_SCOPE, 0, NULL);
	else
		dynexec_process(closure, request);
}

/**
* enforce to check exec permission by substituting dynexec_process function
*/
#define dynexec_process(dynex,request) \
	afb_req_check_permission(request, SPAWN_EXEC_PERMISSION, \
				dynexec_check_exec_permission_cb, dynex);

#endif

/**
* calback of checking permission of
*/
static void dynexec_check_permission_cb(void *closure, int status, afb_req_t request)
{
	if (status < 0)
		afb_req_reply(request, AFB_ERRNO_INSUFFICIENT_SCOPE, 0, NULL);
	else
		dynexec_process(closure, request);
}

/**
* search for the sandbox of given uid
*/
static sandBoxT *spawner_search_sandbox(spawnApiT *spawner, const char *searched_uid)
{
	sandBoxT *sandbox = spawner->sandboxes;
	while (sandbox->uid && strcasecmp(sandbox->uid, searched_uid))
		sandbox++;
	return sandbox->uid ? sandbox : NULL;
}

/**
* implementation of 'exec' verb
*/
static void on_request_execute(afb_req_t request, unsigned naparam, afb_data_t const params[])
{
	int rc;
	afb_data_t arg;
	json_object *query, *exec;
	json_object *uid = NULL, *timeout = NULL, *verbose = NULL, *encoder = NULL;
	spawnApiT *spawner;
	const char *sandbox_uid;
	sandBoxT *sandbox;
	struct dynexec *dynex;

	/* get the JSON query */
	rc = afb_req_param_convert(request, 0, AFB_PREDEFINED_TYPE_JSON_C, &arg);
	if (rc < 0) {
		afb_req_reply(request, AFB_ERRNO_INVALID_REQUEST, 0, NULL);
		return;
	}
	query = (json_object*)afb_data_ro_pointer(arg);

	/* extract arguments */
	rc = rp_jsonc_unpack(query, "{ss so s?o s?o s?o s?o}",
		"sandbox", &sandbox_uid,
		"exec",    &exec,
		"uid",     &uid,
		"timeout", &timeout,
		"verbose", &verbose,
		"encoder", &encoder);
	if (rc < 0) {
		afb_req_reply(request, AFB_ERRNO_INVALID_REQUEST, 0, NULL);
		return;
	}

	/* extract the sandbox */
	spawner = (spawnApiT*)afb_req_get_vcbdata(request);
	sandbox = spawner_search_sandbox(spawner, sandbox_uid);
	if (sandbox == NULL) {
		afb_req_reply_string(request, AFB_ERRNO_INVALID_REQUEST, "invalid sandbox uid");
		return;
	}

	/* ensure command uid and make the dynexec structure */
	uid = uid == NULL ? json_object_new_string("dynamic") : json_object_get(uid),
	dynex = uid == NULL ? NULL : calloc(1, sizeof *dynex);
	if (dynex == NULL) {
		afb_req_reply(request, AFB_ERRNO_OUT_OF_MEMORY, 0, NULL);
		return;
	}
	afb_req_set_userdata(request, dynex, (void (*)(void*))free_dynexec);

	/* wrap the arguments */
	rc = rp_jsonc_pack(&dynex->params, "{so sO* sO* sO* sO}",
		"uid",     uid == NULL ? json_object_new_string("dynamic") : json_object_get(uid),
		"timeout", timeout,
		"verbose", verbose,
		"encoder", encoder,
		"exec",    exec);
	if (rc) {
		afb_req_reply(request, AFB_ERRNO_INTERNAL_ERROR, 0, NULL);
		return;
	}

	/* parse the command */
	rc = spawn_config_read_one_command(sandbox, &dynex->cmd, dynex->params);
	if (rc < 0) {
		afb_req_reply(request, AFB_ERRNO_INVALID_REQUEST, 0, NULL);
		return;
	}

	/* check the privilege if any */
	if (sandbox->privilege)
		afb_req_check_permission(request, sandbox->privilege,
					dynexec_check_permission_cb, dynex);
	else
		dynexec_process(dynex, request);
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
	afb_api_t api,
	const char *verb,
	const char *info,
	afb_req_callback_t callback,
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session
) {
        int rc = afb_api_add_verb(api, verb, info, callback, vcbdata, auth, session, 0);
        if (rc < 0)
                AFB_API_ERROR(api, "failed to register verb %s", verb);
        return rc;
}


static int pre_init_api_spawn(afb_api_t api, spawnApiT *spawn)
{
        shellCmdT *cmd;
	sandBoxT *sandbox;
       	afb_auth_t *authent;

        // add static controls verbs
        int rc = add_verb(api, "ping", "ping test", PingTest, NULL, NULL, 0);
        if (rc >= 0)
                rc = add_verb(api, "info", "info about sandboxes", Infosandbox, spawn, NULL, 0);
        if (rc >= 0)
                rc = add_verb(api, "exec", "execute a command", on_request_execute, spawn, NULL, 0);

        // dynamic verbs
        for (sandbox = spawn->sandboxes ; sandbox && rc >= 0 && sandbox->uid != NULL ; sandbox++) {
                for (cmd = sandbox->cmds ; rc >= 0 && cmd->uid != NULL ; cmd++) {
                        authent = cmd->authent.type == afb_auth_Yes ? NULL : &cmd->authent;
                        rc = add_verb(api, cmd->apiverb, cmd->info, cmdApiRequest, cmd, authent, 0);
                }
        }

	if (rc >= 0)
		rc = ctl_actionset_add_verbs(&spawn->extra, api, plugins, spawn);
	if (rc >= 0)
		rc = ctl_actionset_add_events(&spawn->onevent, api, plugins, spawn);
        return rc;
}

static int init_api_spawn(afb_api_t api, spawnApiT *spawn)
{
	int rc = ctl_set_requires(&spawn->metadata, api);
	if (rc >= 0)
		rc = ctl_actionset_exec(&spawn->onstart, api, plugins, spawn);
        return rc;
}

static int main_api_ctl(
		afb_api_t api,
		afb_ctlid_t ctlid,
		afb_ctlarg_t ctlarg,
		void *userdata
) {
	spawnApiT *spawn = userdata;

	switch (ctlid)
	{
	/** called on root entries, the entries not linked to an API */
	case afb_ctlid_Root_Entry:
		break;

	/** called for the preinit of an API, has a companion argument */
	case afb_ctlid_Pre_Init:
        	spawn->api = api;
		return pre_init_api_spawn(api, spawn);

	/** called for init */
	case afb_ctlid_Init:
		return init_api_spawn(api, spawn);

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

/* create one API per config file object */
static int process_one_config(void *closure, json_object *rootdesc, afb_api_t rootapi)
{
	int rc;
	spawnApiT *spawn;
	afb_api_t api;

	/* allocates */
        spawn = calloc(1, sizeof *spawn);
	if (spawn == NULL) {
		AFB_ERROR("out of memory");
		rc = -1;
        }
	else {
        	spawn->api = rootapi;
		spawn->onstart = CTL_ACTIONSET_INITIALIZER;
		spawn->onevent = CTL_ACTIONSET_INITIALIZER;
		spawn->extra = CTL_ACTIONSET_INITIALIZER;
		spawn->config = rootdesc;
        	/* read metadata */
		rc = ctl_subread_metadata(&spawn->metadata, rootdesc, true);
		if (rc >= 0)
			rc = ctl_subread_plugins(&plugins, rootdesc, NULL, "plugins");
                if (rc >= 0)
                        /* read onload actions */
			rc = ctl_subread_actionset(&spawn->onstart, rootdesc, "onload");
                if (rc >= 0)
                        /* read onload actions */
			rc = ctl_subread_actionset(&spawn->onstart, rootdesc, "onstart");
                if (rc >= 0)
                        /* read event actions */
			rc = ctl_subread_actionset(&spawn->onevent, rootdesc, "events");
                if (rc >= 0)
                        /* read event actions */
			rc = ctl_subread_actionset(&spawn->extra, rootdesc, "extra");
                if (rc >= 0)
                        /* read sandboxes */
                        rc = spawn_config_read(spawn, rootdesc);
		if (rc >= 0) {
			rc = afb_create_api(&api, spawn->metadata.api, spawn->metadata.info, 1, main_api_ctl, spawn);
			if (rc < 0)
				AFB_ERROR("creation of api %s failed: %d", spawn->metadata.api, rc);
		}
	}

	/* clean or freeze */
	if (rc >= 0)
		json_object_get(rootdesc);
	else {
		if (spawn != NULL) {
			ctl_actionset_free(&spawn->onstart);
			ctl_actionset_free(&spawn->onevent);
		}
                free(spawn);
        }
	return rc;
}


/* Reads the JSON file of 'path' as a JSON-C object
 * and call the callback with it. */
static int call_for_json_path(
	void *closure,
	int (*callback)(void*,json_object*,afb_api_t),
	const char *path,
	afb_api_t rootapi
) {
	int rc;
	char *expath = rp_expand_vars_env_only(path, 0);
	json_object *json = json_object_from_file(expath ?: path);
	if (json) {
		rc = callback(closure, json, rootapi);
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
	int (*callback)(void*,json_object*,afb_api_t),
	void *closure
) {
	char *configpath;
	int rc;

	rc = config ? callback(closure, config, rootapi) : 0;
	configpath = getenv("AFB_SPAWN_CONFIG");
	if (!rc && configpath)
		rc = call_for_json_path(closure, callback, configpath, rootapi);

	/* TODO */
	return rc;
}

/**
* initialize the spawn binding
*
* @param rootapi the root api
* @param path    the path of the binding
* @param uid     the uid of the binding (or NULL)
* @param config  the configuration of the binding (or NULL)
*
* @return 0 on success or a negative number on failure
*/
static int initialiaze_spawn_binding(
	afb_api_t rootapi,
	const char *path,
	const char *uid,
	json_object *config
) {
	int rc;

	AFB_API_INFO(rootapi, "Initialisation of Spawn Binding uid=%s path=%s config=%s",uid?:"",path,json_object_to_json_string(config));

	// register builtin encoders before plugin get load
	rc = encoder_generator_factory_init();
	if (rc == 0)
		rc = iter_root_configs(rootapi, path, uid, config, process_one_config, NULL);
	return rc;
}
