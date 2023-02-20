/*
   +----------------------------------------------------------------------+
   | Xdebug                                                               |
   +----------------------------------------------------------------------+
   | Copyright (c) 2002-2021 Derick Rethans                               |
   +----------------------------------------------------------------------+
   | This source file is subject to version 1.01 of the Xdebug license,   |
   | that is bundled with this package in the file LICENSE, and is        |
   | available at through the world-wide-web at                           |
   | https://xdebug.org/license.php                                       |
   | If you did not receive a copy of the Xdebug license and are unable   |
   | to obtain it through the world-wide-web, please send a note to       |
   | derick@xdebug.org so we can mail you a copy immediately.             |
   +----------------------------------------------------------------------+
 */
#include <sys/socket.h>
#include <sys/un.h>
#include "php_xdebug.h"
#include "lib_private.h"
#include "log.h"

ZEND_EXTERN_MODULE_GLOBALS(xdebug)

#include "file.h"

static const int buffer_soft_cap = 4096;

void xdebug_file_init(xdebug_file *xf)
{
	xf->type = XDEBUG_FILE_TYPE_NULL;
	xf->fp.normal  = NULL;
#if HAVE_XDEBUG_ZLIB
	xf->fp.gz      = NULL;
#endif
	xf->name       = NULL;
	xf->buffer     = NULL;

	if (XINI_LIB(buffer_output)) {
		// Create buffer with pre-allocated space.
		xf->buffer = xdebug_str_new();
		xf->buffer->a = buffer_soft_cap+1;
		xf->buffer->d = xdmalloc(xf->buffer->a);
		xf->buffer->d[xf->buffer->l] = '\0';
	}
}

xdebug_file *xdebug_file_ctor(void)
{
	xdebug_file *tmp = xdmalloc(sizeof(xdebug_file));
	xdebug_file_init(tmp);

	return tmp;
}

void xdebug_file_deinit(xdebug_file *xf)
{
	xf->type = XDEBUG_FILE_TYPE_NULL;
	xf->fp.normal = NULL;
#if HAVE_XDEBUG_ZLIB
	xf->fp.gz     = NULL;
#endif
	xdfree(xf->name);
	if (xf->buffer) {
		xdebug_str_free(xf->buffer);
		xf->buffer = NULL;
	}
}

void xdebug_file_dtor(xdebug_file *xf)
{
	xdebug_file_deinit(xf);
	xdfree(xf);
}

const char *socket_ext = ".sock";

int xdebug_file_open(xdebug_file *file, const char *filename, const char *extension, const char *mode)
{
	int len_filename = strlen(filename);
	int len_socket_ext = strlen(socket_ext);

	if (len_filename > len_socket_ext && strncmp(filename + len_filename - len_socket_ext, socket_ext, len_socket_ext) == 0) {
		// Set as early as possible so that we don't segfault when an error occurs.

		file->name = xdstrdup(filename);
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);

		if (!fd) {
			return 0;
		}

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, filename, len_filename);

		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			return 0;
		}

		file->type = XDEBUG_FILE_TYPE_UNIX;
		file->fp.normal = fdopen(fd, "w");

		if (!file->fp.normal) {
			return 0;
		}

		return 1;
	}

	if (XINI_LIB(use_compression)) {
#ifdef HAVE_XDEBUG_ZLIB
		FILE *tmp_file;
		char *combined_extension;

		/* Can't use profiler append with compression */
		if (strcmp(mode, "ab") == 0) {
			xdebug_log_ex(
				XLOG_CHAN_CONFIG, XLOG_WARN, "ZLIB-A",
				"Cannot append to profiling file while file compression is turned on. Falling back to creating an uncompressed file"
			);
			goto uncompressed;
		}

		combined_extension = extension ? xdebug_sprintf("%s.gz", extension) : xdstrdup("gz");
		tmp_file = xdebug_fopen((char*) filename, mode, combined_extension, &(file->name));
		xdfree(combined_extension);

		if (!tmp_file) {
			return 0;
		}

		file->type = XDEBUG_FILE_TYPE_GZ;
		file->fp.normal = tmp_file;
		file->fp.gz = gzdopen(fileno(tmp_file), mode);

		if (!file->fp.gz) {
			fclose(tmp_file);
			return 0;
		}

		return 1;
#else
		char *combined_extension;

		combined_extension = extension ? xdebug_sprintf("%s.gz", extension) : xdstrdup("gz");
		xdebug_log_ex(
			XLOG_CHAN_CONFIG, XLOG_WARN, "NOZLIB",
			"Cannot create the compressed file '%s.%s', because support for zlib has not been compiled in. Falling back to '%s%s%s'",
			filename, combined_extension,
			filename, extension ? "." : "", extension ? extension : ""
		);
		xdfree(combined_extension);
#endif
	}

#ifdef HAVE_XDEBUG_ZLIB
uncompressed:
#endif
	file->type = XDEBUG_FILE_TYPE_NORMAL;
	file->fp.normal = xdebug_fopen((char*) filename, mode, extension, &(file->name));

	if (!file->fp.normal) {
		return 0;
	}

	return 1;
}

