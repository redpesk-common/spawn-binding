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

#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <afb/afb-binding.h>
#include <rp-utils/rp-jsonc.h>
#include <afb-helpers4/afb-data-utils.h>
#include <rp-utils/rp-expand-vars.h>

#include "ctl-lib-plugin.h"
#include "ctl-lib.h"

/**
* internal ATM hidden structure holding an action
*/
struct ctl_action_s
{
	/** uid of the action */
	const char *uid;

	/** descriptor  */
	json_object *desc;
};

/***************************************************************************/
/***************************************************************************/
/**** METADATA                                                          ****/
/***************************************************************************/
/***************************************************************************/

/* find in the metadata in the root JSON-C description search for the key "metadata" */
int ctl_read_metadata(ctl_metadata_t *meta, json_object *metaobj)
{
        int rc = rp_jsonc_unpack(metaobj, "{ss,ss,s?s,s?s,s?o,s?s,s?s !}",
					"uid", &meta->uid,
					"api", &meta->api,
					"version", &meta->version,
					"info", &meta->info,
					"require", &meta->requireJ,
					"author", &meta->author,
					"date", &meta->date);
	if (rc) {
		AFB_ERROR("Invalid metadata: %s; in: %s",
				rp_jsonc_get_error_string(rc),
				json_object_to_json_string(metaobj));
		rc = -1;
	}
	return rc;
}

/* read the meta from main config */
int ctl_subread_metadata(ctl_metadata_t *meta, json_object *rootdesc, bool optional)
{
        int rc;
	json_object *metaobj;
	if (json_object_is_type(rootdesc, json_type_object)
	 && json_object_object_get_ex(rootdesc, "metadata", &metaobj))
		rc = ctl_read_metadata(meta, metaobj);
	else if (optional)
		rc = 0;
	else {
		AFB_ERROR("missing metadata in %s", json_object_get_string(rootdesc));
		rc = -1;
	}
	return rc;
}

/* callback for setting required apis */
static int set_require_cb(void *closure, json_object *required)
{
	afb_api_t api = closure;
	return afb_api_require_api(api, json_object_get_string(required), 1);
}

/* set to api the required api as set in meta */
int ctl_set_requires(ctl_metadata_t *meta, afb_api_t api)
{
	int rc = 0;
	if (meta->requireJ != NULL)
		rc = rp_jsonc_optarray_until(meta->requireJ, set_require_cb, api);
	return rc;
}

/***************************************************************************/
/***************************************************************************/
/**** ACTION AND ACTIONSET                                              ****/
/***************************************************************************/
/***************************************************************************/

/* search the action of uid in aset */
ctl_action_t *ctl_actionset_search(ctl_actionset_t *aset, const char *uid)
{
        ctl_action_t *iter = aset->actions, *end = &iter[aset->count];
        for ( ; iter != end ; iter++)
                if (0 == strcasecmp(uid, iter->uid))
                        return iter;
	return NULL;
}

/* free the actionset */
void ctl_actionset_free(ctl_actionset_t *actionset)
{
	actionset->count = 0;
	free(actionset->actions);
	actionset->actions = NULL;
}

/***************************************************************************/
/***************************************************************************/
/**** SCAN OF ACTIONS                                                   ****/
/***************************************************************************/
/***************************************************************************/

/* scanning result */
typedef struct
	{
                /* type of entry */
		enum { SUR_UNKNOWN, SUR_API, SUR_PLUGIN } type;
		struct
			{
				const char *string; /* base adress */
				unsigned length;    /* length in chars */
			}
                        /* the extracted segments */
			prefix, base, fragment;
	}
		scan_uri_result_t;

/* scan the uri */
static int scan_action_uri(const char *uri, scan_uri_result_t *sur)
{
	int begb, begf, end, rc = -1;

	end = 0;
	sscanf(uri, "%*[^:]://%n%*[^#]#%n%*s%n", &begb, &begf, &end);
	if (end == 0) {
		AFB_ERROR("bad action string %s", uri);
		rc = -1;
	}
	else {
		sur->prefix.string = uri;
		sur->prefix.length = begb - 3;
		sur->base.string = &uri[begb];
		sur->base.length = begf - begb - 1;
		sur->fragment.string = &uri[begf];
		sur->fragment.length = end - begf;
		if (sur->prefix.length == 3 && 0 == memcmp(sur->prefix.string, "api", 3))
			sur->type = SUR_API;
		else if (sur->prefix.length == 6 && 0 == memcmp(sur->prefix.string, "plugin", 6))
			sur->type = SUR_PLUGIN;
		else {
			AFB_ERROR("unknown action prefix %s", uri);
			sur->type = SUR_UNKNOWN;
		}
		if (sur->type != SUR_UNKNOWN) {
			if (sur->base.length == 0 || sur->fragment.length == 0)
				AFB_ERROR("unknown bad action uri %s", uri);
			else
				rc = 0;
		}
	}
	return rc;
}

