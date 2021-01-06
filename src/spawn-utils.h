/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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


#ifndef _SPAWN_UTILS_INCLUDE_
#define _SPAWN_UTILS_INCLUDE_

#define  AFB_BINDING_VERSION 3
#include <afb/afb-binding.h>
#include <systemd/sd-event.h>

#include "spawn-defaults.h"
#include <json.h>

#ifdef MEMFD_CREATE_MISSING
  // missing from Fedora, OpenSuse, ... !!!
  int memfd_create (const char *name, unsigned int __flags);
#endif

// spawn-utils.c
mode_t utilsUmaskSet (const char *mask);
int utilsTaskPrivileged(void);

int utilsFileStat (const char *filepath, int mode);
ssize_t utilsFileLoad (const char *filepath, char **buffer);
int utilsFileAddControl (afb_api_t api, const char *uid, int dirFd, const char *ctrlname, const char *ctrlval);
const char* utilsExecCmd (afb_api_t api, const char* source, const char* command);
int utilsExecFdCmd (afb_api_t api, const char* source, const char* command);
int utilsGetPathInod (const char* path);

const char* utilsExpandString (spawnDefaultsT *defaults, const char* inputS, const char* prefix, const char* trailer);
const char* utilsExpandKey (const char* inputString);
const char* utilsExpandJson (const char* src, json_object *keysJ);

#endif /* _SPAWN_UTILS_INCLUDE_ */