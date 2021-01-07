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
   char *uid;
   struct encoderRegistryS *next;
   taskFormatCbT *formats;
} encoderRegistryT;

// registry holds a linked list of core+pugins encoders
taskFormatCbT encoderBuiltin[];
static encoderRegistryT *registryHead = NULL;


typedef struct {
    char *data;
    long index;
    long size;
} fmtDocBufT;

// builtin document encoder
typedef struct {
    fmtDocBufT *buffer;
    long lncount;
    json_object *arrayJ;
    json_object *errorJ;
} fmtDocStreamT;

typedef struct {
   fmtDocStreamT *outStream;
   fmtDocStreamT *errStream;
} fmtDocContextT;

typedef struct {
   ssize_t lineCount;
   ssize_t lineSize;
} fmtDocOptsT;

// generic encoder callback type
typedef int(*encoderAddLinesCbT)(taskIdT *taskId, fmtDocBufT *buffer, ssize_t start, void *context, json_object *errorJ);

// return a new empty document stream
fmtDocBufT *fmtResetDocBuffer(fmtDocBufT *buffer, ssize_t size) {
    // if no buffer provided let create one
    if (!buffer) {
        buffer = calloc(1, sizeof(fmtDocBufT));
        buffer->data= malloc(size+3); // +3 needed for oversized lines trailer '\\'
        buffer->size=size;
    }
    buffer->index=0;
    return buffer;
}

