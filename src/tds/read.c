/* FreeTDS - Library of routines accessing Sybase and Microsoft databases
 * Copyright (C) 1998-1999  Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#if TIME_WITH_SYS_TIME
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# endif
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#if HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <assert.h>

#include "tds.h"
#include "tdsiconv.h"
#ifdef DMALLOC
#include <dmalloc.h>
#endif

static char software_version[] = "$Id: read.c,v 1.49 2003/05/02 09:18:37 freddy77 Exp $";
static void *no_unused_var_warn[] = { software_version, no_unused_var_warn };

/**
 * \ingroup libtds
 * \defgroup network Network functions
 * Functions for reading or writing from network.
 */

/** \addtogroup network
 *  \@{ 
 */

/**
 * Loops until we have received buflen characters 
 * return -1 on failure 
 */
static int
goodread(TDSSOCKET * tds, unsigned char *buf, int buflen)
{
	int got = 0;
	int len, retcode;
	fd_set fds;
	time_t start, now;
	struct timeval selecttimeout;

	FD_ZERO(&fds);
	if (tds->timeout) {
		start = time(NULL);
		now = start;

		/* FIXME return even if not finished read if timeout */
		while ((buflen > 0) && ((now - start) < tds->timeout)) {
			int timeleft = tds->timeout;

			len = 0;
			retcode = 0;

			do {
				FD_SET(tds->s, &fds);
				selecttimeout.tv_sec = timeleft;
				selecttimeout.tv_usec = 0;
				retcode = select(tds->s + 1, &fds, NULL, NULL, &selecttimeout);
				/* ignore EINTR errors */
				if (retcode < 0 && errno == EINTR)
					retcode = 0;

				now = time(NULL);
				timeleft = tds->timeout - (now - start);
			} while ((retcode == 0) && timeleft > 0);
			len = READSOCKET(tds->s, buf + got, buflen);
			if (len <= 0) {
				if (len < 0 && errno == EINTR)
					len = 0;
				else
					return (-1);	/* SOCKET_ERROR */
			}

			buflen -= len;
			got += len;
			now = time(NULL);

			if (tds->queryStarttime && tds->longquery_timeout) {
				if ((now - (tds->queryStarttime)) >= tds->longquery_timeout) {
					(*tds->longquery_func) (tds->longquery_param);
				}
			}
		}		/* while buflen... */
	} else {
		/* got = READSOCKET(tds->s, buf, buflen); */
		while (got < buflen) {
			FD_SET(tds->s, &fds);
			select(tds->s + 1, &fds, NULL, NULL, NULL);
			len = READSOCKET(tds->s, buf + got, buflen - got);
			if (len <= 0) {
				if (len < 0 && (errno == EINTR || errno == EINPROGRESS))
					len = 0;
				else
					return (-1);	/* SOCKET_ERROR */
			}
			got += len;
		}

	}

	return (got);
}

/*
** Return a single byte from the input buffer
*/
unsigned char
tds_get_byte(TDSSOCKET * tds)
{
	int rc;

	if (tds->in_pos >= tds->in_len) {
		do {
			if (IS_TDSDEAD(tds) || (rc = tds_read_packet(tds)) < 0)
				return 0;
		} while (!rc);
	}
	return tds->in_buf[tds->in_pos++];
}

/*+
 * Unget will always work as long as you don't call it twice in a row.  It
 * it may work if you call it multiple times as long as you don't backup
 * over the beginning of network packet boundary which can occur anywhere in
 * the token stream.
 */
void
tds_unget_byte(TDSSOCKET * tds)
{
	/* this is a one trick pony...don't call it twice */
	tds->in_pos--;
}

unsigned char
tds_peek(TDSSOCKET * tds)
{
	unsigned char result = tds_get_byte(tds);

	tds_unget_byte(tds);
	return result;
}				/* tds_peek()  */


/**
 * Get an int16 from the server.
 */
TDS_SMALLINT
tds_get_smallint(TDSSOCKET * tds)
{
	unsigned char bytes[2];

	tds_get_n(tds, bytes, 2);
#if WORDS_BIGENDIAN
	if (tds->emul_little_endian) {
		return TDS_BYTE_SWAP16(*(TDS_SMALLINT *) bytes);
	}
#endif
	return *(TDS_SMALLINT *) bytes;
}


