/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * copynes.c
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
#include <termios.h>		/* POSIX compiant terminal I/O bits */
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h> 
#include <sys/ioctl.h>
#include <sys/termios.h>	/* platform specific terminal I/O bits */

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
#define FAILED_INVALID_PARAMS	7
#define FAILED_DATA_WRITE		8

char *errors[] =
{
    "",
    "failed to open data device",
    "failed to open control device",
    "failed to send command",
	"failed to open the specified plugin",
	"failed to send a block of data",
	"failed to read from data channel",
	"passed invalid parameters to library function",
	"failed to write data to the data channel"
};

/* protocol commands */
uint8_t CMD_GET_VERSION[] = 	{ 0xa1 };
uint8_t CMD_LOAD_PLUGIN[] = 	{ 0x4b, 0x00, 0x04, 0x04, 0xb4 };
uint8_t CMD_RUN_PLUGIN[] =		{ 0x7e, 0x00, 0x04, 0x00, 0xe7 };

#define CMD_SIZE(x) (sizeof(x) / sizeof(x[0]))

/* CopyNES state */
struct copynes_s
{
	int data;
	int control;
    int status;
	int err;
	int rbyte;
	int rcount;
	char* data_device;
	char* control_device;
	char* current_plugin;
	fd_set readfds;
	fd_set exceptfds;
	uint8_t uservar_enabled[4];
	uint8_t uservar_value[4];
	struct termios old_tios_data_device;
	struct termios old_tios_control_device;
};

/* private interface function declarations */
static void copynes_get_status(copynes_t cn);
static void copynes_set_status(copynes_t cn);
static void copynes_configure_tios(struct termios * tios); /* used by copynes_configure_devices */
static void copynes_configure_devices(copynes_t cn);


copynes_t copynes_new()
{
    return (copynes_t)calloc(1, sizeof(struct copynes_s));
}


void copynes_free(void* cn)
{
    copynes_close((copynes_t)cn);
    free(cn);
}


/* initialize/deinitialize the copy nes device */
int copynes_open(copynes_t cn, const char* data_device, const char* control_device)
{
    /* clear the struct memory */
    memset(cn, 0, sizeof(copynes_t));
    
    /* store the device strings */
    cn->data_device = strdup(data_device);
    cn->control_device = strdup(control_device);
    cn->uservar_enabled[0] = 0;
    cn->uservar_enabled[1] = 0;
    cn->uservar_enabled[2] = 0;
    cn->uservar_enabled[3] = 0;

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
	
	/* configure the devices */
	copynes_configure_devices(cn);
	
    /* flush the buffers */
    copynes_flush(cn);
    
    return 0;
}


void copynes_close(copynes_t cn)
{
	/* reset the termios settings */
	tcsetattr(cn->data, TCSAFLUSH, &cn->old_tios_data_device);
	tcsetattr(cn->control, TCSAFLUSH, &cn->old_tios_control_device);
	
	/* close the devices */
    close(cn->data);
    close(cn->control);
	
	/* free up the strings */
    if(cn->data_device != 0)
        free(cn->data_device);
    if(cn->control_device != 0)
        free(cn->control_device);
	if(cn->current_plugin != 0)
		free(cn->current_plugin);
}


/* reset the copy nes device into the mode specified */
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


/* flush the I/O buffers in the CopyNES */
void copynes_flush(copynes_t cn)
{
    /* flush I/O buffers on both serial devices */
    tcflush(cn->data, TCIOFLUSH);
    tcflush(cn->control, TCIOFLUSH);
}