/* scan in object the uri of key "action" */
static int scan_object_uri(json_object *obj, scan_uri_result_t *sur)
{
	json_object *uriJ;
	int rc;

	if (json_object_object_get_ex(obj, "action", &uriJ)
	 && json_object_is_type(uriJ, json_type_string))
		rc = scan_action_uri(json_object_get_string(uriJ), sur);
	else {
		AFB_ERROR("action string not in %s", json_object_to_json_string(obj));
		rc = -1;
	}
	return rc;
}

/*
 * scan in object the privileges of key "privileges"
 * and allocates and fills the structure auth
 */
static int scan_object_privileges(json_object *obj, afb_auth_t **auth)
{
	int rc = 0;
	json_object *privJ;
	*auth = NULL;
	if (json_object_object_get_ex(obj, "privileges", &privJ)) {
		if (!json_object_is_type(privJ, json_type_string)) {
			AFB_ERROR("bad privileges in %s", json_object_to_json_string(obj));
			rc = -1;
		}
		else {
			*auth = malloc(sizeof **auth);
			if (*auth == NULL) {
				AFB_ERROR("out of memory");
				rc = -1;
			}
			else {
				(*auth)->type = afb_auth_Permission;
				(*auth)->text = json_object_get_string(privJ);
				(*auth)->next = NULL;
			}
		}
	}
	return rc;
}

/* record the action described by actionJ in the actionset of closure */
static int add_action_cb(void *closure, json_object *actionJ)
{
        const char *uri, *uid, *info, *privilege;
	json_object *argsJ;
	scan_uri_result_t sur;
	ctl_actionset_t *actionset = closure;
	ctl_action_t *action = &actionset->actions[actionset->count];

	/* scan if action is conform ? */
	int rc = rp_jsonc_unpack(actionJ, "{ss,ss,s?s,s?s,s?o}",
                        "uid", &uid,
                        "action", &uri,
                        "info", &info,
                        "privileges", &privilege,
                        "args", &argsJ);
        if (rc != 0) {
		/* no?! report the error */
                AFB_ERROR("Invalid action: %s; in: %s", rp_jsonc_get_error_string(rc), json_object_to_json_string(actionJ));
                rc = -1;
        }
	else if (argsJ != NULL && !json_object_is_type(argsJ, json_type_array) && !json_object_is_type(argsJ, json_type_object)) {
                AFB_ERROR("Invalid arguments for action: %s; in: %s", rp_jsonc_get_error_string(rc), json_object_to_json_string(actionJ));
                rc = -1;
	}
        else {
		rc = scan_action_uri(uri, &sur);
		if (rc == 0) {
			/* valid, then record it */
			action->uid = uid;
			action->desc = actionJ;
			actionset->count++;
		}
        }
        return rc;
}

/* add actions of actionsJ in actionset */
int ctl_read_actionset_extend(ctl_actionset_t *actionset, json_object *actionsJ)
{
	int rc;
	unsigned oldcount = actionset->count;
	unsigned addcount = json_object_is_type(actionsJ, json_type_array) ? (unsigned)json_object_array_length(actionsJ) : 1;
	unsigned newcount = oldcount + addcount;
	ctl_action_t *actions = reallocarray(actionset->actions, newcount, sizeof *actions);
	if (actions == NULL) {
		AFB_ERROR("out of memory");
		rc = -1;
	}
	else {
		actionset->actions = actions;
		memset(&actions[oldcount], 0, newcount * sizeof *actions);
		rc = rp_jsonc_optarray_until(actionsJ, add_action_cb, actionset);
	}
	return rc;
}

/* add in actionset the actions described at key in obj */
int ctl_subread_actionset(ctl_actionset_t *actionset, json_object *config, const char *key)
{
	json_object *entries = !key ? config
	                            : json_object_is_type(config, json_type_object) ? json_object_object_get(config, key)
				                                                    : NULL;
	if (entries == NULL)
		return 0;
	return ctl_read_actionset_extend(actionset, entries);
}

