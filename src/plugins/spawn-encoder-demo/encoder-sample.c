/*
 * Copyright (C) 2021 "IoT.bzh"
 * Author "Fulup Ar Foll" <fulup@iot.bzh>
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at https://opensource.org/licenses/MIT.
 */

#define _GNU_SOURCE

#include <assert.h>

#include <afb/afb-binding.h>

#include <afb-helpers4/afb-data-utils.h>
#include <rp-utils/rp-jsonc.h>

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-encoders.h"

#include "spawn-encoders-plugins.h"

#include "spawn-subtask.h"

#include "spawn-subtask-internal.h"

/*
 * demo custom plugin encoder
 * --------------------------------------------------------------
 *  - stdout:  per xxx line json array event
 *  - stderr:  per line json array with counter+data
 *  - termination: json status event with pid, error code, ...
 *
 *  - to activate MyEncoder add to your command definition
 *    'encoder':{"plugin": "MyEncoders", output:'Sample1', 'blkcount':  xxxx}
 *
 *  - a plugin may contains one or multiple encoder. Plugins use a privage namespace for encoder 'uid'
 *
 *  Note: this demo encoder is provided as template for developpers to create there own code.
 *  to keep sample simple it leverage generic read pipe and line parsing from builtin encoder.
 *
 */

DECLARE_SPAWN_ENCODER_PLUGIN("encoder_sample", encoder_entry)


#define MY_DEFAULT_blkcount 10   // send one event every 10 lines
#define MY_DEFAULT_maxlen 512  // any line longer than this will be split

// default generic encoder utilities pluginCB
static encoderPluginCbT *pluginCB;

// hold per command encoder opts
typedef struct {
    int blkcount;
    int maxlen;
} MyCmdOpts;

// hold per taskId encoder context
typedef struct {
    json_object *stdoutJ;
    streamBufT *sout;
    streamBufT *serr;
    int linecount;
    int blockcount;
    int errcount;
} MyTaskCtxT;


// group stdout lines into an array and send them only at linecount
static int MyCustomStdoutCB (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context) {
    MyTaskCtxT *taskctx = (MyTaskCtxT*)context;
    int err;

    // this is a new new bloc create json_array and initialize line counter
    if (!taskctx->stdoutJ) {
        taskctx->stdoutJ= json_object_new_array();
        taskctx->linecount= taskctx->blockcount;
    }

    // update error message from read buffer callback
    if (errorJ) {
        taskId->statusJ= errorJ;
        goto OnErrorExit;
    }

    // add buffer line into a json array
    err= json_object_array_add (taskctx->stdoutJ, json_object_new_string(&docId->data[start]));
    if (err) goto OnErrorExit;

    // we reach block number of line count
    if (!taskctx->linecount-- && !err) {
	afb_data_t data = afb_data_json_c_hold(taskctx->stdoutJ);
        afb_event_push(taskId->event, 1, &data);
        taskctx->stdoutJ= NULL; // force a new json array as previous one was free by afb_event_push
    }
    return 0;

OnErrorExit:
    return 1;
}

// Send each stderr line as a string event
static int MyCustomStderrCB (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context) {
    MyTaskCtxT *taskctx = (MyTaskCtxT*)context;
    int err;

    json_object *responseJ;

    // build a json object with counter and data for each new stderr output line
    err=rp_jsonc_pack (&responseJ, "{si, ss}", "errcount", taskctx->errcount++, "data", &docId->data[start]);
    if (err) goto OnErrorExit;

    afb_data_t data = afb_data_json_c_hold(responseJ);
    afb_event_push(taskId->event, 1, &data);
    return 0;

OnErrorExit:
    return 1;
}

// this function is call at config parsing time. Once for each command declaring {output:'MyEncoder'...
static int MyCustomInitCB (shellCmdT *cmd, json_object *optsJ, void* context) {
    int err;

    // create option handle and attach it to conresponding command handler
    MyCmdOpts *opts = malloc(sizeof (MyCmdOpts));
    opts->blkcount =  MY_DEFAULT_blkcount;
    ((encoder_generator_t*)cmd->encoder.generator)->fmtctx = (void*)opts;

    // If config private a custom 'opts' value parse it now
    if (optsJ) {
        err = rp_jsonc_unpack(optsJ, "{s?i s?i}" ,"blkcount", &opts->blkcount, "maxlen", &opts->maxlen);
        if (err) {
            AFB_API_ERROR(cmd->sandbox->binding->api, "MyCustomInitCB: [invalid format] sandbox=%s cmd=%s opts=%s ", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ));
            goto OnErrorExit;
        }
    }
    return 0;

 OnErrorExit:
    return 1;
}

// Send one event json blog and stdout as array when task stop
static int MyCustomSampleCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    shellCmdT *cmd= taskId->cmd;
    MyCmdOpts *opts=cmd->encoder.generator->fmtctx;
    MyTaskCtxT *taskctx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_START:  {

            // prepare handles to store stdout/err stream
            taskctx= calloc (1, sizeof(MyTaskCtxT));
            taskctx->sout= (*pluginCB->bufferSet) (NULL, opts->maxlen);
            taskctx->serr= (*pluginCB->bufferSet) (NULL, opts->maxlen);
            taskctx->blockcount= opts->blkcount;

            // attach handle to taskId
            taskId->context= (void*)taskctx;
            break;
        }

        case ENCODER_TASK_STDOUT: {
            err= (*pluginCB->readStream) (taskId, taskId->outfd, taskctx->sout, opts->maxlen, (*pluginCB->textParser), MyCustomStdoutCB, operation, taskctx);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "MyCustomSampleCB: [Stdout fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STDERR: {
            err= (*pluginCB->readStream) (taskId, taskId->errfd, taskctx->serr, opts->maxlen, (*pluginCB->textParser), MyCustomStderrCB, operation, taskctx);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "MyCustomSampleCB: [Stdout fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STOP: {

            err=rp_jsonc_pack (&taskId->responseJ, "{ss si so* so*}"
                , "cmd", taskId->cmd->uid
                , "pid", taskId->pid
                , "status", taskId->statusJ
                , "stdout", taskctx->stdoutJ
                );
            if (err) goto OnErrorExit;

            // free private task encoder memory structure
            free (taskctx->sout);
            free (taskctx->serr);
            free (taskctx);
            break;
        }

        case ENCODER_TASK_KILL: {
            // update error message before FMT_TASK_STOP take it
            taskId->errorJ= json_object_new_string("[timeout] forced sigkill");
            break;
        }

        default:
           AFB_REQ_ERROR(taskId->request, "fmtDocArrayCB: [action fail] sandbox=%s cmd=%s action=%d pid=%d", cmd->sandbox->uid, cmd->uid, action, taskId->pid);
           goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return 1;
}

// list custom encoders for registration
encoder_generator_t MyEncoders[] = {
  {.uid="my-custom-encoder"  , .info="One event per blkcount=xxx lines", .initCB=MyCustomInitCB, .actionsCB=MyCustomSampleCB},
  {.uid= NULL} // terminator
};


static int encoder_entry(encoderPluginCbT *pluginCB)
{
	return pluginCB->registrate(SpawnEncoderManifest.name, MyEncoders);
}