/**
 * Get an int32 from the server.
 */
TDS_INT
tds_get_int(TDSSOCKET * tds)
{
	unsigned char bytes[4];

	tds_get_n(tds, bytes, 4);
#if WORDS_BIGENDIAN
	if (tds->emul_little_endian) {
		return TDS_BYTE_SWAP32(*(TDS_INT *) bytes);
	}
#endif
	return *(TDS_INT *) bytes;
}

#if ENABLE_EXTRA_CHECKS
# define TEMP_INIT(s) char* temp = (char*)malloc(s); const size_t temp_size = s
# define TEMP_FREE free(temp);
# define TEMP_SIZE temp_size
#else
# define TEMP_INIT(s) char temp[s]
# define TEMP_FREE ;
# define TEMP_SIZE sizeof(temp)
#endif

/**
 * Fetch a string from the wire.
 * Output string is NOT null terminated.
 * If TDS version is 7 or 8 read unicode string and convert it.
 * @return bytes written to \a dest
 * @param tds  connection information
 * @param string_len length of string to read from wire (in characters)
 * @param dest destination buffer, if NULL string is read and discarded
 * @param need like \a string_len, only different?
 */
int
tds_get_string(TDSSOCKET * tds, int string_len, char *dest, int need)
{
	/* temp is the "preconversion" buffer, the place where the UCS-2 data 
	 * are parked before converting them to ASCII.  It has to have a size, 
	 * and there's no advantage to allocating dynamically 
	 * Also this prevent memory error problem on dynamic memory
	 */
	TEMP_INIT(256);
	char *p, *pend;
	unsigned int in_left, wire_bytes;
	const unsigned int bpc = tds->iconv_info->server_charset.max_bytes_per_char;	/* bytes per char */


	/*
	 * FIX: 02-Jun-2000 by Scott C. Gray (SCG)
	 * 
	 * Bug to malloc(0) on some platforms.
	 */
	if (string_len == 0) {
		TEMP_FREE;
		return 0;
	}

	tdsdump_log(TDS_DBG_NETWORK, "tds_get_string: need %d on wire for %d to client?\n", need, string_len);

	if (IS_TDS7_PLUS(tds)) {
		if (dest == NULL) {
			tds_get_n(tds, NULL, string_len * bpc);
			TEMP_FREE;
			return string_len;
		}
		p = dest;
		pend = dest + need;
		while (string_len > 0 && p < pend) {
			in_left = string_len * bpc;
			if (in_left > TEMP_SIZE)
				in_left = TEMP_SIZE;
			wire_bytes = in_left;
			tds_get_n(tds, temp, wire_bytes);
			p += tds_iconv(to_client, tds->iconv_info, temp, &wire_bytes, p, pend - p);	/* p += tds7_unicode2ascii(tds, temp, in_left, p, pend - p); */
			string_len -= (in_left - wire_bytes) / bpc;
		}
		TEMP_FREE;
		return p - dest;
	} else {
		/* FIXME convert to client charset */
		assert(need >= string_len);
		tds_get_n(tds, dest, string_len);
		TEMP_FREE;
		return string_len;
	}
}

/**
 * Fetch character data the wire.
 * Output is NOT null terminated.
 * If \a iconv_info is not NULL, convert data accordingly.
 * @return bytes written to \a dest
 * \todo put a TDSICONVINFO structure in every TDSCOLINFO
 */
