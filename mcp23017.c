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
#include "i2c.h"
#include "defs.h"
#include "fmberryd.h"

static int i2cbus = -1;
static int address;

// initialise i2c bus for IOexpander(s) used to read the RDS_INT signal from the transmitters
// mcp23017 is by default already in BANK=0 mode and GPIOA and B in input mode
int mcp23017_init_i2c (uint8_t bus)
{
    for (int j=0; j<MAXIOEXPANDERS; j++)
    {
        address = IOexpander[j].address;
        if (address)
        {
            IOexpander[j].i2cbus=i2c_init(bus, address);
            if (IOexpander[j].i2cbus == -1) 
                return -1;
        }
        else   
            break;
    }
    return 0;
}

// set  the mccp23017 interrupt register in mirror mode and enable the interrupts for the ports with a MMR70
int mcp23017_init_INT ()
{
 
     // build up the GPINTEN register for the ports with a MMR70 connected, for each multiplexere
    int IOexpanderport;
    uint16_t GPINTEN=0;
    for (int k=0; k<MAXTRANSMITTERS; k++)
    {
        if (mmr70[k].frequency)
        {
            IOexpanderport=mmr70[k].IOexpanderport;
            GPINTEN = IOexpander[mmr70[k].IOexpanderindex].GPINTEN;
            GPINTEN |= 1 << IOexpanderport;
            IOexpander[mmr70[k].IOexpanderindex].GPINTEN= GPINTEN;
        }
        else
           break;
    }
 
    // set in mirror mode and enable the interrupts
	char buf[3];
    for (int j=0; j<MAXIOEXPANDERS; j++)
    {
        if (IOexpander[j].address)
        {
            if (i2c_send (IOexpander[j].i2cbus, 0x0a, 0b01000000)==-1)
                return -1;
            GPINTEN = IOexpander[j].GPINTEN;
    	    buf[0] = 0x04;	// register for GPINTENA, since mcp23017 is in mirror mode the next register is for GPINTENB, we can write both GPINTEN for A en B together				
	        buf[1] = GPINTEN & 0xFF;
	        buf[2] = (GPINTEN & 0xFF00) >> 8;
    	    if ((write(IOexpander[j].i2cbus, buf, 3)) != 3) {
	    	    return -1;
	        }   
        }
        else
            break;
    }
    return 0;
}



uint16_t mcp23017_read_trs_rdsstatus (int index)
{
    char buf[2];

    i2cbus = IOexpander[index].i2cbus;
    buf[0]=0x12; // pointer to GPIOA register
									
	// set mcp20137 pointer to GPIOA
    if ((write(i2cbus, buf, 1)) != 1)
		return -1;

    // and read the values of both GPIOA and GPIOB
    if (read(i2cbus, buf, 2) != 2)
        return -1;
    else
        return buf;

}
  
