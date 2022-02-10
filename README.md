![FMBerry Logo](http://tbspace.de/holz/uzsjpoghdq.png)
MultiFMBerry
=======
> Originally written by Tobias Mädel (t.maedel@alfeld.de, http://tbspace.de). Extended by Johan Muizelaar (https://github.com/johanmz) to control multiple transmitters.

> 

What is this? 
-------------
This is a fork of FMBerry from Tobias Mädel, a piece of software that allows you to transmit FM radio with your Raspberry Pi. The original software from Tobias allows you to control one transmitter, MultiFMBerry allows you to control multiple transmitters. The software is tested with 8 transmitters using a Raspberry Pi 3B but more should also be possible.

The goal was to create an alternative for analog cable FM after the switch-off, but even more just as a fun project.


How does it work? 
-------------
It uses Sony-Ericsson MMR-70 transmitters, which was originally intended for use with Sonys Walkman cellphones from early 2000s.
You can get these still for cheap from Ebay in 2022.

Why does it need additional IC's apart from the MRR70's?
-------------

Since all transmitters  use the same I²C address (0x66), one ore more multiplexers (TCA9548A) are used to switch the transmitters before sending commands and RDS updates to the transmitters. Each TCA9548A has 8 I²C ports so it can handle 8 transmitters. One or more IO expanders (MCP23017) are used to read the RDS interrupt signals, this is the signal that the transmitter needs the next RDS block. Each MCP23017 can handle up to 16 transmitters. 


What do I need to build this? 
-------------
* MMR-70 transmitter, two or more. If you want to control only one transmitter, it makes more sense to use the software and hardware setup from Tobias since it doesn't need the additional IC's.
* Raspberry Pi (tested with 3B and 4 but see the limitations of the Pi4 in [AUDIO.md](audio/AUDIO.md) before you start!)
* TCA9548A, MCP23017 and AMS1117 breakout board(s) from AliExpress or Ebay
* Soldering equipment (soldering iron and some solder)
* USB sound card for each transmitter, the cheap "3D Sound" USB sound sticks from AliExpress work fine.
* One or more USB hubs if you have more then 4 transmitters. You need a hub with Multiple Transaction Translators (MTT). The BIG7 from UUgear and the Waveshare USB3.2 HAT (5 port) work fine and have the same form factor as the Pi as a bonus. See also [AUDIO.md](audio/AUDIO.md).


The hardware is explained here:
[HARDWARE.md](hardware/HARDWARE.md)

How many transmitters and audio streams can one Pi control?
------------
It depends:

* The Raspberry Pi 4 has a limit of 4 USB sound cards due to limitations in the USB bus. See [AUDIO.md](audio/AUDIO.md). On the Raspberry Pi 3B, you can use much more USB sound cards with a suitable USB hub, see next bullet.
* If you use a hub and it does not support Multiple Transaction Translators (MTT), streaming to approx. more then 3 audio streams will result in distortion. See [AUDIO.md](audio/AUDIO.md). Use a hub with MTT.
* The Raspberry Pi OS Linux kernel has a limit of 8 sound cards. You can increase this limit by compiling your own kernel. See [AUDIO.md](audio/AUDIO.md)
* For RDS, the Pi needs to send each 21,5ms data to each transmitter over the I²C bus. Not only the data needs to be send but also the I²C commands to switch the multiplexer and read the IO expander. With many transmitters, this might fully occupy the I²C bus. For more capacity, you can increase the speed of the I²C bus from 100Khz to 400Khz (see steps below) or disable RDS for some tranmitters in the .conf file.
* Each audiostream uses about 5% CPU. The 3B has 4 cores which should be enough for many streams. FMBerry itself does not use much CPU either.
* The software itself has no practical limit for the number of transmitters. You need one TCA9548A per 8 transmitters and one MCP23017 per 16 transmitters. If you want to use more the 4 TCA9548A or MCP23017 IC's, increase the max in defs.h before compiling the software.

This software has been tested succesfully with 8 transmitters on a Raspbery Pi 3B with 8 USB sound cards using a BIG7 UUgear USB hub, I plan to test with more transmitters.

Installation
-------------

### Step 1: Enabling I²C

Open raspi-blacklist.conf:

```
sudo nano /etc/modprobe.d/raspi-blacklist.conf
```

Comment out the Line "``blacklist i2c-bcm2708``" with a #.
Save with Ctrl+O and close nano with Ctrl+X

To make sure I²C Support is loaded at boottime open /etc/modules.

```
sudo nano /etc/modules
```

Add the following lines:

``i2c-dev``

Then again, Save with Ctrl+O and then close nano with Ctrl+X.

Please reboot your Raspberry after this step. 

### Step 2: Installing I²C tools and dependencies for the build

First update your local package repository with
```
sudo apt-get update
```

then install all needed software with the following command:
```
sudo apt-get install i2c-tools build-essential git libconfuse-dev
```


### Step 3: Speeding up the I²C bus

For RDS, every transmitter needs to receive data each 21,5ms over the I²C bus. Also, the I²C messages for the multiplexer and IO expander need to be send and received.  With more then 10 transmitters or so, the I²C bus speed should be increased to 400 KHz. 
```
sudo nano /boot/config.txt
```
Find the line ``dtparam=i2c`` and change it to ``dtparam=i2c_arm=on,i2c_arm_baudrate=400000``. Save and reboot.

### Step 4: Building the software
To build the software execute the following commands (in your homefolder):

```
git clone https://github.com/johanmz/FMBerry.git
cd FMBerry
make
```

Compiling the software will take a couple of seconds.

### Step 5: Specify your hardware in the config file
FMBerry needs to know about the MCP23017 IO expander, TCA9548A multiplexer, how many tranmitters you have and to which TCA9548A and MCP23017 port each transmitter is connected. Specify this in the .conf file. 

In this file you can also specify the frequency, RDS information, etc for each transmitter.

If you want to modify the file after the make/compile, edit the fmberry.conf file in /etc.


### Step 6: Installing the software
FMBerry is essentially a daemon called fmberryd.
To install it into your system path type 
```
sudo make install
```

You can start it by typing 
```
sudo /etc/init.d/fmberry start
```

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
* ``ctlfmberry all stop`` - Stop FMBerry daemon
* ``ctlfmberry log`` - show logfile for FMBerry

``<tr>``: specify one or more transmitters (as named in the .conf file) or ``all`` for all transmitters. 
Examples: 

``ctlfmberry 0,1 muteon`` 

``ctlfmberry 1 gainlow``

``ctlfmberry all muteoff``
       
That's it! :)
### Step 7: Debugging
FMBerry writes debugging output to /var/log/syslog.

You can watch the information by running 
```
ctlfmberry log
```
It's essentially just a
 ```
 cat /var/log/syslog | grep fmberryd
 ```

It will tell you what's wrong. 

### Updating the software
Please check for new dependencies. You can safely just run the 
```
apt-get install
``` 
command again. It will only install new dependencies if necessary.

First stop the daemon by typing 
```
/etc/init.d/fmberry stop
``` 

Then run 
```
git pull
make
sudo make install
```

You can then start FMBerry again with 
```
/etc/init.d/fmberry start
```

## Notes
* WARNING! Tobias wrote that he is not a professional C programmer, neither am I. Please expect this software to have major security flaws. Please don't expose it's control port to the internet! I'm fairly certain that this software is vulnerable to buffer overflows. 
* If you are a C programmer, please help by securing this software and sending a pull request. 
* The Daemon itself is essentially a simple TCP server. It is listening to Port 42516. (set in fmberry.conf) You can control it by sending the exact same commands you would give to ctlfmberry.
* For information on How to control the Daemon have a look into ctlfmberry. It's a simple shell script.


## Projects using FMBerry

https://github.com/Manawyrm/FMBerryRDSMPD (streaming of MPD title data via RDS)

https://github.com/akkinitsch/FMBerryRemote (streaming of internet radio streams, controllable via Webinterface)

http://achilikin.blogspot.de/2013/06/sony-ericsson-mmr-70-transmitter-led.html (enabling the LED on the transmitter to be software controllable, not supported in this fork but interesting information about the MRR70 can be found there)

## Common software problems

__The daemon does not show anything.__

That's normal. You have to use ./ctlfmberry to control the daemon.

__I can't seem to hear music.__

Turn up the volume/unmute your raspi with alsamixer.

__I am getting compile errors.__

Did you install all dependencies? (All lines with apt-get)


__Hardware issues__

See [HARDWARE.md](hardware/HARDWARE.md)


__Sound issues__

See [AUDIO.md](audio/AUDIO.md)