/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * main.c
 * Copyright (C) David Huseby 2009 <dave@linuxprogrammer.org>
 * 
 * libcopynes.c is free software; you can redistribute it and/or
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termio.h>
#include <string.h>
#include "copynes.h"

#define _POSIX_SOURCE 1
#define FAILED_DATA_OPEN 1
#define FAILED_CONTROL_OPEN 2
#define FAILED_COMMAND_SEND 3

char *errors[] =
{
    "",
    "failed to open data device",
    "failed to open control device",
    "failed to send command"
};

struct copynes_s
{
	int data;
	int control;
    int status;
	int err;
	char* data_device;
	char* control_device;
};


void copynes_flush(copynes_t cn)
{
    /* flush I/O buffers on both serial devices */
    tcflush(cn->data, TCIOFLUSH);
    tcflush(cn->control, TCIOFLUSH);
}

void copynes_get_status(copynes_t cn)
{
    /* get the status bits on the control port */
    ioctl(cn->control, TIOCMGET, &cn->status);
}

void copynes_set_status(copynes_t cn)
{
    /* set the status bits on the control port */
    ioctl(cn->control, TIOCMSET, &cn->status);
}

void copynes_close(copynes_t cn)
{
    close(cn->data);
    close(cn->control);
    if(cn->data_device != 0)
        free(cn->data_device);
    if(cn->control_device != 0)
        free(cn->control_device);
}

int copynes_open(copynes_t cn, char* data_device, char* control_device)
{
    /* clear the struct memory */
    memset(cn, 0, sizeof(copynes_t));
    
    /* store the device strings */
    cn->data_device = strdup(data_device);
    cn->control_device = strdup(control_device);
    
    /* try to open the data channel */
    cn->data = open(cn->data_device, O_RDWR  | O_NOCTTY | O_NDELAY);

    if(cn->data == -1) 
    {
        cn->err = -FAILED_DATA_OPEN;
        return cn->err;
    }

    /* try to open the control channel */
    cn->control = open(cn->control_device, O_RDWR | O_NOCTTY | O_NDELAY);

    if (cn->control == -1) 
    {
        cn->err = -FAILED_CONTROL_OPEN;
        return cn->err;
    }
    
    /* flush the buffers */
    copynes_flush(cn);
    
    return 0;
}

copynes_t copynes_new()
{
    return (copynes_t)malloc(sizeof(struct copynes_s));
}

void copynes_free(void* cn)
{
    copynes_close((copynes_t)cn);
    free(cn);
}

int copynes_reset(copynes_t cn, int mode)
{
    if(mode & RESET_PLAYMODE)
    {
        /* clr /RTS=1 */
        copynes_get_status(cn);
        cn->status &= ~TIOCM_RTS;
        copynes_set_status(cn);
    }
    else
    {
        /* set /RTS=0 */
        copynes_get_status(cn);
        cn->status |= TIOCM_RTS;
        copynes_set_status(cn);
    }
    
    if(!(mode & RESET_NORESET))
    {
        /* pull /RESET low    clear D2
           set /DTR=0 */
        copynes_get_status(cn);
        cn->status &= ~TIOCM_DTR;
        copynes_set_status(cn);
        usleep(USLEEP_SHORT);
    }
    
    /* pull /RESET high       set D2
       clr /DTR=1 */
    copynes_get_status(cn);
    cn->status |= TIOCM_DTR;
    copynes_set_status(cn);
    
    /* stabalize */
    usleep(USLEEP_SHORT);
    copynes_get_status(cn);
    copynes_flush(cn);
    usleep(USLEEP_SHORT);
    
    return 0;
}

int copynes_nes_on(copynes_t cn)
{
    /* get the status of the NES */
    copynes_get_status(cn);
    
    /* TIOCM_CAR is set if the NES is off */
    return !(cn->status & TIOCM_CAR);
}

/* get the copy nes version string, the buffer is allocated using malloc and
   the caller must free it themselves */
int copynes_get_version(copynes_t cn, char** str)
{
    int bytes, i;
    unsigned char a = 0xA1;
	char* p;
	
	copynes_flush(cn);
	
    *str = (char*)calloc(255, sizeof(char));
    p = *str;
    
    bytes = write(cn->data, &a, 1);

    if (bytes != 1) 
    {
        cn->err = -FAILED_COMMAND_SEND;
        return cn->err;
    } 
    else 
    {
        usleep(USLEEP_SHORT);
        
        // read the bytes
        for(i = 0; i < 255; i++) 
        {
            bytes = read(cn->data, &p[i], 1);
        
            if(bytes <= 0)
                break;
        }
    }
    
    return strlen(p);
}

char* copynes_error_string(copynes_t cn)
{
    int idx = -1 * cn->err;
    return errors[idx];
}