/* read data from the CopyNES */
ssize_t copynes_read(copynes_t cn, void* buf, size_t count, struct timeval *timeout)
{
	ssize_t ret = 0;
	unsigned int i = 0;
	int bytes = 0;
	
	if((count <= 0) || (buf == 0))
	{
		cn->err = FAILED_INVALID_PARAMS;
		return -cn->err;
	}
	
	/* try to read as much data as was requested */
	while(i < count)
	{
		/* check to see if we've run out of time */
		if((timeout != 0) && (timeout->tv_sec <= 0) && (timeout->tv_usec <= 0))
			break;
		
		/* clear the fd sets */
		FD_ZERO(&cn->readfds);
		FD_ZERO(&cn->exceptfds);
		
		/* add the file descriptors to the test sets */
		FD_SET(cn->data, &cn->readfds);
		FD_SET(cn->data, &cn->exceptfds);
		
		/* wait for input */
		if((ret = select(cn->data + 1, &cn->readfds, 0, &cn->exceptfds, timeout)) < 0)
		{
			cn->err = FAILED_DATA_READ;
			return -cn->err;
		}
		
		/* we've got data ready to read */
		if((ret > 0) && FD_ISSET(cn->data, &cn->readfds))
		{
			if((bytes = read(cn->data, (buf + i), (count - i))) < 0)
			{
				cn->err = FAILED_DATA_READ;
				return -cn->err;
			}
			
			i += bytes;
		}
	}
	
	return (ssize_t)i;
}

/* write data to the CopyNES */
ssize_t copynes_write(copynes_t cn, void* buf, size_t size)
{
	ssize_t ret = 0;
	
	if((size <= 0) || (buf == 0))
	{
		cn->err = FAILED_INVALID_PARAMS;
		return -cn->err;
	}
	
	if((ret = write(cn->data, buf, size)) < 0)
	{
		cn->err = FAILED_DATA_WRITE;
		return -cn->err;
	}
	
	return ret;
}


/* test to see if the NES is on or not */
int copynes_nes_on(copynes_t cn)
{
    /* get the status of the NES */
    copynes_get_status(cn);
    
    /* TIOCM_CAR is set if the NES is off */
    return !(cn->status & TIOCM_CAR);
}


/* get the copy nes version string, the buffer is allocated using malloc and
   the caller must free it themselves */
ssize_t copynes_get_version(copynes_t cn, void* buf, size_t size)
{
	ssize_t ret = 0;
	struct timeval t = { 1L, 0L };
	
	/* send the get version command */
	if(copynes_write(cn, CMD_GET_VERSION, CMD_SIZE(CMD_GET_VERSION)) != CMD_SIZE(CMD_GET_VERSION))
	{
		cn->err = FAILED_COMMAND_SEND;
        return -cn->err;
	}
	
	if((ret = copynes_read(cn, buf, size, &t)) < 0)
	{
		return ret;
	}
	
	return ret;
}

/* BH - Added Oct 30 2009
 * 	extern funcion to set user variables for running the plugin.
 */
int
copynes_set_uservars(copynes_t cn, uint8_t enabled[4], uint8_t value[4])
{
	int i = 0;
	for (i = 0; i < 4; i++) {
		cn->uservar_enabled[i] = enabled[i];
		cn->uservar_value[i] = value[i];
	}
	return 0;
}


/* BH - Added Oct 30 2009
 *      called from load_plugin to apply the uservars
 *      set by set_uservars when plugin is loaded.
 */
int 
copynes_apply_uservars(copynes_t cn, uint8_t* prg, long prg_size)
{
	typedef struct uservar {
		uint8_t description[14];
		uint8_t enabled;
		uint8_t value;
	} uservar_t;
	struct uservar* usrvar[4];
	int i = 0;

	for (i = 0; i < 4; i++) {
		if (cn->uservar_enabled[i]) {
			/* last 4 uservar sized chunks are for the uservars */
			usrvar[i] = (struct uservar*)&prg[prg_size - (sizeof (struct uservar) * (4 - i))];
			usrvar[i]->enabled = cn->uservar_enabled[i];
			usrvar[i]->value = cn->uservar_value[i];
		}
	}
	return 0;
}