/***************************************************************************/
/***************************************************************************/
/**** DEFAULT PATH                                                      ****/
/***************************************************************************/
/***************************************************************************/

int ctl_default_path_search(rp_path_search_t **ps, const char *subdir)
{
        char buffer[200];

        *ps = NULL; /* ensure NULL on error */
        if (subdir == NULL)
	        return rp_path_search_make_dirs(ps, "${AFB_ROOTDIR}:${AFB_WORKDIR}");

        snprintf(buffer, sizeof buffer, "${AFB_ROOTDIR}/%s:${AFB_WORKDIR}/%s", subdir, subdir);
        return rp_path_search_make_dirs(ps, buffer);
}

/***************************************************************************/
/***************************************************************************/
/**** SCAN OF PLUGINS                                                   ****/
/***************************************************************************/
/***************************************************************************/

/* structure for callback in searching plugins */
typedef struct {
        /* searched uid */
        const char *uid;
        /* base name of the search */
        const char *basename;
        /* the plugin store */
        plugin_store_t *plugins;
} search_plugin_data_t;

static int search_plugin(void *closure, const char *path, size_t length)
{
        static const char *patterns[] = {
                        "%s/%s",
                        "%s/%s.so",
                        "%s/%s.ctlso",
                        NULL
                };

        search_plugin_data_t *spd = closure;
        char buffer[PATH_MAX+1];
        const char **iter = patterns;

        /* for each pattern */
        for ( ; *iter != NULL ; iter++) {
                /* makes the path for the name */
                snprintf(buffer, sizeof buffer - 1, *iter, path, spd->basename);
                buffer[sizeof buffer - 1] = 0;
                /* try loading it */
                if (plugin_store_load(spd->plugins, buffer, spd->uid, NULL) == 0) {
                        /* loaded, check an exported symbol for the UID */
			void *ptr = plugin_store_get_object(*spd->plugins, spd->uid, CTL_PLUGIN_TAG);
                        /* check that the eventually found symbol matches the plugin heading */
			if (CTL_PLUGIN_CHECK(ptr)) {
                                /* yes, we are done */
				AFB_INFO("loaded plugin uid %s path %s (uid=%s - info=%s)",
						spd->uid, buffer,
						CTL_PLUGIN_UID(ptr) ?: "",
						CTL_PLUGIN_INFO(ptr) ?: "");
				return 1;
			}
			AFB_WARNING("presumed and loaded plugin doesn't fit check cretiria path %s", buffer);
		}
	}
        return 0;
}

/* structure for adding plugins */
typedef struct {
        /* the plugin store */
        plugin_store_t *plugins;
        /* the search path to use */
        rp_path_search_t *paths;
} add_plugin_data_t;

/* add one plugin */
static int add_plugin_cb(void *closure, json_object *pluginJ)
{
        search_plugin_data_t spd;
        add_plugin_data_t *apd = closure;
	const char *sPath = NULL;
	json_object *fileJ = NULL;
        rp_path_search_t *paths;

        /* extract plugin's data */
	int rc = rp_jsonc_unpack(pluginJ, "{ss,s?s,s?o}",
				"uid", &spd.uid,
				"spath", &sPath,
				"libs", &fileJ
			);
	if (rc) {
		AFB_ERROR("invalid plugin description %s", json_object_get_string(pluginJ));
		return -1;
	}

	/* if search path not in Json config file, then try default */
        paths = rp_path_search_addref(apd->paths);
        if (sPath != NULL)
                rp_path_search_extend_dirs(&paths, sPath, 1);

	/* get first name */
	spd.basename = spd.uid;
        if (fileJ != NULL) {
                if (!json_object_is_type(fileJ, json_type_array))
                        spd.basename = json_object_get_string(fileJ);
                else if (json_object_array_length(fileJ) > 0)
                        spd.basename = json_object_get_string(json_object_array_get_idx(fileJ, 0));
        }

        /* search the first name */
        spd.plugins = apd->plugins;
        rc = rp_path_search_list(paths, search_plugin, &spd);
        if (rc <= 0 && json_object_is_type(fileJ, json_type_array)) {
                /* search the next names */
                unsigned i = 1, n = (unsigned)json_object_array_length(fileJ);
                for (; i < n && rc <= 0 ; i++) {
                        spd.basename = json_object_get_string(json_object_array_get_idx(fileJ, i));
                        rc = rp_path_search_list(paths, search_plugin, &spd);
                }
        }
        if (rc <= 0) {
                AFB_ERROR("not able to locate plugin %s", json_object_to_json_string(pluginJ));
                rc = -1;
        }

        rp_path_search_unref(paths);
        return rc;
}

