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

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-encoders.h"
#include "spawn-subtask.h"

typedef struct encoderRegistryS {
   const char *uid;
   struct encoderRegistryS *next;
   encoderCbT *encoders;
} encoderRegistryT;

// registry holds a linked list of core+pugins encoders
encoderCbT encoderBuiltin[];
static encoderRegistryT *registryHead = NULL;

// builtin document encoder
typedef struct {
    streamBufT *buffer;
    json_object *arrayJ;
    json_object *errorJ;
    long lncount;
} streamCtxT;

typedef struct {
   streamCtxT stdout;
   streamCtxT stderr;
} taskCtxT;

typedef struct {
   ssize_t lineCount;
   ssize_t lineSize;
} streamOptsT;

// return a new empty document stream
static streamBufT *encoderBufferSetCB(streamBufT *buffer, ssize_t size) {
    // if no buffer provided let create one
    if (!buffer) {
        buffer = calloc(1, sizeof(streamBufT));
        buffer->data= malloc(size+3); // +3 needed for oversized lines trailer '\\'
        buffer->size=size;
    }
    buffer->index=0;
    buffer->count=0;
    return buffer;
}

// document encoder callback add one line into document json array
static int jsonEventCB (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context) {
    json_object *blobJ, *lineJ;

    // try to build a json from current buffer
    blobJ =json_tokener_parse(&docId->data[start]);

    if (blobJ) {
        afb_event_push(taskId->event, blobJ);

    } else {
        // push error event
        if (!errorJ) errorJ= json_object_new_string("[parsing fail] invalid json");
        wrap_json_pack(&lineJ, "{ss so*}", "data", &docId->data[start], "warning", errorJ);
        afb_event_push(taskId->event, lineJ);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return 1;
}

// send one event for each completed json blob. If oversized send as a string error
static int encoderJsonParserCB (taskIdT *taskId, streamBufT *docId, ssize_t len, encoderEventCbT callback, void* context) {
    int err;
    ssize_t idx, dataIdx=0;

    // nothing to do
    if (len == 0 && docId->index == 0) return 0;

    // nothing else to read this is our last call
    if (len == 0) {
        if (docId->index == docId->size) {
            AFB_API_NOTICE(taskId->cmd->api, "encoderJsonParserCB: [json too long] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
            docId->data[docId->index++] = '\\';
            docId->data[docId->index++] = '\0';
            err= callback(taskId, docId, 0, json_object_new_string ("line too long truncated with '\\'"),context);
            if (err) goto OnErrorExit;
        } else {
            docId->data[docId->index++] = '\0';
            err= callback(taskId, docId, 0, NULL, context);
            if (err) goto OnErrorExit;
        }

        // reset docId index and add continuation prefix
        docId->index=0;

    } else {
        long initial=docId->count;
        char eol;

        // search within docId for newlines and optionally push them into jsonarray
        for (idx=docId->index; idx<=(docId->index + len); idx++) {

            // search for json blob ignoring extra prefix/trailer characters
            if (docId->data[idx] == '{') docId->count++;
            else if (docId->data[idx] == '}') docId->count--;

            // ignore prefix characters
            if (initial == docId->count) {
                if (!docId->count) dataIdx=idx;
                continue;
            }
            // if json blob not close keep searching
            if (docId->count)  {
                initial=-1;
                continue;
            }

            // temporarely close C string to send event
            eol= docId->data[idx+1];
            docId->data[idx+1]='\0';
            err= callback(taskId, docId, dataIdx,NULL, context);
            if (err) goto OnErrorExit;
            docId->data[idx+1]=eol;

            initial=0; // restart a new json blob
        }

        // json not close let's update data index
        if (!dataIdx) {
            docId->index = docId->index + len;
        } else {
            // if some data remain move them to buffer head
            if (dataIdx == idx) {
                docId->index = 0;
            } else {
                long remaining = docId->index+len-dataIdx;
                if (remaining > 0) {
                    memmove (&docId->data[0], &docId->data[dataIdx], remaining);
                    docId->index= remaining-1;
                }
            }
        }
    }
    return 0;

OnErrorExit:
    return 1;
}



// document encoder callback add one line into document json array
static int lineEventCB (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context) {
    int err;
    json_object *lineJ;

    err = wrap_json_pack(&lineJ, "{ss* so*}", "data", &docId->data[start], "warning", errorJ);
    if (err) goto OnErrorExit;

    err= afb_event_push(taskId->event, lineJ);
    if (err) goto OnErrorExit;

    return 0;

OnErrorExit:
    return 1;
}


// document encoder callback add one line into document json array
static int DefaultArrayCB (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context) {
    streamCtxT *taskctx = (streamCtxT*) context;

    // create output array only when needed
    if (!taskctx->arrayJ) taskctx->arrayJ= json_object_new_array();
    // update error message from buffer
    if (errorJ) {
        if (!taskctx->errorJ) taskctx->errorJ=errorJ;
        else json_object_put (errorJ);
    }
    // docId index point C line start
    json_object_array_add (taskctx->arrayJ, json_object_new_string(&docId->data[start]));

    // make sure we do not not overload response array
    if (!taskctx->lncount--) {
        taskctx->errorJ= json_object_new_string("[too many lines] inscrease document 'linemax' option");
        goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

// search for newline delimiters and add it to json response array
static int encoderLineParserCB (taskIdT *taskId, streamBufT *docId, ssize_t len, encoderEventCbT callback, void* context) {
    int err;
    ssize_t idx, dataIdx=0;

    // nothing to do
    if (len == 0 && docId->index == 0) return 0;

    // nothing else to read this is our last call
    if (len == 0) {
        if (docId->index == docId->size) {
            AFB_API_NOTICE(taskId->cmd->api, "fmtAddLinesToArray: [line too long] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
            docId->data[docId->index++] = '\\';
            docId->data[docId->index++] = '\0';
            err= callback(taskId, docId, 0, json_object_new_string ("line too long truncated with '\\'"), context);
            if (err) goto OnErrorExit;
        } else {
            docId->data[docId->index++] = '\0';
            err= callback(taskId, docId, 0, NULL, context);
            if (err) goto OnErrorExit;
        }

        // reset docId index and add continuation prefix
        docId->index=0;

    } else {

        // search within docId for newlines and optionally push them into jsonarray
        for (idx=docId->index; idx<=(docId->index + len-1); idx++) {

            // for every newline close 'C' string and push it to the stdoutJ array
            if (docId->data[idx] == '\n') {
                docId->data[idx] = '\0';
                err= callback (taskId, docId, dataIdx, NULL, context);
                if (err) goto OnErrorExit;

                dataIdx= idx+1; // next line start
            }
        }

        // no newline found simplely update docId index
        if (!dataIdx) {
            docId->index = docId->index + len;
        } else {
            // move remaining characters to docId start
            if (dataIdx != idx +1) {
                long remaining = docId->index+len-dataIdx;
                if (remaining > 0) {
                    memmove (&docId->data[0], &docId->data[dataIdx], remaining);
                    docId->index= remaining-1;
                }
            } else {
                docId->index = 0; // reset docId
            }
        }
    }
    return 0;

OnErrorExit:
    return 1;
}

// fmtParsing take config cmd option and parse them into something usefull for taskId start
static int encoderInitCB (shellCmdT *cmd, json_object *optsJ, void* fmtctx) {
    int err;

    streamOptsT *opts = malloc(sizeof (streamOptsT));
    opts->lineSize =  MAX_DOC_LINE_SIZE;
    opts->lineCount = MAX_DOC_LINE_COUNT;
    cmd->encoder->fmtctx = (void*)opts;
    if (optsJ) {
        err = wrap_json_unpack(optsJ, "{s?i s?i !}" ,"maxline", &opts->lineCount, "maxlen", &opts->lineSize);
        if (err) {
            AFB_API_ERROR(cmd->api, "fmtDocArrayParse: [invalid format] sandbox=%s cmd=%s opts=%s ", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ));
            goto OnErrorExit;
        }
    }
    return 0;

 OnErrorExit:
    return 1;
}

// read any avaliable data from stdout/err fd in nonblocking mode
static int encoderReadFd (taskIdT *taskId, int pipefd, streamBufT *buffer, ssize_t bufsize, encoderParserCbT parserCB, encoderEventCbT eventCB, encoderOpsE operation, void *userdata) {
    ssize_t freeIdx, len;
    int err;

    do {
        freeIdx= bufsize - buffer->index;
        len = read (pipefd, &buffer->data[buffer->index], freeIdx);

        // we have some new data or data buffer is full
        if (len > 0 || freeIdx == 0 || operation == ENCODER_OPS_CLOSE) {
            err= (*parserCB) (taskId, buffer, len, eventCB, (void*)userdata);
            if (err) goto OnErrorExit;
        }
    } while (len > 0);
    return 0;

OnErrorExit:
    return 1;
}

// Every encoder should have a formating callback supporting switch options.
static int encoderDefaultCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder->fmtctx;
    taskCtxT *taskctx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_START:  {

            // prepare handles to store stdout/err stream
            taskCtxT *taskctx = calloc (1, sizeof(taskCtxT));
            taskctx->stdout.buffer= encoderBufferSetCB(NULL, opts->lineSize);
            taskctx->stderr.buffer= encoderBufferSetCB(NULL, opts->lineSize);
            taskctx->stdout.lncount = opts->lineCount;
            taskctx->stderr.lncount = opts->lineCount;

            // attach handle to taskId
            taskId->context= (void*)taskctx;
            //fprintf (stderr, "**** ENCODER_DOC_START taskId=0x%p pid=%d\n", taskId, taskId->pid);

            break;
        }

        case ENCODER_TASK_STDOUT: {
            //fprintf (stderr, "**** ENCODER_DOC_STDOUT taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskctx->stdout.buffer, opts->lineSize, encoderLineParserCB, DefaultArrayCB, operation, &taskctx->stdout);
            if (err) {
                AFB_API_ERROR(cmd->api, "encoderReadFd: [encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STDERR: {
            //fprintf (stderr, "**** ENCODER_DOC_STDERR taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->errfd, taskctx->stderr.buffer, opts->lineSize, encoderLineParserCB, DefaultArrayCB, operation, &taskctx->stderr);
            if (err) {
                AFB_API_ERROR(cmd->api, "encoderReadFd: [encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STOP: {

            json_object *errorJ=NULL;
            //fprintf (stderr, "**** ENCODER_DOC_STOP taskId=0x%p pid=%d\n", taskId, taskId->pid);

            if (taskctx->stdout.errorJ || taskctx->stderr.errorJ) {
                err= wrap_json_pack (&errorJ, "{so* so*}"
                    , "stdout", taskctx->stdout.errorJ
                    , "stderr", taskctx->stderr.errorJ
                );
                if (err) goto OnErrorExit;
            }

            err=wrap_json_pack (&taskId->responseJ, "{ss si so* so* so* so*}"
                , "cmd", taskId->cmd->uid
                , "pid", taskId->pid
                , "status", taskId->statusJ
                , "warning", errorJ
                , "stdout", taskctx->stdout.arrayJ
                , "stderr", taskctx->stderr.arrayJ
                );
            if (err) goto OnErrorExit;

            // free private task encoder memory structure
            free (taskctx->stdout.buffer);
            free (taskctx->stderr.buffer);
            free (taskctx);
            break;
        }

        case ENCODER_TASK_KILL: {
            // update error message before FMT_TASK_STOP take it
            taskId->errorJ= json_object_new_string("[timeout] forced sigkill");
            break;
        }

        default:
           AFB_API_ERROR(cmd->api, "fmtDocArrayCB: [action fail] sandbox=%s cmd=%s action=%d pid=%d", cmd->sandbox->uid, cmd->uid, action, taskId->pid);
           goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

// Send one event for line on stdout and stderr at the command end
static int encoderLineCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder->fmtctx;
    taskCtxT *taskctx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_STDOUT:
            //fprintf (stderr, "**** ENCODER_LINE_STDOUT taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskctx->stdout.buffer, opts->lineSize, encoderLineParserCB, lineEventCB, operation, &taskctx->stdout);
            if (err) {
                AFB_API_ERROR(cmd->api, "encoderReadFd: [encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;

        // anything else is delegated to document encoder
        default:
            err= encoderDefaultCB (taskId, action, operation, fmtctx);
            if (err) goto OnErrorExit;
            break;
    }
    return 0;

OnErrorExit:
    return 1;
}

// Send one event json blog and stdout as array when task stop
static int encoderJsonCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder->fmtctx;
    taskCtxT *taskctx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_STDOUT:
            //fprintf (stderr, "**** ENCODER_JSON_STDOUT taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskctx->stdout.buffer, opts->lineSize, encoderJsonParserCB, jsonEventCB, operation, &taskctx->stdout);
            if (err) {
                AFB_API_ERROR(cmd->api, "encoderReadFd: [encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;

        // anything else is delegated to document encoder
        default:
            err= encoderDefaultCB (taskId, action, operation, fmtctx);
            if (err) goto OnErrorExit;
            break;

    }

    return 0;

OnErrorExit:
    return 1;
}

// add a new plugin encoder to the registry
static int encoderRegisterCB (const char *uid, encoderCbT *actionsCB) {
    encoderRegistryT *registryIdx, *registryEntry;

    // create holding hat for encoder/decoder CB
    registryEntry= (encoderRegistryT*) calloc (1, sizeof(encoderRegistryT));
    registryEntry->uid = uid;
    registryEntry->encoders = actionsCB;


    // if not 1st encoder insert at the end of the chain
    if (!registryHead) {
        registryHead = registryEntry;
    } else {
        for (registryIdx= registryHead; registryIdx->next; registryIdx=registryIdx->next);
        registryIdx->next = registryEntry;
    }

    return 0;
}

// search for a plugin encoders/decoders CB list
int encoderFind (shellCmdT *cmd, json_object *encoderJ) {
    char *pluginuid = NULL, *formatuid = NULL;
    afb_api_t api = cmd->api;
    json_object *optsJ=NULL;
    encoderRegistryT *registryIdx;
    int index, err;

    // if no format defined default is 1st builtin formater
    if (!encoderJ) {
        cmd->encoder= &encoderBuiltin[0];
        err= cmd->encoder->initCB (cmd, NULL, cmd->encoder->fmtctx);
        if (err) goto OnErrorExit;
        goto OnFormatExit;
    }

    // encoder is either a string with formatuid or a complex object with options
    if (json_object_is_type (encoderJ, json_type_string)) {
       formatuid= (char*)json_object_get_string(encoderJ);
    } else {
        err = wrap_json_unpack(encoderJ, "{s?s,ss,s?o !}"
            ,"plugin", &pluginuid
            ,"output", &formatuid
            ,"opts", &optsJ
            );
        if (err) {
            AFB_API_ERROR(api, "encoderFind: [invalid format] sandbox='%s' cmd='%s' not a valid json format='%s'", cmd->sandbox->uid, cmd->uid, json_object_get_string(encoderJ));
            goto OnErrorExit;
        }
    }

    if (pluginuid) {
        // search within plugin list
        for (registryIdx= registryHead; registryIdx->next; registryIdx=registryIdx->next) {
            if (registryIdx->uid && !strcasecmp (registryIdx->uid, pluginuid)) break;
        }
        if (!registryIdx->uid) {
            AFB_API_ERROR(api, "encoderFind: [plugin not found] sandbox='%s' cmd='%s' format='%s' plugin=%s", cmd->sandbox->uid, cmd->uid, formatuid, pluginuid);
            goto OnErrorExit;
        }
    } else {
        // search for core builtin encoders (registry->uid==NULL)
        for (registryIdx= registryHead; registryIdx->uid && registryIdx->next; registryIdx=registryIdx->next);
        if (registryIdx->uid) {
            AFB_API_ERROR(api, "encoderFind: [Internal error] missing builtin core formaters (hoops!!!)");
            goto OnErrorExit;
        }
    }

    // search format encoder within selected plugin
    encoderCbT *encoders = registryIdx->encoders;
    for (index=0; encoders[index].uid; index++) {
        if (!strcasecmp (encoders[index].uid, formatuid)) break;
    }

    if (!encoders[index].uid) {
        AFB_API_ERROR(api, "encoderFind: [encoder not find] sandbox=%s cmd=%s format='%s'", cmd->sandbox->uid, cmd->uid, formatuid);
        goto OnErrorExit;
    }

    // every encoder should define its formating callback
    if (!encoders[index].actionsCB) {
        AFB_API_ERROR(api, "encoderFind: [encoder invalid] sandbox=%s cmd=%s format=%s (no encoder callback defined !!!)", cmd->sandbox->uid, cmd->uid, encoders[index].uid);
        goto OnErrorExit;
    }

    // update command with coresponding format and all actionsCB to parse format option
    cmd->encoder= &encoders[index];
    err= cmd->encoder->initCB (cmd, optsJ, cmd->encoder->fmtctx);

    if (err) goto OnErrorExit;

OnFormatExit:
    return 0;

OnErrorExit:
    return 1;
}

// Builtin in output formater. Note that first one is used when cmd does not define a format
encoderCbT encoderBuiltin[] = {
  {.uid="TEXT"  , .info="unique event at closure with all outputs", .initCB=encoderInitCB, .actionsCB=encoderDefaultCB},
  {.uid="LINE"  , .info="one event per line",  .initCB=encoderInitCB, .actionsCB=encoderLineCB},
  {.uid="JSON"  , .info="one event per json blob",  .initCB=encoderInitCB, .actionsCB=encoderJsonCB},
  {.uid= NULL} // must be null terminated
};

// Default callback structure is passed to plugin at initialisation time
encoderPluginCbT encoderPluginCb = {
    .magic=PLUGIN_ENCODER_MAGIC,
    .registrate   = encoderRegisterCB,
    .bufferSet    = encoderBufferSetCB,
    .jsonParser   = encoderJsonParserCB,
    .textParser   = encoderLineParserCB,
    .readStream   = encoderReadFd,
};

// register callback and use it to register core encoders
int encoderInit (void) {

  // Builtin Encoder don't have UID
  int status= encoderRegisterCB (NULL, encoderBuiltin);
  return status;
}

