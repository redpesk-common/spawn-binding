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
#ifndef _SPAWN_ENCODER_S_INCLUDE_
#define _SPAWN_ENCODER_S_INCLUDE_

#include <stdbool.h>
#include <json-c/json.h>
#include "spawn-binding.h"

/***************************************************************************/

/**
* encoder system errors
*/
typedef
enum encoder_error {
	ENCODER_NO_ERROR = 0,
	ENCODER_ERROR_PLUGIN_NOT_FOUND = -1,
	ENCODER_ERROR_ENCODER_NOT_FOUND = -2,
	ENCODER_ERROR_INVALID_ENCODER = -3,
	ENCODER_ERROR_INVALID_OPTIONS = -4,
	ENCODER_ERROR_INVALID_SPECIFIER = -5,
	ENCODER_ERROR_OUT_OF_MEMORY = -6,
	ENCODER_ERROR_SYSTEM = -7,
}
	encoder_error_t;

/**
* Get the text for the given error code
*/
extern
const char *
encoder_error_text(encoder_error_t code);

/***************************************************************************/

typedef struct encoder encoder_t;
typedef struct encoder_generator encoder_generator_t;

/***************************************************************************/

struct encoder_generator
{
	/** identifier of the generator */
	const char *uid;

	/** some text for documentation */
	const char *info;

	/**
	* if not zero, the encoder is synchronous: it locks the request until completion
	* and returns its value in the reply.
	* Otherwise, a reply is given when the command is started, its output are sent in events
	*/
	int synchronous;

	/** tuning data */
	const void *tuning;

	/** check options */
	encoder_error_t (*check)(json_object *options);

	/** instanciate data */
	encoder_error_t (*create)(const encoder_generator_t *generator, json_object *options, void **data);

	/** begin processing */
	encoder_error_t (*begin)(void *data, taskIdT *taskId);

	/** process input */
	encoder_error_t (*read)(void *data, taskIdT *taskId, int fd, bool error);

	/** terminate processing */
	encoder_error_t (*end)(void *data, taskIdT *taskId);

	/** destroy data */
	void (*destroy)(void *data);
};

/***************************************************************************/

/**
* Initialization of the factory of encoder generators
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_factory_init(void);

/**
* Adds an array of generators under the given uid
* @param uid the pluginuid
* @param generators an array of generators terminated with an item of NULL uid
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_factory_add(const char *uid, const encoder_generator_t *generators);

/***************************************************************************/

/**
* Search the encoder generator of given pluginuid and encoderuid.
* and return it in encoder
*
* @param pluginuid  uid of the plugin or NULL for builtins
* @param encoderuid uid of the encoder or NULL for default one
* @param generator  pointer for storing the found encoder generator
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_search(
	const char *pluginuid,
	const char *encoderuid,
	const encoder_generator_t **generator);

/**
* Get the encoder generator of given pluginuid and encoderuid.
*
* @param pluginuid  uid of the plugin or NULL for builtins
* @param encoderuid uid of the encoder or NULL for default one
* @param generator  pointer for storing the found encoder generator
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_get(
	const char *pluginuid,
	const char *encoderuid,
	const encoder_generator_t **generator);

/**
* Get the encoder generator of given JSON specifier and check it for the given options.
* Also return the options.
*
* @param specifier  JSON object describing the expected encoder generator
* @param generator  pointer for storing the found encoder generator
* @param options    pointer for storing JSON object of options
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_get_JSON(
	json_object *specifier,
	const encoder_generator_t **generator,
	json_object **options);

/**
* check the options for the given encoder generator
*
* @param generator  the encoder generator
* @param options    JSON object of options
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_check_options(const encoder_generator_t *generator, json_object *options);

/**
* Instanciate an encoder according to the options
*
* @param generator  the encoder generator
* @param options    JSON object of options
* @param encoder    pointer for storing the create encoder
* @return the error code, ENCODER_NO_ERROR if there is no error
*/
extern
encoder_error_t
encoder_generator_create_encoder(const encoder_generator_t *generator, json_object *options, encoder_t **encoder);

/***************************************************************************/

/**
* Destroy the encoder instance
* @param encoder the encoder to be destroyed
*/
extern
void
encoder_destroy(encoder_t *encoder);

int encoderStart(encoder_t *encoder, taskIdT *taskId);
void encoderClose(encoder_t *encoder, taskIdT *taskId);
int encoderRead(encoder_t *encoder, taskIdT *taskId, int fd, bool error);


#endif /* _SPAWN_ENCODER_S_INCLUDE_ */
