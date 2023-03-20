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


#ifndef _SPAWN_BINDING_INCLUDE_
#define _SPAWN_BINDING_INCLUDE_

#include <afb/afb-binding.h>
#include <afb-helpers4/ctl-lib.h>

// anonymous type definition to allow anonymous type in .h
typedef struct shellCmdS shellCmdT;
typedef struct taskIdS   taskIdT;
typedef struct sandBoxS  sandBoxT;

/**
* structure holding one api
*/
typedef struct {
	/** the API */
	afb_api_t api;

	/** meta data from controller */
	ctl_metadata_t metadata;

	/** on-start controller actions */
	ctl_actionset_t onstart;

	/** on-events controller actions */
	ctl_actionset_t onevent;

	/** extra verbs of controller actions */
	ctl_actionset_t extra;

	/** holder for the configuration */
	json_object *config;

	/** the sandboxes*/
	sandBoxT *sandboxes;
}
	spawnApiT;




#endif /* _SPAWN_BINDING_INCLUDE_ */