/* extend plugin store with plugin described by pluginsJ */
int ctl_read_plugins_extend(plugin_store_t *plugins, json_object *pluginsJ, rp_path_search_t *paths)
{
        int rc;
        add_plugin_data_t apd;

        apd.plugins = plugins;
        if (paths == NULL)
                ctl_default_path_search(&apd.paths, "lib");
        else
                apd.paths = rp_path_search_addref(paths);
        if (apd.paths == NULL) {
                AFB_ERROR("out of memory");
                rc = -1;
        }
	else {
                rc = rp_jsonc_optarray_until(pluginsJ, add_plugin_cb, &apd);
                rp_path_search_unref(apd.paths);
        }
        return rc;
}

/* extend plugin store with plugin described by item of object config at the given key */
int ctl_subread_plugins(plugin_store_t *plugins, json_object *config, rp_path_search_t *paths, const char *key)
{
	json_object *entries;

        if (key == NULL)
                entries = config;
        else if (json_object_is_type(config, json_type_object))
                entries = json_object_object_get(config, key);
        else
	        entries = NULL;
	if (entries == NULL)
		return 0;
	return ctl_read_plugins_extend(plugins, entries, paths);
}

/***************************************************************************/
/***************************************************************************/
/**** PREPARING ARGUMENTS FOR CALLS AND SUBCALLS                        ****/
/***************************************************************************/
/***************************************************************************/

/* structur for preparing arguments */
typedef struct {
	/* the count of arguments */
	unsigned nargs;
	/* the arguments */
	afb_data_t const *args;
	/* pointer to be freed (can be NULL) */
	void *tobefreed;
} prepared_args_t;

/* function for preparing arguments
 *
 * those functions take:
 *   - an object from the definition of the action
 *   - a count of input params
 *   - an array of params
 *   - a pointer for receiving prepared data
 */
typedef int (prepare_call_func_t)(
		json_object      *argsJ,
		unsigned          nparams,
		afb_data_t const  params[],
		prepared_args_t  *prep);

/* this function copies the JSON object in from,
 * adds it a copy of the content of 'and'
 * and return it wrapped in 'to'
 */
static int meld_data_object(afb_data_t from, json_object *and, afb_data_t *to)
{
	int rc = afb_data_convert(from, AFB_PREDEFINED_TYPE_JSON_C, &from);
	if (rc == 0) {
		json_object *fromJ = rp_jsonc_clone(afb_data_ro_pointer(from));
		afb_data_unref(from);
                rp_jsonc_object_add(fromJ, and);
		*to = afb_data_json_c_hold(fromJ);
	}
	return rc;
}

/* prepare direct just addref to params */
static int prepare_call_direct(json_object *argsJ, unsigned nparams, afb_data_t const params[], prepared_args_t *prep)
{
	afb_data_array_addref(prep->nargs = nparams, prep->args = params);
	prep->tobefreed = NULL;
	return 0;
}

/* prepare meld first just melds the first parameter with argsJ */
static int prepare_call_meld_first(json_object *argsJ, unsigned nparams, afb_data_t const params[], prepared_args_t *prep)
{
	int rc;
	unsigned idx, nargs = nparams + !nparams;
	afb_data_t *args = malloc(nargs * sizeof *args);
	if (args == NULL)
		return AFB_ERRNO_OUT_OF_MEMORY;
	if (nparams == 0) {
		rc = afb_create_data_raw(&args[0], AFB_PREDEFINED_TYPE_JSON_C, argsJ, 0, NULL, NULL);
		if (rc < 0) {
			free(args);
			return AFB_ERRNO_OUT_OF_MEMORY;
		}
	}
	else {
		rc = meld_data_object(params[0], argsJ, &args[0]);
		if (rc < 0) {
			free(args);
			return AFB_ERRNO_INVALID_REQUEST;
		}
	}
	for (idx = 1 ; idx < nparams ; idx++)
		args[idx] = afb_data_addref(params[idx]);
	prep->nargs = nargs;
	prep->args = args;
	prep->tobefreed = args;
	return 0;
}

