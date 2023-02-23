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

#ifndef _CTL_COMPAT_INCLUDED_
#define _CTL_COMPAT_INCLUDED_
#ifdef __cplusplus
extern "C" {
#endif

/*
* This file is implementing an emulation of the controller
* for binding already coded for using it.
* It makes the bridge to access the new implementation of
* ctl-lib.
*/

#include "ctl-lib.h"


typedef void CtlSourceT;
typedef void CtlActionT;

typedef struct ConfigSectionS {
  const char *key;
  const char *uid;
  const char *info;
  int (*loadCB)(afb_api_t apihandle, struct ConfigSectionS *section, json_object *sectionJ);
  void *handle;
  CtlActionT *actions;
} CtlSectionT;

typedef struct {
    const char *api;
    const char *uid;
    const char *info;
    const char *version;
    const char *author;
    const char *date;
    json_object *configJ;
    json_object *requireJ;
    CtlSectionT *sections;
    CtlPluginT *ctlPlugins;
    void *external;
} CtlConfigT;


typedef struct ctl_metadata_s ctl_metadata_t CtlConfigT;



// ctl-action.c
extern int AddActionsToSection(afb_api_t apiHandle, CtlSectionT *section, json_object *actionsJ, int exportApi);
extern CtlActionT *ActionConfig(afb_api_t apiHandle, json_object *actionsJ,  int exportApi);
extern void ActionExecUID(afb_req_t request, CtlConfigT *ctlConfig, const char *uid, json_object *queryJ);

// ctl-config.c
extern void* getExternalData(CtlConfigT *ctlConfig);
extern void setExternalData(CtlConfigT *ctlConfig, void *data);
extern char* ConfigSearch(afb_api_t apiHandle, json_object *responseJ);
extern char* CtlConfigSearch(afb_api_t apiHandle, const char *dirList, const char *prefix) ;
extern int CtlConfigExec(afb_api_t apiHandle, CtlConfigT *ctlConfig) ;
extern CtlConfigT *CtlLoadMetaDataJson(afb_api_t apiHandle,json_object *ctlConfigJ);
extern CtlConfigT *CtlLoadMetaData(afb_api_t apiHandle,const char* filepath);
extern int CtlLoadSections(afb_api_t apiHandle, CtlConfigT *ctlHandle, CtlSectionT *sections);

// ctl-event.c
extern int EventConfig(afb_api_t apihandle, CtlSectionT *section, json_object *actionsJ);

// ctl-control.c
extern int ControlConfig(afb_api_t apiHandle, CtlSectionT *section, json_object *actionsJ);

// ctl-onload.c
extern int OnloadConfig(afb_api_t apiHandle, CtlSectionT *section, json_object *actionsJ);








// ctl-plugin.c


#include "ctl-plugin.h"


typedef struct {
    const char *uid;
    const char *info;
    afb_api_t api;
    void *dlHandle;
    void *context;
    json_object *paramsJ;
} CtlPluginT;


typedef int(*DispatchPluginInstallCbT)(CtlPluginT *plugin, void* handle);


#define CTLP_CAPI_REGISTER(pluglabel) CTL_PLUGIN_DECLARE(pluglabel)



#define CTLP_CAPI(funcname, source, argsJ, queryJ) int funcname(CtlSourceT *source, json_object* argsJ, json_object* queryJ)

#define CTLP_ONLOAD(plugin, handle) int CtlPluginOnload(CtlPluginT *plugin, void* handle)
#define CTLP_INIT(plugin, handle) int CtlPluginInit(CtlPluginT *plugin, void* handle)

extern int PluginConfig(afb_api_t apiHandle, CtlSectionT *section, json_object *pluginsJ);


















#ifdef __cplusplus
}
#endif
#endif /* _CTL_COMPAT_INCLUDED_ */
