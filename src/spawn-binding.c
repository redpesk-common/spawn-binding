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

// usefull classical include
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>

#include <afb/afb-binding.h>

#include <rp-utils/rp-path-search.h>
#include <rp-utils/rp-jsonc.h>
#include <rp-utils/rp-expand-vars.h>
#include <afb-helpers4/afb-data-utils.h>

#include "ctl-lib.h"

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include "spawn-utils.h"
#include "spawn-defaults.h"
#include "spawn-expand.h"
#include "spawn-encoders-plugins.h"


/* simple implementation of ping */
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


static void InfoCmds (sandBoxT *sandbox, json_object *responseJ)
{
    // create a static action valid for all response
    static json_object *actionsJ= NULL;
    if (!actionsJ) {
        actionsJ =json_tokener_parse("['start','stop']");
    }

    for (int idx=0; sandbox->cmds[idx].uid; idx++) {
        shellCmdT *cmd = &sandbox->cmds[idx];
        json_object *cmdJ, *usageJ;
        json_object_get (actionsJ);
        json_object_get (cmd->usageJ);
        json_object_get (cmd->sampleJ);

        rp_jsonc_pack (&usageJ, "{so so*}", "action", actionsJ, "args", cmd->usageJ);
        rp_jsonc_pack (&cmdJ, "{ss ss ss* so so*}"
        , "uid",  cmd->uid
        , "verb", cmd->apiverb
        , "info", cmd->info
        , "usage", usageJ
        , "sample", cmd->sampleJ
        );

        json_object_array_add (responseJ, cmdJ);
    }
}

// build global info page for developper dynamic HTML5 page
static void Infosandbox (afb_req_t request, unsigned naparam, afb_data_t const params[])
{
    int idx;
    sandBoxT *sandboxes = (sandBoxT*) afb_req_get_vcbdata(request);
    json_object *responseJ, *globalJ, *sandboxJ, *sandboxesJ, *cmdsJ;
	afb_data_t repldata;

#if 1
	globalJ = NULL;
#else
    CtlConfigT* ctlConfig = (CtlConfigT*)afb_api_get_userdata(afb_req_get_api(request));
    err= rp_jsonc_pack (&globalJ, "{ss ss* ss* ss*}",
										"uid", ctlConfig->uid,
										"info",	ctlConfig->info,
										"version", ctlConfig->version,
										"author", ctlConfig->author);
    if (err) {
        AFB_DEBUG ("Fail to wrap json binding metadata");
        goto OnErrorExit;
    }
#endif

    // loop on shell command sandboxes
    sandboxesJ= json_object_new_array();
    for (idx=0; sandboxes[idx].uid; idx++){
        // create sandbox object with sandbox_info and sandbox-cmds
	cmdsJ = json_object_new_array();
        InfoCmds (&sandboxes[idx], cmdsJ);
        rp_jsonc_pack (&sandboxJ, "{ss ss* so*}",
				"uid", sandboxes[idx].uid,
				"info", sandboxes[idx].info,
				"verbs", cmdsJ);
        json_object_array_add(sandboxesJ, sandboxJ);
    }

    rp_jsonc_pack (&responseJ, "{so so}", "metadata", globalJ, "groups", sandboxesJ);

	repldata = afb_data_json_c_hold(responseJ);

    afb_req_reply(request, 0, 1, &repldata);
}

// Static verb not depending on shell json config file
static afb_verb_t CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping",     .callback = PingTest    , .info = "shell API ping test"},
    { .verb = "info",     .callback = Infosandbox     , .info = "shell List sandboxes"},
    { .verb = NULL} /* marker for end of the array */
};



// retrieve action handle from request and execute the request
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

