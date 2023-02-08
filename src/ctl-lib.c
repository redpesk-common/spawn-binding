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

#include <afb/afb-binding.h>
#include <rp-utils/rp-jsonc.h>

#include "ctl-lib.h"

/**
* find in the metadata the in the root JSON-C descrption
*/
int ctl_metadata_read_json(ctl_metadata_t *meta, json_object *rootdesc)
{
	json_object *metadataJ;
	if (!json_object_is_type(rootdesc, json_type_object)
	 || !json_object_object_get_ex(rootdesc, "metadata", &metadataJ)) {
		AFB_ERROR("missing metadata in %s", json_object_get_string(rootdesc));
		return -1;
	}
	if (rp_jsonc_unpack(metadataJ, "{ss,ss,ss,s?s,s?o,s?s,s?s !}",
					"uid", &meta->uid,
					"version", &meta->version,
					"api", &meta->api,
					"info", &meta->info,
					"require", &meta->requireJ,
					"author", &meta->author,
					"date", &meta->date)) {
		AFB_ERROR("Invalid metadata:\n-- %s", json_object_get_string(metadataJ));
		return -1;
	}
	return 0;
}
