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


#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <sys/stat.h>

// we need AFB definition to access binder config API
#include <afb/afb-binding.h>
#include <rp-utils/rp-uuid.h>

#include "spawn-sandbox.h"
#include "spawn-expand.h"
#include "spawn-expand-defs.h"

enum what_is_wanted {
    WANT_SANDBOX_UID,
    WANT_COMMAND_UID,
    WANT_API_NAME,
};

static char*GetEnviron(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    const char*key= dflt;
    const char*value;

    if (!label) return NULL;

    value= getenv(label);
    if (!value) {
        if (key) {
            value=key;
        } else {
            value="#undef";
        }
    }
    return (char*)value;
}

static char*GetUuidString(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    char *uuid = malloc(RP_UUID_STRINGZ_LENGTH);
    rp_uuid_new_stringz(uuid);
    return uuid;
}

static char*GetDateString(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    #define MAX_DATE_LEN 80
    time_t now= time(NULL);
    char *date= malloc(MAX_DATE_LEN);
    struct tm *time= localtime(&now);

    strftime (date, MAX_DATE_LEN, "%d-%b-%Y %T (%Z)",time);
    return date;
}

static char*GetUid(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    char string[10];
    uid_t uid= getuid();
    snprintf (string, sizeof(string), "%d",uid);
    return strdup(string);
}

static char*GetGid(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    char string[10];
    gid_t gid= getgid();
    snprintf (string, sizeof(string), "%d",gid);
    return strdup(string);
}

static char*GetPid(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    char string[10];
    pid_t pid= getpid();
    snprintf (string, sizeof(string), "%d",pid);
    return strdup(string);
}

static char*GetBindingRoot(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    const char *rootdir= getenv("AFB_ROOTDIR") ?: ".";
    return (char*)rootdir;
}

static char*GetBindingSettings(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    json_object *settings= afb_api_settings(afbBindingRoot);
    const char *value= json_object_get_string(settings);
    return (char*)value;
}

static char*GetObjectUid(const char *label, void *ctx, spawnExpandSpecificT *specific) {
    enum what_is_wanted request= (enum what_is_wanted)(intptr_t)ctx;
    if (specific) {
        switch (specific->type) {
        case expand_sandbox:
            switch (request) {
            case  WANT_SANDBOX_UID:
                return (char*)(specific->value.sandbox->uid);
            case  WANT_API_NAME:
                return (char*)(afb_api_name(specific->value.sandbox->binding->api));
            default:
                break;
            }
            break;
        case expand_cmd:
            switch (request) {
            case  WANT_COMMAND_UID:
                return (char*)(specific->value.cmd->uid);
            case  WANT_SANDBOX_UID:
                return (char*)(specific->value.cmd->sandbox->uid);
            case  WANT_API_NAME:
                return (char*)(afb_api_name(specific->value.cmd->api));
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    return NULL;
}

// return user id as defined within sandbox
static char*GetSandBoxUser(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    char string[10];
    if (!specific || specific->type != expand_sandbox || !specific->value.sandbox->acls)
        return NULL;
    snprintf (string, sizeof(string), "%d", specific->value.sandbox->acls->uid);
    return strdup(string);
}

// check if system file exit in /sbin otherwise prefix '/sbin' with '/usr'
static char*SelectSbinPath(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    const char *filepath = (const char*) dflt;
    struct stat statbuf;
    int err = lstat(filepath, &statbuf);

    // if file does not exist or is a symbolic link then system uses /usr/sbin
    if (err < 0 || (statbuf.st_mode & S_IFMT)==S_IFLNK) {
        return "/usr/sbin";
    }

    // filepath exist and is not a symlink /sbin is not is /usr/sbin
    return "/sbin";
}

static char*GetBinderMidName(const char *label, void *dflt, spawnExpandSpecificT *specific) {
    const char *rootdir = getenv("AFB_NAME") ?: "spawn-binding";
    return (char*)rootdir;
}

// Warning: REDDEFLT_CB will get its return free
spawnDefaultsT spawnVarDefaults[]= {
    // static strings
    {"LOGNAME"        , GetEnviron, SPAWN_MEM_STATIC, (void*)"Unknown"},
    {"HOSTNAME"       , GetEnviron, SPAWN_MEM_STATIC, (void*)"localhost"},
    {"HOME"           , GetEnviron, SPAWN_MEM_STATIC, (void*)"/sandbox"},

    {"AFB_ROOTDIR"    , GetBindingRoot, SPAWN_MEM_STATIC, NULL},
    {"AFB_CONFIG"     , GetBindingSettings, SPAWN_MEM_STATIC, NULL},
    {"AFB_NAME"       , GetBinderMidName, SPAWN_MEM_STATIC, NULL},

    {"SANDBOX_UID"    , GetObjectUid, SPAWN_MEM_STATIC, (void*)(intptr_t)WANT_SANDBOX_UID},
    {"COMMAND_UID"    , GetObjectUid, SPAWN_MEM_STATIC, (void*)(intptr_t)WANT_COMMAND_UID},
    {"API_NAME"       , GetObjectUid, SPAWN_MEM_STATIC, (void*)(intptr_t)WANT_API_NAME},

    {"SBINDIR"        , SelectSbinPath, SPAWN_MEM_STATIC, "/sbin/mkfs"},

    {"SBOXUSER"       , GetSandBoxUser, SPAWN_MEM_DYNAMIC, NULL},
    {"PID"            , GetPid, SPAWN_MEM_DYNAMIC, NULL},
    {"UID"            , GetUid, SPAWN_MEM_DYNAMIC, NULL},
    {"GID"            , GetGid, SPAWN_MEM_DYNAMIC, NULL},
    {"TODAY"          , GetDateString, SPAWN_MEM_DYNAMIC, NULL},
    {"UUID"           , GetUuidString, SPAWN_MEM_DYNAMIC, NULL},

    {NULL             , GetEnviron, SPAWN_MEM_STATIC, NULL} /* sentinel and default callback */
};