/* prepare meld all melds all the parameter of the array argsJ */
static int prepare_call_meld_all(json_object *argsJ, unsigned nparams, afb_data_t const params[], prepared_args_t *prep)
{
	int rc;
	json_object *obj;
	unsigned idx, narr = (unsigned)json_object_array_length(argsJ) > 0;
	unsigned nargs = narr > nparams ? narr : nparams;

	afb_data_t *args = malloc(nparams * sizeof *args);
	if (args == NULL)
		return AFB_ERRNO_OUT_OF_MEMORY;

	for (idx = 0 ; idx < nargs ; idx++) {
		if (idx >= narr)
			args[idx] = afb_data_addref(params[idx]);
		else {
			obj = json_object_array_get_idx(argsJ, idx);
			if (idx >= nparams)
				rc = afb_create_data_raw(&args[idx], AFB_PREDEFINED_TYPE_JSON_C, obj, 0, NULL, NULL);
			else
				rc = meld_data_object(params[idx], obj, &args[idx]);
			if (rc < 0) {
				while (idx)
					afb_data_unref(args[--idx]);
				free(args);
				return rc;
			}
		}
	}
	prep->nargs = nargs;
	prep->args = args;
	prep->tobefreed = args;
	return 0;
}

/***************************************************************************/
/***************************************************************************/
/**** IMPLEMENT ACTIONS AS VERBS                                        ****/
/***************************************************************************/
/***************************************************************************/

/* structure for subcall verbs */
typedef struct {
	/* api to call */
	const char *api;
	/* verb to call */
	const char *verb;
	/* arguments to meld */
	json_object *argsJ;
	/* preparer function */
	prepare_call_func_t *preparer;
} call_api_data_t;

/* creates the call_api_data_t result accordingly to sur and argsJ */
static int prepare_call_api_data(
		call_api_data_t **result,
		const scan_uri_result_t *sur,
		json_object *argsJ
) {
	int rc;
	char *ptr;
	call_api_data_t *cad;

	/* allocate with strings */
	*result = cad = malloc(2 + sur->base.length + sur->fragment.length + sizeof *cad);
	if (cad == NULL) {
		AFB_ERROR("out of memory");
		rc = AFB_ERRNO_OUT_OF_MEMORY;
	}
	else {
		/* initialize the strings api & verb */
		ptr = (char*)&cad[1];
		cad->api = ptr;
		memcpy(ptr, sur->base.string, sur->base.length);
		ptr[sur->base.length] = 0;
		ptr = &ptr[sur->base.length + 1];
		cad->verb = ptr;
		memcpy(ptr, sur->fragment.string, sur->fragment.length);
		ptr[sur->fragment.length] = 0;

		/* initialize preparer of arguments */
		cad->argsJ = argsJ;
		if (json_object_is_type(argsJ, json_type_array) && json_object_array_length(argsJ) > 0)
			cad->preparer = prepare_call_meld_all;
		else if (json_object_is_type(argsJ, json_type_object) && json_object_object_length(argsJ) > 0)
			cad->preparer = prepare_call_meld_first;
		else
			cad->preparer = prepare_call_direct;

		rc = 0;
	}
	return rc;
}

/* structure for plugin verbs */
typedef struct {
	/* arguments of the action */
	json_object *argsJ;
	/* data as passed when creating the verb */
	void *data;
	/* the callback function */
	union {
		CTLPLUG_FUNC_T(plugin_call)    call;
		CTLPLUG_FUNC_T(plugin_subcall) subcall;
	}
		callback;
	/* the type of the callback */
	enum {
		cpt_type_call,
		cpt_type_subcall
	}
		type;
} call_plugin_data_t;


