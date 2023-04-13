/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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
#include "spawn-expand.h"

#include "lib/vfmt.h"
#include "lib/stream-buf.h"
#include "lib/line-buf.h"
#include "lib/jsonc-buf.h"

/***************************************************************************************/

//#include "spawn-encoders-plugins.h"

struct encoder
{
	const encoder_generator_t *generator;
	void *data;
};

/***************************************************************************************/

static
void drop_fd(int fd)
{
	char block[4096];
	while(read(fd, block, sizeof block) == (ssize_t)(sizeof block));
}

/***************************************************************************************/

// hold per taskId encoder context
typedef
	struct {
		const char *sout;
		const char *serr;
		FILE *fout;
		FILE *ferr;
	}
		LogCtxT;

/** check options */
static
encoder_error_t log_check(json_object *options)
{
	const char *fileout, *fileerr;
	int err = options == NULL ? 0 : rp_jsonc_unpack(options, "{s?s s?s}" ,"stdout", &fileout, "stderr", &fileerr);
	return err ? ENCODER_ERROR_INVALID_OPTIONS : ENCODER_NO_ERROR;
}

/** instanciate data */
static
encoder_error_t
log_instanciate(const encoder_generator_t *generator, json_object *options, void **data)
{
	LogCtxT *ctx;
	
	/* allocate */
	ctx = calloc(1, sizeof *ctx);
	if (ctx == NULL)
		return ENCODER_ERROR_OUT_OF_MEMORY;

	/* init */
	if (options == NULL 
	 || 0 == rp_jsonc_unpack(options, "{s?s s?s}", "stdout", &ctx->sout, "stderr", &ctx->serr)) {
		*data = ctx;
		return ENCODER_NO_ERROR;
	}
	free(ctx);
	return ENCODER_ERROR_INVALID_OPTIONS;
}

/** open a file */
static FILE *openexp(const char *filename, const char *mode, taskIdT *task)
{
	char *path = utilsExpandKeyTask(filename, task);
	FILE *file = fopen(path, mode);
	if (file == NULL)
		vfmtcl((void*)spawnTaskLog, task, AFB_SYSLOG_LEVEL_ERROR, "opening file %s with mode %s failed: %s",
			path, mode, strerror(errno));
	free(path);
	return file;
}

/** terminate processing */
encoder_error_t
log_begin(void *data, taskIdT *task)
{
	LogCtxT *ctx = data;
	ctx->fout = ctx->sout == NULL ? stdout : openexp(ctx->sout, "a", task);
	if (ctx->fout != NULL) {
		ctx->ferr = ctx->serr == NULL ? stderr : openexp(ctx->serr, "a", task);
		if (ctx->ferr != NULL)
			return ENCODER_NO_ERROR;
		if (ctx->sout != NULL)
			fclose(ctx->fout);
	}
	return ENCODER_ERROR_SYSTEM;
}

/** process input */
encoder_error_t
log_read(void *data, taskIdT *taskId, int fd, bool error)
{
	char buffer[4096];
	LogCtxT *ctx = data;
	FILE *file = error ? ctx->ferr : ctx->fout;

	for (;;) {
		// read
		ssize_t sts = read(fd, buffer, sizeof buffer);
		if (sts > 0)
			fwrite(buffer, 1, (size_t)sts, file);
		else if (sts == 0 || errno != EINTR)
			break;
	}
	return ENCODER_NO_ERROR;
}

/** terminate processing */
encoder_error_t
log_end(void *data, taskIdT *taskId)
{
	LogCtxT *ctx = data;
	if (ctx->ferr != stderr)
		fclose(ctx->ferr);
	if (ctx->fout != stdout)
		fclose(ctx->fout);
	return ENCODER_NO_ERROR;
}

/** drop resources */
static
void
log_destroy(void *data)
{
	LogCtxT *ctx = data;
	free(ctx);
}

/***************************************************************************************/

