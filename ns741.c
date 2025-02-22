/*
	FMBerry - an cheap and easy way of transmitting music with your Pi.

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
#include <stdio.h>
#include <syslog.h>

#include "rds.h"
#include "ns741.h"
#include "i2c.h"
#include "tca9548a.h"
#include "fmberryd.h"


// I2C address of MMR-70
static const int address = 0x66;
static int i2cbus[MAXNRTRANSMITTERS];

#define NS741_DEFAULT_F 99800 // default F=99.80MHz

// Calculates value of P for 0A/0B registers
#define NS741_FREQ(F) ((uint16_t)((uint32_t)F*1000ULL/8192ULL))

// Program Service name
static char ps_name[MAXNRTRANSMITTERS][8];
// Radiotext message, by default an empty line
static uint8_t text_len[MAXNRTRANSMITTERS] ;
//                          "----------------------------64----------------------------------"
static char radiotext[MAXNRTRANSMITTERS][64];
// = "\r                                                               ";
// *hrr hrr*                "Twilight Sparkle is best Pony.                                  "

uint8_t rds_register[4] = {0x03, 0x05, 0x05, 0x05};

static uint8_t group_index[MAXNRTRANSMITTERS];// = 0; // group to be transmitted
static uint8_t block_index[MAXNRTRANSMITTERS];// = 0; // block index within the group

// RDS 2A Group containing Radiotext chars
// It is better not to use CC of your country
// to avoid collision with any local radio stations
static uint16_t rds_text[MAXNRTRANSMITTERS][4] ;

// RDS 0B Group containing Program Service name
// It is better not to use CC of your country
// to avoid collision with any local radio stations
static uint16_t rds_ps[MAXNRTRANSMITTERS][4];

#define RDS_MAX_GROUPS 20 // 4 groups - Program Service name, up to 16 - Radiotext
#define RDS_MAX_BLOCKS (RDS_MAX_GROUPS << 2)

// set to 0 to print groups
static uint8_t rds_debug = RDS_MAX_BLOCKS;
static uint8_t rds_debug_max = RDS_MAX_BLOCKS;

// NS741 registers map, some change applied to 
// recommended values from TSMZ1-603 spec
static uint8_t ns741_reg[MAXNRTRANSMITTERS][22]=
{		// default map for all 22 RW registers
		0x02, // 00h: Power OFF, Crystal ON
		0xE1, // 01h: PE switch ON, PE selection 50us (Europe), Subcarrier ON, Pilot Level 1.6
		0x0A, // 02h: RFG 0.5 mW, Mute ON
		0x00, 0x00, 0x00, 0x00, // 03h-06h: RDS registers
		0x7E, 0x0E, // 07h-08h: recommended default
		0x08, // 09h: recommended default
		(NS741_FREQ(NS741_DEFAULT_F) & 0xFF), // 0Ah-0Bh, frequency
		((NS741_FREQ(NS741_DEFAULT_F) & 0xFF00) >> 8),
		0x0C, // 0Ch: recommended default
		0xE0, // 0Dh: ALC (Auto Level Control) OFF, AG (Audio Gain) -9dB
		0x30, // 0Eh: recommended default
		0x40, // 0Fh: input Audio Gain -9dB
		0xA0, // 10h: RDS with checkword, RDS disabled
		0xE4, // 11h: recommended default
		0x00, // 12h: recommended default
		0x42, // 13h: recommended default
		0xC0, // 14h: recommended default
		0x41, // 15h: recommended default
		0xF4  // 16h: recommended default
};

// set the N741 register map for every transmitter. Copy the values from transmitter 0, set above
void ns741_init_registers(uint8_t nr_transmitters)
{
	for (int j = 1; j < nr_transmitters;j++) {
		for (int k = 0; k< sizeof(ns741_reg[1]); k++)
			ns741_reg[j][k] = ns741_reg[0][k];
		
		// increase the default frequency of every next transmitter by 200 Khz
		ns741_reg[j][0x0A] = (NS741_FREQ(NS741_DEFAULT_F + j*200) & 0xFF);
		ns741_reg[j][0x0B] = ((NS741_FREQ(NS741_DEFAULT_F + j*200) & 0xFF00) >> 8);
	}
}

void ns741_init_rds_registers(uint8_t transmitter)
{
	group_index[transmitter]=0;
	block_index[transmitter]=0;
	text_len[transmitter]=1;

	// set defaults for RDS 0B Group containing Program Service name and RDS 2A Group containing Radiotext chars
	rds_ps[transmitter][2] = 0xE0CD; // E0 - No AF exists, CD - Filler code, see Table 11 of RBDS Standard
	rds_ps[transmitter][3] = 0;
	rds_text[transmitter][2] = 0;
	rds_text[transmitter][3] = 0;
}

// initialise i2c bus for the transmitters
int ns741_init_i2c (uint8_t bus, uint8_t nr_transmitters)
{
	int index;
	int port;

	// open the i2cbus for the transmitters, 0x66 
	for (int k=0;k<nr_transmitters;k++)
	{
		index = mmr70[k].i2c_mplexindex;
		port = mmr70[k].i2c_mplexport;
		if (tca9548a_select_port(index, port) == -1)
			return -1;
		
		i2cbus[k]=-1;
		i2cbus[k] = i2c_init(bus, address);
		if ((i2cbus[k] == -1) || (i2c_send(i2cbus[k], 0x00, 0x00) == -1))
		{
			syslog(LOG_ERR, "Init of transmitter %s failed! Double-check hardware and .conf and try again!\n", mmr70[k].name);
			return -1;
		}
		// reset register of first transmitter to default values
		if (i2c_send_data(i2cbus[k], 0, ns741_reg[k], sizeof(ns741_reg[k])) == -1)
		{	
			syslog(LOG_ERR, "Cannot send data to transmitter %s! Double-check hardware and .conf and try again!\n", mmr70[k].name);
			return -1;
		}
	}
	return 0;
}

// register 0x00 controls power and oscillator:
//	bit 0 - power
//	bit 1 - oscillator
void ns741_power(uint8_t transmitter, uint8_t on)
{
	uint8_t reg = 0x02; // oscillator is active
	if (on)
		reg |= 0x01; // power is active

	i2c_send(i2cbus[transmitter], 0x00, reg);
	ns741_reg[transmitter][0] = reg;
	return;
}

// register 0x01 controls stereo subcarrier and pilot: 
//  bit 0: pre-emphasis on = 1
//  bit 1: pre-emphasis selection: 0 - 50us, 1 - 75us
//	bit 4: set to 1 to turn off subcarrier
//	bit 7: set to 0 to turn off pilot tone
// default: 0x81
void ns741_stereo(uint8_t transmitter, uint8_t on)
{
	uint8_t reg = ns741_reg[transmitter][1];
	if (on) {
		reg &= ~0x10; // enable subcarrier
		reg |= 0xE0;  // enable pilot tone at 1.6 level
	}
	else {
		reg |= 0x10;  // disable subcarrier
		reg &= ~0xE0; // disable pilot tone
	}
	
	ns741_reg[transmitter][1] = reg;
	i2c_send(i2cbus[transmitter], 0x01, reg);
	return;
}

// register 0x02 controls output power and mute: 
//	RF output power 0-3 (bits 7-6) corresponding to 0.5, 0.8, 1.0 and 2.0 mW
//	reserved bits 1 & 3 are set
//	mute is off (bit 0)
// Set mute will turn off RDS as well as Pilot tone disabled
// default: 0x0A - 0.5mW + Mute ON
void ns741_mute(uint8_t transmitter, uint8_t on)
{
	uint8_t reg = ns741_reg[transmitter][2];
	if (on)
		reg |= 1;
	else
		reg &= ~1;

	i2c_send(i2cbus[transmitter], 0x02, reg);
	ns741_reg[transmitter][2] = reg;
	return;
}

void ns741_txpwr(uint8_t transmitter, uint8_t strength)
{
	uint8_t reg = ns741_reg[transmitter][2];
	// clear RF power bits: set power level 0 - 0.5mW
	reg &= ~0xC0;
	strength &= 0x03; // just in case normalize strength
	reg |= (strength << 6);

	i2c_send(i2cbus[transmitter], 0x02, reg);
	ns741_reg[transmitter][2] = reg;
	return;
}

// f_khz/10, for example 95000 for 95000KHz or 95MHz
void ns741_set_frequency(uint8_t transmitter, uint32_t f_khz)
{
	/* calculate frequency in 8.192kHz steps*/
	uint16_t val = NS741_FREQ(f_khz);

	// it is recommended to mute transmitter before changing frequency
	uint8_t reg = ns741_reg[transmitter][2];
	i2c_send(i2cbus[transmitter], 0x02, reg | 0x01);

	i2c_send(i2cbus[transmitter], 0x0A, val);
	i2c_send(i2cbus[transmitter], 0x0B, val >> 8);

	// restore previous mute state
	i2c_send(i2cbus[transmitter], 0x02, reg);
	return;
}