/* load a specified CopyNES plugin, NOTE: plugin must be full path to the .bin */
int copynes_load_plugin(copynes_t cn, const char* plugin)
{
	FILE* f = 0;
	uint8_t* prg = 0;

	/* try to open the plugin file */
	if((f = fopen(plugin, "rb")) == 0)
	{
		cn->err = FAILED_PLUGIN_OPEN;
		return -cn->err;
	}
	
	
	/* seek to the plugin prg data */
	fseek(f, 128, SEEK_SET);
	prg = calloc(KB(1), sizeof(uint8_t));
	
	/* read in the plugin prg data */	
	fread(prg, KB(1), sizeof(uint8_t), f);
	copynes_apply_uservars(cn, prg, KB(1));

	/* send the command to store the plugin prg data at 0400h */
	if(copynes_write(cn, CMD_LOAD_PLUGIN, CMD_SIZE(CMD_LOAD_PLUGIN)) != CMD_SIZE(CMD_LOAD_PLUGIN))
	{
		fclose(f);
		cn->err = FAILED_COMMAND_SEND;
		return -cn->err;
	}

	/* send the data to the CopyNES */
	if(copynes_write(cn, prg, KB(1)) != KB(1))
	{
		free(prg);
		fclose(f);
		cn->err = FAILED_BLOCK_SEND;
		return -cn->err;
	}
	
	/* cleanup */
	fclose(f);
	free(prg);
	
	/* remember which plugin we're running */
	if(cn->current_plugin != 0)
		free(cn->current_plugin);
	cn->current_plugin = strdup(plugin);

	/* wait a bit */
	usleep(USLEEP_SHORT);
	
	return 0;
}


/* run the loaded plugin */
int copynes_run_plugin(copynes_t cn)
{
	/* send the command to execute the code at 0400h */
	if(copynes_write(cn, CMD_RUN_PLUGIN, CMD_SIZE(CMD_RUN_PLUGIN)) != CMD_SIZE(CMD_RUN_PLUGIN))
	{
		cn->err = FAILED_COMMAND_SEND;
		return -cn->err;
	}
	
	/* initialize the reset counters */
	cn->rbyte = 0;
	cn->rcount = 0;
	
	return 0;
}


/* packet reading states */
#define PACKET_START		0
#define PACKET_READ_SIZE_1	1
#define PACKET_READ_SIZE_2	2
#define PACKET_READ_FORMAT 	3
#define PACKET_READ_DATA	4
#define PACKET_DO_RESET		5
#define PACKET_READ_RBYTE_1	6
#define PACKET_READ_RBYTE_2	7
#define PACKET_END			8

