/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Gregor Kralik <g.kralik@gmail.com                            |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_sapnwrfc.h"
#include "Zend/zend_exceptions.h"
#include "ext/spl/spl_exceptions.h"

#include "string_helper.h"

#include "sapnwrfc.h"

// class entries
zend_class_entry *sapnwrfc_connection_ce;
zend_class_entry *sapnwrfc_function_ce;
zend_class_entry *sapnwrfc_connection_exception_ce;
zend_class_entry *sapnwrfc_functioncall_exception_ce;

// object handlers
zend_object_handlers sapnwrfc_connection_object_handlers;
zend_object_handlers sapnwrfc_function_object_handlers;

// connection object
typedef struct _sapnwrfc_connection_object {
    RFC_CONNECTION_HANDLE rfc_handle;
    RFC_CONNECTION_PARAMETER *rfc_login_params;
    int rfc_login_params_len;
    zend_object zobj;
} sapnwrfc_connection_object;

// function object
typedef struct _sapnwrfc_function_object {
    zend_object zobj;
} sapnwrfc_function_object;

// connection exception object
typedef struct _sapnwrfc_connection_exception_object {
    zend_object zobj;
} sapnwrfc_connection_exception_object;

// function call exception object
typedef struct _sapnwrfc_functioncall_exception_object {
    zend_object zobj;
} sapnwrfc_functioncall_exception_object;

// helpers for accessing the internal objects
static inline sapnwrfc_connection_object *sapnwrfc_connection_object_fetch(zend_object *obj)
{
    return (sapnwrfc_connection_object *)((char *)obj - XtOffsetOf(sapnwrfc_connection_object, zobj));
}
#define SAPNWRFC_CONNECTION_OBJ_P(zv) SAPNWRFC_CONNECTION_OBJ(Z_OBJ_P(zv))
#define SAPNWRFC_CONNECTION_OBJ(zo) sapnwrfc_connection_object_fetch(zo)

static inline sapnwrfc_function_object *sapnwrfc_function_object_fetch(zend_object *obj)
{
    return (sapnwrfc_function_object *)((char *)obj - XtOffsetOf(sapnwrfc_function_object, zobj));
}
#define SAPNWRFC_FUNCTION_OBJ_P(zv) SAPNWRFC_FUNCTION_OBJ(Z_OBJ_P(zv))
#define SAPNWRFC_FUNCTION_OBJ(zo) sapnwrfc_function_object_fetch(zo)

// forward declaration of class methods
PHP_METHOD(Connection, __construct);
PHP_METHOD(Connection, attributes);
PHP_METHOD(Connection, ping);
PHP_METHOD(Connection, getFunction);
PHP_METHOD(Connection, close);
PHP_METHOD(Connection, setIniPath);
PHP_METHOD(Connection, reloadIniFile);
PHP_METHOD(Connection, version);
PHP_METHOD(Connection, rfcVersion);

PHP_METHOD(Function, __construct);