int XDEBUG_ATTRIBUTE_FORMAT(printf, 2, 3) xdebug_file_printf(xdebug_file *file, const char *fmt, ...)
{
	va_list argv;

	if (file->buffer) {
		va_start(argv, fmt);
		xdebug_str_add_va_fmt(file->buffer, fmt, argv);
		va_end(argv);

		if (file->buffer->l > buffer_soft_cap) {
			if (xdebug_file_buffer_flush(file)) {
				return 0;
			}
		}

		return 1;
	}

	switch (file->type) {
		case XDEBUG_FILE_TYPE_NORMAL:
		case XDEBUG_FILE_TYPE_UNIX:
			va_start(argv, fmt);
			vfprintf(file->fp.normal, fmt, argv);
			va_end(argv);
			break;
#if HAVE_XDEBUG_ZLIB
		case XDEBUG_FILE_TYPE_GZ: {
			xdebug_str formatted_string = XDEBUG_STR_INITIALIZER;

			va_start(argv, fmt);
			xdebug_str_add_va_fmt(&formatted_string, fmt, argv);
			va_end(argv);

			gzwrite(file->fp.gz, formatted_string.d, formatted_string.l);

			xdebug_str_destroy(&formatted_string);
			break;
		}
#endif
		default:
			xdebug_log_ex(XLOG_CHAN_BASE, XLOG_CRIT, "FTYPE", "Unknown file type used with '%s'", file->name);
			return 0;
	}

	return 1;
}

int xdebug_file_flush(xdebug_file *file)
{
	if (file->buffer) {
		// Do not flush here since this is called after basically every line.
		return 0;
	}

	switch (file->type) {
		case XDEBUG_FILE_TYPE_NORMAL:
			return fflush(file->fp.normal);
#if HAVE_XDEBUG_ZLIB
		case XDEBUG_FILE_TYPE_GZ:
			return gzflush(file->fp.gz, Z_FULL_FLUSH);
#endif
		case XDEBUG_FILE_TYPE_UNIX:
			return 0;
		default:
			xdebug_log_ex(XLOG_CHAN_BASE, XLOG_CRIT, "FTYPE", "Unknown file type used with '%s'", file->name);
			return EOF;
	}
}

int xdebug_file_close(xdebug_file *file)
{
	xdebug_file_buffer_flush(file);

	switch (file->type) {
		case XDEBUG_FILE_TYPE_NORMAL:
		case XDEBUG_FILE_TYPE_UNIX:
			return fclose(file->fp.normal);
#if HAVE_XDEBUG_ZLIB
		case XDEBUG_FILE_TYPE_GZ: {
			int gzret;

			gzret = gzclose(file->fp.gz);
			fclose(file->fp.normal);

			return gzret;
		}
#endif
		default:
			xdebug_log_ex(XLOG_CHAN_BASE, XLOG_CRIT, "FTYPE", "Unknown file type used with '%s'", file->name);
			return EOF;
	}
}

size_t xdebug_file_write(const void *ptr, size_t size, size_t nmemb, xdebug_file *file)
{
	if (file->buffer) {
		xdebug_str_add(file->buffer, (char*)ptr, nmemb);

		if (file->buffer->l > buffer_soft_cap) {
			if (xdebug_file_buffer_flush(file)) {
				return 0;
			}
		}

		return nmemb;
	}

	switch (file->type) {
		case XDEBUG_FILE_TYPE_NORMAL:
		case XDEBUG_FILE_TYPE_UNIX:
			return fwrite(ptr, size, nmemb, file->fp.normal);
#if HAVE_XDEBUG_ZLIB
		case XDEBUG_FILE_TYPE_GZ:
			return gzfwrite(ptr, size, nmemb, file->fp.gz);
#endif
		default:
			xdebug_log_ex(XLOG_CHAN_BASE, XLOG_CRIT, "FTYPE", "Unknown file type used with '%s'", file->name);
			return EOF;
	}
}

int xdebug_file_buffer_flush(xdebug_file *file)
{
	if (!file->buffer) {
		return 0;
	}

 	size_t n;

	switch (file->type) {
		case XDEBUG_FILE_TYPE_NORMAL:
		case XDEBUG_FILE_TYPE_UNIX:
			n = fwrite(file->buffer->d, sizeof(char), file->buffer->l, file->fp.normal);
			break;
#if HAVE_XDEBUG_ZLIB
		case XDEBUG_FILE_TYPE_GZ:
			n = gzwrite(file->fp.gz, file->buffer->d, file->buffer->l);
			break;
#endif
		default:
			xdebug_log_ex(XLOG_CHAN_BASE, XLOG_CRIT, "FTYPE", "Unknown file type used with '%s'", file->name);
			return EOF;
	}

	if (n != file->buffer->l) {
		xdebug_log_ex(XLOG_CHAN_BASE, XLOG_ERR, "FTYPE", "Failed to flush file '%s'", file->name);
		return n;
	}

	file->buffer->l = 0;
	file->buffer->d[file->buffer->l] = '\0';

	return n;
}