// output gain 0-6, or -9dB to +9db, 3dB step
void ns741_volume(uint8_t transmitter, uint8_t gain)
{
	uint8_t reg = ns741_reg[transmitter][0x0D];
	if (gain > 6)
		gain = 6;
	reg &= ~0x0E;
	reg |= gain << 1;
	ns741_reg[transmitter][0x0D] = reg;

	i2c_send(i2cbus[transmitter], 0x0D, reg);
}

// set input gain -9dB on/off
void ns741_input_gain(uint8_t transmitter, uint8_t on)
{
	uint8_t reg = ns741_reg[transmitter][0x0F];

	if (on)
		reg |= 0x40;
	else
		reg &= ~0x40;
	ns741_reg[transmitter][0x0F] = reg;
	i2c_send(i2cbus[transmitter], 0x0F, reg);
}

// register 0x10 controls RDS:
//	  reserved bits 0-5, with bit 5 set
//	  bit 6: 0 - RDS off, 1 - RDS on
//    bit 7: RDS data format, 1 - with checkword
// default: 0xA0
void ns741_rds(uint8_t transmitter, uint8_t on)
{
	uint8_t reg = ns741_reg[transmitter][0x10];
	if (on)
		reg |= 0x40;
	else
		reg &= ~0x40;

	ns741_reg[transmitter][0x10] = reg;
	i2c_send(i2cbus[transmitter], 0x10, reg);
	return;
}