/* creates the call_plugin_data_t result accordingly to sur and argsJ */
static int prepare_call_plugin_data(
		call_plugin_data_t **result,
		const scan_uri_result_t *sur,
		json_object *argsJ,
		plugin_store_t pstore,
		void *data
) {
	int rc = -1;
	const void *plug;
	call_plugin_data_t *cpd;
	char *plugname, *objname;

	/* allocation */
	cpd = malloc(sizeof *cpd);
	plugname = strndup(sur->base.string, sur->base.length);
	objname = strndup(sur->fragment.string, sur->fragment.length);
	if (plugname == NULL || objname == NULL || cpd == NULL)
		AFB_ERROR("out of memory");
	else {
		/* search the described entry */
		plug = plugin_store_get_object(pstore, plugname, objname);
		if (plug == NULL)
			AFB_ERROR("can't locate plugin %s#%s", plugname, objname);
		else {
			cpd->argsJ = argsJ;
			cpd->data = data;
			/* check the entry type */
			if (CTLPLUG_CHECK(plug, plugin_call)) {
				cpd->callback.call = CTLPLUG_DATA(plug, plugin_call);
				cpd->type = cpt_type_call;
				rc = 0;
			}
			else if (CTLPLUG_CHECK(plug, plugin_subcall)) {
				cpd->callback.subcall = CTLPLUG_DATA(plug, plugin_subcall);
				cpd->type = cpt_type_subcall;
				rc = 0;
			}
			else if (!ctlplug_check_magic(plug))
				AFB_ERROR("plugin %s#%s doesn't match magic", plugname, objname);
			else
				AFB_ERROR("bad tag of plugin %s#%s (found type %s)", plugname, objname, ctlplug_tag(plug));
		}
	}
	/* release memory */
	free(objname);
	free(plugname);
	if (rc < 0) {
		free(cpd);
		cpd = NULL;
	}
	*result = cpd;
	return rc;
}

/* asynchronous subcall reply handler */
static void verb_subcall_response_cb(void *closure, int status, unsigned nreplies, afb_data_t const replies[], afb_req_t req)
{
	afb_data_array_addref(nreplies, replies);
	afb_req_reply(req, status, nreplies, replies);
	free(closure);
}

/* subcall api implementation */
static void verb_subcall_cb(afb_req_t request, unsigned nparams, afb_data_t const params[])
{
	prepared_args_t prep;
	call_api_data_t *cad = afb_req_get_vcbdata(request);
	int rc = cad->preparer(cad->argsJ, nparams, params, &prep);
	if (rc < 0)
		afb_req_reply(request, rc, 0, NULL);
	else
		afb_req_subcall(request, cad->api, cad->verb,
				prep.nargs, prep.args,
				afb_req_subcall_pass_events | afb_req_subcall_on_behalf,
				verb_subcall_response_cb, prep.tobefreed);
}

/* add the verb described by the subcall api action */
static int add_verb_api(
		afb_api_t api,
		const char *verbname,
		const char *info,
		const scan_uri_result_t *sur,
		afb_auth_t *auth,
		json_object *argsJ
) {
	int rc;
	call_api_data_t *cad;

	/* prepare the call */
	rc = prepare_call_api_data(&cad, sur, argsJ);
	if (rc >= 0) {
		/* add the verb */
		rc = afb_api_add_verb(api, verbname, info, verb_subcall_cb, cad, auth, 0, 0);
		if (rc < 0) {
			AFB_API_ERROR(api, "creation of verb %s failed (%d)", verbname, rc);
			free(cad);
			free(auth);
		}
	}
	return rc;
}

/* subcall plugin implementation */
static void plugin_subcall_verb_cb(afb_req_t request, unsigned nparams, afb_data_t const params[])
{
	call_plugin_data_t *cpd  = afb_req_get_vcbdata(request);
	cpd->callback.subcall(request, nparams, params, cpd->argsJ, cpd->data);
}

/* call plugin implementation */
static void plugin_call_verb_cb(afb_req_t request, unsigned nparams, afb_data_t const params[])
{
	call_plugin_data_t *cpd  = afb_req_get_vcbdata(request);
	afb_api_t api = afb_req_get_api(request);
	afb_req_reply(request, 0, 0, NULL);
	cpd->callback.call(api, nparams, params, cpd->argsJ, cpd->data);
}

/* add the verb described by the plugin action */
static int add_verb_plugin(
		afb_api_t api,
		const char *verbname,
		const char *info,
		const scan_uri_result_t *sur,
		afb_auth_t *auth,
		json_object *argsJ,
		plugin_store_t pstore,
		void *data
) {
	call_plugin_data_t *cpd;
	int rc = prepare_call_plugin_data(&cpd, sur, argsJ, pstore, data);
	if (rc >= 0) {
		switch(cpd->type) {
		case cpt_type_subcall:
			rc = afb_api_add_verb(api, verbname, info, plugin_subcall_verb_cb, cpd, auth, 0, 0);
			break;
		case cpt_type_call:
			rc = afb_api_add_verb(api, verbname, info, plugin_call_verb_cb, cpd, auth, 0, 0);
			break;
		default:
			rc = -8;
			break;
		}
		if (rc < 0) {
			free(cpd);
			AFB_API_ERROR(api, "can't create verb %s failed (%d)", verbname, rc);
		}
	}
	return rc;
}