/** definition of the modes for line text */
typedef
	enum {
		/** send arrays of lines as event at the end */
		mode_text_event,
		/** send arrays of lines as the reply at the end */
		mode_text_sync,
		/** send lines as events, line by line */
		mode_text_line,
		/** send text blob as the reply at the end */
		mode_text_raw
	}
	TextModeT;

/** definition of a stream for output and error */
typedef
	struct {
		/** json object data */
		json_object *data;
		/** handler of buffer */
		stream_buf_t buf;
		/** if overflow is detected */
		bool overflowed;
	}
	TextBufT;

/** context of a text encoder */
typedef
	struct {
		/** the mode */
		TextModeT mode;
		/** the maximum count of lines to hold */
		int maxline;
		/** for holding output */
		TextBufT out;
		/** for holding errors */
		TextBufT err;
	}
	TextCtxT;

/** pair of encoder context and task for callbacks */
typedef
	struct {
		/** the encoder context */
		TextCtxT *ctx;
		/** the task */
		taskIdT *task;
		/** the buffer */
		TextBufT *buf;
	}
	TextTaskCtxT;

/** check options */
static
encoder_error_t
text_check(json_object *options)
{
	int maxline, maxlen, err;

	if (options == NULL)
		return ENCODER_NO_ERROR;

	err = rp_jsonc_unpack(options, "{s?i s?i}", "maxline", &maxline, "maxlen", &maxlen);
	if (err || maxlen <= 0 || maxline <= 0)
		return ENCODER_ERROR_INVALID_OPTIONS;

	return ENCODER_NO_ERROR;
}

/** instanciate data */
static
encoder_error_t
text_instanciate(const encoder_generator_t *generator, json_object *options, void **data)
{
	TextCtxT *ctx;
	int maxlen, err;
	
	/* allocate */
	ctx = calloc(1, sizeof *ctx);
	if (ctx == NULL)
		return ENCODER_ERROR_OUT_OF_MEMORY;

	/* init */
	maxlen =  MAX_DOC_LINE_SIZE;
	ctx->maxline = MAX_DOC_LINE_COUNT;
	ctx->mode = (TextModeT)(intptr_t)generator->tuning;
	if (options != NULL) {
		err = rp_jsonc_unpack(options, "{s?i s?i}", "maxline", &ctx->maxline, "maxlen", &maxlen);
		if (err || maxlen <= 0 || ctx->maxline <= 0) {
			free(ctx);
			return ENCODER_ERROR_INVALID_OPTIONS;
		}
	}
	if (stream_buf_init(&ctx->out.buf, maxlen) != NULL) {
		if (stream_buf_init(&ctx->err.buf, maxlen) != NULL) {
			*data = ctx;
			return ENCODER_NO_ERROR;
		}
		stream_buf_clear(&ctx->out.buf);
	}
	free(ctx);
	return ENCODER_ERROR_OUT_OF_MEMORY;
}

/** encode one line */
static
void
text_line_cb(void *closure, const char *line, size_t length)
{
	TextTaskCtxT *ctx = closure;
	json_object *object = json_object_new_string_len(line, length);
	if (ctx->ctx->mode == mode_text_line) {
		json_object *event = json_object_new_object();
		if (event != NULL) {
			const char *name = ctx->buf == &ctx->ctx->err ? "stderr" : "stdout";
			if (json_object_object_add(event, name, object) == 0) {
				spawnTaskPushEventJSON(ctx->task, event);
				return;
			}
			json_object_put(event);
		}
		json_object_put(object);
		object = NULL;
	}
	if (object == NULL)
		vfmtcl((void*)spawnTaskLog, ctx->task, AFB_SYSLOG_LEVEL_ERROR, "out of memory");
	else {
		json_object *array = ctx->buf->data;
		if (array == NULL) {
			ctx->buf->data = array = json_object_new_array_ext(ctx->ctx->maxline + 1);
			if (array == NULL) {
				json_object_put(object);
				vfmtcl((void*)spawnTaskLog, ctx->task, AFB_SYSLOG_LEVEL_ERROR, "out of memory");
				return;
			}
		}
		else {
			int len = ctx->ctx->maxline - (int)json_object_array_length(array);
			if (len <= 0) {
				ctx->buf->overflowed = true;
				json_object_put(object);
				if (len < 0)
					return;
				object = json_object_new_string("...");
			}
		}
		json_object_array_add(array, object);
	}
}

