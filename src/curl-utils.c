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
 *
 *  references:
 *    - https://curl.se/libcurl/c/curl_multi_perform.html
 *    - https://gist.github.com/lijoantony/4098139
 *
*/

#define _GNU_SOURCE

#include "spawn-binding.h"
#include "spawn-utils.h"
#include "spawn-enums.h"
#include "spawn-sandbox.h"
#include "spawn-subtask.h"
#include <ctl-config.h>

#include <errno.h>
#include <systemd/sd-event.h>

#include <stdio.h>
#include <string.h>

/* somewhat unix-specific */
#include <sys/time.h>
#include <unistd.h>

#include "curl-utils.h"

#define FLAGS_SET(v, flags) ((~(v) & (flags)) == 0)

// static unique multi handle
static multiCtxT *multiCtx=NULL;

static void requestCB (httpCtxT *request, CURLcode status) {
    fprintf (stderr, "**** in requestCB\n");
}

// callback might be called as many time as needed to transfert all data
static size_t easyRespondCB(void *data, size_t blkSize, size_t blkCount, void *ctx)  {
  httpCtxT *httpCtx = (httpCtxT*)ctx;
  size_t length= blkSize * blkCount;

  // final callback is called from multiCheckInfoCB when CURLMSG_DONE
  if (!data) return 0;

  httpCtx->data= realloc(httpCtx->data, httpCtx->size + length + 1);
  if(!httpCtx->data) return 0;  // hoops

  memcpy(&(httpCtx->data[httpCtx->size]), data, length);
  httpCtx->size += length;
  httpCtx->data[httpCtx->size] = 0;

  return length;
 }

static void multiCheckInfoCB (multiCtxT *multiCtx) {
  int count;
  CURLMsg *msg;
  httpCtxT *httpCtx;

    // read action resulting messages
     while ((msg = curl_multi_info_read(multiCtx->multi, &count))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            CURLcode status= msg->data.result;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &httpCtx);
            curl_multi_remove_handle(multiCtx->multi, easy);
            curl_easy_cleanup(msg->easy_handle);
            multiCtx->callback (httpCtx, status);
            break;
        }
    }
}

// call from systemd mainloop. Map event name and pass event to curl action loop
static int multiHttpCB (sd_event_source *source, int sock, uint32_t revents, void *ctx) {
    multiCtxT *multiCtx= (multiCtxT*)ctx;
    int action, running=0;

    // translate systemd event into curl event
    if (FLAGS_SET(revents, EPOLLIN | EPOLLOUT)) action= CURL_POLL_INOUT;
    else if (revents & EPOLLIN)  action= CURL_POLL_IN;
    else if (revents & EPOLLOUT) action= CURL_POLL_OUT;
    else action= 0;

    CURLMcode status= curl_multi_socket_action(multiCtx->multi, sock, action, &running);
    if (status != CURLM_OK) goto OnErrorExit;

    multiCheckInfoCB (multiCtx);
    return 0;

OnErrorExit:
    fprintf (stderr, "multiHttpCB: curl_multi_socket_action fail\n");
    return -1;
}

static int multiSocketCB (CURL *curl, int sock, int action, void *ctx, void *socketp) {
    sd_event_source *source= socketp;
    multiCtxT *multiCtx= (multiCtxT*)ctx;
    uint32_t events= 0;
    int err;

    // map CURL events with system events
    switch (action) {
      case CURL_POLL_REMOVE:
        if (source) {
            (void)sd_event_source_set_enabled(source, SD_EVENT_OFF);
            (void)sd_event_source_unref(source);
        }
        source =NULL;
        break;
      case CURL_POLL_IN:
        events= EPOLLIN;
        break;
      case CURL_POLL_OUT:
        events= EPOLLOUT;
        break;
      case CURL_POLL_INOUT:
        events= EPOLLIN|EPOLLOUT;
        break;
      default:
        goto OnErrorExit;
    }

    if (source) {
      err = sd_event_source_set_io_events(source, events);
      if (err < 0) goto OnErrorExit;

      err = sd_event_source_set_enabled(source, SD_EVENT_ON);
      if (err < 0) goto OnErrorExit;

    } else {

      // at initial call source does not exist, we create a new one and add it to sock context
      err= sd_event_add_io(multiCtx->evtLoop, &source, sock, events, multiHttpCB, multiCtx);
      if (err < 0) goto OnErrorExit;

      // add new created source to soeck context on 2nd call it will comeback as socketp
      err= curl_multi_assign(multiCtx->multi, sock, source);
      if (err != CURLM_OK) goto OnErrorExit;

      (void) sd_event_source_set_description(source, "afb-curl");
    }

    return 0;

OnErrorExit:
  fprintf (stderr, "multiSocketCB:  sd_event_source_set fail\n");
  return -1;
}

