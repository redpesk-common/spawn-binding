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

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include "spawn-utils.h"
#include "spawn-defaults.h"

#include <ctl-config.h>
#include <filescan-utils.h>
#include <pthread.h>
#include <sys/stat.h>


static int SandboxConfig(afb_api_t api, CtlSectionT *section, json_object *sandboxesJ);
static int AutostartConfig(afb_api_t api, CtlSectionT *section, json_object *sandboxesJ);

// keep autostart to run ot after init
static CtlSectionT *autostartSection=NULL;

// Config Section definition (note: controls section index should match handle
// retrieval in HalConfigExec)
static CtlSectionT ctrlSections[] = {
    { .key = "plugins",.loadCB = PluginConfig, .handle= (void*) &encoderPluginCb},
    { .key = "onload", .loadCB    = AutostartConfig },
    { .key = "sandboxes", .loadCB = SandboxConfig },
    { .key = "events", .loadCB    = EventConfig },
    { .key = NULL }
};

static void PingTest (afb_req_t request) {
    static int count=0;
    char response[32];
    json_object *queryJ =  afb_req_json(request);

    snprintf (response, sizeof(response), "Pong=%d", count++);
    AFB_API_NOTICE (request->api, "shell:ping count=%d query=%s", count, json_object_get_string(queryJ));
    afb_req_success_f(request,json_object_new_string(response), NULL);

    return;
}

static void InfoCmds (sandBoxT *sandbox, json_object *responseJ) {

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

        wrap_json_pack (&usageJ, "{so so*}", "action", actionsJ, "args", cmd->usageJ);
        wrap_json_pack (&cmdJ, "{ss ss ss* so so*}"
        , "uid",  cmd->uid
        , "verb", cmd->apiverb
        , "info", cmd->info
        , "usage", usageJ
        , "sample", cmd->sampleJ
        );

        json_object_array_add (responseJ, cmdJ);
    }
}

static void Infosandbox (afb_req_t request) {
    int err, idx;
    sandBoxT *sandboxes = (sandBoxT*) afb_req_get_vcbdata(request);
    json_object *responseJ= json_object_new_array();

    // build global info page for developper dynamic HTML5 page
    json_object *globalJ, *sandboxJ, *sandboxesJ;
    CtlConfigT* ctlConfig = (CtlConfigT*)afb_api_get_userdata(afb_req_get_api(request));
    err= wrap_json_pack (&globalJ, "{ss ss* ss* ss*}", "uid", ctlConfig->uid, "info",ctlConfig->info, "version", ctlConfig->version, "author", ctlConfig->author);
    if (err) {
        AFB_DEBUG ("Fail to wrap json binding metadata");
        goto OnErrorExit;
    }

    // loop on shell command sandboxes
    sandboxesJ= json_object_new_array();
    for (idx=0; sandboxes[idx].uid; idx++) {
        json_object *cmdsJ= json_object_new_array();
        // create sandbox object with sandbox_info and sandbox-cmds
        InfoCmds (&sandboxes[idx], cmdsJ);
        err += wrap_json_pack (&sandboxJ, "{ss ss* so*}"
            , "uid", sandboxes[idx].uid
            , "info", sandboxes[idx].info
            , "verbs", cmdsJ
            );
        if (err) {
            AFB_DEBUG ("Fail to wrap json cmds info sandbox=%s", sandboxes[idx].uid);
            goto OnErrorExit;
        }
        json_object_array_add(sandboxesJ, sandboxJ);
    }

    err= wrap_json_pack (&responseJ, "{so so}", "metadata", globalJ, "groups", sandboxesJ);
    if (err) {
        AFB_DEBUG ("Fail to wrap json binding global response");
        goto OnErrorExit;
    }

    afb_req_success(request, responseJ, NULL);
    return;

OnErrorExit:
    return;
}

// Static verb not depending on shell json config file
static afb_verb_t CtrlApiVerbs[] = {
    /* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
    { .verb = "ping",     .callback = PingTest    , .info = "shell API ping test"},
    { .verb = "info",     .callback = Infosandbox     , .info = "shell List sandboxes"},
    { .verb = NULL} /* marker for end of the array */
};

static int CtrlLoadStaticVerbs (afb_api_t api, afb_verb_t *verbs, void *vcbdata) {
    int errcount=0;

    for (int idx=0; verbs[idx].verb; idx++) {
        errcount+= afb_api_add_verb(api, CtrlApiVerbs[idx].verb, CtrlApiVerbs[idx].info, CtrlApiVerbs[idx].callback, vcbdata, 0, 0,0);
    }

    return errcount;
};