ssize_t copynes_read_packet(copynes_t cn, copynes_packet_t *p, struct timeval timeout)
{
	int bytes = 0;
	int i = 0;
	int j = 0;
	int state = PACKET_START;
	uint8_t tmpbyte = 0;
	uint16_t tmpshort = 0;
	copynes_packet_t pkt = 0;
	struct timeval t;
	
	t.tv_sec = timeout.tv_sec;
	t.tv_usec = timeout.tv_usec;
	
	while(state != PACKET_END)
	{
		switch(state)
		{
			case PACKET_START:
			{
				/* allocate the packet struct */
				*p = calloc(1, sizeof(struct copynes_packet_s));
				pkt = *p;
				
				/* move to the next state */
				state = PACKET_READ_SIZE_1;
				
				break;
			}
			
			case PACKET_READ_SIZE_1:
			{
				/* reset timeval struct */
				t.tv_sec = timeout.tv_sec;
				t.tv_usec = timeout.tv_usec;
				/* read in the least significant byte */
				if(copynes_read(cn, &((uint8_t*)&tmpshort)[1], sizeof(uint8_t), &t) != sizeof(uint8_t))
				{
					cn->err = FAILED_DATA_READ;
					return -cn->err;
				}
				
				/* move to the next state */
				state = PACKET_READ_SIZE_2;
				
				break;
			}
				
			case PACKET_READ_SIZE_2:
			{
				/* reset timeval struct */
				t.tv_sec = timeout.tv_sec;
				t.tv_usec = timeout.tv_usec;

				/* read in the most significant byte */
				if(copynes_read(cn, &((uint8_t*)&tmpshort)[0], sizeof(uint8_t), &t) != sizeof(uint8_t))
				{
					cn->err = FAILED_DATA_READ;
					return -cn->err;
				}
				
				/* the size is now stored in big endian order--network order--
				   so we need to convert it to the platform order using ntohs */
				pkt->blocks = ntohs(tmpshort);
	
				/* convert from number of 256 byte blocks to the number of bytes */
				pkt->size = (pkt->blocks << 8);
				
				/* move to the next state */
				state = PACKET_READ_FORMAT;
				break;
			}
		
			case PACKET_READ_FORMAT:
			{
				/* reset timeval struct */
				t.tv_sec = timeout.tv_sec;
				t.tv_usec = timeout.tv_usec;
				
				/* read in the packet format */
				if(copynes_read(cn, &tmpbyte, sizeof(uint8_t), &t) != sizeof(uint8_t))
				{
					cn->err = FAILED_DATA_READ;
					return -cn->err;
				}
				
				/* store the packet type */
				pkt->type = tmpbyte;
				
				/* figure out where to go from here */
				switch(pkt->type)
				{
					case PACKET_PRG_ROM:
					case PACKET_CHR_ROM:
					case PACKET_WRAM:
					{
						if(pkt->size > 0)
						{
							/* allocate a buffer for the data */
							pkt->data = calloc(pkt->size, sizeof(uint8_t));
				
							/* move to the next state */
							state = PACKET_READ_DATA;
							
							/* set up the indexes */
							i = 0;
							j = 0;
						}
						else
						{
							/* move to the end state */
							state = PACKET_END;
						}
						
						break;
					}
					case PACKET_RESET:
					{
						/* intialize the rbyte */
						cn->rbyte = pkt->blocks / 4;
						
						/* fall through */
					}
					case PACKET_EOD:
					{
						/* move to the end state */
						state = PACKET_END;
						
						break;
					}
				}
				
				break;
			}
			
			case PACKET_READ_DATA:
			{
				/* reset timeval struct */
				t.tv_sec = timeout.tv_sec;
				t.tv_usec = timeout.tv_usec;
				
				/* read the bytes */
				bytes = 0;
				if(i < pkt->size)
				{
					if(j < KB(1))
					{
						/* read the remaining data up to 1K */
						bytes = copynes_read(cn, &pkt->data[i + j], (KB(1) - j), &t);
					
						/* track how many bytes we've read */
						j += bytes;
					}
					
					/* if we've finished reading the 1K of data... */
					if(j >= KB(1))
					{
						/* update total of how much we've read */
						i += j;
						
						/* reset 1K counter */
						j = 0;
						
						/* check to see if we need to reset the NES */
						if(cn->rbyte)
						{
							state = PACKET_DO_RESET;
						}
					}
				}
				else
				{
					/* move to the next state */
					state = PACKET_END;
				}
				break;
			}
			
			case PACKET_DO_RESET:
			{
				if(cn->rbyte)
				{
					cn->rcount++;
					if(cn->rbyte <= cn->rcount)
					{
						/* reset the NES */
						copynes_reset(cn, RESET_COPYMODE);

						/* reload the plugin */
						copynes_load_plugin (cn, cn->current_plugin);

						/* rerun the plugin. NOTE: this will reset rbyte and rcount */
						copynes_run_plugin (cn);
						usleep(USLEEP_LONG);
						
						/* move to the next state */
						state = PACKET_READ_RBYTE_1;
					}
				}
				else
				{
					/* make sure we don't get stuck here */
					state = PACKET_READ_DATA;
				}
				
				break;
			}
				
			case PACKET_READ_RBYTE_1:
			{
				/* reset tmpshort */
				tmpshort = 0;
				
				/* reset timeval struct */
				t.tv_sec = timeout.tv_sec;
				t.tv_usec = timeout.tv_usec;
				
				/* read in the least significant byte */
				if(copynes_read(cn, &((uint8_t*)&tmpshort)[1], sizeof(uint8_t), &t) != sizeof(uint8_t))
				{
					cn->err = FAILED_DATA_READ;
					return -cn->err;
				}
				
				/* move to the next state */
				state = PACKET_READ_RBYTE_2;
				
				break;
			}
				
			case PACKET_READ_RBYTE_2:
			{
				/* reset timeval struct */
				t.tv_sec = timeout.tv_sec;
				t.tv_usec = timeout.tv_usec;

				/* read in the most significant byte */
				if(copynes_read(cn, &((uint8_t*)&tmpshort)[0], sizeof(uint8_t), &t) != sizeof(uint8_t))
				{
					cn->err = FAILED_DATA_READ;
					return -cn->err;
				}
				
				/* the size is now stored in big endian order--network order--
				   so we need to convert it to the platform order using ntohs */
				cn->rbyte = ntohs(tmpshort) / 4;
				
				/* go back to the read data state */
				state = PACKET_READ_DATA;
				
				break;
			}
		}
	}
	
	return (ssize_t)i;
}

