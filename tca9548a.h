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

#ifndef __TCA9548A_H_FMBERRY_
#define __TCA9548A_H_FMBERRY_

#include <stdint.h>

int tca9548a_init_i2c (uint8_t bus);
int tca9548a_select_port (uint8_t index, uint8_t port);

#endif