static void cmdApiRequest(afb_req_t request) {
    // retrieve action handle from request and execute the request
    json_object *queryJ = afb_req_json(request);
    shellCmdT* cmd = (shellCmdT*) afb_req_get_vcbdata(request);
    spawnTaskVerb (request, cmd, queryJ);
}

static int cmdLoadOne(afb_api_t api, sandBoxT *sandbox, shellCmdT *cmd, json_object *cmdJ) {
    int err = 0;
    const char *privilege=NULL;
    afb_auth_t *authent=NULL;
    json_object *execJ=NULL, *encoderJ=NULL;

    // should already be allocated
    assert (cmdJ);

    // set default values
    memset(cmd, 0, sizeof (shellCmdT));
    cmd->sandbox   = sandbox;
    cmd->magic = MAGIC_SPAWN_CMD;

    // default verbose is sandbox->verbose
    cmd->verbose= -1;

    // parse shell command and lock format+exec object if defined
    err = wrap_json_unpack(cmdJ, "{ss,s?s,s?i,s?i,s?s,s?o,s?o,s?o,s?o,s?b !}"
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
    if (cmd->verbose<0) cmd->verbose=sandbox->verbose;

    // find encode/decode callback
    cmd->api = api;
    err = encoderFind (cmd, encoderJ);
    if (err) goto OnErrorExit;

    // If not special privilege use sandbox one
    if (!privilege) privilege= sandbox->privilege;
    if (privilege) {
       authent= (afb_auth_t*) calloc(1, sizeof (afb_auth_t));
       authent->type = afb_auth_Permission;
       authent->text = privilege;
    }

    // use sandbox timeout as default
    if (!cmd->timeout && sandbox->acls) cmd->timeout= sandbox->acls->timeout;

    // pre-parse command to boost runtime execution
    err= spawnParse (cmd, execJ);
    if (err) goto OnErrorExit;

    // initialize semaphore to protect tids hashtable
    err = pthread_rwlock_init(&cmd->sem, NULL);
    if (err < 0) {
        AFB_API_ERROR(api, "[fail init semaphore] API sandbox=%s cmd=%s", sandbox->uid, cmd->uid);
        goto OnErrorExit;
    }

    // if prefix not empty add it to verb api
    if (!sandbox->prefix) cmd->apiverb = cmd->uid;
    else {if (asprintf ((char**) &cmd->apiverb, "%s/%s", sandbox->prefix, cmd->uid)<0) goto OnErrorExit;}

    err = afb_api_add_verb(api, cmd->apiverb, cmd->info, cmdApiRequest, cmd, authent, 0, 0);
    if (err) {
        AFB_API_ERROR(api, "[fail to register] API sandbox=%s cmd=%s verb=%s", sandbox->uid, cmd->uid, cmd->apiverb);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return 1;
}

static int sandboxLoadOne(afb_api_t api, sandBoxT *sandbox, json_object *sandboxJ) {
    int err = 0;
    json_object *cmdsJ, *namespaceJ=NULL, *capsJ=NULL, *aclsJ=NULL, *cgroupsJ=NULL, *envsJ=NULL, *seccompJ=NULL;

    // should already be allocated
    assert (sandboxJ);
    assert (api);
    json_object_get(sandboxJ);

    memset(sandbox, 0, sizeof (sandBoxT)); // default is empty
    sandbox->magic= MAGIC_SPAWN_SBOX;

    // user 'O' to force json objects not to be released
    err = wrap_json_unpack(sandboxJ, "{ss,s?s,s?s,s?s,s?i,s?o,s?o,s?o,s?o,s?o,s?o,so !}"
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
        AFB_API_ERROR(api, "[Fail-to-parse] sandbox config JSON='%s'", json_object_to_json_string(sandboxJ));
        goto OnErrorExit;
    }

    // if verbose is 5 then check string expansion works
    if (sandbox->verbose) utilsExpandJsonDebug(); // in verbose test parsing before forking a child

    if (envsJ) {
            sandbox->envs= sandboxParseEnvs(api, sandbox, envsJ);
            if (!sandbox->envs) goto OnErrorExit;
    }

    if (aclsJ) {
        sandbox->acls= sandboxParseAcls(api, sandbox, aclsJ);
        if (!sandbox->acls) goto OnErrorExit;
    } else {
        if (utilsTaskPrivileged()) {
            AFB_API_ERROR(api, "[security-error] ### privilege mode uid='%s' requirer acls->user=xxxx ###", sandbox->uid);
            goto OnErrorExit;
        }
    }

    if (capsJ) {
        sandbox->caps= sandboxParseCaps(api, sandbox, capsJ);
        if (!sandbox->caps) goto OnErrorExit;
    }

    if (seccompJ) {
        sandbox->seccomp= sandboxParseSecRules(api, sandbox, seccompJ);
        if (!sandbox->seccomp) goto OnErrorExit;
    }

    if (cgroupsJ) {
        if (!utilsTaskPrivileged()) {
            AFB_API_NOTICE(api, "[cgroups ignored] sandbox=%s user=%d not privileged (sandboxLoadOne)", sandbox->uid, getuid());
        } else {
            sandbox->cgroups= sandboxParseCgroups(api, sandbox, cgroupsJ);
            if (!sandbox->cgroups) goto OnErrorExit;
        }
    }

    // if namespace defined parse try to parse it
    if (namespaceJ) {
        sandbox->namespace = sandboxParseNamespace (api, sandbox, namespaceJ);
        if (!sandbox->namespace) goto OnErrorExit;
    }

    // loop on cmds
    if (json_object_is_type(cmdsJ, json_type_array)) {
        int count = (int)json_object_array_length(cmdsJ);
        sandbox->cmds= (shellCmdT*)calloc(count+1, sizeof (shellCmdT));

        for (int idx = 0; idx < count; idx++) {
            json_object *cmdJ = json_object_array_get_idx(cmdsJ, idx);
            err = cmdLoadOne(api, sandbox, &sandbox->cmds[idx], cmdJ);
            if (err) goto OnErrorExit;
        }

    } else {
        sandbox->cmds= (shellCmdT*) calloc(2, sizeof(shellCmdT));
        err= cmdLoadOne(api, sandbox, &sandbox->cmds[0], cmdsJ);
        if (err) goto OnErrorExit;
    }

    return 0;

 OnErrorExit:
    if (sandbox->namespace) free(sandbox->namespace);
    if (sandbox->caps) free(sandbox->caps);
    if (sandbox->acls) free(sandbox->acls);
    return 1;
}

static int AutostartConfig(afb_api_t api, CtlSectionT *section, json_object *onloadJ) {

    // run autostart section after everything is loaded
    if (!onloadJ) return 0;

    // save autostart to run it after sealing API
    autostartSection= section;


    // delagate autostart parsing to controller builtin OnloadConfig
    return (OnloadConfig (api, section, onloadJ));
}

static int SandboxConfig(afb_api_t api, CtlSectionT *section, json_object *sandboxesJ) {
    sandBoxT *sandboxes;
    spawnBindingT *binding= calloc(1, sizeof(spawnBindingT));
    binding->magic= MAGIC_SPAWN_BDING;
    binding->api  = api;
    int err;

    // config is processed nothing more to be done
    if (!sandboxesJ) {
        return 0;
    }

    // shell array is close with a nullvalue;
    if (json_object_is_type(sandboxesJ, json_type_array)) {
        int count = (int)json_object_array_length(sandboxesJ);
        sandboxes =  (sandBoxT*) calloc(count + 1, sizeof (sandBoxT));

        for (int idx = 0; idx < count; idx++) {
            json_object *sandboxJ = json_object_array_get_idx(sandboxesJ, idx);
            err = sandboxLoadOne(api, &sandboxes[idx], sandboxJ);
            if (err) {
                AFB_API_ERROR(api, "[sandboxLoadOne fail] to load sandbox=%s (SandboxConfig)", sandboxes[idx].uid);
                goto OnErrorExit;
            }
            // attach binding to newly created sandbox
            sandboxes[idx].binding= binding;
            // when having more than one sandboxes default prefix is sandbox->uid
            if (!sandboxes[idx].prefix) sandboxes[idx].prefix= sandboxes[idx].uid;
        }

    } else {
        sandboxes = (sandBoxT*)calloc(2, sizeof (sandBoxT));
        err = sandboxLoadOne(api, &sandboxes[0], sandboxesJ);
        if (err) {
            AFB_API_ERROR(api, "[sandboxLoadOne fail] to load sandbox=%s (SandboxConfig)", sandboxes[0].uid);
            goto OnErrorExit;
        }
        // attach binding to newly created sandbox
        sandboxes[0].binding= binding;
    }

    // add static controls verbsmake
    err = CtrlLoadStaticVerbs (api, CtrlApiVerbs, (void*)sandboxes);
    if (err) {
        AFB_API_ERROR(api, "[CtrlLoadStaticVerbs fail] to Registry static API verbs (SandboxConfig)");
        goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    AFB_API_ERROR (api, "[Fail config spawn-binding] ### check json config ###");
    return -1; // force binding kill
}

static int CtrlInitOneApi(afb_api_t api) {
    int err = 0;

    // retrieve section config from api handle
    CtlConfigT* ctlConfig = (CtlConfigT*)afb_api_get_userdata(api);

    err = CtlConfigExec(api, ctlConfig);
    if (err) {
        AFB_API_ERROR(api, "Error at CtlConfigExec step");
        return err;
    }

    return err;
}

static int CtrlLoadOneApi(void* vcbdata, afb_api_t api) {
    CtlConfigT* ctlConfig = (CtlConfigT*)vcbdata;

    // save closure as api's data context
    afb_api_set_userdata(api, ctlConfig);

    // load section for corresponding API
    int error = CtlLoadSections(api, ctlConfig, ctrlSections);

    // init and seal API function
    afb_api_on_init(api, CtrlInitOneApi);
    afb_api_seal(api);

    // call autoload now that API is active
    if (!error && autostartSection) OnloadConfig (api, autostartSection, NULL);

    return error;
}

int afbBindingEntry (afb_api_t api) {
    int status = 0;
    char *searchPath=NULL, *envConfig, *configpath, *prefix;
    afb_api_t handle;
    char *configs[SPAWN_MAX_CONF_FILE+1];
    int confcount=0;

    AFB_API_NOTICE(api, "Spawn Controller in afbBindingEntry");

    // register builtin encoders before plugin get load
    (void)encoderInit();

    configpath= getenv("AFB_SPAWN_CONFIG");
    if (!configpath) {

        envConfig= getenv("AFB_SPAWN_PATH");
        if (!envConfig) envConfig = CONTROL_CONFIG_PATH;

        status=asprintf (&searchPath,"%s:%s/etc", envConfig, GetBindingDirPath(api));
        if (status < 0) goto OnErrorExit;
        AFB_API_DEBUG(api, "json config directory : %s", searchPath);

        prefix = "control";
        configpath= CtlConfigSearch(api, searchPath, prefix);
        if (!configpath) {
            AFB_API_ERROR(api, "### [config-not-found] No %s-%s* found in %s \n### [should set] AFB_SPAWN_CONFIG) ###", prefix, GetBinderName(), searchPath);
            status = ERROR;
            goto OnErrorExit;
        }
        configs[confcount++]= configpath;
    } else {
        if (utilsFileModeIs (configpath, S_IFDIR)) {
            json_object *confdirJ= ScanForConfig(configpath ,false, GetBinderName(), ".json");
            if (!confdirJ) {
                AFB_API_ERROR(api, "### spawn-binding invalid config directory='%s' ###", configpath);
                status = ERROR;
                goto OnErrorExit;
            }
            // loop on directory searching for config
            for (int idx=0; idx< json_object_array_length(confdirJ); idx++) {
                json_object *slotJ = json_object_array_get_idx(confdirJ, idx);
                const char*fullpath, *filename;
                int err=wrap_json_unpack(slotJ, "{ss ss}", "fullpath", &fullpath, "filename", &filename);
                if (err) {
                    AFB_API_ERROR(api, "### spawn-binding config not found directory='%s'", configpath);
                    status = ERROR;
                    goto OnErrorExit;
                }
                if(asprintf (&configs[confcount++], "%s/%s", fullpath, filename)<0){
                    goto OnErrorExit;
                }
            }

        } else {
            // split string into multiple configpath
            for (char* filepath=strtok(configpath, ":"); filepath; filepath=strtok(NULL, ":")) {
                if (confcount == SPAWN_MAX_CONF_FILE) break;
                configs[confcount++]=strdup(filepath);
            }
        }
    }
    configs[confcount]=NULL; // terminate config file list

    // loop on config file and create one API per file
    for (int idx=0; configs[idx]; idx++) {
        // load config file and create API
        const char *pathfile= utilsExpandKey(configs[idx]);
        CtlConfigT* ctlConfig = CtlLoadMetaData(api, configs[idx]);
        if (!ctlConfig) {
            AFB_API_ERROR(api, "### spawn-binding invalid json config -- check with 'jq <%s' ###",  pathfile);
            status = ERROR;
            goto OnErrorExit;
        }

        // create one API per config file (Pre-V3 return code ToBeChanged)
        handle = afb_api_new_api(api, ctlConfig->api, ctlConfig->info, 1, CtrlLoadOneApi, ctlConfig);
        status = (handle) ? 0 : -1;
        if (status < 0) {
            AFB_API_ERROR(api, "### spawn-binding fail to register api='%s' File='%s' ###", ctlConfig->api, pathfile);
            goto OnErrorExit;
        }

        free (configs[idx]);
    }
    return status;

OnErrorExit:
    if (searchPath) free(searchPath);
    return status;
}
