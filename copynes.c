/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * libcopynes.c
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termio.h>
#include <string.h>
#include <arpa/inet.h>
#include "copynes.h"

#define _POSIX_SOURCE 1

#define KB(x) (x * 1024)

/* error codes */
#define FAILED_DATA_OPEN 		1
#define FAILED_CONTROL_OPEN 	2
#define FAILED_COMMAND_SEND 	3
#define FAILED_PLUGIN_OPEN 		4
#define FAILED_BLOCK_SEND		5
#define FAILED_DATA_READ		6

char *errors[] =
{
    "",
    "failed to open data device",
    "failed to open control device",
    "failed to send command",
	"failed to open the specified plugin",
	"failed to send a block of data",
	"failed to read from data channel"
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
        cn->err = FAILED_DATA_OPEN;
        return -cn->err;
    }

    /* try to open the control channel */
    cn->control = open(cn->control_device, O_RDWR | O_NOCTTY | O_NDELAY);

    if (cn->control == -1) 
    {
        cn->err = FAILED_CONTROL_OPEN;
        return -cn->err;
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

int copynes_get_version(copynes_t cn, char** str)
{
    int bytes, i;
    unsigned char a = 0xA1;
	char* p;
	
    *str = (char*)calloc(255, sizeof(char));
    p = *str;
    
	/* send the command */
    if(write(cn->data, &a, 1) != 1) 
    {
        cn->err = FAILED_COMMAND_SEND;
        return -cn->err;
    } 

	/* wait a bit */
	usleep(USLEEP_SHORT);
	
	/* read the bytes */
	for(i = 0; i < 255; i++) 
	{
		bytes = read(cn->data, &p[i], 1);
	
		if(bytes <= 0)
			break;
	}
    
    return strlen(p);
}

int copynes_load_plugin(copynes_t cn, char* plugin)
{
	FILE* f = 0;
	int i;
	char* prg = 0;
	char cmd[] = { 0x4b, 0x00, 0x04, 0x04, 0xb4 };
	
	/* try to open the plugin file */
	if((f = fopen(plugin, "rb")) == 0)
	{
		cn->err = FAILED_PLUGIN_OPEN;
		return -cn->err;
	}
	
	/* send the command to store the plugin prg data at 0400h */
	if(write(cn->data, cmd, 5) != 5)
	{
		fclose(f);
		cn->err = FAILED_COMMAND_SEND;
		return -cn->err;
	}
	
	/* seek to the plugin prg data */
	fseek(f, 128, SEEK_SET);
	prg = calloc(KB(1), sizeof(char));
	
	/* read in the plugin prg data */	
	fread(prg, KB(1), sizeof(char), f);
	
	/* send the data to the CopyNES */
	if(write(cn->data, prg, KB(1)) != KB(1))
	{
		free(prg);
		fclose(f);
		cn->err = FAILED_BLOCK_SEND;
		return -cn->err;
	}
	
	/* cleanup */
	fclose(f);
	free(prg);
	
	/* wait a bit */
	usleep(USLEEP_SHORT);
	
	return 0;
}

int copynes_run_plugin(copynes_t cn)
{
	char cmd[] = { 0x7e, 0x00, 0x04, 0x00, 0xe7 };
	
	/* send the command to execute the code at 0400h */
	if(write(cn->data, cmd, 5) != 5)
	{
		cn->err = FAILED_COMMAND_SEND;
		return -cn->err;
	}
	
	return 0;
}

int copynes_read_mirroring(copynes_t cn, uint8_t* mirroring)
{
	/* read in the mirroring byte */
	if(read(cn->data, mirroring, sizeof(uint8_t)) != sizeof(uint8_t))
	{
		cn->err = FAILED_DATA_READ;
		return -cn->err;
	}
	
	return 0;
}

int copynes_read_packet(copynes_t cn, copynes_packet_t *p)
{
	int bytes, i;
	uint8_t tmpbyte = 0;
	uint16_t tmpshort = 0;
	copynes_packet_t pkt = 0;
	
	/* allocate the packet struct */
	*p = calloc(1, sizeof(struct copynes_packet_s));
	pkt = *p;
	
	/* read in the packet size and store it in big endian (network) order */
	if(read(cn->data, &((uint8_t*)&tmpshort)[0], sizeof(uint8_t)) != sizeof(uint8_t))
	{
		cn->err = FAILED_DATA_READ;
		return -cn->err;
	}
	if(read(cn->data, &((uint8_t*)&tmpshort)[1], sizeof(uint8_t)) != sizeof(uint8_t))
	{
		cn->err = FAILED_DATA_READ;
		return -cn->err;
	}
	
	/* convert the size from network order to host order so that we're portable
	   I want this to run on my PPC mac just fine ;-) */
	pkt->size = ntohs(tmpshort);
	
	/* convert from number of 256 byte blocks to the number of bytes */
	pkt->size << 8;
	
	/* read in the packet format */
	if(read(cn->data, &tmpbyte, 1) != 1)
	{
		cn->err = FAILED_DATA_READ;
		return -cn->err;
	}
	pkt->type = tmpbyte;
	
	/* check to see if there is packet body data to read */
	if((pkt->size > 0) && (pkt->type != PACKET_EOD))
	{
		/* yep, allocate a buffer */
		pkt->data = calloc(pkt->size, sizeof(uint8_t));
		
		/* read in the data */
		while(i < pkt->size)
		{
			/* try to read the remainder of the packet */
			bytes = read(cn->data, &pkt->data[i], (pkt->size - i));
			
			/* break if error */
			if(bytes < 0)
				break;
			
			/* move ahead the number of bytes we've read */
			i += bytes;
			
			/* slow down so that we don't get ahead of the CopyNES */
			usleep(USLEEP_SHORT);
		}
		
		/* make sure we read the entire packet */
		if(i < pkt->size)
		{
			cn->err = FAILED_DATA_READ;
			return -cn->err;
		}
	}
	
	return 0;
}

char* copynes_error_string(copynes_t cn)
{
    return errors[cn->err];
}
