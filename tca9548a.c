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


// I2C address of tca9548a multiplexer
static int address;

// initialise i2c bus for tca9548a multiplexer(s)
int tca9548a_init_i2c (uint8_t bus)
{
    for (int j=0; j<MAXMULTIPLEXERS; j++)
    {
        address = multiplexer[j].address;
        if (address)
        {
            multiplexer[j].i2cbus=-1;
            multiplexer[j].i2cbus=i2c_init(bus, address);
            if (multiplexer[j].i2cbus == -1) 
                return -1;
        }
        else   
            break;
    }
    return 0;
}

int tca9548a_select_port (uint8_t index, uint8_t port)
{
    int i2cbus = multiplexer[index].i2cbus;
	
    char buf[1];										
	sprintf (buf, "%d", 1 << port);
	if ((write(i2cbus, buf, 1)) != 1) {
		return -1;
	}
	return 0;    

}