/** process text input */
encoder_error_t
text_read(void *data, taskIdT *task, int fd, bool error)
{
	TextTaskCtxT ctx = { .ctx = data, .task = task  };
	ctx.buf = error ? &ctx.ctx->err : &ctx.ctx->out;
	if (ctx.buf->overflowed)
		drop_fd(fd);
	else
		line_buf_read(&ctx.buf->buf, fd, text_line_cb, &ctx);
	return ENCODER_NO_ERROR;
}

/** process raw input */
encoder_error_t
text_read_raw(void *data, taskIdT *task, int fd, bool error)
{
	TextCtxT *ctx = data;
	TextBufT *tbuf = error ? &ctx->err : &ctx->out;
	if (!stream_buf_is_full(&tbuf->buf))
		stream_buf_read_fd(&tbuf->buf, fd);
	else {
		tbuf->overflowed = true;
		drop_fd(fd);
	}
	return ENCODER_NO_ERROR;
}

/** terminate processing */
encoder_error_t
text_end(void *data, taskIdT *task)
{
	json_object *object;
	TextCtxT *ctx = data;

	if (ctx->mode == mode_text_raw) {
		ctx->out.data = json_object_new_string_len(
			stream_buf_data(&ctx->out.buf), stream_buf_length(&ctx->out.buf));
		ctx->err.data = json_object_new_string_len(
			stream_buf_data(&ctx->err.buf), stream_buf_length(&ctx->err.buf));
	}
	else {
		TextTaskCtxT tactx = { .ctx = ctx, .task = task };
		tactx.buf = &ctx->err;
		line_buf_end(&ctx->err.buf, text_line_cb, &tactx);
		tactx.buf = &ctx->out;
		line_buf_end(&ctx->out.buf, text_line_cb, &tactx);
	}
	switch (ctx->mode) {
	case mode_text_raw:
	case mode_text_sync:
	case mode_text_event:
		rp_jsonc_pack(&object, "{so* so* so* so*}",
			"stdout", ctx->out.data,
			"stderr", ctx->err.data,
			"stdout-overflow", ctx->out.overflowed ? json_object_new_boolean(1) : NULL,
			"stderr-overflow", ctx->err.overflowed ? json_object_new_boolean(1) : NULL
			);
		ctx->out.data = ctx->err.data = NULL;
		if (ctx->mode == mode_text_event)
			spawnTaskPushEventJSON(task, object);
		else
			spawnTaskReplyJSON(task, 0, object);
		break;
	case mode_text_line:
		break;
	}

	return ENCODER_NO_ERROR;
}

/** destroy the encoder */
static
void
text_destroy(void *data)
{
	TextCtxT *ctx = data;
	json_object_put(ctx->out.data);
	json_object_put(ctx->err.data);
	stream_buf_clear(&ctx->out.buf);
	stream_buf_clear(&ctx->err.buf);
	free(ctx);
}

/***************************************************************************************/

/** conjson of a json encoder */
typedef
	struct {
		/** tokenizer */
		json_tokener *tokener;
		/** buffer for errors */
		stream_buf_t buf;
	}
	JsonCtxT;

/** pair of encoder conjson and task for callbacks */
typedef
	struct {
		/** the encoder conjson */
		JsonCtxT *ctx;
		/** the task */
		taskIdT *task;
	}
	JsonTaskCtxT;

/** check options */
static
encoder_error_t
json_check(json_object *options)
{
	int maxlen, maxdepth, err;

	if (options == NULL)
		return ENCODER_NO_ERROR;

	err = rp_jsonc_unpack(options, "{s?i s?i}", "maxlen", &maxlen, "maxdepth", &maxdepth);
	if (err || maxlen <= 0 || maxdepth <= 0)
		return ENCODER_ERROR_INVALID_OPTIONS;

	return ENCODER_NO_ERROR;
}