// document encoder callback add one line into document json array
static int fmtAddLinesToCB (taskIdT *taskId, fmtDocBufT *docId, ssize_t start, void *context, json_object *errorJ) {
    fmtDocStreamT *docStream = (fmtDocStreamT*) context;

    // create output array only when needed
    if (!docStream->arrayJ) docStream->arrayJ= json_object_new_array();

    // update error message from buffer
    if (errorJ) docStream->errorJ=errorJ;

    // docId index point C line start
    json_object_array_add (docStream->arrayJ, json_object_new_string(&docId->data[start]));

    // make sure we do not not overload response array
    if (!docStream->lncount--) {
        docStream->errorJ= json_object_new_string("[too many lines] inscrease document 'linemax' option");
        goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

// search for newline delimiters and add it to json response array
static int fmtLineParser (taskIdT *taskId, fmtDocBufT *docId, ssize_t len, encoderAddLinesCbT callback, void* context) {
    int err;
    ssize_t idx, lineIdx=0;

    // docId is full add '\\' and send docId anyhow
    if (len == 0) {
        AFB_API_NOTICE(taskId->cmd->api, "fmtAddLinesToArray: [line too long] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
        docId->data[docId->index++] = '\\';
        docId->data[docId->index++] = '\0';
        err= callback(taskId, docId, 0, context, json_object_new_string ("line too long truncated with '\\'"));
        if (err) goto OnErrorExit;
        // reset docId index and add continuation prefix
        docId->index=0;
        docId->data[docId->index++]='-';
        docId->data[docId->index++]='-';

    } else {

        // search within docId for newlines and optionally push them into jsonarray
        for (idx=docId->index; idx<=(docId->index + len); idx++) {

            // for every newline close 'C' string and push it to the stdoutJ array
            if (docId->data[idx] == '\n') {
                docId->data[idx] = '\0';
                err= callback (taskId, docId, lineIdx, context, NULL);
                if (err) goto OnErrorExit;

                lineIdx= idx+1; // next line start
            }
        }

        // no newline found simplely update docId index
        if (!lineIdx) {
            docId->index = docId->index + len;
        } else {
            // move remaining characters to docId start
            if (lineIdx != idx +1) {
                long remaining = docId->index+len-lineIdx;
                if (remaining > 0) {
                    memmove (&docId->data[0], &docId->data[lineIdx], remaining);
                    docId->index= remaining;
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
static int fmtDocInitCB (shellCmdT *cmd, json_object *optsJ, void* fmtctx) {
    int err;

    fmtDocOptsT *opts = malloc(sizeof (fmtDocOptsT));
    opts->lineSize =  MAX_DOC_LINE_SIZE;
    opts->lineCount = MAX_DOC_LINE_COUNT;
    cmd->format->fmtctx = (void*)opts;
    if (optsJ) {
        err = wrap_json_unpack(optsJ, "{s?i s?i !}" ,"count", &opts->lineCount, "size", &opts->lineSize);
        if (err) {
            AFB_API_ERROR(cmd->api, "fmtDocArrayParse: [invalid format] sandbox=%s cmd=%s opts=%s ", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ));
            goto OnErrorExit;
        }
    }
    return 0;

 OnErrorExit:
    return 1;
}

// Every encoder should have a formating callback supporting switch options.
static int fmtDocActionsCB (taskIdT *taskId, encoderActionE action, void* fmtctx) {
    assert (taskId->magic == MAGIC_SPAWN_TASKID);
    shellCmdT *cmd= taskId->cmd;
    fmtDocOptsT *opts=cmd->format->fmtctx;
    fmtDocContextT *taskctx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_START:  {

            // prepare handles to store stdout/err stream
            fmtDocContextT *docStream = calloc (1, sizeof(fmtDocContextT));
            docStream->outStream= (fmtDocStreamT*)calloc (1, sizeof(fmtDocStreamT));
            docStream->errStream= (fmtDocStreamT*)calloc (1, sizeof(fmtDocStreamT));
            docStream->outStream->buffer= fmtResetDocBuffer(NULL, opts->lineSize);
            docStream->errStream->buffer= fmtResetDocBuffer(NULL, opts->lineSize);
            docStream->outStream->lncount = opts->lineCount;
            docStream->errStream->lncount = opts->lineCount;

            // attach handle to taskId
            taskId->context= (void*)docStream;
                fprintf (stderr, "**** ENCODER_TENCODER_TASK_STARTASK_STOP taskId=0x%p pid=%d taskId->context=0x%p outStream=0x%p errStream=0x%p\n", taskId, taskId->pid, taskId->context, docStream->outStream, docStream->errStream);

            break;
        }

        case ENCODER_TASK_STOP: {
            fmtDocStreamT *outStream= taskctx->outStream;
            fmtDocStreamT *errStream= taskctx->errStream;
            json_object *errorJ=NULL;

    fprintf (stderr, "**** ENCODER_TASK_STOP taskId=0x%p pid=%d taskId->context=0x%p outStream=0x%p errStream=0x%p\n", taskId, taskId->pid, taskId->context, outStream, errStream);


            if (outStream->errorJ || errStream->errorJ) {
                wrap_json_pack (&errorJ, "{so* so*}"
                    , "stdout", outStream->errorJ
                    , "stderr", errStream->errorJ
                );
            }

            wrap_json_pack (&taskId->responseJ, "{ss si so* so* so* so*}"
                , "cmd", taskId->cmd->uid
                , "pid", taskId->pid
                , "status", taskId->statusJ
                , "warning", errorJ
                , "stdout", outStream->arrayJ
                , "stderr", errStream->arrayJ
                );

            // free private task encoder memory structure
            free (taskctx);
            free (outStream->buffer);
            free (errStream->buffer);
            free (outStream);
            free (errStream);
            break;
        }

        case ENCODER_TASK_KILL: {
            // update error message before FMT_TASK_STOP take it
            taskId->errorJ= json_object_new_string("[timeout] forced sigkill");
            break;
        }

        case ENCODER_STDOUT_DATA: {
            fmtDocStreamT *data= taskctx->outStream;
            ssize_t count, len;
    fprintf (stderr, "**** ENCODER_STDOUT_DATA taskId=0x%p pid=%d taskId->context=0x%p data=0x%p\n", taskId, taskId->pid, taskId->context, data);

            do {
                count= opts->lineSize - data->buffer->index;
                len = read (taskId->outFD, &data->buffer->data[data->buffer->index], count);

                // we have some new data or data buffer is full
                if (len > 0 || count==0) {
                    err= fmtLineParser (taskId, data->buffer, len, fmtAddLinesToCB, (void*)data);
                    if (err) {
                        AFB_API_ERROR(cmd->api, "fmtDocArrayCB: [format CB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                        goto OnErrorExit;
                    }
                }
             fprintf (stderr, "**** ENCODER_STDOUT_DATA taskId=0x%p len=%ld count=%ld\n", taskId, len, count);
            } while (len > 0);

            break;
        }

        case ENCODER_STDERR_DATA: {
            fmtDocStreamT *data= taskctx->errStream;
            ssize_t count, len;
    fprintf (stderr, "**** ENCODER_STDERR_DATA taskId=0x%p pid=%d taskId->context=0x%p data=0x%p\n", taskId, taskId->pid, taskId->context, data);

            do {
                count= opts->lineSize - data->buffer->index;
                len = read (taskId->errFD, &data->buffer->data[data->buffer->index], count);

                // we have some new data or data buffer is full
                if (len > 0 || count==0) {
                    err= fmtLineParser (taskId, data->buffer, len, fmtAddLinesToCB, (void*)data);
                    if (err) {
                        AFB_API_ERROR(cmd->api, "fmtDocArrayCB: [format CB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                        goto OnErrorExit;
                    }
                }
            } while (len > 0);
            break;
        }

        default:
           AFB_API_ERROR(cmd->api, "fmtDocArrayCB: [invalid action] sandbox=%s cmd=%s action=%d", cmd->sandbox->uid, cmd->uid, action);
           goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return 1;
}


// add a new plugin encoder to the registry
void encoderRegister (char *uid, taskFormatCbT *actionsCB) {
    encoderRegistryT *registryIdx, *registryEntry;

    // create holding hat for encoder/decoder CB
    registryEntry= (encoderRegistryT*) calloc (1, sizeof(encoderRegistryT));
    registryEntry->uid = uid;
    registryEntry->formats = actionsCB;


    // if not 1st encoder insert at the end of the chain
    if (!registryHead) {
        registryHead = registryEntry;
    } else {
        for (registryIdx= registryHead; registryIdx->next; registryIdx=registryIdx->next);
        registryIdx->next = registryEntry;
    }
}

// search for a plugin encoders/decoders CB list
int encoderFind (shellCmdT *cmd, json_object *formatJ) {
    char *pluginuid = NULL, *formatuid = NULL;
    afb_api_t api = cmd->api;
    json_object *optsJ=NULL;
    encoderRegistryT *registryIdx;
    int index, err;

    // if no format defined default is 1st builtin formater
    if (!formatJ) {
        cmd->format = &encoderBuiltin[0];
        err= cmd->format->initCB (cmd, NULL, cmd->format->fmtctx);
        if (err) goto OnErrorExit;
        goto OnFormatExit;
    }

    err = wrap_json_unpack(formatJ, "{s?s,ss,s?o !}"
        ,"plugin", &pluginuid
        ,"output", &formatuid
        ,"opts", &optsJ
        );
    if (err) {
        AFB_API_ERROR(api, "encoderFind: [invalid format] sandbox='%s' cmd='%s' not a valid json format=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(formatJ));
        goto OnErrorExit;
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
    taskFormatCbT *formats = registryIdx->formats;
    for (index=0; formats[index].uid; index++) {
        if (!strcasecmp (formats[index].uid, formatuid)) break;
    }

    if (!formats[index].uid) {
        AFB_API_ERROR(api, "encoderFind: [format not find] sandbox=%s cmd=%s format=%s", cmd->sandbox->uid, cmd->uid, formatuid);
        goto OnErrorExit;
    }

    // every encoder should define its formating callback
    if (!formats[index].actionsCB) {
        AFB_API_ERROR(api, "encoderFind: [encoder invalid] sandbox=%s cmd=%s format=%s (no encoder callback defined !!!)", cmd->sandbox->uid, cmd->uid, formats[index].uid);
        goto OnErrorExit;
    }

    // update command with coresponding format and all actionsCB to parse format option
    cmd->format= &formats[index];
    err= cmd->format->initCB (cmd, optsJ, cmd->format->fmtctx);

    if (err) goto OnErrorExit;

OnFormatExit:
    return 0;

OnErrorExit:
    return 1;
}

// Builtin in output formater. Note that first one is used when cmd does not define a format
taskFormatCbT encoderBuiltin[] = {
  {.uid="DOCUMENT"  , .info="text output [default formater]", .initCB=fmtDocInitCB, .actionsCB=fmtDocActionsCB},
  {.uid="TEXT"      , .info="per line event",  .initCB=fmtDocInitCB, .actionsCB=fmtDocActionsCB},

  {.uid= NULL} // must be null terminated
};

// register callback and use it to register core encoders
void encoderInit (void) {

  // Builtin Encoder don't have UID
  encoderRegister (NULL, encoderBuiltin);
}

