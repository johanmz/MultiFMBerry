/*
	FMBerry - an cheap and easy way of transmitting music with your Pi.
    Copyright (C) 2011-2013 by Tobias Mädel (t.maedel@alfeld.de)
	Copyright (C) 2013      by Andrey Chilikin (achilikin@gmail.com)
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef __FMBERRYD_H_FMBERRY_
#define __FMBERRYD_H_FMBERRY_

#include <stdint.h>

typedef struct _mmr70_data
{
	int frequency;
	int power;
	int mute;
	int gain;
	int volume;
	int txpower;
	int stereo;
	int rds;
	char rdsid[10];   // max length of id is 8, but need spare null for printing
	char rdstext[66]; // same here, max 64 chars, 2 nulls are extra (need only 1, but I do not like odd numbers)
	int rdspi;
	int rdspty;
	int i2c_mplexaddress;
	int i2c_mplexport;
	int i2c_mplexindex; 
	char IOexpanderconfig[12];
	int IOexpanderport;
	int IOexpanderindex;
}mmr70_data_t;
extern mmr70_data_t mmr70[];

typedef struct _IOexpander_data
{
	char id[12];
	int address;
	int interruptpin;
	int i2cbus;
	uint16_t GPINTEN;
}IOexpander_data_t;
extern IOexpander_data_t IOexpander[];

typedef struct _multiplexer_data
{
	int  address;
	int  i2cbus;
}multiplexer_data_t;
extern multiplexer_data_t multiplexer[];


int main(int argc, char **argv);

// true if string is equal to the second argument
int str_is(const char *str, const char *is);
int str_is_arg(const char *str, const char *is, const char **arg);

int ListenTCP(uint16_t port);
int ProcessTCP(int sock, mmr70_data_t *pdata);
#endif
