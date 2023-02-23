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

#ifndef _CTL_LIB_PLUGIN_H_INCLUDED_
#define _CTL_LIB_PLUGIN_H_INCLUDED_

/*
* defines the entry points for plugins of ctl-lib
*/

#include <json-c/json.h>
#include <afb/afb-binding.h>

#include "ctlplug.h"

/* the type and identification tag for identifying plugin compatible */
#ifndef _CTL_PLUGIN_ID_
#define _CTL_PLUGIN_ID_ ctl_lib_plugin_magic
#endif
#define CTL_PLUGIN_ID            _CTL_PLUGIN_ID_
#define CTL_PLUGIN_TAG           CTLPLUG_TAG(CTL_PLUGIN_ID)

/* the plugins are embedding an UID and an infirmative text */
typedef struct { const char *uid, *info; } _ctl_lib_plugin_desc_t_;
CTLPLUG_DEFINE(CTL_PLUGIN_ID,_ctl_lib_plugin_desc_t_)

/**
* Macro for declaring that a code is plugin compatible
* The arguments are its UID and its INFO, both are text.
*/
#define CTL_PLUGIN_DECLARE(uid,info)  CTLPLUG_DECLARE(CTL_PLUGIN_ID,CTL_PLUGIN_ID,((_ctl_lib_plugin_desc_t_){ uid, info }))

/**
* That macro check if the given pointer identifies a plugin declaration
* Return true if it is the case or else returns false
*/
#define CTL_PLUGIN_CHECK(ptr)         CTLPLUG_CHECK(ptr,CTL_PLUGIN_ID)

/**
* Macro returning the uid of the plugin header under ptr.
* Only valid if CTL_PLUGIN_CHECK(ptr) returned true
*/
#define CTL_PLUGIN_UID(ptr)           (CTL_PLUGIN_CHECK(ptr) ? CTLPLUG_DATA(ptr,CTL_PLUGIN_ID).uid : NULL)

/**
* Macro returning the info text of the plugin header under ptr.
* Only valid if CTL_PLUGIN_CHECK(ptr) returned true
*/
#define CTL_PLUGIN_INFO(ptr)          (CTL_PLUGIN_CHECK(ptr) ? CTLPLUG_DATA(ptr,CTL_PLUGIN_ID).info : NULL)

/**
*/
CTLPLUG_DEFINE_FUNC(plugin_call, void, afb_api_t, unsigned, afb_data_t const[], json_object *, void *)

/**
*/
CTLPLUG_DEFINE_FUNC(plugin_subcall, void, afb_req_t, unsigned, afb_data_t const[], json_object *, void *)

#endif /* _CTL_LIB_PLUGIN_H_INCLUDED_ */
