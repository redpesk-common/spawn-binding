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


#ifndef _SPAWN_ENCODER_S_INCLUDE_
#define _SPAWN_ENCODER_S_INCLUDE_

// usefull classical include
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include  "spawn-binding.h"
#include <afb-timer.h>



typedef enum {
    ENCODER_STDOUT_UNKNOWN,
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
  int (*initCB)(shellCmdT *cmd, json_object *optsJ, void* fmtctx);
  int (*actionsCB)(taskIdT *taskId, encoderActionE action, encoderOpsE subAction, void* fmtctx);
  void *fmtctx;
} taskEncoderCbT;

// spawn-encoder.c
void encoderInit(void);
void encoderRegister (char *uid, taskEncoderCbT *actionsCB);
int  encoderFind (shellCmdT *cmd, json_object *encoderJ);
typedef void (*registerCbT) (const char *uid, taskEncoderCbT *actionsCB);

#endif /* _SPAWN_ENCODER_S_INCLUDE_ */