static int cmdLoadOne(afb_api_t api, sandBoxT *sandbox, shellCmdT *cmd, json_object *cmdJ)
{
	int err = 0;
	const char *privilege = NULL;
	afb_auth_t *authent = NULL;
	json_object *execJ = NULL, *encoderJ = NULL;

	// should already be allocated
	assert (cmdJ);

	// set default values
	memset(cmd, 0, sizeof (shellCmdT));
	cmd->sandbox  = sandbox;
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
		AFB_API_ERROR(api, "[parsing-error] sandbox='%s' fail to parse cmd=%s", sandbox->uid, json_object_to_json_string(cmdJ));
		goto OnErrorExit;
	}

	// if verbose undefined
	if (cmd->verbose < 0)
		cmd->verbose = sandbox->verbose;

	// find encode/decode callback
	cmd->api = api;
	err = encoderFind(cmd, encoderJ);
	if (err)
		goto OnErrorExit;

	// If not special privilege use sandbox one
	if (!privilege)
		privilege = sandbox->privilege;
	if (privilege) {
		authent = (afb_auth_t*)calloc(1, sizeof (afb_auth_t));
		authent->type = afb_auth_Permission;
		authent->text = privilege;
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
		AFB_API_ERROR(api, "[fail init semaphore] API sandbox=%s cmd=%s", sandbox->uid, cmd->uid);
		goto OnErrorExit;
	}

	// if prefix not empty add it to verb api
	if (!sandbox->prefix)
		cmd->apiverb = cmd->uid;
	else {
		if (asprintf ((char**) &cmd->apiverb, "%s/%s", sandbox->prefix, cmd->uid)<0)
			goto OnErrorExit;
	}

	err = afb_api_add_verb(api, cmd->apiverb, cmd->info, cmdApiRequest, cmd, authent, 0, 0);
	if (err) {
		AFB_API_ERROR(api, "[fail to register] API sandbox=%s cmd=%s verb=%s", sandbox->uid, cmd->uid, cmd->apiverb);
		goto OnErrorExit;
	}

	return 0;

OnErrorExit:
	return 1;
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




static int sandboxLoadOne(spawnBindingT *spawnapi, sandBoxT *sandbox, json_object *sandboxJ)
{
	int err = 0;
	json_object *cmdsJ, *namespaceJ=NULL, *capsJ=NULL, *aclsJ=NULL, *cgroupsJ=NULL, *envsJ=NULL, *seccompJ=NULL;

	sandbox->magic = MAGIC_SPAWN_SBOX;
	sandbox->binding = spawnapi;
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
		AFB_API_ERROR(spawnapi->api, "[Fail-to-parse] sandbox config JSON='%s'", json_object_to_json_string(sandboxJ));
		goto OnErrorExit;
	}

	// if verbose is 5 then check string expansion works
	if (sandbox->verbose)
		utilsExpandJsonDebug(); // in verbose test parsing before forking a child

	/* get environment */
	if (envsJ) {
		sandbox->envs = sandboxParseEnvs(spawnapi->api, sandbox, envsJ);
		if (!sandbox->envs)
			goto OnErrorExit;
	}

	/* ? */
	if (aclsJ) {
		sandbox->acls = sandboxParseAcls(spawnapi->api, sandbox, aclsJ);
		if (!sandbox->acls)
			goto OnErrorExit;
	}
	else {
		if (utilsTaskPrivileged()) {
			AFB_API_ERROR(spawnapi->api, "[security-error] ### privilege mode uid='%s' requirer acls->user=xxxx ###", sandbox->uid);
			goto OnErrorExit;
		}
	}

	if (capsJ) {
		sandbox->caps = sandboxParseCaps(spawnapi->api, sandbox, capsJ);
		if (!sandbox->caps)
			goto OnErrorExit;
	}

	if (seccompJ) {
		sandbox->seccomp = sandboxParseSecRules(spawnapi->api, sandbox, seccompJ);
		if (!sandbox->seccomp)
			goto OnErrorExit;
	}

	if (cgroupsJ) {
		if (!utilsTaskPrivileged()) {
			AFB_API_NOTICE(spawnapi->api, "[cgroups ignored] sandbox=%s user=%d not privileged (sandboxLoadOne)", sandbox->uid, getuid());
		}
		else {
			sandbox->cgroups = sandboxParseCgroups(spawnapi->api, sandbox, cgroupsJ);
			if (!sandbox->cgroups)
				goto OnErrorExit;
		}
	}

	// if namespace defined parse try to parse it
	if (namespaceJ) {
		sandbox->namespace = sandboxParseNamespace (spawnapi->api, sandbox, namespaceJ);
		if (!sandbox->namespace)
			goto OnErrorExit;
	}

	// loop on cmds
	if (json_object_is_type(cmdsJ, json_type_array)) {
		int count = (int)json_object_array_length(cmdsJ);
		sandbox->cmds= (shellCmdT*)calloc(count+1, sizeof (shellCmdT));

		for (int idx = 0; idx < count; idx++) {
			json_object *cmdJ = json_object_array_get_idx(cmdsJ, idx);
			err = cmdLoadOne(spawnapi->api, sandbox, &sandbox->cmds[idx], cmdJ);
			if (err)
				goto OnErrorExit;
		}
	}
	else {
		sandbox->cmds= (shellCmdT*) calloc(2, sizeof(shellCmdT));
		err = cmdLoadOne(spawnapi->api, sandbox, &sandbox->cmds[0], cmdsJ);
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



static int config_sandboxes(spawnBindingT *spawnapi, json_object *sandboxesJ)
{
	unsigned sbidx, sbcnt, nncnt;
	sandBoxT *sandboxes;
	json_object *sandboxJ;
	int rc;

	/* get the count of sandboxes and the first item */
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
			rc = sandboxLoadOne(spawnapi, &sandboxes[nncnt], sandboxJ);
			nncnt += rc >= 0;
		}
		if (rc < 0 || ++sbidx >= sbcnt)
			break;
		sandboxJ = json_object_array_get_idx(sandboxesJ, sbidx);
	}
	if (rc >= 0) {
		if (nncnt > 1) {
			// when having more than one sandboxes default prefix is sandbox->uid
			for(sbidx = 0 ; sbidx < nncnt ; sbidx++)
				if (!sandboxes[sbidx].prefix)
					sandboxes[sbidx].prefix= sandboxes[sbidx].uid;
		}
		// add static controls verbsmake
		rc = afb_api_set_verbs(spawnapi->api, CtrlApiVerbs);
		if (rc >= 0)
			return 0;
		AFB_API_ERROR(spawnapi->api, "registry static of static verbs");
	}
	spawnapi->sandboxes = sandboxes;

	free(sandboxes);
	return -1;
}



























