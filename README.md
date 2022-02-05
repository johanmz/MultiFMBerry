![FMBerry Logo](http://tbspace.de/holz/uzsjpoghdq.png)
MultiFMBerry
=======
> Originally written by Tobias Mädel (t.maedel@alfeld.de, http://tbspace.de). Modified by Johan Muizelaar (https://github.com/johanmz) to control multiple transmitters.

> 

What is this? 
-------------
This is a fork of FMBerry from Tobias Mädel, a piece of software that allows you to transmit FM radio with your Raspberry Pi. The original software from Tobias allows you to control one transmitter, MultiFMBerry allows you to control multiple transmitters. The software is tested with 8 transmitters using a Raspberry Pi 3B but more should also be possible.

The goal was as an alternative for  analog cable FM after the switch-off, but even more just as a fun project.


How does it work? 
-------------
It uses Sony-Ericsson MMR-70 transmitters, which was originally intended for use with Sonys Walkman cellphones from early 2000s.
You can get these still for cheap from Ebay in 2022.

Why does it need additional IC's apart from the MRR70's?
-------------

Since all transmitters  use the same I2C address (0x66), one ore more multiplexers (TCA9548A) are used to switch the transmitters before sending commands. Each TCA9548A can handle 8 transmitters. One or more IO expanders (MCP23017) are used to read the RDS interrupt signals, this is the signal that the transmitter needs the next RDS block. Each MCP23017 can handle up to 16 transmitters. 


What do I need to build this? 
-------------
* MMR-70 transmitter, two or more. If you want to control only one transmitter, it makes more sense to use the software and hardware setup from Tobias since it doesn't need the additional IC's.
* Raspberry Pi (tested with 3B and 4 but see the limitations of the Pi4 in AUDIO.md before you start!)
* TCA9548A, MCP23017 and AMS1117 breakout board(s) from AliExpress or Ebay
* Soldering equipment (soldering iron and some solder)
* USB sound card for each transmitter, the cheap "3D Sound" USB sound sticks from AliExpress work fine.
* One or more USB hubs if you have more then 4 transmitters. You need a hub with Multiple Transaction Translators (MTT). The BIG7 from UUgear and the Waveshare USB3.2 HAT (5 port) work fine and have the same form factor as the Pi as a bonus. See also AUDIO.md.


The hardware is explained here:
[HARDWARE.md](https://github.com/Manawyrm/FMBerry/blob/master/HARDWARE.md#fmberry---hardware)

How many transmitters and audio streams can one Pi control?
------------
It depends:

* The Raspberry Pi 4 has a limit of 4 USB sound cards due to limitations in the USB bus. It should be possible to add more sound cards using a USB C power delivery hub connected to the OTG (power) connector, I haven't tried this myself. See AUDIO.md. On the Raspberry Pi 3B, you can use much more USB sound cards with a suitable hub, see below.
* If you want to use more then 4 USB sound cards (on a Raspberry Pi 3 since the Pi 4 has a limit of 4 cards, see above), you obviously need one ore more USB hubs. Make sure that the hub has Multiple Transaction Translators and not Single Transaction Translators, otherwise your sound will break up with approx 3 or more streams. Unfortunately this is not always specified. I've succesfully used the BIG7 from UUGear and USB 3.2 HAT from Waveshare.
* The Raspberry Pi OS Linux kernel has a limit of 8 sound cards. You can increase this limit and compile your own kernel, this is not as hard as might seem. See https://www.raspberrypi.com/documentation/computers/linux_kernel.html. Set ``CONFIG_SND_DYNAMIC_MINORS=Y`` and ``CONFIG_MAX_CARDS=32``. 
* Each audiostream uses approx 15% CPU on the Pi 3B. The 3B has 4 cores which should be enough for 20 or so transmitters but I haven't confirmed this yet. FMBerry itself hardly uses CPU.
* For RDS, the Pi needs to send each 21,5ms data to each transmitter over the I2C bus. Not only the data needs to be send but also the I2C commands to switch the multiplexer and read the IO expander. With many transmitters, this might fully occupy the I2C bus. For more capacity, you can increase the speed of the I2C bus from 100Khz to 400Khz (see steps below) or disable RDS for some tranmitters in the .conf file.
* The software itself has no practical limit for the number of transmitters. You need one TCA9548A per 8 transmitters and one MCP23017 per 16 transmitters. If you want to use more the 4 TCA9548A or MCP23017 IC's, increase the max in defs.h before compiling the software.

This software has been tested succesfully with 8 transmitters on a Raspbery Pi 3B with 8 USB sound cards using a BIG7 UUgear USB hub, I plan to test with more transmitters.

Installation
-------------

### Step 1: Enabling I²C

Open raspi-blacklist.conf:

``sudo nano /etc/modprobe.d/raspi-blacklist.conf``

Comment out the Line "``blacklist i2c-bcm2708``" with a #.
Save with Ctrl+O and close nano with Ctrl+X

To make sure I²C Support is loaded at boottime open /etc/modules.

``sudo nano /etc/modules``

Add the following lines:

``i2c-dev``

Then again, Save with Ctrl+O and then close nano with Ctrl+X.

Please reboot your Raspberry after this step. 

### Step 2: Installing I²C tools and dependencies for the build

First update your local package repository with
``sudo apt-get update``

then install all needed software with the following command:
``sudo apt-get install i2c-tools build-essential git libconfuse-dev``
 
### Step 3: Checking the hardware

You can check your wiring with the following command:

``i2cdetect -y 1``

You should then see the TCA9548A on port 0x70 and the MCP23017 on port 0x20. Check each transmitter with

``i2cset 1 0x70 port``

``i2cdetect -y 1``

Where port is 1 (0b0000001), 2 (0b0000010), 4 (0b0000100) etc, depending on the number of transmitters and to which ports you connected the transmitters. Now also you should see the transmitter at 0x66.

If you are not able to see your transmitter please double check your wiring!

If you connect you MMR-70 to I²C bus 0 on Raspberry Pi rev2 make sure that header P5 pins are configured as [I²C pins](http://www.raspberrypi.org/phpBB3/viewtopic.php?p=355638#p355638)!

![Output of i2cdetect](http://tbspace.de/holz/csuqzygpwb.png)

### Step 4: Speeding up the I2C bus

For RDS, every transmitter needs to receive data each 21,5ms over the I2C bus. Also, the I2C messages for the multiplexer and IO expander need to be send and received.  With more then 10 transmitters or so, the I2C bus speed should be increased to 400 KHz. 
```
sudo nano /boot/config.txt
```
Find the line ``dtparam=i2c`` and change it to ``dtparam=i2c_arm=on,i2c_arm_baudrate=400000``. Save and reboot.

### Step 5: Building the software
To build the software execute the following commands (in your homefolder):

```
git clone https://github.com/johanmz/FMBerry.git
cd FMBerry
```

``make``

Compiling the software will take a couple of seconds.

### Step 6: Specify your hardware in the config file
FMBerry needs to know about the MCP23017 IO expander, TCA9548A multiplexer, how many tranmitters you have and to which TCA9548A and MCP23017 port each transmitter is connected. Specify this in the .conf file. 

In this file you can also specify the frequency, RDS information, etc for each transmitter.

If you want to modify the file after the make/compile, edit the fmberry.conf file in /etc.


### Step 7: Installing the software
FMBerry is essentially a daemon called fmberryd.
To install it into your system path type 
```sudo make install```. 

You can start it by typing ``sudo /etc/init.d/fmberry start``.

To control the daemon you have to use ctlfmberry.

It currently allows the following commands:
* ``ctlfmberry <tr> set freq 99000`` - Frequency in kHz (76000 - 108000)
* ``ctlfmberry <tr> poweron``
* ``ctlfmberry <tr> poweroff``
* ``ctlfmberry <tr> set rdsid DEADBEEF`` (8 chars! Longer strings will be truncated, shorter - padded with spaces)
* ``ctlfmberry <tr> set rdstext Mike Oldfield - Pictures in the Dark`` (max. 64 chars. Longer strings will be truncated)
* `` ctlfmberry <tr> set rdspi 0x7000`` - RDS PI between 0x0000 and 0xFFF`` Avoid locally used PI codes
* `` ctlfmberry <tr> set rdspty 10`` - RDS program type between 0 and 31
* ``ctlfmberry <tr> set txpwr 0`` - 0.5 mW Outputpower
* ``ctlfmberry <tr> set txpwr 1`` - 0.8 mW Outputpower
* ``ctlfmberry <tr> set txpwr 2`` - 1.0 mW Outputpower
* ``ctlfmberry <tr> set txpwr 3`` - 2.0 mW Outputpower (Default.)
* ``ctlfmberry <tr> stereo on`` - Enables stereo signal (Default)
* ``ctlfmberry <tr> stereo off`` - Disables stereo signal
* ``ctlfmberry <tr> muteon`` - Mute audio
* ``ctlfmberry <tr> muteoff`` - Unmute audio
* ``ctlfmberry <tr> gainlow`` - Audio gain -9dB
* ``ctlfmberry <tr> gainoff`` - Audio gain 0dB"
* ``ctlfmberry <tr> set volume 0-6`` Audio volume level 0 to 6, equal -9dB to +9db, 3dB step
* ``ctlfmberry <tr> status`` - Print current status
* ``ctlfmberry <tr> stop`` - Stop FMBerry daemon

``<tr>``: specify one or more transmitters (as named in the .conf file) or ``all`` for all transmitters. 
Examples: 

``ctlfmberry 0,1 muteon`` 

``ctlfmberry 1 gainlow``

``ctlfmberry all muteoff``
       
That's it! :)
### Step 7: Debugging
FMBerry writes debugging output to /var/log/syslog.

You can watch the information by running ``ctlfmberry log``. It's essentially just a ```cat /var/log/syslog | grep fmberryd```

It will tell you what's wrong. 

### Updating the software
Please check for new dependencies. You can safely just run the ```apt-get install``` command again. It will only install new dependencies if necessary.

First stop the daemon by typing ```/etc/init.d/fmberry stop```. 

Then run ```git pull``` followed by a ```make``` and a ```sudo make install```.

You can then start FMBerry again with ```/etc/init.d/fmberry start```.
## Notes
* WARNING! Tobias wrote that he is not a professional C programmer, neither am I. Please expect this software to have major security flaws. Please don't expose it's control port to the internet! I'm fairly certain that this software is vulnerable to buffer overflows. 
* If you are a C programmer, please help by securing this software and sending a pull request. 
* The Daemon itself is essentially a simple TCP server. It is listening to Port 42516. (set in fmberry.conf) You can control it by sending the exact same commands you would give to ctlfmberry.
* For information on How to control the Daemon have a look into ctlfmberry. It's a simple shell script.


## Projects using FMBerry

https://github.com/Manawyrm/FMBerryRDSMPD (streaming of MPD title data via RDS)
https://github.com/akkinitsch/FMBerryRemote (streaming of internet radio streams, controllable via Webinterface)
http://achilikin.blogspot.de/2013/06/sony-ericsson-mmr-70-transmitter-led.html (enabling the LED on the transmitter to be software controllable, not supported in this fork)

## Common problems
__Not enough bandwidth error message on Raspberry Pi 4 when using more the 4 USB sound cards, cannot use more then 4 USB sound cards on the Pi 4.__

This is a known limitation of the USB chipset of the Pi4. A USB hub with power delivery connected to the USB-C connector might work. See https://github.com/raspberrypi/linux/issues/3962. 

__Sound is distorted when using more then three or four USB soundcards on a Pi 3__

__I only the first 7 or so USB sound cards but I have more__

Rasperry Pi OS kernel has a limit of 8 sound cards, you need to compile the kernel with an updated configuration. See AUDIO.md 

Your hub probably has a singe transaction translator. Use a USB hub that has multiple transaction translators. Unfortunately most vendors don't specify this. A great hub with MTT is the BIG7 from UUgear.

__The daemon does not show anything.__

That's normal. You have to use ./ctlfmberry to control the daemon.

__I can't seem to hear music.__

Turn up the volume/unmute your raspi with alsamixer.

__I am getting compile errors.__

Did you install all dependencies? (All lines with apt-get)

__The transmission dies after a couple of minutes.__

You didn't disable the internal processor of the MMR70. Do this by connecting TP18 to GND.

__The power supply of the raspberry pi shorts out/there are no lights anymore___

There is a short circuit. Probably caused by a wiring fault or by using an 80pin IDE cable for connecting the FMBerry.


__Alternative linux distributions don't detect the I2C bus (ArchLinux, OpenWRT, OSMC)__

Linux 3.18 introduced a new feature called Device Tree support. To get the I²C Bus working, you need to put this configuration at the end of /boot/config.txt (change the first parameter according to the RPi you have): 
```
device_tree=bcm2708-rpi-b-plus.dtb
device_tree_param=i2c1=on
device_tree_param=spi=on
```


Thanks to Daniel for the solution to that problem! 
