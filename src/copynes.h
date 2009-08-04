/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * copynes.h
 * Copyright (C) David Huseby 2009 <dave@linuxprogrammer.org>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with main.c; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#ifndef __LIBCOPYNES__
#define __LIBCOPYNES__

#define USLEEP_SHORT 100000
#define USLEEP_LONG 1000000

/* reset modes */
#define RESET_COPYMODE 			0
#define RESET_PLAYMODE 			1
#define RESET_ALTPORT  			2
#define RESET_NORESET  			4

/* mirroring values */
#define MIRRORING_HORIZONTAL	0		/* hard wired */
#define MIRRORING_VERTICAL		1		/* hard wired */
#define MIRRORING_4SCREEN		2		/* e.g. Gauntlet */
#define MIRRORING_MMC			4		/* e.g. MMC1 */

/* packet types */
#define PACKET_PRG_ROM			1		/* PRG ROM */
#define PACKET_CHR_ROM			2 		/* CHR ROM */
#define PACKET_WRAM				3		/* WRAM */
#define PACKET_RESET			4		/* Reset command from CopyNES */
#define PACKET_EOD				0		/* End of data */

typedef struct copynes_s *copynes_t;

typedef struct copynes_packet_s
{
	int blocks;							/* in 256 byte blocks */
	int size;							/* in bytes */
	int type;							/* packet type */
	uint8_t* data;						/* the data */
} *copynes_packet_t;

copynes_t copynes_new();
void copynes_free(void* cn);

/* initialize/deinitialize the copy nes device */
int copynes_open(copynes_t cn, const char* data_device, const char* control_device);
void copynes_close(copynes_t cn);

/* reset the copy nes device into the mode specified */
int copynes_reset(copynes_t cn, int mode);

/* flush the I/O buffers in the CopyNES */
void copynes_flush(copynes_t cn);

/* read data from the CopyNES */
ssize_t copynes_read(copynes_t cn, void* buf, size_t count, struct timeval *timeout);

/* write data to the CopyNES */
ssize_t copynes_write(copynes_t cn, void* buf, size_t size);

/* test to see if the NES is on or not */
int copynes_nes_on(copynes_t cn);

/* get the copy nes version string */
ssize_t copynes_get_version(copynes_t cn, void* buf, size_t size);

/* load a specified CopyNES plugin, NOTE: plugin must be full path to the .bin */
int copynes_load_plugin(copynes_t cn, const char* plugin);

/* run the loaded plugin */
int copynes_run_plugin(copynes_t cn);

/* read a standard CopyNES packet */
ssize_t copynes_read_packet(copynes_t cn, copynes_packet_t *p, struct timeval timeout);

/* get the error string associated with the error */
char* copynes_error_string(copynes_t cn);

#endif
