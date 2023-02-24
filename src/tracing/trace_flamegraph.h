#ifndef XDEBUG_TRACE_FLAMEGRAPH_H
#define XDEBUG_TRACE_FLAMEGRAPH_H

#include "tracing_private.h"

#define XDEBUG_FLAMEGRAPH_MODE_CALLS 0
#define XDEBUG_FLAMEGRAPH_MODE_TIME 1

typedef struct _xdebug_trace_flamegraph_frame
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
} xdebug_trace_flamegraph_frame;


typedef struct _xdebug_trace_flamegraph_context
{
	xdebug_file *trace_file;

        zend_long mode;

	xdebug_trace_flamegraph_frame root;
	xdebug_trace_flamegraph_frame *current;
	uint64_t                      current_start;
} xdebug_trace_flamegraph_context;

extern xdebug_trace_handler_t xdebug_trace_handler_flamegraph;
#endif
