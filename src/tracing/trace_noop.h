#ifndef XDEBUG_TRACE_NOOP_H
#define XDEBUG_TRACE_NOOP_H

#include "tracing_private.h"

typedef struct _xdebug_trace_noop_context
{
	xdebug_file *trace_file;
} xdebug_trace_noop_context;

extern xdebug_trace_handler_t xdebug_trace_handler_noop;
#endif