// RDS_CP flag, third block type: C (cp=0) or C' (cp=1)
void ns741_rds_cp(uint8_t transmitter, uint8_t cp)
{		
	uint8_t reg = ns741_reg[transmitter][0x0F];
	if (cp)
		reg |=0x80;
	else
		reg &= 0x7F;
	ns741_reg[transmitter][0x0F] = reg;
	i2c_send(i2cbus[transmitter], 0x0F, reg);
	return;
}

void ns741_rds_set_radiotext(uint8_t transmitter, const char *text)
{
	uint8_t i;

	// first copy text to our buffer
	for (i = 0; *text && (i < 64); i++, text++)
		radiotext[transmitter][i] = *text;
	text_len[transmitter] = i;
	if (i < 64) {
		text_len[transmitter]++;
		radiotext[transmitter][i++] = '\r';
		// pad buffer with spaces
		for (; i < 64; i++)
			radiotext[transmitter][i] = ' ';
	}
}

void ns741_rds_reset_radiotext(uint8_t transmitter)
{
	rds_text[transmitter][1] ^= RDS_AB;
}

void ns741_rds_set_rds_pi(uint8_t transmitter, uint16_t rdspi)
{
	// rdspi = RDS_PI(RDS_RUSSIA,CAC_LOCAL,0),
	rds_ps[transmitter][0] = rdspi;
	rds_text[transmitter][0] = rdspi;
}

void ns741_rds_set_rds_pty(uint8_t transmitter, uint8_t rdspty)
{
	rds_ps[transmitter][1] = RDS_GT(0,0) | RDS_PTY(rdspty) | RDS_MS;
	rds_text[transmitter][1] = RDS_GT(2,0) | RDS_PTY(rdspty) | RDS_MS;
}

void ns741_rds_debug(uint8_t on)
{
	rds_debug = on ? 0x80 : RDS_MAX_BLOCKS;
}

// text - up to 8 characters
// shorter text will be padded with spaces for transmission
// so exactly 8 chars will be transmitted
void ns741_rds_set_progname(uint8_t transmitter, const char *text)
{
	uint8_t i;
	// first copy text to our buffer
	for (i = 0; *text && (i < 8); i++, text++)
		ps_name[transmitter][i] = *text;
	// pad buffer with spaces
	for (; i < 8; i++)
		ps_name[transmitter][i] = ' ';
}

// in total we can send 20 groups:
// 4 groups with Program Service Name and 16 with Radiotext
uint8_t ns741_rds_isr(uint8_t transmitter)
{
	uint8_t *data;
	static uint16_t *block[MAXNRTRANSMITTERS];

	if (block_index[transmitter] == 0) {
		if (group_index[transmitter] > 3) {
			uint8_t i = (group_index[transmitter] - 4) << 2;
			if (i < text_len[transmitter]) {
				block[transmitter] = rds_text[transmitter];
				block[transmitter][1] &= ~RDS_RTIM;
				block[transmitter][1] |= group_index[transmitter] - 4;
				data = (uint8_t *)&block[transmitter][2];
				data[1] = radiotext[transmitter][i];
				data[0] = radiotext[transmitter][i+1];
				data[3] = radiotext[transmitter][i+2];
				data[2] = radiotext[transmitter][i+3];
			}
			else {
				rds_debug_max = group_index[transmitter] << 2;
				group_index[transmitter] = 0; // switch back to PS
			}
		}

		if (group_index[transmitter] < 4) {
			if ((group_index[transmitter] == 0) && (rds_debug & 0x80))
				rds_debug = 0;
			block[transmitter] = rds_ps[transmitter];
			block[transmitter][1] &= ~RDS_PSIM;
			block[transmitter][1] |= group_index[transmitter];
			data = (uint8_t *)&block[transmitter][3];
			uint8_t i = group_index[transmitter] << 1; // 0,2,4,6
			data[1] = ps_name[transmitter][i];
			data[0] = ps_name[transmitter][i+1];
		}
		// uncomment following line if Group type B is used
		// ns741_rds_cp((block[1] & 0x0800) >> 8);
	}

	if (rds_debug < rds_debug_max) {
		data = (uint8_t *)&block[transmitter][block_index[transmitter]];
		printf("T%02d G%02d B%02d  %02X%02X\n", transmitter, group_index[transmitter],  block_index[transmitter], data[1], data[0]);
		rds_debug++;
	}

	i2c_send_word(i2cbus[transmitter], rds_register[block_index[transmitter]], (uint8_t *)&block[transmitter][block_index[transmitter]]);
	block_index[transmitter] = (block_index[transmitter] + 1) & 0x03;
	if (!block_index[transmitter])
		group_index[transmitter] = (group_index[transmitter] + 1) % RDS_MAX_GROUPS;
	return group_index[transmitter];
}