/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <curl/curl.h>
#include <sys/types.h>

#include <systemd/sd-event.h>

typedef struct httpCtxS{
	void *context;
	char *data;
	size_t size;
    CURL *easy;
    afb_req_t request;
    char error[CURL_ERROR_SIZE];
} httpCtxT;

typedef struct multiCtxS{
	void *context;
    sd_event *evtLoop;
    sd_event_source *timer;
    CURLM *multi;
	void (*callback)(httpCtxT *http, CURLcode status);
} multiCtxT;