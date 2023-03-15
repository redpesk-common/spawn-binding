#include <stdlib.h>

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>
#include <afb-helpers4/ctl-lib-plugin.h>


CTL_PLUGIN_DECLARE("test-ctl-plug", "simple test plugin");

static void call_response_cb(void *closure, int status, unsigned nreplies, afb_data_t const replies[], afb_api_t api)
{
}

CTLPLUG_EXPORT_FUNC(plugin_call,
void,
on_call, /* <- exported name */
	afb_api_t api,
	unsigned  nparams,
	afb_data_t const params[],
	json_object *args,
	void *context
) {
	int sts;
	afb_data_t data;
	AFB_API_NOTICE(api, "hello from test-ctl-plug/call");
	afb_create_data_raw(&data, AFB_PREDEFINED_TYPE_JSON_C, &args, 0, 0, 0);
	afb_api_call(api, "ctl", "distro", 1, &data, call_response_cb, NULL);
	AFB_API_NOTICE(api, "end from test-ctl-plug/call");
}

static void subcall_response_cb(void *closure, int status, unsigned nreplies, afb_data_t const replies[], afb_req_t request)
{
	afb_data_array_addref(nreplies, replies);
	afb_req_reply(request, status, nreplies, replies);
	AFB_REQ_NOTICE(request, "end from test-ctl-plug/subcall");
}

CTLPLUG_EXPORT_FUNC(plugin_subcall,
void,
on_subcall, /* <- exported name */
	afb_req_t request,
	unsigned  nparams,
	afb_data_t const params[],
	json_object *args,
	void *context
) {
	unsigned n = 10;
	afb_data_t data[10];
	int sts;

	if (request == NULL) { // !!!
		AFB_NOTICE("hello from test-ctl-plug/subcall BUT ...");
	}
	else {
		AFB_REQ_NOTICE(request, "hello from test-ctl-plug/subcall");
		afb_data_array_addref(nparams, params);
		afb_req_subcall(request, "ctl", "sync", nparams, params, 0, subcall_response_cb, NULL);
	}
}

CTLPLUG_EXPORT_FUNC(plugin_call,
void,
my_exit, /* <- exported name */
	afb_api_t api,
	unsigned  nparams,
	afb_data_t const params[],
	json_object *args,
	void *context
) {
	AFB_API_NOTICE(api, "hello from test-ctl-plug/exit");
	exit(0);
}

/* end */