// class method tables
static zend_function_entry sapnwrfc_connection_class_functions[] = {
    PHP_ME(Connection, __construct, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Connection, attributes, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Connection, ping, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Connection, getFunction, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Connection, close, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(Connection, setIniPath, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Connection, reloadIniFile, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Connection, version, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Connection, rfcVersion, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END
};

static zend_function_entry sapnwrfc_function_class_functions[] = {
    PHP_ME(Function, __construct, NULL, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

// connection object handlers
static zend_object *sapnwrfc_connection_object_create(zend_class_entry *ce)
{
    sapnwrfc_connection_object *intern;

    intern = ecalloc(1, sizeof(sapnwrfc_connection_object) + zend_object_properties_size(ce));
    zend_object_std_init(&intern->zobj, ce);
    object_properties_init(&intern->zobj, ce);
    intern->zobj.handlers = &sapnwrfc_connection_object_handlers;

    return &intern->zobj;
}

static void sapnwrfc_connection_object_free(zend_object *object)
{
    RFC_ERROR_INFO error_info;
    sapnwrfc_connection_object *intern;

    intern = SAPNWRFC_CONNECTION_OBJ(object);

    /* free the RFC handle */
    if(intern->rfc_handle) {
        RfcCloseConnection(intern->rfc_handle, &error_info);
        intern->rfc_handle = NULL;
    }

    intern->rfc_handle = NULL;

    // free login parameters
    if(intern->rfc_login_params) {
        for(int i = 0; i < intern->rfc_login_params_len; i++) {
            free((char *)intern->rfc_login_params[i].name);
            free((char *)intern->rfc_login_params[i].value);
        }
        efree(intern->rfc_login_params);
    }

    /* call Zend's free handler, which will free the object properties */
    zend_object_std_dtor(&intern->zobj);
}

// function object handlers
static zend_object *sapnwrfc_function_object_create(zend_class_entry *ce)
{
    sapnwrfc_function_object *intern;

    intern = ecalloc(1, sizeof(sapnwrfc_function_object) + zend_object_properties_size(ce));
    zend_object_std_init(&intern->zobj, ce);
    object_properties_init(&intern->zobj, ce);
    intern->zobj.handlers = &sapnwrfc_function_object_handlers;

    return &intern->zobj;
}

static void sapnwrfc_function_object_free(zend_object *object)
{
    sapnwrfc_function_object *intern;

    intern = SAPNWRFC_FUNCTION_OBJ(object);

    /* call Zend's free handler, which will free the object properties */
    zend_object_std_dtor(&intern->zobj);
}

// exception factories
static void sapnwrfc_throw_connection_exception_ex(char *msg, int code, zend_string *rfcKey, zend_string *rfcMessage)
{
    zval connection_exception;
    zend_string *exception_message;

    exception_message = zend_string_init(msg, strlen(msg), 0);

    TSRMLS_FETCH();

    zend_replace_error_handling(EH_THROW, zend_ce_exception, NULL);

    object_init_ex(&connection_exception, sapnwrfc_connection_exception_ce);
    zend_update_property_string(sapnwrfc_connection_exception_ce, &connection_exception, "message", sizeof("message") - 1, ZSTR_VAL(exception_message) TSRMLS_CC);
    zend_update_property_long(sapnwrfc_connection_exception_ce, &connection_exception, "code", sizeof("code") - 1, code TSRMLS_CC);
    zend_update_property_string(sapnwrfc_connection_exception_ce, &connection_exception, "rfcKey", sizeof("rfcKey") - 1, ZSTR_VAL(rfcKey) TSRMLS_CC);
    zend_update_property_string(sapnwrfc_connection_exception_ce, &connection_exception, "rfcMessage", sizeof("rfcMessage") - 1, ZSTR_VAL(rfcMessage) TSRMLS_CC);

    zend_throw_exception_object(&connection_exception TSRMLS_CC);

    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
}

static void sapnwrfc_throw_connection_exception(char *msg, int code)
{
    sapnwrfc_throw_connection_exception_ex(msg, code,
        zend_string_init("", strlen(""), 0),
        zend_string_init("", strlen(""), 0));
}

static void sapnwrfc_open_connection(sapnwrfc_connection_object *intern, zval *connection_params)
{
    RFC_ERROR_INFO error_info;
    HashTable *connection_params_hash;
    int i = 0;
    zend_string *key;
    zval *val;

    connection_params_hash = Z_ARRVAL_P(connection_params);
    intern->rfc_login_params_len = zend_hash_num_elements(connection_params_hash);

    intern->rfc_login_params = ecalloc(intern->rfc_login_params_len, sizeof(RFC_CONNECTION_PARAMETER));

    ZEND_HASH_FOREACH_STR_KEY_VAL(connection_params_hash, key, val) {
        if (key) { // is string
            intern->rfc_login_params[i].name = zend_string_to_sapuc(key);
            intern->rfc_login_params[i].value = char_to_sapuc(Z_STRVAL_P(val));

            i++;
        }
    } ZEND_HASH_FOREACH_END();

    intern->rfc_handle = RfcOpenConnection(intern->rfc_login_params, intern->rfc_login_params_len, &error_info);

    if (!intern->rfc_handle) {
        sapnwrfc_throw_connection_exception_ex("Could not open connection",
                                            error_info.code,
                                            sapuc_to_zend_string(error_info.key),
                                            sapuc_to_zend_string(error_info.message));
    }
}

PHP_METHOD(Connection, __construct)
{
    sapnwrfc_connection_object *intern;
    zval *connection_params;
    long len;

    zend_replace_error_handling(EH_THROW, zend_ce_exception, NULL);

    // get the connection parameters
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &connection_params) == FAILURE) {
        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        return;
    }

    len = zend_hash_num_elements(Z_ARRVAL_P(connection_params));
    if (len == 0) {
        sapnwrfc_throw_connection_exception("No connection parameters given", 0);
        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        return;
    }

    // get the connection object (we need access to the rfc_handle)
    intern = SAPNWRFC_CONNECTION_OBJ_P(getThis());

    // open connection
    // TODO inline?
    sapnwrfc_open_connection(intern, connection_params);

    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
}

PHP_METHOD(Connection, close)
{
    sapnwrfc_connection_object *intern;
    RFC_RC rc = RFC_OK;
    RFC_ERROR_INFO error_info;

    zend_replace_error_handling(EH_THROW, NULL, NULL);
    zend_parse_parameters_none();

    intern = SAPNWRFC_CONNECTION_OBJ_P(getThis());

    if(intern->rfc_handle == NULL) {
        // no connection open, return false.
        RETURN_FALSE;
    }

    rc = RfcCloseConnection(intern->rfc_handle, &error_info);
    if (rc == RFC_OK) {
        intern->rfc_handle = NULL;

        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        RETURN_TRUE;
    }

    // we got an error, throw an exception with details
    sapnwrfc_throw_connection_exception_ex("Could not close connection",
                                           error_info.code,
                                           sapuc_to_zend_string(error_info.key),
                                           sapuc_to_zend_string(error_info.message));
    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
    RETURN_NULL();
}

PHP_METHOD(Connection, attributes)
{
    sapnwrfc_connection_object *intern;
    RFC_ATTRIBUTES attributes;
    RFC_ERROR_INFO error_info;
    RFC_RC rc = RFC_OK;

    zend_replace_error_handling(EH_THROW, zend_ce_exception, NULL);

    intern = SAPNWRFC_CONNECTION_OBJ_P(getThis());
    rc = RfcGetConnectionAttributes(intern->rfc_handle, &attributes, &error_info);
    if (rc != RFC_OK) {
        sapnwrfc_throw_connection_exception_ex("Could not fetch connection attributes", error_info.code,
                                               sapuc_to_zend_string(error_info.key),
                                               sapuc_to_zend_string(error_info.message));
        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        RETURN_NULL();
    }

    array_init(return_value);
    add_assoc_str(return_value, "dest", sapuc_to_zend_string(attributes.dest));
    add_assoc_str(return_value, "host", sapuc_to_zend_string(attributes.host));
    add_assoc_str(return_value, "partnerHost", sapuc_to_zend_string(attributes.partnerHost));
    add_assoc_str(return_value, "sysNumber", sapuc_to_zend_string(attributes.sysNumber));
    add_assoc_str(return_value, "sysId", sapuc_to_zend_string(attributes.sysId));
    add_assoc_str(return_value, "client", sapuc_to_zend_string(attributes.client));
    add_assoc_str(return_value, "user", sapuc_to_zend_string(attributes.user));
    add_assoc_str(return_value, "language", sapuc_to_zend_string(attributes.language));
    add_assoc_str(return_value, "trace", sapuc_to_zend_string(attributes.trace));
    add_assoc_str(return_value, "isoLanguage", sapuc_to_zend_string(attributes.isoLanguage));
    add_assoc_str(return_value, "codepage", sapuc_to_zend_string(attributes.codepage));
    add_assoc_str(return_value, "partnerCodepage", sapuc_to_zend_string(attributes.partnerCodepage));
    add_assoc_str(return_value, "rfcRole", sapuc_to_zend_string(attributes.rfcRole));
    add_assoc_str(return_value, "type", sapuc_to_zend_string(attributes.type));
    add_assoc_str(return_value, "partnerType", sapuc_to_zend_string(attributes.partnerType));
    add_assoc_str(return_value, "rel", sapuc_to_zend_string(attributes.rel));
    add_assoc_str(return_value, "partnerRel", sapuc_to_zend_string(attributes.partnerRel));
    add_assoc_str(return_value, "kernelRel", sapuc_to_zend_string(attributes.kernelRel));
    add_assoc_str(return_value, "cpicConvId", sapuc_to_zend_string(attributes.cpicConvId));
    add_assoc_str(return_value, "progName", sapuc_to_zend_string(attributes.progName));
    add_assoc_str(return_value, "partnerBytesPerChar", sapuc_to_zend_string(attributes.partnerBytesPerChar));
    add_assoc_str(return_value, "partnerSystemCodepage", sapuc_to_zend_string(attributes.partnerSystemCodepage));

    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
}

PHP_METHOD(Connection, ping)
{
    sapnwrfc_connection_object *intern;
    RFC_ERROR_INFO error_info;
    RFC_RC rc = RFC_OK;

    zend_replace_error_handling(EH_THROW, zend_ce_exception, NULL);
    zend_parse_parameters_none();

    intern = SAPNWRFC_CONNECTION_OBJ_P(getThis());
    rc = RfcPing(intern->rfc_handle, &error_info);
    if (rc != RFC_OK) {
        sapnwrfc_throw_connection_exception_ex("Failed to reload INI file", error_info.code,
                                               sapuc_to_zend_string(error_info.key),
                                               sapuc_to_zend_string(error_info.message));

        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        RETURN_NULL();
    }

    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
    RETURN_TRUE;
}

PHP_METHOD(Connection, getFunction)
{

}

PHP_METHOD(Connection, setIniPath)
{
    RFC_ERROR_INFO error_info;
    RFC_RC rc = RFC_OK;
    zend_string *path;

    zend_replace_error_handling(EH_THROW, zend_ce_exception, NULL);
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "S", &path) == FAILURE) {
        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        return;
    }

    rc = RfcSetIniPath(zend_string_to_sapuc(path), &error_info);
    if (rc != RFC_OK) {
        sapnwrfc_throw_connection_exception_ex("Failed to reload INI file", error_info.code,
                                               sapuc_to_zend_string(error_info.key),
                                               sapuc_to_zend_string(error_info.message));

        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        RETURN_NULL();
    }

    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
    RETURN_TRUE;
}

