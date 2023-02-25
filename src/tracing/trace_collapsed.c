#include "lib/php-header.h"

#include "php_xdebug.h"
#include "tracing_private.h"
#include "trace_collapsed.h"

#include "lib/log.h"
#include "lib/str.h"
#include "lib/var.h"

extern ZEND_DECLARE_MODULE_GLOBALS(xdebug);

void xdebug_trace_collapsed_frame_init(xdebug_trace_collapsed_frame *frame)
{
	frame->function = NULL;

	frame->calls = 0;
	frame->entry = 0;
	frame->nanotime = 0;

	frame->first_child = NULL;
	frame->next = NULL;
	frame->parent = NULL;
}

void xdebug_trace_collapsed_frame_deinit(xdebug_trace_collapsed_frame *frame)
{
        xdebug_trace_collapsed_frame *child = frame->first_child;

        while (child != NULL) {
	        xdebug_trace_collapsed_frame *next = child->next;
                xdebug_trace_collapsed_frame_deinit(child);
                child = next;
        }

        xdfree(frame);
}

char* xdebug_trace_collapsed_function_name(xdebug_trace_collapsed_context *context, xdebug_func f)
{
        char *key = NULL;

	switch (f.type) {
		case XFUNC_NORMAL: {
			key = f.function;
			break;
		}

		case XFUNC_STATIC_MEMBER:
		case XFUNC_MEMBER: {
		        if (f.scope_class) {
			        key = xdebug_sprintf("%s%s%s",
				        ZSTR_VAL(f.scope_class),
				        f.type == XFUNC_STATIC_MEMBER ? "::" : "->",
				        f.function ? f.function : "?"
			        );
		        } else {
		                key = xdebug_sprintf("%s%s%s",
			                f.object_class ? ZSTR_VAL(f.object_class) : "?",
			                f.type == XFUNC_STATIC_MEMBER ? "::" : "->",
			                f.function ? f.function : "?"
		                );
                        }

			break;
		}

#if PHP_VERSION_ID >= 80100
		case XFUNC_FIBER:
			key = f.function;
                        break;
#endif

		case XFUNC_EVAL:
			return "eval";

		case XFUNC_INCLUDE:
			return "include";

		case XFUNC_INCLUDE_ONCE:
			return "include_once";

		case XFUNC_REQUIRE:
			return "require";

		case XFUNC_REQUIRE_ONCE:
			return "require_once";

		case XFUNC_MAIN:
			return "{main}";

		case XFUNC_ZEND_PASS:
			return "{zend_pass}";

		default:
			return "{unknown}";
	}

        int key_len = strlen(key);

        zval *interned = zend_hash_str_find(context->string_table, key, key_len);

        if (interned == NULL) {
                interned = xdmalloc(sizeof(zval));

                ZVAL_STRINGL(interned, key, key_len);

                zend_hash_str_add(context->string_table, key, key_len, interned);
        }

        return Z_STRVAL_P(interned);
}

void *xdebug_trace_collapsed_init(char *fname, zend_string *script_filename, long options)
{
	xdebug_trace_collapsed_context *tmp_collapsed_context;

        char *mode_str = XINI_TRACE(trace_collapsed_sample_type);

        int mode;

        if (strcmp(mode_str, "calls") == 0) {
                mode = XDEBUG_COLLAPSED_MODE_CALLS;
        } else if (strcmp(mode_str, "time") == 0) {
                mode = XDEBUG_COLLAPSED_MODE_TIME;
        } else {
                xdebug_log_ex(XLOG_CHAN_TRACE, XLOG_ERR, "CUSTOM", "invalid collapsed mode %s", mode_str);
                return NULL;
        }

	tmp_collapsed_context = xdmalloc(sizeof(xdebug_trace_collapsed_context));
        tmp_collapsed_context->root = xdmalloc(sizeof(xdebug_trace_collapsed_frame));
	tmp_collapsed_context->current = tmp_collapsed_context->root;
	tmp_collapsed_context->mode = mode;

        xdebug_trace_collapsed_frame_init(tmp_collapsed_context->root);

        ALLOC_HASHTABLE(tmp_collapsed_context->string_table);
        zend_hash_init(tmp_collapsed_context->string_table, 1024, zval_ptr_dtor, NULL, 0);

	tmp_collapsed_context->trace_file = xdebug_trace_open_file(fname, script_filename, options);

	if (!tmp_collapsed_context->trace_file) {
		xdfree(tmp_collapsed_context);
		return NULL;
	}

	return tmp_collapsed_context;
}

