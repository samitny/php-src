/*
   +----------------------------------------------------------------------+
   | PHP version 4.0                                                      |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997, 1998, 1999, 2000 The PHP Group                   |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.0 of the PHP license,       |
   | that is bundled with this package in the file LICENSE, and is        |
   | available at through the world-wide-web at                           |
   | http://www.php.net/license/2_0.txt.                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Rasmus Lerdorf <rasmus@lerdorf.on.ca>                       |
   |          Zeev Suraski <zeev@zend.com>                                |
   +----------------------------------------------------------------------+
 */
/* $Id: */

#include <stdio.h>
#include "php.h"
#include "ext/standard/php_standard.h"
#include "php_variables.h"
#include "php_globals.h"
#include "SAPI.h"

#include "zend_globals.h"


PHPAPI void php_register_variable(char *var, char *val, pval *track_vars_array ELS_DC PLS_DC)
{
	char *p = NULL;
	char *ip;		/* index pointer */
	char *index;
	int var_len, val_len, index_len;
	zval *gpc_element, **gpc_element_p, **top_gpc_p=NULL;
	zend_bool is_array;
	zend_bool free_index;
	HashTable *symtable1=NULL;
	HashTable *symtable2=NULL;
	
	if (PG(register_globals)) {
		symtable1 = EG(active_symbol_table);
	}
	if (track_vars_array) {
		if (symtable1) {
			symtable2 = track_vars_array->value.ht;
		} else {
			symtable1 = track_vars_array->value.ht;
		}
	}
	if (!symtable1) {
		/* we don't need track_vars, and we're not setting GPC globals either. */
		return;
	}

	/*
	 * Prepare variable name
	 */
	ip = strchr(var, '[');
	if (ip) {
		is_array = 1;
		*ip = 0;
	} else {
		is_array = 0;
	}
	/* ignore leading spaces in the variable name */
	while (*var && *var==' ') {
		var++;
	}
	var_len = strlen(var);
	if (var_len==0) { /* empty variable name, or variable name with a space in it */
		return;
	}
	/* ensure that we don't have spaces or dots in the variable name (not binary safe) */
	for (p=var; *p; p++) {
		switch(*p) {
			case ' ':
			case '.':
				*p='_';
				break;
		}
	}

	/* Prepare value */
	val_len = strlen(val);
	if (PG(magic_quotes_gpc)) {
		val = php_addslashes(val, val_len, &val_len, 0);
	} else {
		val = estrndup(val, val_len);
	}

	index = var;
	index_len = var_len;
	free_index = 0;

	while (1) {
		if (is_array) {
			char *escaped_index;

			if (!index) {
				MAKE_STD_ZVAL(gpc_element);
				array_init(gpc_element);
				zend_hash_next_index_insert(symtable1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			} else {
				if (PG(magic_quotes_gpc) && (index!=var)) {
					/* no need to addslashes() the index if it's the main variable name */
					escaped_index = php_addslashes(index, index_len, &index_len, 0);
				} else {
					escaped_index = index;
				}
				if (zend_hash_find(symtable1, escaped_index, index_len+1, (void **) &gpc_element_p)==FAILURE
					|| (*gpc_element_p)->type != IS_ARRAY) {
					MAKE_STD_ZVAL(gpc_element);
					array_init(gpc_element);
					zend_hash_update(symtable1, escaped_index, index_len+1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
				}
				if (index!=escaped_index) {
					efree(escaped_index);
				}
			}
			if (!top_gpc_p) {
				top_gpc_p = gpc_element_p;
			}
			symtable1 = (*gpc_element_p)->value.ht;
			/* ip pointed to the '[' character, now obtain the key */
			index = ++ip;
			index_len = 0;
			if (*ip=='\n' || *ip=='\r' || *ip=='\t' || *ip==' ') {
				ip++;
			}
			if (*ip==']') {
				index = NULL;
			} else {
				ip = strchr(ip, ']');
				if (!ip) {
					php_error(E_WARNING, "Missing ] in %s variable", var);
					return;
				}
				*ip = 0;
				index_len = strlen(index);
			}
			ip++;
			if (*ip=='[') {
				is_array = 1;
				*ip = 0;
			} else {
				is_array = 0;
			}
		} else {
			MAKE_STD_ZVAL(gpc_element);
			gpc_element->value.str.val = val;
			gpc_element->value.str.len = val_len;
			gpc_element->type = IS_STRING;
			if (!index) {
				zend_hash_next_index_insert(symtable1, &gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			} else {
				zend_hash_update_ptr(symtable1, index, index_len+1, gpc_element, sizeof(zval *), (void **) &gpc_element_p);
			}
			if (!top_gpc_p) {
				top_gpc_p = gpc_element_p;
			}
			break;
		}
	}

	if (top_gpc_p) {
		(*top_gpc_p)->is_ref = 1;
		if (symtable2) {
			zend_hash_update_ptr(symtable2, var, var_len+1, *top_gpc_p, sizeof(zval *), NULL);
			(*top_gpc_p)->refcount++;
		}	
	}
}


void php_treat_data(int arg, char *str ELS_DC PLS_DC SLS_DC)
{
	char *res = NULL, *var, *val;
	pval *array_ptr;
	int free_buffer=0;
	char *strtok_buf = NULL;
	
	switch (arg) {
		case PARSE_POST:
		case PARSE_GET:
		case PARSE_COOKIE:
			if (PG(track_vars)) {
				ALLOC_ZVAL(array_ptr);
				array_init(array_ptr);
				INIT_PZVAL(array_ptr);
				switch (arg) {
					case PARSE_POST:
						zend_hash_add_ptr(&EG(symbol_table), "HTTP_POST_VARS", sizeof("HTTP_POST_VARS"), array_ptr, sizeof(pval *),NULL);
						break;
					case PARSE_GET:
						zend_hash_add_ptr(&EG(symbol_table), "HTTP_GET_VARS", sizeof("HTTP_GET_VARS"), array_ptr, sizeof(pval *),NULL);
						break;
					case PARSE_COOKIE:
						zend_hash_add_ptr(&EG(symbol_table), "HTTP_COOKIE_VARS", sizeof("HTTP_COOKIE_VARS"), array_ptr, sizeof(pval *),NULL);
						break;
				}
			} else {
				array_ptr=NULL;
			}
			break;
		default:
			array_ptr=NULL;
			break;
	}

	if (arg == PARSE_POST) {			/* POST data */
		res = SG(request_info).post_data;
		free_buffer = 0;
	} else if (arg == PARSE_GET) {		/* GET data */
		var = SG(request_info).query_string;
		if (var && *var) {
			res = (char *) estrdup(var);
			free_buffer = 1;
		} else {
			free_buffer = 0;
		}
	} else if (arg == PARSE_COOKIE) {		/* Cookie data */
		var = SG(request_info).cookie_data;
		if (var && *var) {
			res = (char *) estrdup(var);
			free_buffer = 1;
		} else {
			free_buffer = 0;
		}
	} else if (arg == PARSE_STRING) {		/* String data */
		res = str;
		free_buffer = 1;
	}
	if (!res) {
		return;
	}

#if HAVE_FDFLIB
	if((NULL != SG(request_info).content_type) && (0 == strcmp(SG(request_info).content_type, "application/vnd.fdf"))) {
		pval *tmp;

		ALLOC_ZVAL(tmp);
		tmp->value.str.len = SG(request_info).post_data_length;
		tmp->value.str.val = estrndup(SG(request_info).post_data, SG(request_info).post_data_length);
		tmp->type = IS_STRING;
		INIT_PZVAL(tmp);
		zend_hash_add(&EG(symbol_table), "HTTP_FDF_DATA", sizeof("HTTP_FDF_DATA"), &tmp, sizeof(pval *),NULL);

	} else {
#endif
		if (arg == PARSE_COOKIE) {
			var = strtok_r(res, ";", &strtok_buf);
		} else if (arg == PARSE_POST) {
			var = strtok_r(res, "&", &strtok_buf);
		} else {
			var = strtok_r(res, PG(arg_separator), &strtok_buf);
		}

		while (var) {
			val = strchr(var, '=');
			if (val) { /* have a value */
				*val++ = '\0';
				/* FIXME: XXX: not binary safe, discards returned length */
				php_url_decode(var, strlen(var));
				php_url_decode(val, strlen(val));
				php_register_variable(var, val, array_ptr ELS_CC PLS_CC);
			}
			if (arg == PARSE_COOKIE) {
				var = strtok_r(NULL, ";", &strtok_buf);
			} else if (arg == PARSE_POST) {
				var = strtok_r(NULL, "&", &strtok_buf);
			} else {
				var = strtok_r(NULL, PG(arg_separator), &strtok_buf);
			}
		}
#if HAVE_FDFLIB
	}
#endif
	if (free_buffer) {
		efree(res);
	}
}



void php_import_environment_variables(ELS_D PLS_DC)
{
	char **env, *p, *t;
	zval *array_ptr=NULL;

	if (PG(track_vars)) {
		ALLOC_ZVAL(array_ptr);
		array_init(array_ptr);
		INIT_PZVAL(array_ptr);
		zend_hash_add_ptr(&EG(symbol_table), "HTTP_ENV_VARS", sizeof("HTTP_ENV_VARS"), array_ptr, sizeof(pval *),NULL);
	}

	for (env = environ; env != NULL && *env != NULL; env++) {
		p = strchr(*env, '=');
		if (!p) {				/* malformed entry? */
			continue;
		}
		t = estrndup(*env, p - *env);
		php_register_variable(t, p+1, array_ptr ELS_CC PLS_CC);
		efree(t);
	}
}


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 */
