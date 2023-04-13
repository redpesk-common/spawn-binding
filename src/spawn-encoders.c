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

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <rp-utils/rp-jsonc.h>
#include <afb-helpers4/afb-data-utils.h>

#include "spawn-defaults.h"
#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-encoders.h"
#include "spawn-subtask.h"
#include "spawn-subtask-internal.h"
#include "spawn-expand.h"

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
} docTaskCtxT;

typedef struct {
   ssize_t lineCount;
   ssize_t maxlen;
} streamOptsT;

typedef struct {
   ssize_t maxlen;
   FILE *fileout;
   FILE *fileerr;
} logOptsT;

typedef struct {
   streamBufT stdout;
   streamBufT stderr;
   logOptsT *opts;
} logTaskCtxT;


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
    afb_data_t data;

    // try to build a json from current buffer
    blobJ =json_tokener_parse(&docId->data[start]);

    if (blobJ) {
        //rp_jsonc_pack(&lineJ, "{si ss}", "pid", taskId->pid, "output", &docId->data[start]);
	data = afb_data_json_c_hold(blobJ);
        afb_event_push(taskId->event, 1, &data);

    } else {
        // push error event
        if (!errorJ)
		errorJ = json_object_new_string("[parsing fail] invalid json");
        rp_jsonc_pack(&lineJ, "{si ss so*}", "pid", taskId->pid, "output", &docId->data[start], "warning", errorJ);
	data = afb_data_json_c_hold(lineJ);
        afb_event_push(taskId->event, 1, &data);
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
            if (taskId->verbose)
                AFB_REQ_NOTICE(taskId->request, "[json too long] encoderJsonParserCB: sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
            docId->data[docId->index++] = '\\';
            docId->data[docId->index++] = '\0';
            err= callback(taskId, docId, 0, json_object_new_string ("line(s) too long folded with '\\' [increase {'linemax':xxxx}]"),context);
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
            err= callback(taskId, docId, dataIdx,NULL,context);
            if (err) goto OnErrorExit;
            docId->data[idx+1]=eol;
            dataIdx=idx+1;

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
                } else {
                    docId->index=0;
                }
            }
        }
    }
    return 0;

OnErrorExit:
    return 1;
}

// none encoder callback output line on server
static int logEventCB (taskIdT *taskId, streamBufT *data, ssize_t start, json_object *errorJ, void *context) {
    long output= (long)context;
    logTaskCtxT *taskCtx= (logTaskCtxT*) taskId->context;

    if (output == STDOUT_FILENO) fprintf (taskCtx->opts->fileout, "[%s] %s\n", taskId->uid, &data->data[start]);
    else fprintf (taskCtx->opts->fileerr, "[%s] %s\n", taskId->uid, &data->data[start]);
    return 0;
}

// line encoder callback send one event per new line
static int lineEventCB (taskIdT *taskId, streamBufT *sbuf, ssize_t start, json_object *errorJ, void *context) {
    int err;
    json_object *lineJ;
    afb_data_t data;

    err = rp_jsonc_pack(&lineJ, "{si ss* so*}", "pid", taskId->pid, "output", &sbuf->data[start], "warning", errorJ);
    if (err) goto OnErrorExit;

    data = afb_data_json_c_hold(lineJ);
    afb_event_push(taskId->event, 1, &data);
    return 0;

OnErrorExit:
    return 1;
}

// store all data in a buffer and turn it as string on cmd end
static int rawDataCB (taskIdT *taskId, streamBufT *data, ssize_t start, json_object *errorJ, void *context) {
    streamCtxT *streamctx = (streamCtxT*) context;

    // update error message from buffer
    if (errorJ) {
        if (!streamctx->errorJ) streamctx->errorJ=errorJ;
        else json_object_put (errorJ);
    }
    // data index point C line start
    streamctx->arrayJ = json_object_new_string(&data->data[start]);
    return 0;
}