void xdebug_trace_collapsed_deinit(void *ctxt)
{
	xdebug_trace_collapsed_context *context = (xdebug_trace_collapsed_context*) ctxt;

	xdebug_file_close(context->trace_file);
	xdebug_file_dtor(context->trace_file);
	context->trace_file = NULL;

        xdebug_trace_collapsed_frame_deinit(context->root);
        context->root = NULL;

        zend_hash_destroy(context->string_table);
        FREE_HASHTABLE(context->string_table);
        context->string_table = NULL;

	xdfree(context);
}

void xdebug_trace_collapsed_write_frame(
    xdebug_trace_collapsed_context *context,
    xdebug_trace_collapsed_frame *frame,
    xdebug_str prefix
)
{
	xdebug_str str = XDEBUG_STR_INITIALIZER;

	xdebug_str_add_str(&str, &prefix);
	xdebug_str_add(&str, frame->function, false);

        switch (context->mode) {
                case XDEBUG_COLLAPSED_MODE_CALLS:
	                xdebug_file_printf(context->trace_file, "%s %d\n", str.d, frame->calls);
                        break;
                case XDEBUG_COLLAPSED_MODE_TIME:
	                xdebug_file_printf(context->trace_file, "%s %d\n", str.d, frame->nanotime);
                        break;
        }

	xdebug_str_addc(&str, ';');

	for (frame = frame->first_child; frame != NULL; frame = frame->next) {
		// Pass as value so that we can safely free it later.
		xdebug_trace_collapsed_write_frame(context, frame, str);
	}

	xdfree(str.d);
}

void xdebug_trace_collapsed_write_footer(void *ctxt)
{
	xdebug_trace_collapsed_context *context = (xdebug_trace_collapsed_context*) ctxt;
	xdebug_trace_collapsed_frame *frame = context->root->first_child;

	xdebug_str prefix = XDEBUG_STR_INITIALIZER;

	while (frame != NULL) {
		xdebug_trace_collapsed_write_frame(context, frame, prefix);

		frame = frame->next;
	}
}

char *xdebug_trace_collapsed_get_filename(void *ctxt)
{
	xdebug_trace_collapsed_context *context = (xdebug_trace_collapsed_context*) ctxt;

	return context->trace_file->name;
}

void xdebug_trace_collapsed_function_entry(void *ctxt, function_stack_entry *fse, int function_nr)
{
	xdebug_trace_collapsed_context *context = (xdebug_trace_collapsed_context*) ctxt;

	xdebug_trace_collapsed_frame *frame = context->current->first_child;

        char *function = xdebug_trace_collapsed_function_name(context, fse->function);

	while (frame != NULL && frame->function != function) {
		frame = frame->next;
	}

	if (frame == NULL) {
		frame = xdmalloc(sizeof(xdebug_trace_collapsed_frame));

                xdebug_trace_collapsed_frame_init(frame);

                frame->function = function;
		frame->next = context->current->first_child;
		frame->parent = context->current;

		context->current->first_child = frame;
	}

        frame->entry = fse->nanotime;

	context->current = frame;
}

void xdebug_trace_collapsed_function_exit(void *ctxt, function_stack_entry *fse, int function_nr)
{
	xdebug_trace_collapsed_context *context = (xdebug_trace_collapsed_context*) ctxt;

	context->current->calls++;
	context->current->nanotime += xdebug_get_nanotime() - context->current->entry;

	context->current->entry = 0;
	context->current = context->current->parent;
}

xdebug_trace_handler_t xdebug_trace_handler_collapsed =
{
	xdebug_trace_collapsed_init,
	xdebug_trace_collapsed_deinit,
	NULL, /* xdebug_trace_collapsed_write_header */
	xdebug_trace_collapsed_write_footer,
	xdebug_trace_collapsed_get_filename,
	xdebug_trace_collapsed_function_entry,
	xdebug_trace_collapsed_function_exit,
	NULL, /* xdebug_trace_collapsed_function_return_value */
	NULL, /* xdebug_trace_collapsed_generator_return_value */
	NULL /* xdebug_trace_collapsed_assignment */
};
