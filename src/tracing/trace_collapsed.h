#ifndef XDEBUG_TRACE_COLLAPSED_H
#define XDEBUG_TRACE_COLLAPSED_H

#include "tracing_private.h"

#define XDEBUG_COLLAPSED_MODE_CALLS 0
#define XDEBUG_COLLAPSED_MODE_TIME 1

typedef struct _xdebug_trace_collapsed_frame
{
	char* function;
        int function_internal;
        int function_type;

	uint64_t calls;
	uint64_t entry;
	uint64_t nanotime;

	void *first_child;
	void *next;
	void *parent;
} xdebug_trace_collapsed_frame;


typedef struct _xdebug_trace_collapsed_context
{
	xdebug_file *trace_file;

        zend_long mode;

	xdebug_trace_collapsed_frame root;
	xdebug_trace_collapsed_frame *current;
	uint64_t                      current_start;
} xdebug_trace_collapsed_context;

extern xdebug_trace_handler_t xdebug_trace_handler_collapsed;
#endif