PHP_METHOD(Connection, reloadIniFile)
{
    RFC_ERROR_INFO error_info;
    RFC_RC rc = RFC_OK;

    zend_replace_error_handling(EH_THROW, zend_ce_exception, NULL);
    zend_parse_parameters_none();

    rc = RfcReloadIniFile(&error_info);
    if (rc != RFC_OK) {
        sapnwrfc_throw_connection_exception_ex("Failed to reload INI file", error_info.code,
                                               sapuc_to_zend_string(error_info.key),
                                               sapuc_to_zend_string(error_info.message));

        zend_replace_error_handling(EH_NORMAL, NULL, NULL);
        RETURN_NULL();
    }

    zend_replace_error_handling(EH_NORMAL, NULL, NULL);
    RETURN_TRUE;
}

// Connection class methods
PHP_METHOD(Connection, version)
{
    char *version;
    int len;

    zend_parse_parameters_none();

    len = spprintf(&version, 0, "%s", PHP_SAPNWRFC_VERSION);

    RETURN_STRINGL(version, len);
}

PHP_METHOD(Connection, rfcVersion)
{
    char *version;
    int len;
    unsigned int major, minor, patch;

    zend_parse_parameters_none();

    RfcGetVersion(&major, &minor, &patch);

    len = spprintf(&version, 0, "%d.%d.%d", major, minor, patch);

    RETURN_STRINGL(version, len);
}


