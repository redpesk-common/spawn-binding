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

#ifndef CTL_LIB_H
#define CTL_LIB_H

/*
* includes
*/
#include <stdbool.h>
#include <json-c/json.h>
#include <afb-helpers4/plugin-store.h>
#include <rp-utils/rp-path-search.h>

/*
* predeclare types
*/
typedef struct ctl_metadata_s  ctl_metadata_t;
typedef struct ctl_action_s    ctl_action_t;
typedef struct ctl_actionset_s ctl_actionset_t;

/***************************************************************************/
/***************************************************************************/
/**** METADATA                                                          ****/
/***************************************************************************/
/***************************************************************************/
/**
* metadata of the configuration file
*/
struct ctl_metadata_s
{
        /** uid */
	const char *uid;

        /** name of the api */
	const char *api;

        /** optional info */
	const char *info;

        /** optional version */
	const char *version;

        /** optional author */
	const char *author;

        /** option date */
	const char *date;

        /** holds the config, allowing reference to strings of subobjects */
	json_object *configJ;

        /** object holding requirements of the api */
	json_object *requireJ;
};

/**
* Reads in the metadata object 'meta' from the JSON-C object 'metaobj'.
* 'metaobj' should at least contain the strings of keys "uid" and "api".
*
* @param meta    the meta structure to fill
* @param metaobj the configuration object to read
*
* @return 0 on success or a negative error code
*/
extern int ctl_read_metadata(ctl_metadata_t *meta, json_object *metaobj);

/**
* Reads in the metadata object 'meta' from the subobject of JSON-C object 'rootdesc'
* of key "metadata". For short, calls 'ctl_read_metadata' on subobject of key "metadata"
* if present.
*
* @param meta     the meta structure to fill
* @param rootdesc the root configuration object
* @param optional tells if metaobject is optional or not
*
* @return 0 on success or a negative error code
*/
extern int ctl_subread_metadata(ctl_metadata_t *meta, json_object *rootdesc, bool optional);

/**
* Look in the meta and if requirement are set, calls the function
* 'afb_api_require_api' accordingly.
*
* @param meta the meta object
* @param api  the api to setup
*
* @return 0 on success or a negative error code
*/
extern int ctl_set_requires(ctl_metadata_t *meta, afb_api_t api);

/***************************************************************************/
/***************************************************************************/
/**** ACTIONSETS                                                        ****/
/***************************************************************************/
/***************************************************************************/
/**
* the action set record actions
*/
struct ctl_actionset_s
{
	/** count of actions */
	unsigned count;

	/** the actions */
	ctl_action_t *actions;
};

/**
* initializer of the actionset
*/
#define CTL_ACTIONSET_INITIALIZER  ((struct ctl_actionset_s){ .count = 0, .actions = NULL })

/**
* Free the content of the given actionset
*
* @param actionset the actionset to free
*/
extern void ctl_actionset_free(ctl_actionset_t *actionset);

/**
* Extend the actionset with the actions described by actionsJ
*
* @param actionset the actionset to extend
* @param actionsJ description of the actions to add
*
* @return 0 on success or a negative error code
*/
extern int ctl_read_actionset_extend(ctl_actionset_t *actionset, json_object *actionsJ);

/**
* Extend the actionset with the actions described by the subobject
* of key in config.
*
* Alias of ctl_read_actionset_add
*
* @param actionset the actionset to extend
* @param config    main config object
* @param key       key of the subobject holding actions descrption
*
* @return 0 on success or a negative error code
*/
extern int ctl_subread_actionset(ctl_actionset_t *actionset, json_object *config, const char *key);

/**
* Add to api all the verbs described by the actions of the actionset
* When the action requires a plugin, it is searched in pstore and data is the closure
*
* @param actionset the actionset
* @param api       the api where verbs are added
* @param pstore    the plugin store
* @param data      a closure for the plugins callbacks
*
* @return 0 on success or a negative error code
*/
extern int ctl_actionset_add_verbs(ctl_actionset_t *actionset, afb_api_t api, plugin_store_t pstore, void *data);