// store raw pipe data en push it as a json string on command end
static int encoderRawParserCB (taskIdT *taskId, streamBufT *data, ssize_t len, encoderEventCbT callback, void* context) {
    int err;

    // nothing to do
    if (len == 0 && data->index == 0) return 0;

    // keep raw buffer data until command finishes
    if (len == 0) {
        if (data->index == data->size) {
            // no more space double buffer size
            data->size = data->size * 2;
            data->data = realloc (data->data, data->size);
            AFB_REQ_NOTICE(taskId->request, "[buffer size doubled] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
            if (!data->data) goto OnErrorExit;

        } else {
            if (data->data[data->index++] > '\r') data->index++; // if no new line keep last char
            data->data[data->index++] = '\0';
            err= callback(taskId, data, 0, NULL, context);
            data->index=0;
            if (err) goto OnErrorExit;
        }
    } else {
       data->index = data->index + len;
    }
    return 0;

OnErrorExit:
    return 1;
}


// document encoder callback add one line into document json array
static int DefaultArrayCB (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context) {
    streamCtxT *streamctx = (streamCtxT*) context;

    // create output array only when needed
    if (!streamctx->arrayJ) streamctx->arrayJ= json_object_new_array();
    // update error message from buffer
    if (errorJ) {
        if (!streamctx->errorJ) streamctx->errorJ=errorJ;
        else json_object_put (errorJ);
    }
    // docId index point C line start
    json_object_array_add (streamctx->arrayJ, json_object_new_string(&docId->data[start]));

    // make sure we do not not overload response array
    if (!streamctx->lncount--) {
        streamctx->errorJ= json_object_new_string("[too many lines] inscrease document 'linemax' option");
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
            AFB_REQ_NOTICE(taskId->request, "[line too long] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
            docId->data[docId->index++] = '\\';
            docId->data[docId->index++] = '\0';
            err= callback(taskId, docId, 0, json_object_new_string ("line too long truncated with '\\'"), context);
            if (err) goto OnErrorExit;
        } else {
            if ((unsigned char)docId->data[docId->index++] > '\r') docId->index++; // if last char is not a newline keep it
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
            if ((unsigned char)docId->data[idx] <= '\r') {  // CR ou NL are equivalent
                docId->data[idx] = '\0';
                err= callback (taskId, docId, dataIdx, NULL, context);
                if (err) goto OnErrorExit;

                for (idx=idx+1; (unsigned char)docId->data[idx] <= '\r'; idx++); // remove any trailling cr/nl
                dataIdx= idx; // next line start
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
                } else {
                    docId->index = 0; // reset docId
                }
            }
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
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder.encoder->fmtctx;
    docTaskCtxT *taskCtx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_START:  {

            // prepare handles to store stdout/err stream
            if (taskId->verbose >8) fprintf (stderr, "**** ENCODER_DOC_START uid=%s pid=%d\n", taskId->uid, taskId->pid);
            taskCtx = calloc (1, sizeof(docTaskCtxT));
            taskCtx->stdout.buffer= encoderBufferSetCB(NULL, opts->maxlen);
            taskCtx->stderr.buffer= encoderBufferSetCB(NULL, opts->maxlen);
            taskCtx->stdout.lncount = opts->lineCount;
            taskCtx->stderr.lncount = opts->lineCount;

            // attach handle to taskId
            taskId->context= (void*)taskCtx;

            break;
        }

        case ENCODER_TASK_STDOUT: {
            if (taskId->verbose >8)  fprintf (stderr, "**** ENCODER_DOC_STDOUT uid=%s pid=%d\n", taskId->uid, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskCtx->stdout.buffer, opts->maxlen, encoderLineParserCB, DefaultArrayCB, operation, &taskCtx->stdout);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STDERR: {
            if (taskId->verbose >8) fprintf (stderr, "**** ENCODER_DOC_STDERR uid=%spid=%d\n", taskId->uid, taskId->pid);
            err= encoderReadFd (taskId, taskId->errfd, taskCtx->stderr.buffer, opts->maxlen, encoderLineParserCB, DefaultArrayCB, operation, &taskCtx->stderr);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STOP: {

            json_object *errorJ=NULL;
            if (taskId->verbose >8) fprintf (stderr, "**** ENCODER_DOC_STOP uid=%s pid=%d\n", taskId->uid, taskId->pid);
            if (taskCtx->stdout.errorJ || taskCtx->stderr.errorJ) {
                err= rp_jsonc_pack (&errorJ, "{so* so*}"
                    , "stdout", taskCtx->stdout.errorJ
                    , "stderr", taskCtx->stderr.errorJ
                );
                if (err) goto OnErrorExit;
            }

            err=rp_jsonc_pack (&taskId->responseJ, "{ss si so* so* so* so*}"
                , "cmd", taskId->cmd->uid
                , "pid", taskId->pid
                , "status", taskId->statusJ
                , "warning", errorJ
                , "stdout", taskCtx->stdout.arrayJ
                , "stderr", taskCtx->stderr.arrayJ
                );
            if (err) goto OnErrorExit;

            // free private task encoder memory structure
            free (taskCtx->stdout.buffer);
            free (taskCtx->stderr.buffer);
            free (taskCtx);
            break;
        }

        case ENCODER_TASK_KILL: {
            // update error message before FMT_TASK_STOP take it
            if (taskId->verbose >8) fprintf (stderr, "**** ENCODER_DOC_KILL taskId=0x%p pid=%d\n", taskId, taskId->pid);
            taskId->errorJ= json_object_new_string("[timeout] forced sigkill");
            break;
        }

        default:
           AFB_REQ_ERROR(taskId->request, "[action-fail] sandbox=%s cmd=%s action=%d pid=%d", cmd->sandbox->uid, cmd->uid, action, taskId->pid);
           goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

// Send one event for line on stdout and stderr at the command end
static int encoderLineCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder.encoder->fmtctx;
    docTaskCtxT *taskCtx= (docTaskCtxT*)taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_STDOUT:
            if (taskId->verbose >8) fprintf (stderr, "**** ENCODER_LINE_STDOUT taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskCtx->stdout.buffer, opts->maxlen, encoderLineParserCB, lineEventCB, operation, &taskCtx->stdout);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB-fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
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

// Send one event for line on stdout and stderr at the command end
static int encoderRawCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder.encoder->fmtctx;
    docTaskCtxT *taskCtx= (docTaskCtxT*)taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_STDOUT:
            if (taskId->verbose >8) fprintf (stderr, "**** ENCODER_RAW_STDOUT taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskCtx->stdout.buffer, opts->maxlen, encoderRawParserCB, rawDataCB, operation, &taskCtx->stdout);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB-fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
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

// for debug print both stdout & stderr directly on server
static int encoderLogCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    shellCmdT *cmd= taskId->cmd;
    logOptsT *opts=cmd->encoder.encoder->fmtctx;
    logTaskCtxT *taskCtx= (logTaskCtxT*)taskId->context;

    int err;

    switch (action) {
        case ENCODER_TASK_START:  {
            // prepare handles to store stdout/err stream
            if (taskId->verbose >8) fprintf (stderr, "**** encoderLogCB uid=%s pid=%d\n", taskId->uid, taskId->pid);

            taskCtx = calloc (1, sizeof(logTaskCtxT));
            taskCtx->opts=opts;
            taskCtx->stdout.data= malloc(opts->maxlen);
            taskCtx->stderr.data= malloc(opts->maxlen);
            taskId->context= taskCtx;

            // write start entry into log
            fprintf (opts->fileout, "[%s] --- start %s [%s]---\n", taskId->uid, taskId->cmd->command, utilsExpandKey("$TODAY"));

            break;
        }

        case ENCODER_TASK_STDOUT: {
            if (taskId->verbose >8)  fprintf (stderr, "**** encoderLogCB uid=%s pid=%d\n", taskId->uid, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, &taskCtx->stdout, opts->maxlen, encoderLineParserCB, logEventCB, operation, (void*)STDOUT_FILENO);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STDERR: {
            if (taskId->verbose >8) fprintf (stderr, "**** encoderLogCB uid=%spid=%d\n", taskId->uid, taskId->pid);
            err= encoderReadFd (taskId, taskId->errfd, &taskCtx->stderr, opts->maxlen, encoderLineParserCB, logEventCB, operation, (void*)STDERR_FILENO);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
                goto OnErrorExit;
            }
            break;
        }

        case ENCODER_TASK_STOP: {

            // flush output files
            fprintf (opts->fileout, "[%s] --- end ---\n", taskId->uid);
            fflush(taskCtx->opts->fileout);
            fflush(taskCtx->opts->fileerr);

            if (taskId->verbose >8) fprintf (stderr, "**** encoderLogCB uid=%s pid=%d\n", taskId->uid, taskId->pid);
            err=rp_jsonc_pack (&taskId->responseJ, "{ss si so* so*}"
                , "cmd", taskId->cmd->uid
                , "pid", taskId->pid
                , "status", taskId->statusJ
                , "warning",  taskId->errorJ
                );
            if (err) goto OnErrorExit;
            break;
        }

        case ENCODER_TASK_KILL: {
            // update error message before FMT_TASK_STOP take it
            if (taskId->verbose >8) fprintf (stderr, "**** encoderLogCB taskId=0x%p pid=%d\n", taskId, taskId->pid);
            taskId->errorJ= json_object_new_string("[timeout] forced sigkill");
            break;
        }

        default:
           AFB_REQ_ERROR(taskId->request, "[action-fail] sandbox=%s cmd=%s action=%d pid=%d", cmd->sandbox->uid, cmd->uid, action, taskId->pid);
           goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return 1;
}

// Send one event json blog and stdout as array when task stop
static int encoderJsonCB (taskIdT *taskId, encoderActionE action, encoderOpsE operation, void* fmtctx) {
    shellCmdT *cmd= taskId->cmd;
    streamOptsT *opts=cmd->encoder.encoder->fmtctx;
    docTaskCtxT *taskCtx= taskId->context;
    int err;

    switch (action) {

        case ENCODER_TASK_STDOUT:
            //fprintf (stderr, "**** ENCODER_JSON_STDOUT taskId=0x%p pid=%d\n", taskId, taskId->pid);
            err= encoderReadFd (taskId, taskId->outfd, taskCtx->stdout.buffer, opts->maxlen, encoderJsonParserCB, jsonEventCB, operation, &taskCtx->stdout);
            if (err) {
                AFB_REQ_ERROR(taskId->request, "[encoderCB-fail] sandbox=%s cmd=%s pid=%d", cmd->sandbox->uid, cmd->uid, taskId->pid);
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

// fmtParsing take config cmd option and parse them into something usefull for taskId start
static int encoderInitLog (shellCmdT *cmd, json_object *optsJ, void* fmtctx) {
    int err;
    const char *fileerr = NULL, *fileout = NULL;

    logOptsT *opts = malloc(sizeof (logOptsT));
    opts->fileerr = stderr;
    opts->fileout = stdout;
    opts->maxlen = MAX_DOC_LINE_SIZE;

    ((encoderCbT*)cmd->encoder.encoder)->fmtctx = (void*)opts;
    if (optsJ) {
        err = rp_jsonc_unpack(optsJ, "{s?s s?s s?i !}" ,"stdout", &fileout, "stderr", &fileerr, "maxlen", &opts->maxlen);
        if (err) {
            AFB_API_ERROR(cmd->sandbox->binding->api, "[invalid format] sandbox=%s cmd=%s opts=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ));
            goto OnErrorExit;
        }

        // if decicate log file are given open them now
        if (fileout) opts->fileout= fopen (utilsExpandKeyCmd(fileout, cmd), "a");
        if (fileerr) opts->fileerr= fopen (utilsExpandKeyCmd(fileerr, cmd), "a");
        if (!opts->fileout || !opts->fileerr) {
            AFB_API_ERROR(cmd->sandbox->binding->api, "[invalid logfile] sandbox=%s cmd=%s opts=%s err=%s", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ), strerror(errno));
            goto OnErrorExit;
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
    opts->maxlen =  MAX_DOC_LINE_SIZE;
    opts->lineCount = MAX_DOC_LINE_COUNT;
    ((encoderCbT*)cmd->encoder.encoder)->fmtctx = (void*)opts;
    if (optsJ) {
        err = rp_jsonc_unpack(optsJ, "{s?i s?i !}" ,"maxline", &opts->lineCount, "maxlen", &opts->maxlen);
        if (err) {
            AFB_API_ERROR(cmd->sandbox->binding->api, "[invalid format] sandbox=%s cmd=%s opts=%s ", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ));
            goto OnErrorExit;
        }
    }
    return 0;

 OnErrorExit:
    return 1;
}


#if !defined(BUILTIN_FACTORY_NAME)
#define BUILTIN_FACTORY_NAME "builtins"
#endif


typedef
struct encoder_factory
{
	struct encoder_factory *next;
	const encoder_generator_t *generators;
	const char *uid;
}
	encoder_factory_t;


// Builtin in output formater. Note that first one is used when cmd does not define a format
static /*const*/ encoderCbT encoderBuiltin[] = { /*1st default == TEXT*/
  {.uid="TEXT" , .info="unique event at closure with all outputs", .initCB=encoderInitCB, .actionsCB=encoderDefaultCB},
  {.uid="SYNC" , .info="return json data at cmd end", .initCB=encoderInitCB, .actionsCB=encoderDefaultCB, .synchronous=1},
  {.uid="RAW"  , .info="return raw data at cmd end", .initCB=encoderInitCB, .actionsCB=encoderRawCB, .synchronous=1},
  {.uid="LINE" , .info="one event per line",  .initCB=encoderInitCB, .actionsCB=encoderLineCB},
  {.uid="JSON" , .info="one event per json blob",  .initCB=encoderInitCB, .actionsCB=encoderJsonCB},
  {.uid="LOG"  , .info="keep stdout/stderr on server",  .initCB=encoderInitLog, .actionsCB=encoderLogCB},
  {.uid= NULL} // must be null terminated
};





// registry holds a linked list of core+pugins encoders
static encoder_factory_t *first_factory = NULL;


// add a new plugin encoder to the registry
static
encoder_error_t
add_encoder_generator_factory(const char *uid, const encoder_generator_t *generators)
{
	encoder_factory_t *factory, **ptrfac;

	// create holding hat for encoder/decoder CB
	factory = calloc (1, sizeof *factory);
	if (factory == NULL)
		return ENCODER_ERROR_OUT_OF_MEMORY;

	// init the structure
	factory->next = NULL;
	factory->generators = generators;
	factory->uid = uid;

	// link it at latest position
	ptrfac = &first_factory;
	while (*ptrfac != NULL)
		ptrfac = &(*ptrfac)->next;
	*ptrfac = factory;

	// done
	return ENCODER_NO_ERROR;
}

// register callback and use it to register core encoders
encoder_error_t
encoder_generator_factory_init (void)
{
  return add_encoder_generator_factory (BUILTIN_FACTORY_NAME, encoderBuiltin);
}

// search the encoder in the registry
encoder_error_t
encoder_generator_search(const char *pluginuid, const char *encoderuid, const encoder_generator_t **generator)
{
	const encoder_factory_t *factory;
	const encoder_generator_t *itgen;

	// search the factory
	factory = first_factory;
	if (pluginuid != NULL) {
		while (factory && (factory->uid == NULL || strcasecmp(factory->uid, pluginuid)))
			factory = factory->next;
	}
	if (factory == NULL)
		return ENCODER_ERROR_PLUGIN_NOT_FOUND;

	// search the encoder
	itgen = factory->generators;
	if (encoderuid != NULL) {
		while (itgen->uid != NULL && strcasecmp (itgen->uid, encoderuid))
			itgen++;
		if (itgen->uid == NULL)
			return ENCODER_ERROR_ENCODER_NOT_FOUND;
	}
	*generator = itgen;
	return ENCODER_NO_ERROR;
}

encoder_error_t
encoder_generator_get(const char *pluginuid, const char *encoderuid, const encoder_generator_t **generator)
{
	encoder_error_t ege;
	const encoder_generator_t *gener;

	// search for an existing encoder
	ege = encoder_generator_search(pluginuid, encoderuid, &gener);
	if (ege != ENCODER_NO_ERROR)
		return ege;

	// every encoder should define its formating callback
	if (gener->actionsCB == NULL)
		return ENCODER_ERROR_INVALID_ENCODER;

	*generator = gener;
	return ENCODER_NO_ERROR;
}

encoder_error_t
encoder_generator_get_JSON(json_object *specifier, const encoder_generator_t **generator, json_object **options)
{
	int err;
	const char *pluginuid = NULL, *encoderuid = NULL;

	// extract encoder specification
	*options = NULL;
	if (specifier != NULL) {
		if (json_object_is_type (specifier, json_type_string)) {
			// encoder is a string
			encoderuid = json_object_get_string(specifier);
		} else {
			// encoder is a complex object with options
			err = rp_jsonc_unpack(specifier, "{s?s,ss,s?o !}"
					,"plugin", &pluginuid
					,"output", &encoderuid
					,"opts", options
			);
			if (err)
				return ENCODER_ERROR_INVALID_SPECIFIER;
		}
	}

	return encoder_generator_get(pluginuid, encoderuid, generator);
}

encoder_error_t
encoder_generator_check_options(const encoder_generator_t *generator, json_object *options)
{
	if (options && generator->check != NULL && generator->check(options) < 0)
		return ENCODER_ERROR_INVALID_OPTIONS;
	return ENCODER_NO_ERROR;
}



const char *encoder_error_text(encoder_error_t code)
{
	switch(code) {
	case ENCODER_ERROR_PLUGIN_NOT_FOUND:	return "PLUGIN_NOT_FOUND"; break;
	case ENCODER_ERROR_ENCODER_NOT_FOUND:	return "ENCODER_NOT_FOUND"; break;
	case ENCODER_ERROR_INVALID_ENCODER:	return "INVALID_ENCODER"; break;
	case ENCODER_ERROR_INVALID_OPTIONS:	return "INVALID_OPTIONS"; break;
	case ENCODER_ERROR_INVALID_SPECIFIER:	return "INVALID_SPECIFIER"; break;
	case ENCODER_ERROR_OUT_OF_MEMORY:	return "OUT_OF_MEMORY"; break;
	default: return ""; break;
	}
}


#include "spawn-encoders-plugins.h"

// Default callback structure is passed to plugin at initialisation time
encoderPluginCbT encoderPluginCb = {
    .registrate   = add_encoder_generator_factory,
    .bufferSet    = encoderBufferSetCB,
    .jsonParser   = encoderJsonParserCB,
    .textParser   = encoderLineParserCB,
    .readStream   = encoderReadFd,
};





/**
* starts the encoder
*/
int encoderStart(const encoderCbT *encoder, taskIdT *taskId)
{
	if (encoder->fmtctx == NULL && encoder->initCB)
		encoder->initCB(taskId->cmd, taskId->cmd->encoder.options, encoder->fmtctx);
	return encoder->actionsCB(taskId, ENCODER_TASK_START, ENCODER_OPS_STD, encoder->fmtctx);
}

/**
* closes the encoder
*/
void encoderClose(const encoderCbT *encoder, taskIdT *taskId)
{
	encoder->actionsCB(taskId, ENCODER_TASK_STDOUT, ENCODER_OPS_CLOSE, encoder->fmtctx);
	encoder->actionsCB(taskId, ENCODER_TASK_STDERR, ENCODER_OPS_CLOSE, encoder->fmtctx);
	encoder->actionsCB(taskId, ENCODER_TASK_STOP, ENCODER_OPS_CLOSE, encoder->fmtctx);
}

/**
* abort the encoder
*/
void encoderAbort(const encoderCbT *encoder, taskIdT *taskId)
{
	encoder->actionsCB(taskId, ENCODER_TASK_KILL, ENCODER_OPS_STD, encoder->fmtctx);
}

/**
* process input
*/
int encoderRead(const encoderCbT *encoder, taskIdT *taskId, int fd, bool error)
{
	return encoder->actionsCB(taskId, error ? ENCODER_TASK_STDERR : ENCODER_TASK_STDOUT, ENCODER_OPS_STD, encoder->fmtctx);
}
