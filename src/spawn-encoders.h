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
#ifndef _SPAWN_ENCODER_S_INCLUDE_
#define _SPAWN_ENCODER_S_INCLUDE_

#include <json-c/json.h>
#include "spawn-binding.h"

/**
* Defintion of main actions for encoders
*/
typedef enum {
    ENCODER_TASK_UNSET,
    ENCODER_TASK_STDOUT,
    ENCODER_TASK_STDERR,
    ENCODER_TASK_START,
    ENCODER_TASK_STOP,
    ENCODER_TASK_KILL,
} encoderActionE;

typedef enum {
    ENCODER_OPS_STD,
    ENCODER_OPS_CLOSE,
} encoderOpsE;

typedef struct  {
  const char *uid;
  const char *info;
  int synchronous;
  int (*initCB)(shellCmdT *cmd, json_object *optsJ, void* fmtctx);
  int (*actionsCB)(taskIdT *taskId, encoderActionE action, encoderOpsE subAction, void* fmtctx);
  void *fmtctx;
  int (*check)(json_object *optsJ);
} encoderCbT;

typedef struct {
    char *data;
    long index;
    long size;
    long count;
} streamBufT;

typedef int encoderEventCbT (taskIdT *taskId, streamBufT *docId, ssize_t start, json_object *errorJ, void *context);
typedef int encoderParserCbT(taskIdT *taskId, streamBufT *docId, ssize_t len, encoderEventCbT callback, void* context);


int encoder_generator_factory_init(void);

//typedef struct encoder_generator encoder_generator_t;
typedef encoderCbT encoder_generator_t;
typedef enum encoder_generator_error encoder_generator_error_t;

enum encoder_generator_error {
	ENCODER_GENERATOR_NO_ERROR = 0,
	ENCODER_GENERATOR_ERROR_PLUGIN_NOT_FOUND = -1,
	ENCODER_GENERATOR_ERROR_ENCODER_NOT_FOUND = -2,
	ENCODER_GENERATOR_ERROR_INVALID_ENCODER = -3,
	ENCODER_GENERATOR_ERROR_INVALID_OPTIONS = -4,
	ENCODER_GENERATOR_ERROR_INVALID_SPECIFIER = -5,
};

encoder_generator_error_t
encoder_generator_search(const char *pluginuid, const char *encoderuid, const encoder_generator_t **encoder);


encoder_generator_error_t
encoder_generator_get(const char *pluginuid, const char *encoderuid, json_object *options, const encoder_generator_t **result);

encoder_generator_error_t
encoder_generator_get_JSON(json_object *specifier, const encoder_generator_t **result, json_object **options);

const char *encoder_generator_error_text(encoder_generator_error_t code);


void encoderClose(const encoderCbT *encoder, taskIdT *taskId);
int encoderStart(const encoderCbT *encoder, taskIdT *taskId);
void encoderAbort(const encoderCbT *encoder, taskIdT *taskId);
int encoderRead(const encoderCbT *encoder, taskIdT *taskId, int fd, bool error);


#endif /* _SPAWN_ENCODER_S_INCLUDE_ */
