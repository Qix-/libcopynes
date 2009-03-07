/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * libcopynes.h
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
#define RESET_COPYMODE 0
#define RESET_PLAYMODE 1
#define RESET_ALTPORT  2
#define RESET_NORESET  4

typedef struct copynes_s *copynes_t;

copynes_t copynes_new();
void copynes_free(void* cn);

/* initialize/deinitialize the copy nes device */
int copynes_open(copynes_t cn, char* data_device, char* control_device);
void copynes_close(copynes_t cn);

/* reset the copy nes device into the mode specified */
int copynes_reset(copynes_t cn, int mode);

/* test to see if the NES is on or not */
int copynes_nes_on(copynes_t cn);

/* get the copy nes version string, the buffer is allocated using malloc and
   the caller must free it themselves */
int copynes_get_version(copynes_t cn, char** str);

/* get the error string associated with the error */
char* copynes_error_string(copynes_t cn);

#endif