// Curl needs curl_multi_socket_action to be called on regular events
static int multiTimeoutCB(sd_event_source *timer, uint64_t usec, void *ctx) {
      multiCtxT *multiCtx= (multiCtxT*)ctx;
      int running= 0;

      curl_multi_perform(multiCtx->multi, &running);
      int err= curl_multi_socket_action(multiCtx->multi, CURL_SOCKET_TIMEOUT, 0, &running) ;
      if (err != CURLM_OK) goto OnErrorExit;

      multiCheckInfoCB (multiCtx);
      return 0;

OnErrorExit:
    fprintf (stderr, "multiTimeoutCB:  curl_multi_socket_action fail\n");
    return -1;
}


static int multiTimerCB (CURLM *curl, long timeout, void *ctx) {
    multiCtxT *multiCtx= (multiCtxT*)ctx;
    int err;

    // if time is negative just kill it
    if (timeout < 0) {
        if (multiCtx->timer) {
              err= sd_event_source_set_enabled(multiCtx->timer, SD_EVENT_OFF);
              if (err < 0) goto OnErrorExit;
        }
    } else {
        uint64_t usec;
        sd_event_now(multiCtx->evtLoop, CLOCK_MONOTONIC, &usec);
        if (!multiCtx->timer) { // new timer
            sd_event_add_time(multiCtx->evtLoop, &multiCtx->timer, CLOCK_MONOTONIC, usec+timeout*1000, 0, multiTimeoutCB, multiCtx);
            sd_event_source_set_description(multiCtx->timer, "curl-timer");

        } else {
            sd_event_source_set_time(multiCtx->timer, usec+timeout*1000);
            sd_event_source_set_enabled(multiCtx->timer, SD_EVENT_ONESHOT);
        }
    }
    return 0;

OnErrorExit:
    return -1;
}

void curlTest (afb_req_t request, shellCmdT *cmd, json_object *argsJ, int verbose) {
    CURLMcode mstatus;
    int running;

    if (!multiCtx) { // 1st run init everything
      curl_global_init(CURL_GLOBAL_DEFAULT);
      multiCtx =calloc(1, sizeof(multiCtxT));
      multiCtx->multi= curl_multi_init();
      multiCtx->callback= requestCB;
      multiCtx->evtLoop=afb_api_get_event_loop(afbBindingRoot);

      curl_multi_setopt(multiCtx->multi, CURLMOPT_SOCKETDATA, multiCtx);
      curl_multi_setopt(multiCtx->multi, CURLMOPT_SOCKETFUNCTION, multiSocketCB);
      curl_multi_setopt(multiCtx->multi, CURLMOPT_TIMERDATA, multiCtx);
      curl_multi_setopt(multiCtx->multi, CURLMOPT_TIMERFUNCTION, multiTimerCB);
    }

    // fulup test
    httpCtxT *httpCtx= calloc(1, sizeof(httpCtxT));
    httpCtx->easy = curl_easy_init();
    httpCtx->request = request;

    curl_easy_setopt(httpCtx->easy, CURLOPT_URL, "http://example.com");
    curl_easy_setopt(httpCtx->easy, CURLOPT_HEADER, 0L);
    curl_easy_setopt(httpCtx->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(httpCtx->easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(httpCtx->easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(httpCtx->easy, CURLOPT_WRITEFUNCTION, easyRespondCB);
    curl_easy_setopt(httpCtx->easy, CURLOPT_WRITEDATA, httpCtx);
    curl_easy_setopt(httpCtx->easy, CURLOPT_PRIVATE, httpCtx);
    curl_easy_setopt(httpCtx->easy, CURLOPT_ERRORBUFFER, httpCtx->error);
    curl_easy_setopt(httpCtx->easy, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(httpCtx->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);

    // add handle to multi
    mstatus= curl_multi_add_handle (multiCtx->multi, httpCtx->easy);
    if (mstatus) goto OnErrorExit;

    while(0) {
      int numfds=0;
      mstatus = curl_multi_wait(multiCtx->multi, NULL, 0, 1000, &numfds);
      curl_multi_perform(multiCtx->multi, &running);
      fprintf (stderr, ".");
      if(mstatus != CURLM_OK) {
        fprintf(stderr, "curl_multi_wait() failed, code %d.\n", mstatus);
        break;
      }
    }
    // further processing will happen in multiSocketCB
    return;

OnErrorExit:
  afb_req_fail_f (request, "test-fail", "curl-test fail");
}