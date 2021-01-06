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

#define _GNU_SOURCE

// we need AFB definition to access binder config API
#define  AFB_BINDING_VERSION 3
#include <afb/afb-binding.h>
#include <ctl-config.h>


#include "spawn-defaults.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <time.h>

static char*GetEnviron(const char *label, void *ctx) {
    const char*key= ctx;
    const char*value;

    value= getenv(label);
    if (!value) {
        if (key) {
            value=key;
        } else {
            value="#undef";
        }
    }
    return strdup(value);
}

static char*GetUuidString(const char *label, void *ctx) {
    char *uuid = malloc(37);
    uuid_t binuuid;

    uuid_generate_random(binuuid);
    uuid_unparse_lower(binuuid, uuid);

    return uuid;
}

static char*GetDateString(const char *label, void *ctx) {
    #define MAX_DATE_LEN 80
    time_t now= time(NULL);
    char *date= malloc(MAX_DATE_LEN);
    struct tm *time= localtime(&now);

    strftime (date, MAX_DATE_LEN, "%d-%b-%Y %h:%M (%Z)",time);
    return date;
}

static char*GetUid(const char *label, void *ctx) {
    char string[10];
    uid_t uid= getuid();
    snprintf (string, sizeof(string), "%d",uid);
    return strdup(string);
}

static char*GetGid(const char *label, void *ctx) {
    char string[10];
    gid_t gid= getgid();
    snprintf (string, sizeof(string), "%d",gid);
    return strdup(string);
}

static char*GetPid(const char *label, void *ctx) {
    char string[10];
    pid_t pid= getpid();
    snprintf (string, sizeof(string), "%d",pid);
    return strdup(string);
}

static char*GetBindingRoot(const char *label, void *ctx) {
    const char *rootdir= GetBindingDirPath(afbBindingRoot);
    return strdup(rootdir);
}

static char*GetBindingSettings(const char *label, void *ctx) {
    json_object *settings= afb_api_settings(afbBindingRoot);
    const char *value= json_object_get_string(settings);
    return strdup(value);
}

// Warning: REDDEFLT_CB will get its return free
spawnDefaultsT spawnVarDefaults[]= {
    // static strings
    {"LOGNAME"        , GetEnviron, (void*)"Unknown"},
    {"HOSTNAME"       , GetEnviron, (void*)"localhost"},
    {"HOME"           , GetEnviron, (void*)"/sandbox"},

    {"AFB_ROOTDIR"    , GetBindingRoot, NULL},
    {"AFB_CONFIG"     , GetBindingSettings, NULL},

    {"PID"            , GetPid, NULL},
    {"UID"            , GetUid, NULL},
    {"GID"            , GetGid, NULL},
    {"TODAY"          , GetDateString, NULL},
    {"UUID"           , GetUuidString, NULL},

    {NULL} /* sentinel */
};