int
tds_get_char_data(TDSSOCKET * tds, char *dest, int size, const TDSCOLINFO * curcol)
{
	/* temp is the "preconversion" buffer, the place where the UCS-2 data 
	 * are parked before converting them to ASCII.  It has to have a size, 
	 * and there's no advantage to allocating dynamically 
	 * Also this prevent memory error problem on dynamic memory
	 */
	TEMP_INIT(256);
	char *p;
	unsigned int in_left, wire_size;

	/* avoid integer division truncation */
	/* TODO size should be wire size.... */
	wire_size = (size * curcol->on_server.column_size) / curcol->column_size;

	if (size == 0) {
		TEMP_FREE;
		return 0;
	}

	tdsdump_log(TDS_DBG_NETWORK, "tds_get_char_data: reading %d on wire for %d to client\n", wire_size, size);

	/* silly case: discard */
	if (dest == NULL) {
		tds_get_n(tds, NULL, wire_size);
		TEMP_FREE;
		return wire_size;
	}

	/*
	 * The next test is crude.  The question we're trying to answer is, 
	 * "Should these data be converted, and if so, which TDSICONVINFO do we use?"  
	 * A Microsoft server sends nchar data in UCS-2, and char/varchar data in the server's 
	 * installed (single-byte) encoding.  SQL Server 2000 has per-column encodings.  
	 *
	 * The right way to answer this question is by passing sufficient metadata to this function.
	 * The right way to do that is to put a TDSICONVINFO structure in every TDSCOLINFO, and teach
	 * tds_iconv to copy input->output (IOW, to act transparently) if it doesn't have a valid 
	 * conversion descriptor.  Then, with the metadata pre-established, we just pass everything to 
	 * tds_iconv and let it do the Right Thing.  
	 */

	if (curcol->column_size != curcol->on_server.column_size) {	/* TODO: remove this test */
		p = dest;
		in_left = 0;
		while (wire_size > 0) {
			int in_size;

			in_size = wire_size;
			if (in_size > TEMP_SIZE)
				in_size = TEMP_SIZE;
			tds_get_n(tds, &temp[in_left], in_size - in_left);
			tdsdump_log(TDS_DBG_NETWORK, "tds_get_char_data: read %d of %d.\n", in_size - in_left, wire_size);
			in_left = in_size;
			/* FIXME ICONV size is the size to read, not the buffer size (also never decremented... bad) */
			p += tds_iconv(to_client, tds->iconv_info, temp, &in_left, p, size);
			wire_size -= in_size - in_left;

			if (in_left) {
				assert(in_left < 4);
				tdsdump_log(TDS_DBG_NETWORK, "tds_get_char_data: partial character, %d bytes, not converted, "
					    "%d bytes left to read from wire.\n", in_left, wire_size);
				memmove(temp, &temp[in_size - in_left], in_left);
			}
		}
		TEMP_FREE;
		return p - dest;
	} else {
		tds_get_n(tds, dest, size);
		TEMP_FREE;
		return size;
	}
}

/**
 * Get N bytes from the buffer and return them in the already allocated space  
 * given to us.  We ASSUME that the person calling this function has done the  
 * bounds checking for us since they know how many bytes they want here.
 * dest of NULL means we just want to eat the bytes.   (tetherow@nol.org)
 */
void *
tds_get_n(TDSSOCKET * tds, void *dest, int need)
{
	int pos, have;

	pos = 0;

	have = (tds->in_len - tds->in_pos);
	while (need > have) {
		/* We need more than is in the buffer, copy what is there */
		if (dest != NULL) {
			memcpy((char *) dest + pos, tds->in_buf + tds->in_pos, have);
		}
		pos += have;
		need -= have;
		tds_read_packet(tds);
		have = tds->in_len;
	}
	if (need > 0) {
		/* get the remainder if there is any */
		if (dest != NULL) {
			memcpy((char *) dest + pos, tds->in_buf + tds->in_pos, need);
		}
		tds->in_pos += need;
	}
	return dest;
}

/**
 * Return the number of bytes needed by specified type.
 */
int
tds_get_size_by_type(int servertype)
{
	switch (servertype) {
	case SYBINT1:
		return 1;
		break;
	case SYBINT2:
		return 2;
		break;
	case SYBINT4:
		return 4;
		break;
	case SYBINT8:
		return 8;
		break;
	case SYBREAL:
		return 4;
		break;
	case SYBFLT8:
		return 8;
		break;
	case SYBDATETIME:
		return 8;
		break;
	case SYBDATETIME4:
		return 4;
		break;
	case SYBBIT:
		return 1;
		break;
	case SYBBITN:
		return 1;
		break;
	case SYBMONEY:
		return 8;
		break;
	case SYBMONEY4:
		return 4;
		break;
	case SYBUNIQUE:
		return 16;
		break;
	default:
		return -1;
		break;
	}
}

