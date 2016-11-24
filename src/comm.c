/*
 * Race for the Galaxy AI
 *
 * Copyright (C) 2009-2011 Keldon Jones
 *
 * Source file modified by J.-R. Reinhard, November 2016.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "rftg.h"
#include "comm.h"

/*
 * Copy a string located at msg_ptr from a message buffer of length msg_len
 * to a destination buffer of length dest_len. The trailing string \0 has to
 * appear within the buffer length.
 *
 * Returns 1 if message could be read without reading or writing overflow,
 * 0 otherwise.
 * When 1 is returned, the message is copied and the msg_ptr has
 * advanced past the end of the read string.
 * When 0 is returned, the effect on dest, msg and msg_ptr is undefined.
 */
int get_string(char *dest, unsigned int dest_len,
               char *msg, unsigned int msg_len, char **msg_ptr)
{
	char *dest_end = dest + dest_len, *msg_end = msg + msg_len;
	int pos;

	/* Check pointer consistence */
	pos = *msg_ptr - msg;
	if (pos < 0 || pos >= msg_len) return 0;

	/* Copy until we reach the end of the string or surpass a buffer length */
	while (dest != dest_end && *msg_ptr != msg_end)
	{
		/* Copy a byte */
		*dest = **msg_ptr;

		/* Advance message pointer */
		(*msg_ptr)++;

		/* Check for end of string */
		if (!(*dest)) break;

		/* Advance destination pointer */
		dest++;
	}

	/* Test read or write overflow */
	return *dest == 0 ? 1 : 0;
}

/*
 * Copy a 4-byte integer located at msg_ptr from a message buffer of length
 * msg_len to a destination integer. The read integer is in network byte
 * order
 *
 * Returns 1 if integer could be read without reading overflow, 0 otherwise.
 * When 1 is returned, the integer is copied and the msg_ptr has
 * advanced past the end of the read integer.
 * When 0 is returned, the effect on dest, msg and msg_ptr is undefined.
 */
int get_integer(int *dest, char *msg, unsigned int msg_len, char **msg_ptr)
{
	int pos, x;

	/* Check pointer consistency */
	pos = *msg_ptr - msg;
	if (pos < 0 || pos > msg_len - 4) return 0;

	/* Copy the integer */
	memcpy(&x, *msg_ptr, 4);
	*dest = ntohl(x);

	/* Advance message pointer */
	(*msg_ptr) += 4;
	return 1;
}

/*
 * Copy a string to a message.
 *
 * We advance the message pointer past the end of the string.
 */
void put_string(char *ptr, char **msg)
{
	/* Copy bytes */
	while (1)
	{
		/* Copy a byte */
		**msg = *ptr;

		/* Advance message pointer */
		(*msg)++;

		/* Check for end of string */
		if (!(*ptr)) break;

		/* Advance string pointer */
		ptr++;
	}
}

/*
 * Copy an integer to a message.
 *
 * We advance the message pointer past the end of the integer.
 */
void put_integer(int x, char **msg)
{
	/* Translate integer to network form */
	x = htonl(x);

	/* Copy 4 bytes to message */
	memcpy(*msg, &x, 4);

	/* Advance message pointer */
	(*msg) += 4;
}

/*
 * Start creating a message with the given type.
 *
 * We come back and fill in the size later.
 */
void start_msg(char **msg, int type)
{
	/* Start with message type */
	put_integer(type, msg);

	/* Skip past size */
	(*msg) += 4;
}

/*
 * Finish a message by storing the size.
 */
void finish_msg(char *start, char *end)
{
	int size;

	/* Get size */
	size = end - start;

	/* Advance to size area of message */
	start += 4;

	/* Write size */
	put_integer(size, &start);
}

/*
 * Send a formatted message.
 */
void send_msgf(int fd, int type, char *fmt, ...)
{
	char msg[BUF_LEN], *ptr = msg;
	va_list ap;

	/* Start message */
	start_msg(&ptr, type);

	/* Start processing variable arguments */
	va_start(ap, fmt);

	/* Loop over format characters */
	while (*fmt)
	{
		/* Switch on format type */
		switch (*fmt++)
		{
			/* Integer */
			case 'd':

				/* Add integer to message */
				put_integer(va_arg(ap, int), &ptr);
				break;

			/* String */
			case 's':

				/* Add string to message */
				put_string(va_arg(ap, char *), &ptr);
				break;
		}
	}

	/* Stop processing arguments */
	va_end(ap);

	/* Finish message */
	finish_msg(msg, ptr);

	/* Send message */
	send_msg(fd, msg);
}
