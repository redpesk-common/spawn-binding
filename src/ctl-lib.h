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
* This file is a compatibility wrapper for accompagning
* transition from the older controler to the world without it.
*
* First of all, the type CtlConfigT is emulated by the
* type ctl_metadata_t that hold meta data of the configuration file.
*/
typedef struct ctl_metadata_s ctl_metadata_t;
typedef ctl_metadata_t CtlConfigT;
/*
*
*/
#include <json-c/json.h>

/**
* metadata of the configuration file
*/
struct ctl_metadata_s {
	const char *api;
	const char *uid;
	const char *info;
	const char *version;
	const char *author;
	const char *date;
	json_object *configJ;
	json_object *requireJ;
/*
	CtlSectionT *sections;
	CtlPluginT *ctlPlugins;
*/
	void *external;
};


extern int ctl_metadata_read_json(ctl_metadata_t *meta, json_object *rootdesc);


typedef struct {
	const char *uid;
	const char *info;
/*
	const char *privileges;
	json_object *argsJ;
	afb_api_t api;
	CtlActionTypeT type;
	union {
		struct {
			const char* api;
			const char* verb;
		} subcall;

		struct {
			const char* plugin;
			const char* funcname;
		} lua;

		struct {
			const char* funcname;
			int (*callback)(CtlSourceT *source, json_object *argsJ, json_object *queryJ);
			CtlPluginT *plugin;
		} cb;
	} exec;
*/
} CtlActionT;














typedef struct ctl_action_s ctl_action_t;
typedef struct ctl_actionner_s ctl_actionner_t;

struct ctl_actionner_s
{
	int (*apply)(ctl_action_t*);
	int (*free)(ctl_action_t*);
	int (*create)(ctl_action_t*);
	const char *prefix;
};

struct ctl_action_s
{
	/** the actionner */
	ctl_actionner_t *actionner;

	/** data of the actionner */
	void *data;

	/** uid of the action */
	const char *uid;

	/** info about the action or NULL */
	const char *info;

	/** required privilege */
	const char *privilege;
};

extern int ctl_action_actionner_allow_std_api();
extern int ctl_action_actionner_register(ctl_actionner_t *actionner);

int ctl_action_exec(ctl_action_t *action);

int ctl_action_exec(ctl_action_t *action);


#endif  /* CTL_LIB_H */
