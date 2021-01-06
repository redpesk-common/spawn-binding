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


#ifndef _SPAWN_ENCODERS_INCLUDE_
#define _SPAWN_ENCODERS_INCLUDE_

// usefull classical include
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include  "spawn-binding.h"
#include <afb-timer.h>

typedef enum {
    SH_FMT_OPTS_PARSE,
    SH_FMT_STDOUT_BUFFER,
    SH_FMT_STDERR_BUFFER,
    SH_FMT_TASK_START,
    FMT_TASK_STOP,
    FMT_TASK_KILL,
} shellFmtActionE;

typedef struct  {
  const char *uid;
  const char *info;
  int bufferSize;
  int (*encoderCB)(shellCmdT *cmd, shellFmtActionE action, void* handle);
  void *opts;
} taskFormatCbT;

// spawn-encoder.c
void encoderInit(void);
void encoderRegister (char *uid, taskFormatCbT *encoderCB);
int  encoderFind (shellCmdT *cmd, json_object *formatJ);
typedef void (*registerCbT) (const char *uid, taskFormatCbT *encoderCB);

#endif /* _SPAWN_ENCODERS_INCLUDE_ */
