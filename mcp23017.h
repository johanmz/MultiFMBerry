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

#ifndef __MCP23017_H_FMBERRY_
#define __MCP23017_H_FMBERRY_

#include <stdint.h>

int mcp23017_init_i2c (uint8_t bus); // initialise i2c bus for IOexpander(s) used to read the RDS_INT signal from the transmitters
int mcp23017_init_INT (); // set the mccp23017 interrupt register in mirror mode 
uint16_t mcp23017_read_trs_rdsstatus(int index); // read which transmitter(s) have RDS_INT set, i.e. are ready to receive the next RDS block

#endif