/**
* Add to api the event handlers described by the actions of the actionset
* When the handler requires a plugin, it is searched in pstore and data is the closure
*
* @param actionset the actionset
* @param api       the api handling events
* @param pstore    the plugin store
* @param data      a closure for the plugins callbacks
*
* @return 0 on success or a negative error code
*/
extern int ctl_actionset_add_events(ctl_actionset_t *actionset, afb_api_t api, plugin_store_t pstore, void *data);

/**
* execute within the api all the actions of the actionset
* When the action requires a plugin, it is searched in pstore and data is the closure
*
* @param actionset the actionset
* @param api       the api for binding calls
* @param pstore    the plugin store
* @param data      a closure for the plugins callbacks
*
* @return 0 on success or a negative error code
*/
extern int ctl_actionset_exec(ctl_actionset_t *actionset, afb_api_t api, plugin_store_t pstore, void *data);

/***************************************************************************/
/***************************************************************************/
/**** ACTIONS                                                           ****/
/***************************************************************************/
/***************************************************************************/

/**
* search the action of given uid and returns it pointer
*
* @param aset the actionset to search
* @param uid  the uid to search
*
* @return the pointer to the action found or NULL if not found
*/
extern ctl_action_t *ctl_actionset_search(ctl_actionset_t *aset, const char *uid);

/**
* Add to api the verb described by the action
* When the action requires a plugin, it is searched in pstore and data is the closure
*
* @param action the action
* @param api    the api where verbs are added
* @param pstore the plugin store
* @param data   a closure for the plugins callbacks
*
* @return 0 on success or a negative error code
*/
extern int ctl_action_add_verb(ctl_action_t *action, afb_api_t api, plugin_store_t pstore, void *data);

/**
* Add to api the event handler described by the action
* When the handler requires a plugin, it is searched in pstore and data is the closure
*
* @param action the action
* @param api    the api handling events
* @param pstore the plugin store
* @param data   a closure for the plugins callbacks
*
* @return 0 on success or a negative error code
*/
extern int ctl_action_add_event(ctl_action_t *action, afb_api_t api, plugin_store_t pstore, void *data);

/**
* execute within the api all the given action
* When the action requires a plugin, it is searched in pstore and data is the closure
*
* @param actionset the actionset
* @param api       the api for binding calls
* @param pstore    the plugin store
* @param data      a closure for the plugins callbacks
*
* @return 0 on success or a negative error code
*/
extern int ctl_action_exec(ctl_action_t *action, afb_api_t api, plugin_store_t pstore, void *data);

/***************************************************************************/
/***************************************************************************/
/**** PLUGINS                                                           ****/
/***************************************************************************/
/***************************************************************************/

/**
* Extend the plugin store with the plugins described in pluginsJ.
* Effective plugin libraries are searched in the given search path.
*
* @param plugins  the plugin store to extend
* @param pluginsJ description of the plugins to add
* @param path     the search path (or NULL for the default one)
*
* @return 0 on success or a negative error code
*/
extern int ctl_read_plugins_extend(plugin_store_t *plugins, json_object *pluginsJ, rp_path_search_t *path);

/**
* Extend the plugin store with the plugins described by the subobject of config of the given key.
* Effective plugin libraries are searched in the given search path.
*
* @param plugins  the plugin store to extend
* @param config   description of the plugins to add
* @param path     the search path (or NULL for the default one)
* @param key      key of the subobject in config haloding the plugin description
*
* @return 0 on success or a negative error code
*/
extern int ctl_subread_plugins(plugin_store_t *plugins, json_object *config, rp_path_search_t *path, const char *key);

/**
* make the path search object for serching files in the given subdir
*
* @param ps     pointer for the result
* @param subdir sub directory of search or NULL if none
*
* @return 0 and *ps on success or a negative error code
*/
extern int ctl_default_path_search(rp_path_search_t **ps, const char *subdir);

#endif  /* CTL_LIB_H */