static int main_api_ctl(
		afb_api_t api,
		afb_ctlid_t ctlid,
		afb_ctlarg_t ctlarg,
		void *userdata
) {
	spawnBindingT *spawnapi = userdata;

	switch (ctlid)
	{
	/** called on root entries, the entries not linked to an API */
	case afb_ctlid_Root_Entry:
		break;

	/** called for the preinit of an API, has a companion argument */
	case afb_ctlid_Pre_Init:
		spawnapi->api = api;
		return config_sandboxes(spawnapi, json_object_object_get(spawnapi->config, "sandboxes"));
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


/*
static int CtrlLoadOneApi(void* vcbdata, afb_api_t api)
{
    CtlConfigT* ctlConfig = (CtlConfigT*)vcbdata;

    // save closure as api's data context
    afb_api_set_userdata(api, ctlConfig);

    // load section for corresponding API
    int error = CtlLoadSections(api, ctlConfig, ctrlSections);

    // init and seal API function
    afb_api_seal(api);

    // call autoload now that API is active
    if (!error && autostartSection)
		OnloadConfig (api, autostartSection, NULL);

    return error;
}

*/

/* create one API per config file object */
static int apply_one_config(void *closure, json_object *rootdesc)
{
	int rc;
	spawnBindingT *spawnapi;
	afb_api_t api;

	/* allocates and read metadata */
	spawnapi = calloc(1, sizeof *spawnapi);
	if (spawnapi != NULL)
		rc = ctl_metadata_read_json(&spawnapi->metadata, rootdesc);
	else {
		AFB_ERROR("out of memory");
		rc = -1;
	}

	if (rc == 0) {
		spawnapi->config = rootdesc;
		rc = afb_create_api(&api, spawnapi->metadata.api, spawnapi->metadata.info, 1, main_api_ctl, spawnapi);
		if (rc < 0)
			AFB_ERROR("creation of api %s failed: %d", spawnapi->metadata.api, rc);
	}

	/* clean or freeze */
	if (rc == 0)
		json_object_get(spawnapi->config);
	else
		free(spawnapi);
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