/** instanciate data */
static
encoder_error_t
json_instanciate(const encoder_generator_t *generator, json_object *options, void **data)
{
	JsonCtxT *ctx;
	int maxlen, maxdepth, err;
	
	/* allocate */
	ctx = calloc(1, sizeof *ctx);
	if (ctx == NULL)
		return ENCODER_ERROR_OUT_OF_MEMORY;

	/* init */
	maxlen =  MAX_DOC_LINE_SIZE;
	maxdepth = JSON_TOKENER_DEFAULT_DEPTH;
	if (options != NULL) {
		err = rp_jsonc_unpack(options, "{s?i s?i}", "maxlen", &maxlen, "maxdepth", &maxdepth);
		if (err || maxlen <= 0 || maxdepth <= 0) {
			free(ctx);
			return ENCODER_ERROR_INVALID_OPTIONS;
		}
	}
	if (stream_buf_init(&ctx->buf, maxlen) != NULL) {
		ctx->tokener = json_tokener_new_ex(maxdepth);
		if (ctx->tokener != NULL) {
			*data = ctx;
			return ENCODER_NO_ERROR;
		}
		stream_buf_clear(&ctx->buf);
	}
	free(ctx);
	return ENCODER_ERROR_OUT_OF_MEMORY;
}

static
void
json_emit(void *closure, json_object *object, const char *name)
{
	JsonTaskCtxT *ctx = closure;
	json_object *event = json_object_new_object();
	if (event != NULL) {
		if (json_object_object_add(event, name, object) == 0) {
			spawnTaskPushEventJSON(ctx->task, event);
			return;
		}
		json_object_put(event);
	}
	json_object_put(object);
	vfmtcl((void*)spawnTaskLog, ctx->task, AFB_SYSLOG_LEVEL_ERROR, "out of memory");
}

static
void
json_push_cb(void *closure, json_object *object)
{
	if (object != NULL)
		json_emit(closure, object, "stdout");
}

static
void
json_err_cb(void *closure, const char *message)
{
	json_emit(closure, json_object_new_string(message), "json-error");
}

static
void
json_line_cb(void *closure, const char *line, size_t length)
{
	json_emit(closure, json_object_new_string_len(line, length), "stderr");
}

/** process json input */
encoder_error_t
json_read(void *data, taskIdT *task, int fd, bool error)
{
	JsonTaskCtxT ctx = { .ctx = data, .task = task  };
	if (error)
		line_buf_read(&ctx.ctx->buf, fd, json_line_cb, &ctx);
	else
		jsonc_buf_read(ctx.ctx->tokener, fd, json_push_cb, &ctx, json_err_cb);
	return ENCODER_NO_ERROR;
}

/** terminate processing */
encoder_error_t
json_end(void *data, taskIdT *task)
{
	JsonTaskCtxT ctx = { .ctx = data, .task = task  };
	line_buf_end(&ctx.ctx->buf, json_line_cb, &ctx);
	jsonc_buf_end(ctx.ctx->tokener, json_push_cb, &ctx, json_err_cb);
	return ENCODER_NO_ERROR;
}

/** destroy the encoder */
static
void
json_destroy(void *data)
{
	JsonCtxT *ctx = data;
	json_tokener_free(ctx->tokener);
	stream_buf_clear(&ctx->buf);
	free(ctx);
}

