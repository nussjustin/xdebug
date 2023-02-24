#include "lib/php-header.h"

#include "php_xdebug.h"
#include "tracing_private.h"
#include "trace_collapsed.h"

#include "lib/log.h"
#include "lib/str.h"
#include "lib/var.h"

extern ZEND_DECLARE_MODULE_GLOBALS(xdebug);

int xdebug_trace_collapsed_frame_for_function(xdebug_trace_collapsed_frame *frame, xdebug_func function)
{
        if (frame->function_internal != function.internal || frame->function_type != function.type) {
                return 0;
        }

        // All types other than the one listed here are identified by their type only, so we can exit without comparing the names.
	switch (function.type) {
	        case XFUNC_NORMAL:
	        case XFUNC_STATIC_MEMBER:
	        case XFUNC_MEMBER:
  #if PHP_VERSION_ID >= 80100
	        case XFUNC_FIBER:
  #endif
                        break;
                default:
                        return 1;
        }

        char *name = xdebug_show_fname(function, XDEBUG_SHOW_FNAME_DEFAULT);

        int result = strcasecmp(frame->function, name);

        xdfree(name);

        return result == 0;
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
	tmp_collapsed_context->current = &tmp_collapsed_context->root;
	tmp_collapsed_context->mode = mode;

	tmp_collapsed_context->root.function = NULL;
	tmp_collapsed_context->root.function_internal = 0;
	tmp_collapsed_context->root.function_type = 0;

	tmp_collapsed_context->root.calls = 0;
	tmp_collapsed_context->root.entry = 0;
	tmp_collapsed_context->root.nanotime = 0;

	tmp_collapsed_context->root.first_child = NULL;
	tmp_collapsed_context->root.next = NULL;
	tmp_collapsed_context->root.parent = NULL;

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

	xdebug_trace_collapsed_frame *frame = &context->root;

	do {
		// Find deepest
		while (frame->first_child != NULL) {
			frame = frame->first_child;
		}

		xdebug_trace_collapsed_frame *parent = frame->parent;

		// Make next frame the first child
		parent->first_child = frame->next;

                // Now free the frame
                if (frame->function != NULL) {
                        xdfree(frame->function);
                }

		xdfree(frame);

		// Re-try parent.
		frame = parent;
	} while (frame != NULL && frame != &context->root);

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
	xdebug_trace_collapsed_frame *frame = context->root.first_child;

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

	while (frame != NULL && !xdebug_trace_collapsed_frame_for_function(frame, fse->function)) {
		frame = frame->next;
	}

	if (frame == NULL) {
		frame = xdmalloc(sizeof(xdebug_trace_collapsed_frame));

                frame->function = xdebug_show_fname(fse->function, XDEBUG_SHOW_FNAME_DEFAULT);
	        frame->function_internal = fse->function.internal;
	        frame->function_type = fse->function.type;

                frame->calls = 0;
                frame->entry = 0;
                frame->nanotime = 0;

		frame->first_child = NULL;
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