/**
 * Read in one 'packet' from the server.  This is a wrapped outer packet of
 * the protocol (they bundle resulte packets into chunks and wrap them at
 * what appears to be 512 bytes regardless of how that breaks internal packet
 * up.   (tetherow\@nol.org)
 * @return bytes readed or -1 on failure
 */
int
tds_read_packet(TDSSOCKET * tds)
{
	unsigned char header[8];
	int len;
	int x = 0, have, need;


	/* Read in the packet header.  We use this to figure out our packet 
	 * length */

	/* Cast to int are needed because some compiler seem to convert
	 * len to unsigned (as FreeBSD 4.5 one)*/
	if ((len = goodread(tds, header, sizeof(header))) < (int) sizeof(header)) {
		/* GW ADDED */
		if (len < 0) {
			tds_client_msg(tds->tds_ctx, tds, 20004, 9, 0, 0, "Read from SQL server failed.");
			tds_close_socket(tds);
			tds->in_len = 0;
			tds->in_pos = 0;
			return -1;
		}
		/* GW ADDED */
		/*  Not sure if this is the best way to do the error 
		 *  handling here but this is the way it is currently 
		 *  being done. */
		tds->in_len = 0;
		tds->in_pos = 0;
		tds->last_packet = 1;
		if (len == 0) {
			tds_close_socket(tds);
		}
		return -1;
	}
	tdsdump_log(TDS_DBG_NETWORK, "Received header @ %L\n%D\n", header, sizeof(header));

/* Note:
 * this was done by Gregg, I don't think its the real solution (it breaks
 * under 5.0, but I haven't gotten a result big enough to test this yet.
 */
	if (IS_TDS42(tds)) {
		if (header[0] != 0x04 && header[0] != 0x0f) {
			tdsdump_log(TDS_DBG_ERROR, "Invalid packet header %d\n", header[0]);
			/*  Not sure if this is the best way to do the error 
			 *  handling here but this is the way it is currently 
			 *  being done. */
			tds->in_len = 0;
			tds->in_pos = 0;
			tds->last_packet = 1;
			return (-1);
		}
	}

	/* Convert our packet length from network to host byte order */
	len = ((((unsigned int) header[2]) << 8) | header[3]) - 8;
	need = len;

	/* If this packet size is the largest we have gotten allocate 
	 * space for it */
	if (len > tds->in_buf_max) {
		unsigned char *p;

		if (!tds->in_buf) {
			p = (unsigned char *) malloc(len);
		} else {
			p = (unsigned char *) realloc(tds->in_buf, len);
		}
		if (!p)
			return -1;	/* FIXME should close socket too */
		tds->in_buf = p;
		/* Set the new maximum packet size */
		tds->in_buf_max = len;
	}

	/* Clean out the in_buf so we don't use old stuff by mistake */
	memset(tds->in_buf, 0, tds->in_buf_max);

	/* Now get exactly how many bytes the server told us to get */
	have = 0;
	while (need > 0) {
		if ((x = goodread(tds, tds->in_buf + have, need)) < 1) {
			/*  Not sure if this is the best way to do the error 
			 *  handling here but this is the way it is currently 
			 *  being done. */
			tds->in_len = 0;
			tds->in_pos = 0;
			tds->last_packet = 1;
			if (len == 0) {
				tds_close_socket(tds);
			}
			return (-1);
		}
		have += x;
		need -= x;
	}
	if (x < 1) {
		/*  Not sure if this is the best way to do the error handling 
		 *  here but this is the way it is currently being done. */
		tds->in_len = 0;
		tds->in_pos = 0;
		tds->last_packet = 1;
		/* return 0 if header found but no payload */
		return len ? -1 : 0;
	}

	/* Set the last packet flag */
	if (header[1]) {
		tds->last_packet = 1;
	} else {
		tds->last_packet = 0;
	}

	/* Set the length and pos (not sure what pos is used for now */
	tds->in_len = have;
	tds->in_pos = 0;
	tdsdump_log(TDS_DBG_NETWORK, "Received packet @ %L\n%D\n", tds->in_buf, tds->in_len);

	return (tds->in_len);
}

/** \@} */
