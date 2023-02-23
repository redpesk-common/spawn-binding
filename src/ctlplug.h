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

#ifndef _CTLPLUG_H_INCLUDED_
#define _CTLPLUG_H_INCLUDED_

/*
* Definition of feature for strong identification
* of plugin modules.
*
* The modules are shared/dynamic libraries loaded dynamically.
* It defines memory blocs that are accessible using symbols.
* To correctly identify the memory blocs, this file defines the
* shared memory blocs as being made of 2 parts:
*    - a HEADER with a magic tag and an identification string
*    - a DATA that can be anything
*/

#include <stdbool.h>
#include <string.h>

/* helper for stringization */
#define _CTLPLUG_STR_(x)  #x

/*
* identification of CTLPLUG items (see 50=P 4c=L 55=U 47=G)
* ensure detection of endianness
*/
#ifndef _CTLPLUG_MAGIC_
#define _CTLPLUG_MAGIC_   0x504c5547L
#endif

/**
* get the string identifying the type
* it is as simple as: type -> "type"
*/
#define CTLPLUG_TAG(type)     _CTLPLUG_STR_(type)

/**
* defines what is a ctlplug header identifier
* a basic data structure ensuring that if the
* magic tag is correct, the pointer to the TAG
* is meaningful.
*/
typedef struct
{
	/** the magic identifier */
	const int magic;

	/** the tag string of the plugin */
	const char * const tag;
}
	ctlplug_head_t;

/**
* The macro CTLPLUG_MAKE_HEAD provides the default
* initializer for the ctlplug header matching
* the tag derived from type using CTLPLUG_TAG(type)
*
* Example: CTLPLUG_MAKE_HEAD(a_name) is { 0x504c5547L, "a_name" }
*/
#define CTLPLUG_MAKE_HEAD(type)     { _CTLPLUG_MAGIC_, CTLPLUG_TAG(type) }

/**
* The inline function ctlplug_magic returns the
* magic tag value referenced by the pointer 'ptr'
* as if ptr pointed on a ctlplug header.
*
* @param ptr a pointer
*
* @return the magic tag value as if ptr pointed on a ctlplug header
*/
static inline int ctlplug_magic(const void *ptr)
{
	return ptr ? ((const ctlplug_head_t*)ptr)->magic : 0;
}

/**
* The inline function ctlplug_check_magic returns true
* if the magic tag value referenced by the pointer 'ptr'
* as if ptr pointed on a ctlplug header matches the
* predefined magic value for CTLPLUG objects
*
* @param ptr a pointer
*
* @return true if the magic tag value of ptr matches the CTLPLUG magic value
*/
static inline bool ctlplug_check_magic(const void *ptr)
{
	return ctlplug_magic(ptr) == _CTLPLUG_MAGIC_;
}

/**
* The inline function ctlplug_tag returns the
* tag referenced by the pointer 'ptr'
* as if ptr pointed on a ctlplug header.
*
* NOTE this function should be called only if
* ctlplug_check_magic returned true.
*
* @param ptr a pointer
*
* @return the tag as if ptr pointed on a ctlplug header
*/
static inline const char *ctlplug_tag(const void *ptr)
{
	return ((const ctlplug_head_t*)ptr)->tag;
}

/**
* The inline function ctlplug_check_tag returns true
* if the tag referenced by the pointer 'ptr'
* as if ptr pointed on a ctlplug header matches the
* given tag
*
* NOTE this function should be called only if
* ctlplug_check_magic returned true.
*
* @param ptr a pointer
* @param tag the expected tag
*
* @return true if the tag of ptr matches the expected tag
*/
static inline bool ctlplug_check_tag(const void *ptr, const char *tag)
{
	return !strcmp(tag, ctlplug_tag(ptr));
}

/**
* The inline function ctlplug_check returns true
* if the given pointer ptr matches the ctlplugin magic
* and the given tag.
*
* @param ptr a pointer
* @param tag the expected tag
*
* @return true if ptr matches the CTLPLUG magic and the expected tag
*/
static inline bool ctlplug_check(const void *ptr, const char *tag)
{
	return ctlplug_check_magic(ptr) && ctlplug_check_tag(ptr, tag);
}

/**
* macro for calling ctlplug_check using type instead of "type"
*/
#define CTLPLUG_CHECK(ptr,type) ctlplug_check(ptr,CTLPLUG_TAG(type))

/* name of the structure heading plugins of type */
#define _CTLPLUG_DEF_T_(type)   ctlplug_##type##_def_t

/**
* Macro that defines the type for a a plugin header
* of the given type and and holding the given data
* the given datatype.
*/
#define CTLPLUG_DEFINE(type,datatype) \
		typedef struct { \
			ctlplug_head_t head; \
			datatype data; \
		   } _CTLPLUG_DEF_T_(type);

/**
* Macro for instanciating a plugin header of given 'type'/tag
* The header is named 'name'.
* Its data part is initialized with 'data'
*/
#define CTLPLUG_DECLARE(type,name,data) \
		_CTLPLUG_DEF_T_(type)  name = { CTLPLUG_MAKE_HEAD(type), data }

/**
* Macro for getting the data of an header of given 'type' pointed by 'ptr'
* CAUTION: Only valid if CTLPLUG_CHECK(ptr,type) returns true
*/
#define CTLPLUG_DATA(ptr,type)  (((const _CTLPLUG_DEF_T_(type)*)(ptr))->data)

/**
* Macro for getting the type of callback functions defined using
* the macro CTLPLUG_DEFINE_FUNC
*/
#define CTLPLUG_FUNC_T(type)    ctlplug_##type##_func_t

/**
* Defines a function callback type.
*
* Example:
*
*       CTLPLUG_DEFINE_FUNC(name, return-type, arg1-type, arg2-type) 
*
*    defines the function type
*
*       CTLPLUG_FUNC_T(name)
*
*    as being
*
*       return-type (*)(arg1-type, arg2-type)
*
* Also defines a plugin header of 'type' for functions of this type
*/
#define CTLPLUG_DEFINE_FUNC(type,ret,...) \
		typedef ret (*CTLPLUG_FUNC_T(type))(__VA_ARGS__); \
		CTLPLUG_DEFINE(type,CTLPLUG_FUNC_T(type))

/**
* Macro that instanciate with 'name' the plugin header
* of 'type' for the 'function'
* The function must be defined.
*/
#define CTLPLUG_DECLARE_FUNC(type,name,function) \
		CTLPLUG_DECLARE(type,name,function)

/**
* Declare and begins the declaration of the function of type
*
* Example of use:
*
*     CTLPLUG_EXPORT_FUNC(type,
*     int, istrue,
*              char* name
*     ) {
*        return !strcmp(name,"true");
*     }
*
* Exports the function istrue and declares it.
*/
#define CTLPLUG_EXPORT_FUNC(type,ret,name,...) \
		static ret name##_impl(__VA_ARGS__); \
		CTLPLUG_DECLARE(type,name,name##_impl); \
		static ret name##_impl(__VA_ARGS__)

#endif /* _CTLPLUG_H_INCLUDED_ */
