/*
 * Copyright (C) 2013 Martin Willi
 * Copyright (C) 2013 revosec AG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <library.h>
#include <errno.h>
#include <unistd.h>

typedef struct private_stream_t private_stream_t;

/**
 * Private data of an stream_t object.
 */
struct private_stream_t {

	/**
	 * Public stream_t interface.
	 */
	stream_t public;

	/**
	 * Underlying socket
	 */
	int fd;

	/**
	 * FILE* for convenience functions, or NULL
	 */
	FILE *file;

	/**
	 * Callback if data is ready to read
	 */
	stream_cb_t read_cb;

	/**
	 * Data for read-ready callback
	 */
	void *read_data;

	/**
	 * Callback if write is non-blocking
	 */
	stream_cb_t write_cb;

	/**
	 * Data for write-ready callback
	 */
	void *write_data;


};

METHOD(stream_t, read_, ssize_t,
	private_stream_t *this, void *buf, size_t len, bool block)
{
	while (TRUE)
	{
		ssize_t ret;

		if (block)
		{
			ret = read(this->fd, buf, len);
		}
		else
		{
			ret = recv(this->fd, buf, len, MSG_DONTWAIT);
			if (ret == -1 && errno == EAGAIN)
			{
				/* unify EGAIN and EWOULDBLOCK */
				errno = EWOULDBLOCK;
			}
		}
		if (ret == -1 && errno == EINTR)
		{	/* interrupted, try again */
			continue;
		}
		return ret;
	}
}

METHOD(stream_t, write_, ssize_t,
	private_stream_t *this, void *buf, size_t len, bool block)
{
	ssize_t ret;

	while (TRUE)
	{
		if (block)
		{
			ret = write(this->fd, buf, len);
		}
		else
		{
			ret = send(this->fd, buf, len, MSG_DONTWAIT);
			if (ret == -1 && errno == EAGAIN)
			{
				/* unify EGAIN and EWOULDBLOCK */
				errno = EWOULDBLOCK;
			}
		}
		if (ret == -1 && errno == EINTR)
		{	/* interrupted, try again */
			continue;
		}
		return ret;
	}
}

/**
 * Remove a registered watcher
 */
static void remove_watcher(private_stream_t *this)
{
	if (this->read_cb || this->write_cb)
	{
		lib->watcher->remove(lib->watcher, this->fd);
	}
}

/**
 * Watcher callback
 */
static bool watch(private_stream_t *this, int fd, watcher_event_t event)
{
	bool keep = FALSE;

	switch (event)
	{
		case WATCHER_READ:
			keep = this->read_cb(this->read_data, &this->public);
			if (!keep)
			{
				this->read_cb = NULL;
			}
			break;
		case WATCHER_WRITE:
			keep = this->write_cb(this->write_data, &this->public);
			if (!keep)
			{
				this->write_cb = NULL;
			}
			break;
		case WATCHER_EXCEPT:
			break;
	}
	return keep;
}

/**
 * Register watcher for stream callbacks
 */
static void add_watcher(private_stream_t *this)
{
	watcher_event_t events = 0;

	if (this->read_cb)
	{
		events |= WATCHER_READ;
	}
	if (this->write_cb)
	{
		events |= WATCHER_WRITE;
	}
	if (events)
	{
		lib->watcher->add(lib->watcher, this->fd, events,
						  (watcher_cb_t)watch, this);
	}
}

METHOD(stream_t, on_read, void,
	private_stream_t *this, stream_cb_t cb, void *data)
{
	remove_watcher(this);

	this->read_cb = cb;
	this->read_data = data;

	add_watcher(this);
}

METHOD(stream_t, on_write, void,
	private_stream_t *this, stream_cb_t cb, void *data)
{
	remove_watcher(this);

	this->write_cb = cb;
	this->write_data = data;

	add_watcher(this);
}

METHOD(stream_t, vprint, int,
	private_stream_t *this, char *format, va_list ap)
{
	if (!this->file)
	{
		this->file = fdopen(this->fd, "w+");
		if (!this->file)
		{
			return -1;
		}
	}
	return vfprintf(this->file, format, ap);
}

METHOD(stream_t, print, int,
	private_stream_t *this, char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = vprint(this, format, ap);
	va_end(ap);

	return ret;
}

METHOD(stream_t, destroy, void,
	private_stream_t *this)
{
	remove_watcher(this);
	if (this->file)
	{
		fclose(this->file);
	}
	else
	{
		close(this->fd);
	}
	free(this);
}

/**
 * See header
 */
stream_t *stream_create_from_fd(int fd)
{
	private_stream_t *this;

	INIT(this,
		.public = {
			.read = _read_,
			.on_read = _on_read,
			.write = _write_,
			.on_write = _on_write,
			.print = _print,
			.vprint = _vprint,
			.destroy = _destroy,
		},
		.fd = fd,
	);

	return &this->public;
}
