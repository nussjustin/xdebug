#include "lib/php-header.h"

#include "php_xdebug.h"
#include "tracing_private.h"
#include "trace_noop.h"

#include "lib/var.h"

extern ZEND_DECLARE_MODULE_GLOBALS(xdebug);

void *xdebug_trace_noop_init(char *fname, zend_string *script_filename, long options)
{
	xdebug_trace_noop_context *tmp_noop_context;

	tmp_noop_context = xdmalloc(sizeof(xdebug_trace_noop_context));
	tmp_noop_context->trace_file = xdebug_trace_open_file(fname, script_filename, options);

	if (!tmp_noop_context->trace_file) {
		xdfree(tmp_noop_context);
		return NULL;
	}

	return tmp_noop_context;
}

void xdebug_trace_noop_deinit(void *ctxt)
{
	xdebug_trace_noop_context *context = (xdebug_trace_noop_context*) ctxt;

	xdebug_file_close(context->trace_file);
	xdebug_file_dtor(context->trace_file);
	context->trace_file = NULL;

	xdfree(context);
}

char *xdebug_trace_noop_get_filename(void *ctxt)
{
	xdebug_trace_noop_context *context = (xdebug_trace_noop_context*) ctxt;

	return context->trace_file->name;
}

void xdebug_trace_noop_function_entry(void *ctxt, function_stack_entry *fse, int function_nr)
{
}

void xdebug_trace_noop_function_exit(void *ctxt, function_stack_entry *fse, int function_nr)
{
}

xdebug_trace_handler_t xdebug_trace_handler_noop =
{
	xdebug_trace_noop_init,
	xdebug_trace_noop_deinit,
	NULL /* xdebug_trace_noop_write_header */,
	NULL /* xdebug_trace_noop_write_footer */,
	xdebug_trace_noop_get_filename,
	xdebug_trace_noop_function_entry,
	xdebug_trace_noop_function_exit,
	NULL /* xdebug_trace_noop_function_return_value */,
	NULL /* xdebug_trace_noop_generator_return_value */,
	NULL /* xdebug_trace_noop_assignment */
};