PHP_METHOD(Function, __construct)
{

}

static void register_sapnwrfc_connection_object()
{
    zend_class_entry ce;

    memcpy(&sapnwrfc_connection_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    sapnwrfc_connection_object_handlers.offset = XtOffsetOf(sapnwrfc_connection_object, zobj);
    sapnwrfc_connection_object_handlers.free_obj = sapnwrfc_connection_object_free;
    sapnwrfc_connection_object_handlers.clone_obj = NULL;

    INIT_CLASS_ENTRY(ce, "SAPNWRFC\\Connection", sapnwrfc_connection_class_functions);
    ce.create_object = sapnwrfc_connection_object_create;
    sapnwrfc_connection_ce = zend_register_internal_class(&ce);
}

static void register_sapnwrfc_function_object()
{
    zend_class_entry ce;

    memcpy(&sapnwrfc_function_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    sapnwrfc_function_object_handlers.offset = XtOffsetOf(sapnwrfc_function_object, zobj);
    sapnwrfc_function_object_handlers.free_obj = sapnwrfc_function_object_free;
    sapnwrfc_function_object_handlers.clone_obj = NULL;

    INIT_CLASS_ENTRY(ce, "SAPNWRFC\\FunctionEntry", sapnwrfc_function_class_functions);
    ce.create_object = sapnwrfc_function_object_create;
    sapnwrfc_function_ce = zend_register_internal_class(&ce);
    sapnwrfc_function_ce->ce_flags |= ZEND_ACC_FINAL;
}

static void register_sapnwrfc_connection_exception_object()
{
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "SAPNWRFC\\ConnectionException", NULL);
    sapnwrfc_connection_exception_ce = zend_register_internal_class_ex(&ce, spl_ce_RuntimeException);
    sapnwrfc_connection_exception_ce->ce_flags |= ZEND_ACC_FINAL;
    zend_declare_property_string(sapnwrfc_connection_exception_ce, "rfcKey", sizeof("rfcKey") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(sapnwrfc_connection_exception_ce, "rfcMessage", sizeof("rfcMessage") - 1, "", ZEND_ACC_PUBLIC);
}

static void register_sapnwrfc_functioncall_exception_object()
{
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "SAPNWRFC\\FunctionCallException", NULL);
    sapnwrfc_functioncall_exception_ce = zend_register_internal_class_ex(&ce, spl_ce_RuntimeException);
    sapnwrfc_functioncall_exception_ce->ce_flags |= ZEND_ACC_FINAL;
    zend_declare_property_string(sapnwrfc_functioncall_exception_ce, "rfcKey", sizeof("rfcKey") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(sapnwrfc_functioncall_exception_ce, "rfcMessage", sizeof("rfcMessage") - 1, "", ZEND_ACC_PUBLIC);
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(sapnwrfc)
{
    register_sapnwrfc_connection_object();
    register_sapnwrfc_function_object();
    register_sapnwrfc_connection_exception_object();
    register_sapnwrfc_functioncall_exception_object();

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(sapnwrfc)
{

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(sapnwrfc)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "sapnwrfc support", "enabled");
    php_info_print_table_end();
}
/* }}} */

/* {{{ sapnwrfc_functions[]
 *
 * Every user visible function must have an entry in sapnwrfc_functions[].
 */
const zend_function_entry sapnwrfc_functions[] = {
    PHP_FE_END    /* Must be the last line in sapnwrfc_functions[] */
};
/* }}} */

/* {{{ sapnwrfc_module_entry
 */
zend_module_entry sapnwrfc_module_entry = {
    STANDARD_MODULE_HEADER,
    "sapnwrfc",
    sapnwrfc_functions,
    PHP_MINIT(sapnwrfc),
    PHP_MSHUTDOWN(sapnwrfc),
    NULL,
    NULL,
    PHP_MINFO(sapnwrfc),
    PHP_SAPNWRFC_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_SAPNWRFC
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
ZEND_GET_MODULE(sapnwrfc)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