/* add to the api the verb described by action */
int ctl_action_add_verb(ctl_action_t *action, afb_api_t api, plugin_store_t pstore, void *data)
{
	scan_uri_result_t sur;
	afb_auth_t *auth;
	json_object *argsJ, *infoJ;
	const char *info;
	int rc;

	rc = scan_object_uri(action->desc, &sur);
	if (rc == 0)
		rc = scan_object_privileges(action->desc, &auth);
	if (rc == 0) {
		infoJ = json_object_object_get(action->desc, "info");
		info = infoJ == NULL ? NULL : json_object_get_string(infoJ);
		argsJ = json_object_object_get(action->desc, "args");
		switch(sur.type) {
		case SUR_API:
			rc = add_verb_api(api, action->uid, info, &sur, auth, argsJ);
			break;
		case SUR_PLUGIN:
			rc = add_verb_plugin(api, action->uid, info, &sur, auth, argsJ, pstore, data);
			break;
		default:
			rc = -1;
			break;
		};
	}
	return rc;
}

/* add to the api the verbs of the actionset */
int ctl_actionset_add_verbs(ctl_actionset_t *actionset, afb_api_t api, plugin_store_t pstore, void *data)
{
	int rc = 0;
	ctl_action_t *iter = actionset->actions;
	ctl_action_t *end = &iter[actionset->count];
	while (rc >= 0 && iter != end)
		rc = ctl_action_add_verb(iter++, api, pstore, data);
	return rc;
}

/***************************************************************************/
/***************************************************************************/
/**** IMPLEMENT ACTIONS AS EVENT HANDLERS                               ****/
/***************************************************************************/
/***************************************************************************/

/* asynchronous call reply handler */
static void event_call_response_cb(void *closure, int status, unsigned nreplies, afb_data_t const replies[], afb_api_t api)
{
	free(closure);
}

/* implement event handler calling an API */
static void event_call_cb(
		void *closure,
		const char *event_name,
		unsigned nparams,
		afb_data_t const params[],
		afb_api_t api
) {
	prepared_args_t prep;
	call_api_data_t *cad = closure;
	int rc = cad->preparer(cad->argsJ, nparams, params, &prep);
	if (rc < 0)
		AFB_API_ERROR(api, "on event %s, preparation of call %s/%s failed %d", event_name, cad->api, cad->verb, rc);
	else
		afb_api_call(api, cad->api, cad->verb,
				prep.nargs, prep.args,
				event_call_response_cb, prep.tobefreed);
}

/* add the event handler for calling an api */
static int add_event_api(
		afb_api_t api,
		const char *eventname,
		const scan_uri_result_t *sur,
		json_object *argsJ
) {
	int rc;
	call_api_data_t *cad;

	/* prepare the call */
	rc = prepare_call_api_data(&cad, sur, argsJ);
	if (rc >= 0) {
		/* add the verb */
		rc = afb_api_event_handler_add(api, eventname, event_call_cb, cad);
		if (rc < 0) {
			free(cad);
			AFB_API_ERROR(api, "creation of even handler %s failed (%d)", eventname, rc);
		}
	}
	return rc;
}

/* implement event handler calling a plugin */
static void plugin_call_event_cb(
		void *closure,
		const char *event_name,
		unsigned nparams,
		afb_data_t const params[],
		afb_api_t api
) {
	call_plugin_data_t *cpd  = closure;
	cpd->callback.call(api, nparams, params, cpd->argsJ, cpd->data);
}

/* implement event handler subcalling a plugin */
static void plugin_subcall_event_cb(
		void *closure,
		const char *event_name,
		unsigned nparams,
		afb_data_t const params[],
		afb_api_t api
) {
	call_plugin_data_t *cpd  = closure;
	cpd->callback.subcall(NULL, nparams, params, cpd->argsJ, cpd->data);
}

