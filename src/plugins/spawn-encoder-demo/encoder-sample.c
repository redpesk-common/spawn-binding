/*
 * Copyright (C) 2021 "IoT.bzh"
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at https://opensource.org/licenses/MIT.
 */

#define _GNU_SOURCE

#include <assert.h>

#include <afb/afb-binding.h>

#include <afb-helpers4/afb-data-utils.h>
#include <rp-utils/rp-jsonc.h>

#include "lib/stream-buf.h"
#include "lib/line-buf.h"

#include "spawn-binding.h"
#include "spawn-sandbox.h"
#include "spawn-encoders.h"

#include "spawn-encoders-plugins.h"

#include "spawn-subtask.h"

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
 *  - a plugin may contains one or multiple encoder. Plugins use a private namespace for encoder 'uid'
 *
 *  Note: this demo encoder is provided as template for developpers to create there own code.
 *  to keep sample simple it leverage generic read pipe and line parsing from builtin encoder.
 */

DECLARE_SPAWN_ENCODER_PLUGIN("encoder_sample", encoder_entry)


#define MY_DEFAULT_blkcount 10   // send one event every 10 lines
#define MY_DEFAULT_maxlen 512  // any line longer than this will be split

// hold per taskId encoder context
typedef
	struct {
		json_object *array;
		stream_buf_t sout;
		stream_buf_t serr;
		int linecount;
		int errcount;
		int blkcount;
		int maxlen;
	}
		MyCtxT;

typedef
	struct {
		MyCtxT *ctx;
		taskIdT *task;
	}
		MyTaskCtxT;


static void on_out_line(void *closure, const char *line, size_t length)
{
	MyTaskCtxT *tc = closure;
	taskIdT *task = tc->task;
	MyCtxT *ctx = tc->ctx;

	ctx->linecount++;
	if (ctx->array == NULL)
		ctx->array = json_object_new_array();
	json_object_array_add(ctx->array, json_object_new_string_len(line, length));
	if (json_object_array_length(ctx->array) == ctx->blkcount) {
		spawnTaskPushEventJSON(task, ctx->array);
		ctx->array = NULL;
	}
}

// Send each stderr line as a string event
static void on_err_line(void *closure, const char *line, size_t length)
{
	MyTaskCtxT *tc = closure;
	taskIdT *task = tc->task;
	MyCtxT *ctx = tc->ctx;
	json_object *object;

	rp_jsonc_pack (&object, "{si so}",
		"err", ++ctx->errcount,
		"data", json_object_new_string_len(line, length));
	spawnTaskPushEventJSON(task, object);
}

/** check options */
static
encoder_error_t my_check(json_object *options)
{
	int blkcount, maxlen, err = rp_jsonc_unpack(options, "{s?i s?i}" ,"blkcount", &blkcount, "maxlen", &maxlen);
	return err  || blkcount < 1 || maxlen < 1 ? ENCODER_ERROR_INVALID_OPTIONS : ENCODER_NO_ERROR;
}

/** instanciate data */
static
encoder_error_t
my_instanciate(const encoder_generator_t *generator, json_object *options, void **data)
{
	encoder_error_t rc = ENCODER_ERROR_OUT_OF_MEMORY;
	int err;
	MyCtxT *ctx;
	
	/* allocate */
	ctx = calloc(1, sizeof *ctx);
	if (ctx != NULL) {
		/* init */
		ctx->blkcount = MY_DEFAULT_blkcount;
		ctx->maxlen = MY_DEFAULT_maxlen;
		err = rp_jsonc_unpack(options, "{s?i s?i}" ,"blkcount", &ctx->blkcount, "maxlen", &ctx->maxlen);
		if (err || ctx->blkcount < 1 || ctx->maxlen < 1)
			rc = ENCODER_ERROR_INVALID_OPTIONS;
		else {
			if (stream_buf_init(&ctx->sout, ctx->maxlen) != NULL) {
				if (stream_buf_init(&ctx->serr, ctx->maxlen) != NULL) {
					/* finalize */
					*data = ctx;
					return ENCODER_NO_ERROR;
				}
				stream_buf_clear(&ctx->sout);
			}
		}
		free(ctx);
	}
	return rc;
}

/** process input */
encoder_error_t
my_read(void *data, taskIdT *taskId, int fd, bool error)
{
	MyTaskCtxT tc = { .ctx = data, .task = taskId };
	if (error)
		line_buf_read(&tc.ctx->serr, fd, on_err_line, &tc);
	else
		line_buf_read(&tc.ctx->sout, fd, on_out_line, &tc);
	return ENCODER_NO_ERROR;
}

/** terminate processing */
encoder_error_t
my_end(void *data, taskIdT *taskId)
{
	json_object *object;
	MyTaskCtxT tc = { .ctx = data, .task = taskId };
	line_buf_end(&tc.ctx->serr, on_err_line, &tc);
	line_buf_end(&tc.ctx->sout, on_out_line, &tc);
	if (tc.ctx->array != NULL) {
		spawnTaskPushEventJSON(tc.task, tc.ctx->array);
		tc.ctx->array = NULL;
	}
	rp_jsonc_pack (&object, "{si si}",
		"errcount", tc.ctx->errcount,
		"linecount", tc.ctx->linecount);
	spawnTaskPushEventJSON(tc.task, object);
	return ENCODER_NO_ERROR;
}

static
void
my_destroy(void *data)
{
	MyCtxT *ctx = data;
	stream_buf_clear(&ctx->sout);
	stream_buf_clear(&ctx->sout);
	free(ctx);
}

// list custom encoders for registration
encoder_generator_t MyEncoders[] = {
  {
	.uid     = "my-custom-encoder",
	.info    = "One event per blkcount=xxx lines",
	.check   = my_check,
	.create  = my_instanciate,
	.begin   = NULL,
	.read    = my_read,
	.end     = my_end,
	.destroy = my_destroy
  },
  {.uid= NULL} // terminator
};


static
encoder_error_t
encoder_entry(json_object *config)
{
	return encoder_generator_factory_add(SpawnEncoderManifest.name, MyEncoders);

}