/***************************************************************************************/

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
static /*const*/ encoder_generator_t encoderBuiltin[] = { /*1st default == TEXT*/
  {
	.uid = "TEXT" ,
	.info = "unique event at closure with all outputs",
	.check   = text_check,
	.create  = text_instanciate,
	.begin   = NULL,
	.read    = text_read,
	.end     = text_end,
	.destroy = text_destroy,
	.synchronous = 0,
	.tuning  = (void*)(intptr_t)mode_text_event
  },
  {
	.uid = "SYNC" ,
	.info = "return json data at cmd end",
	.check   = text_check,
	.create  = text_instanciate,
	.begin   = NULL,
	.read    = text_read,
	.end     = text_end,
	.destroy = text_destroy,
	.synchronous = 1,
	.tuning  = (void*)(intptr_t)mode_text_sync
  },
  {
	.uid = "LINE" ,
	.info = "one event per line", 
	.check   = text_check,
	.create  = text_instanciate,
	.begin   = NULL,
	.read    = text_read,
	.end     = text_end,
	.destroy = text_destroy,
	.tuning  = (void*)(intptr_t)mode_text_line
  },
  {
	.uid = "RAW" ,
	.info = "return raw data at cmd end",
	.check   = text_check,
	.create  = text_instanciate,
	.begin   = NULL,
	.read    = text_read_raw,
	.end     = text_end,
	.destroy = text_destroy,
	.synchronous = 1,
	.tuning  = (void*)(intptr_t)mode_text_raw
  },
  {
	.uid = "JSON",
	.info = "one event per json blob", 
	.check   = json_check,
	.create  = json_instanciate,
	.begin   = NULL,
	.read    = json_read,
	.end     = json_end,
	.destroy = json_destroy
  },
  {
	.uid = "LOG" ,
	.info = "keep stdout/stderr on server", 
	.check   = log_check,
	.create  = log_instanciate,
	.begin   = log_begin,
	.read    = log_read,
	.end     = log_end,
	.destroy = log_destroy
  },
  {.uid =  NULL} // must be null terminated
};

/***************************************************************************************/

// registry holds a linked list of core+pugins encoders
static encoder_factory_t *first_factory = NULL;

// text of the error
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

// add a new plugin encoder to the registry
encoder_error_t
encoder_generator_factory_add(const char *uid, const encoder_generator_t *generators)
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
  return encoder_generator_factory_add (BUILTIN_FACTORY_NAME, encoderBuiltin);
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
	if (gener->read == NULL)
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
	return generator->check != NULL ? generator->check(options) : ENCODER_NO_ERROR;
}

encoder_error_t
encoder_generator_create_encoder(const encoder_generator_t *generator, json_object *options, encoder_t **p_encoder)
{
	encoder_error_t rc;
	encoder_t *encoder = malloc(sizeof *encoder);
	if (encoder == NULL)
		rc = ENCODER_ERROR_OUT_OF_MEMORY;
	else {
		encoder->generator = generator;
		rc = generator->create == NULL ? ENCODER_NO_ERROR : generator->create(generator, options, &encoder->data);
		if (rc != ENCODER_NO_ERROR)
			free(encoder);
		else
			*p_encoder = encoder;
	}
	return rc;
}

/***************************************************************************************/

void
encoder_destroy(encoder_t *encoder)
{
	if (encoder->generator->destroy)
		encoder->generator->destroy(encoder->data);
	free(encoder);
}

/**
* starts the encoder
*/
int encoderStart(encoder_t *encoder, taskIdT *taskId)
{
	encoder_error_t ec = ENCODER_NO_ERROR;
	if (encoder->generator->begin)
		ec = encoder->generator->begin(encoder->data, taskId);
	if (ec != ENCODER_NO_ERROR) {
		spawnTaskReplyJSON(taskId, AFB_ERRNO_INTERNAL_ERROR, NULL);
	}
	else if (encoder->generator->synchronous) {
		spawnTaskPushInitialStatus(taskId, NULL);
	}
	else {
		spawnTaskReplyJSON(taskId, 0, NULL);
	}
	return 0;
}

/**
* closes the encoder
*/
void encoderClose(encoder_t *encoder, taskIdT *taskId)
{
	if (encoder->generator->end)
		encoder->generator->end(encoder->data, taskId);
	if (!encoder->generator->synchronous)
		spawnTaskPushFinalStatus(taskId, NULL);
}

/**
* process input
*/
int encoderRead(encoder_t *encoder, taskIdT *taskId, int fd, bool error)
{
	if (encoder->generator->read)
		return encoder->generator->read(encoder->data, taskId, fd, error);
	return 0;
}