/* get the error string associated with the error */
char* copynes_error_string(copynes_t cn)
{
    return errors[cn->err];
}


/*
 * Private helper functions
 */

/* get the current status bits of the control channel */
static void copynes_get_status(copynes_t cn)
{
    /* get the status bits on the control port */
    ioctl(cn->control, TIOCMGET, &cn->status);
}


/* set the current status bits of the control channel */
static void copynes_set_status(copynes_t cn)
{
    /* set the status bits on the control port */
    ioctl(cn->control, TIOCMSET, &cn->status);
}


/*
 * NOTE: getting the serial driver configured correctly was a little tricky to
 * figure out but thanks to the awesome Serial Programming Guide for POSIX
 * Operating Systems <http://www.easysw.com/~mike/serial/serial.html> by 
 * Michael R. Sweet, I was able to figure it out.
 */
static void copynes_configure_tios(struct termios * tios)
{
	/* set up 115.2kB baud rate */
	cfsetispeed(tios, B115200);
    cfsetospeed(tios, B115200);
	
	/* enable receiver, make local */
	tios->c_cflag |= (CLOCAL | CREAD);
	
	/* set 8N1: 8-bit width, no parity, and one stop bit */
	tios->c_cflag &= ~PARENB;
	tios->c_cflag &= ~CSTOPB;
	tios->c_cflag &= ~CSIZE;
	tios->c_cflag |= CS8;
		
	/* enable hardware flow control */
	tios->c_cflag |= CRTSCTS;

	/* set up for raw input */
	tios->c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG);
	
	/* 
	 * turn off all parity checking, marking, and stripping...also turn off
	 * all software flow control and all of the bullshit mapping of CR's to LF's
	 * and LF's to CR's...blah..who thought it would be this hard to receive
	 * 8-bit binary data over a serial port?!?  I beat my head against this one 
	 * for about a week before figuring out the correct settings for this flag!
	 */
	tios->c_iflag &= ~(INPCK | IGNPAR | PARMRK | ISTRIP | IXON | IXOFF | IXANY | ICRNL | INLCR | IUCLC | BRKINT);
	
	/* set up raw output */
	tios->c_oflag &= ~OPOST;
}

static void copynes_configure_devices(copynes_t cn)
{
	struct termios dataios;
	struct termios controlios;
	
	/* save the current termios settings for the two devices */
	tcgetattr(cn->data, &cn->old_tios_data_device);
	tcgetattr(cn->control, &cn->old_tios_control_device);
	
	/* get the current data channel settings */
	bzero(&dataios, sizeof(dataios));
	tcgetattr(cn->data, &dataios);
	
	/* configure the data channel tios */
	copynes_configure_tios(&dataios);
		
	/* set the new settings for the data device */
	tcsetattr(cn->data, TCSAFLUSH, &dataios);
	
	/* get the current control channel settings */
	bzero(&controlios, sizeof(controlios));
	tcgetattr(cn->control, &controlios);
	
	/* configure the data channel tios */
	copynes_configure_tios(&controlios);
	
	/* set the new settings for the control device */
	tcsetattr(cn->control, TCSAFLUSH, &controlios);
}

#if 0
int copynes_dump(copynes_t cn)
{
	int ret, total;
	struct timeval t = { 1L, 0L };
	uint8_t buf[1024];
	
	/* open the dump file */
	FILE* dump = fopen("./nesdump.bin", "w+b");
	
	total = 0;
	while(1)
	{
		t.tv_sec = 1;
		
		/* zero out the buffer */
		bzero(buf, 1024);

		ret = copynes_read(cn, buf, 1024, &t);
		
		if(ret > 0)
		{
			/* write to the dump file */
			fwrite(buf, sizeof(uint8_t), ret, dump);
				
			/* record the total */
			total += ret;
		}
		else
		{
			printf("dump complete, read %d bytes\n", total);
			break;
		}
	}
	
	fclose(dump);
	
	return 0;
}
#endif

