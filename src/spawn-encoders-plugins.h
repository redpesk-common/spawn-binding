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
#ifndef _SPAWN_ENCODERS_PLUGINS_INCLUDE_
#define _SPAWN_ENCODERS_PLUGINS_INCLUDE_

#include "spawn-encoders.h"

#define PLUGIN_ENCODER_MAGIC 159357456

// this structure is used by plugin registration callback
typedef const struct {
  long magic;
  streamBufT *(*bufferSet) (streamBufT *buffer, ssize_t size);
  int (*registrate) (const char *uid, const encoderCbT *actionsCB);
  int (*jsonParser) (taskIdT *taskId, streamBufT *docId, ssize_t len, encoderEventCbT callback, void* context);
  int (*textParser) (taskIdT *taskId, streamBufT *docId, ssize_t len, encoderEventCbT callback, void* context);
  int (*readStream) (taskIdT *taskId, int pipefd, streamBufT *buffer, ssize_t bufsize, encoderParserCbT parserCB, encoderEventCbT eventCB, encoderOpsE operation, void *userdata);
} encoderPluginCbT;


#endif /* _SPAWN_ENCODERS_PLUGINS_INCLUDE_ */