/* add the event handler for calling a plugin */
static int add_event_plugin(
		afb_api_t api,
		const char *eventname,
		const scan_uri_result_t *sur,
		json_object *argsJ,
		plugin_store_t pstore,
		void *data
) {
	call_plugin_data_t *cpd;
	int rc = prepare_call_plugin_data(&cpd, sur, argsJ, pstore, data);
	if (rc >= 0) {
		switch(cpd->type) {
		case cpt_type_subcall:
			rc = afb_api_event_handler_add(api, eventname, plugin_subcall_event_cb, cpd);
			break;
		case cpt_type_call:
			rc = afb_api_event_handler_add(api, eventname, plugin_call_event_cb, cpd);
			break;
		default:
			rc = -8;
			break;
		}
		if (rc < 0) {
			free(cpd);
			AFB_API_ERROR(api, "can't create event handler %s failed (%d)", eventname, rc);
		}
	}
	return rc;
}

/* add the event handler described by action */
int ctl_action_add_event(ctl_action_t *action, afb_api_t api, plugin_store_t pstore, void *data)
{
	scan_uri_result_t sur;
	json_object *argsJ;
	int rc;

	rc = scan_object_uri(action->desc, &sur);
	if (rc == 0) {
		argsJ = json_object_object_get(action->desc, "args");
		switch(sur.type) {
		case SUR_API:
			rc = add_event_api(api, action->uid, &sur, argsJ);
			break;
		case SUR_PLUGIN:
			rc = add_event_plugin(api, action->uid, &sur, argsJ, pstore, data);
			break;
		default:
			rc = -1;
			break;
		};
	}
	return rc;
}

/* add the event handlers of the actionset */
int ctl_actionset_add_events(ctl_actionset_t *actionset, afb_api_t api, plugin_store_t pstore, void *data)
{
	int rc = 0;
	ctl_action_t *iter = actionset->actions;
	ctl_action_t *end = &iter[actionset->count];
	while (rc >= 0 && iter != end)
		rc = ctl_action_add_event(iter++, api, pstore, data);
	return rc;
}

/***************************************************************************/
/***************************************************************************/
/**** EXECUTE ACTIONS                                                   ****/
/***************************************************************************/
/***************************************************************************/

/* execute the API call described by sur and argsJ */
static int exec_api(
		afb_api_t api,
		const scan_uri_result_t *sur,
		json_object *argsJ
) {
	int rc;
	call_api_data_t *cad;
	prepared_args_t prep;

	/* prepare the call */
	rc = prepare_call_api_data(&cad, sur, argsJ);
	if (rc >= 0) {
		rc = cad->preparer(cad->argsJ, 0, NULL, &prep);
		if (rc >= 0)
			afb_api_call(api, cad->api, cad->verb,
					prep.nargs, prep.args,
					event_call_response_cb, prep.tobefreed);
		free(cad);
	}
	return rc;
}

/* execute the plugin call described by sur and argsJ */
static int exec_plugin(
		afb_api_t api,
		const scan_uri_result_t *sur,
		json_object *argsJ,
		plugin_store_t pstore,
		void *data
) {
	call_plugin_data_t *cpd;
	int rc = prepare_call_plugin_data(&cpd, sur, argsJ, pstore, data);
	if (rc >= 0) {
		switch(cpd->type) {
		case cpt_type_subcall:
			cpd->callback.subcall(NULL, 0, NULL, argsJ, data);
			break;
		case cpt_type_call:
			cpd->callback.call(api, 0, NULL, argsJ, data);
			break;
		default:
			rc = -8;
			break;
		}
		free(cpd);
	}
	return rc;
}

/* execute the action */
int ctl_action_exec(ctl_action_t *action, afb_api_t api, plugin_store_t pstore, void *data)
{
	scan_uri_result_t sur;
	json_object *argsJ;
	int rc;

	rc = scan_object_uri(action->desc, &sur);
	if (rc == 0) {
		argsJ = json_object_object_get(action->desc, "args");
		switch(sur.type) {
		case SUR_API:
			rc = exec_api(api, &sur, argsJ);
			break;
		case SUR_PLUGIN:
			rc = exec_plugin(api, &sur, argsJ, pstore, data);
			break;
		default:
			rc = -1;
			break;
		};
		if (rc < 0)
			AFB_API_ERROR(api, "can't call %s failed (%d)", sur.prefix.string, rc);
	}
	return rc;
}

/* execute actions of the actionset */
int ctl_actionset_exec(ctl_actionset_t *actionset, afb_api_t api, plugin_store_t pstore, void *data)
{
	int rc = 0;
	ctl_action_t *iter = actionset->actions;
	ctl_action_t *end = &iter[actionset->count];
	while (rc >= 0 && iter != end)
		rc = ctl_action_exec(iter++, api, pstore, data);
	return rc;
}
