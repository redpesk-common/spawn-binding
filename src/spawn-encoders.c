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

taskFormatCbT encoderBuiltin[];

// text stdout/err lines with a document
#ifndef SH_TEXT_MAX_BUF_LINE
  #define SH_TEXT_MAX_BUF_LINE 1024
#endif

#ifndef SH_TEXT_MAX_BUF_SIZE
  #define SH_TEXT_MAX_BUF_SIZE 512
#endif

typedef struct shellRegistryS {
   char *uid;
   struct shellRegistryS *next;
   taskFormatCbT *formats;
} shellRegistryT;

typedef struct {
    json_object *arrayJ;
    long lines;
    long index;
    char *buffer;
} shDocStreamT;

typedef struct {
   shDocStreamT stdout;
   shDocStreamT stderr;
} shDocContextT;

typedef struct {
    long linemax;
    long linesz;
} shDocOptsT;


// registry holds a linked list of core+pugins encoders
static shellRegistryT *registryHead = NULL;

// search for newline delimiters and add it to json response array
static int shDocAddLinesToArray (afb_api_t api,  taskIdT *taskId, shDocOptsT *opts, shDocStreamT *docStream, ssize_t count) {
    long idx;
    long lineIdx=0;

    // do not create response array until we need it
    if (!docStream->arrayJ) docStream->arrayJ= json_object_new_array();

    // we already passed authorized max line capacity
    if (docStream->index == opts->linemax) {
        AFB_API_NOTICE(api, "shDocAddLinesToArray: [too many lines in stdout/err] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
        taskId->errorJ = json_object_new_string ("too many lines");
        goto OnErrorExit;
    }

    // if line longer than buffer truncate line to buffer size
    if (opts->linesz - docStream->index == 0) {
        AFB_API_NOTICE(api, "shDocAddLinesToArray: [line too long] sandbox=%s cmd=%s", taskId->cmd->sandbox->uid, taskId->cmd->uid);
        docStream->buffer[docStream->index++] = '\\';
        docStream->buffer[docStream->index++] = '\0';
        taskId->errorJ = json_object_new_string ("line too long truncated with '\\'");
        json_object_array_add (docStream->arrayJ, json_object_new_string(docStream->buffer));
        docStream->index++;
        docStream->index = 0;
        return 0;
    }

    // search within buffer for newlines and optionally push them into jsonarray
    for (idx=docStream->index; idx<=(docStream->index + count); idx++) {

        // for every newline close 'C' string and push it to the stdoutJ array
        if (docStream->buffer[idx] == '\n') {
            docStream->buffer[idx] = '\0';
            docStream->lines ++;

            //AFB_API_ERROR(api, "shDocAddLinesToArray: line=%s", &docStream->buffer[lineIdx]);
            json_object_array_add (docStream->arrayJ, json_object_new_string(&docStream->buffer[lineIdx]));
            lineIdx = idx+1;
            if (lineIdx > opts->linemax) {
                AFB_API_NOTICE(api, "shDocAddLinesToArray: [too many lines in stdout/err] sandbox=%s cmd=%s pid=%d", taskId->cmd->sandbox->uid, taskId->cmd->uid, taskId->pid);
                taskId->errorJ = json_object_new_string ("too many lines");
                goto OnErrorExit;
            }
        }
    }

    // no newline found simplely update buffer index
    if (!lineIdx) {
        docStream->index = docStream->index + count;

    } else {
        // move remaining characters to buffer start
        if (lineIdx != idx +1) {
            long remaining = docStream->index+count-lineIdx;
            if (remaining > 0) {
                memmove (docStream->buffer, &docStream->buffer[lineIdx], remaining);
                docStream->index= remaining;
            }
        } else {
            docStream->index = 0; // reset buffer
        }
    }

    return 0;

OnErrorExit:
    return 1;
}

// Every encoder should have a formating callback supporting switch options.
static int shDocFormatCB (shellCmdT *cmd, shellFmtActionE action, void* context) {
    afb_api_t api = cmd->api;
    int err;

    switch (action) {

        case SH_FMT_OPTS_PARSE: {
            // parse config command line format option
            json_object *optsJ= (json_object *) context;

            shDocOptsT *opts = malloc(sizeof (shDocOptsT));
            opts->linemax =  SH_TEXT_MAX_BUF_LINE;
            opts->linesz = SH_TEXT_MAX_BUF_SIZE;
            cmd->format->opts = (void*)opts;
            if (optsJ) {
                err = wrap_json_unpack(optsJ, "{s?i s?i !}" ,"count", &opts->linemax, "size", &opts->linesz);
                if (err) {
                    AFB_API_ERROR(api, "shDocOptsCB: [invalid format] sandbox=%s cmd=%s opts=%s ", cmd->sandbox->uid, cmd->uid, json_object_get_string(optsJ));
                    goto OnErrorExit;
                }
            }
            break;
        }

        case SH_FMT_TASK_START:  {
            // prepare json object to store stdout/err
            taskIdT *taskId = (taskIdT*) context;
            shDocOptsT *opts = (shDocOptsT*) cmd->format->opts;
            shDocContextT *docStream = calloc (1, sizeof(shDocContextT));
            taskId->context= (void*)docStream;

            // add two characters to buffer to cut oversized line
            docStream->stdout.buffer  = malloc(opts->linesz) +2;
            docStream->stderr.buffer  = malloc(opts->linesz) +2;
            break;
        }

        case FMT_TASK_STOP: {
            // build json response and clear task tempry buffer
            taskIdT *taskId = (taskIdT*) context;
            shDocContextT *docStream = (shDocContextT*) taskId->context;

            wrap_json_pack (&taskId->responseJ, "{ss si so* so* so*}"
                , "cmd", taskId->cmd->uid
                , "pid", taskId->pid
                , "status", taskId->statusJ
                , "stdout", docStream->stdout.arrayJ
                , "stderr", docStream->stderr.arrayJ
                );

            // free private task buffer
            free (taskId->context);
            break;
        }

        case FMT_TASK_KILL: {
            // update error message FMT_TASK_STOP will be c
            taskIdT *taskId = (taskIdT*) context;
            taskId->errorJ= json_object_new_string("[timeout] forced sigkill");
            break;
        }

        case SH_FMT_STDOUT_BUFFER: {
            shDocOptsT *opts = (shDocOptsT*) cmd->format->opts;
            taskIdT *taskId = (taskIdT*) context;
            shDocContextT *docStream = (shDocContextT*) taskId->context;
            ssize_t count, len;

            do { // read anything avaliable
                count= opts->linesz - docStream->stdout.index;
                len = read (taskId->stdout, &docStream->stdout.buffer[docStream->stdout.index], count);

                if (len > 0 || count==0) {
                    err= shDocAddLinesToArray (api, taskId, opts, &docStream->stdout, len);
                    if (err) goto OnErrorExit;
                }
            } while (len > 0);

            break;
        }

        case SH_FMT_STDERR_BUFFER: {
            shDocOptsT *opts = (shDocOptsT*) cmd->format->opts;
            taskIdT *taskId = (taskIdT*) context;
            shDocContextT *docStream = (shDocContextT*) taskId->context;
            ssize_t count, len;

            do { // read anything avaliable
                count= opts->linesz - docStream->stderr.index;
                len = read (taskId->stderr, &docStream->stderr.buffer[docStream->stderr.index], count);

                if (len > 0 || count==0) {
                    err= shDocAddLinesToArray (api, taskId, opts, &docStream->stderr, count);
                    if (err) goto OnErrorExit;
                }
            } while(len > 0);

            break;
        }

        default:
           AFB_API_ERROR(api, "shDocFormatCB: [invalid action] sandbox=%s cmd=%s action=%d", cmd->sandbox->uid, cmd->uid, action);
           goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return 1;
}


// add a new plugin encoder to the registry
void encoderRegister (char *uid, taskFormatCbT *encoderCB) {
    shellRegistryT *registryIdx, *registryEntry;

    // create holding hat for encoder/decoder CB
    registryEntry= (shellRegistryT*) calloc (1, sizeof(shellRegistryT));
    registryEntry->uid = uid;
    registryEntry->formats = encoderCB;


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
    shellRegistryT *registryIdx;
    int index, err;

    // if no format defined default is 1st builtin formater
    if (!formatJ) {
        cmd->format = &encoderBuiltin[0];
        err= cmd->format->encoderCB (cmd, SH_FMT_OPTS_PARSE, NULL);
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
    if (!formats[index].encoderCB) {
        AFB_API_ERROR(api, "encoderFind: [encoder invalid] sandbox=%s cmd=%s format=%s (no encoder callback defined !!!)", cmd->sandbox->uid, cmd->uid, formats[index].uid);
        goto OnErrorExit;
    }

    // update command with coresponding format and all encoderCB to parse format option
    cmd->format= &formats[index];
    err= formats[index].encoderCB (cmd, SH_FMT_OPTS_PARSE, (void*)optsJ);
    if (err) goto OnErrorExit;

OnFormatExit:
    return 0;

OnErrorExit:
    return 1;
}

// Builtin in output formater. Note that first one is used when cmd does not define a format
taskFormatCbT encoderBuiltin[] = {
  {.uid="DOCUMENT"  , .info="text output [default formater]", .encoderCB=shDocFormatCB},
  {.uid="TEXT"      , .info="per line event",  .encoderCB=shDocFormatCB}, // Fulup need to write per line events

  {.uid= NULL} // must be null terminated
};

// register callback and use it to register core encoders
void encoderInit (void) {

  // Builtin Encoder don't have UID
  encoderRegister (NULL, encoderBuiltin);
}

