/*
 * Copyright (C) 2018-2020 "IoT.bzh"
 * Author "Fulup Ar Foll" <fulup@iot.bzh>
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at https://opensource.org/licenses/MIT.
 */
#define shell_GET_INT32_FROM_INT16(tab_int16, index) ((tab_int16[(index)] << 16) + tab_int16[(index) + 1])

#define _GNU_SOURCE

#include "spawn-binding.h"
#include <ctl-plugin.h>

// demo encoder send one vent every XXX line of output on stdout, stderr is send at child termination event

CTLP_CAPI_REGISTER("king_pigeon");

typedef struct {
    uint32_t previous;
    uint32_t step;
} rCountT;

static int decodePigeonInfo (shellSourceT *source, taskFormatCbT *format, uint16_t *data, uint index, json_object **responseJ) {

    *responseJ = json_object_new_object();
    json_object_object_add (*responseJ, "product", json_object_new_int(data[0]));
    json_object_object_add (*responseJ, "lot", json_object_new_int(data[1]));
    json_object_object_add (*responseJ, "serial", json_object_new_int(data[2]));
    json_object_object_add (*responseJ, "online", json_object_new_int(data[3]));
    json_object_object_add (*responseJ, "hardware", json_object_new_int(data[4]));
    json_object_object_add (*responseJ, "firmware", json_object_new_int(data[5]));

    return 0;
}


static int encodePigeonInfo(shellSourceT *source, taskFormatCbT *format, json_object *sourceJ, uint16_t **response, uint index) {

   if (!json_object_is_type (sourceJ, json_type_int))  goto OnErrorExit;
   (void)json_object_get_int (sourceJ);
   return 0;

OnErrorExit:
    AFB_API_ERROR(source->api, "encodePigeonInfo: [%s] not an interger", json_object_get_string(sourceJ));
    return 1;
}

static int decodeRCount (shellSourceT *source, taskFormatCbT *format, uint16_t *data, uint index, json_object **responseJ) {

    // extract context to get previous value
    rCountT *counter = (rCountT*) source->context;

    // convert two 16bits register into one uint32 value
    uint32_t current= (int32_t) shell_GET_INT32_FROM_INT16(data, index*format->nbreg);
    uint32_t diff;

    // 1st call get shell value without sending a response to client
    if (counter->previous == 0) {
        counter->previous = current;
        goto NoResponse;

    } else {

        // compute counter diff
        diff = current - counter->previous;
        if (diff < counter->step) goto NoResponse;
        // store current for next tic and return a response
        counter->previous = current;
        *responseJ = json_object_new_int64 ((int64_t)diff);
    }

    return 0;

NoResponse:
    *responseJ = NULL; // no response to provide
    return 0; // return 1 would cancel counter subscription an error
}

// Alocate counter handle once at init time
static int initRCount (shellSourceT *source, json_object *argsJ) {
    rCountT * ctx = malloc (sizeof(rCountT));
    json_object *tmpJ;
    ctx->step = 1; // default value
    ctx->previous = 0; // never used

    // extract 'step' value from json sensor config args
    int done = json_object_object_get_ex(argsJ, "step", &tmpJ);
    if (done) ctx->step = json_object_get_int(tmpJ);

    // save context in source handle
    source->context = ctx;

    return 0;
}

// encode/decode callbacks
static taskFormatCbT pigeonEncoders[] = {
    {.uid="devinfo", .info="json_array", .nbreg=6, .decodeCB=decodePigeonInfo, .encodeCB=encodePigeonInfo},
    {.uid="rcount", .info="json_integer", .nbreg=2, .decodeCB=decodeRCount, .encodeCB=NULL, .initCB=initRCount},
    {.uid=NULL} // must be NULL terminated
};

CTLP_ONLOAD(plugin, registryCB) {
    registerCbT callback = (registerCbT)registryCB;
    assert (callback);
    (*callback) (plugin->uid, pigeonEncoders);
    return 0;
}
