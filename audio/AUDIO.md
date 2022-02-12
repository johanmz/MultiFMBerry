MultiFMBerry - Audio
=======

## USB sound cards

If you want to stream audio from your Pi to the transmitters you need USB sound cards. Cheap ones are fine, as long as they are supported by the Pi. I've used very cheap "3D Sound" USB sound cards from AliExpress (reporting themselves as Generalplus or C-Media chipset)

__Limitations of Raspberry Pi 4B__

Unfortunately the Raspberry Pi 4B can only handle 4 sound cards due to limitations of the USB chipset (and according to some reports sometimes even less, depending on the sound card used). Using a hub connected to one of the normal USB ports does not solve the problem since the limitation is in the USB chipset of the Pi 4B.
See https://forums.raspberrypi.com/viewtopic.php?t=263124 and https://github.com/raspberrypi/linux/issues/3962

As a workaround, it should be possible to connect a USB-C hub to the USB-C power connector of the Pi 4B, using the OTG functionality. The Pi then either has to be powered to the USB-C hub (Power Delivery) or through the 5V GPIO pins. I haven't tried this myself. See https://github.com/raspberrypi/linux/issues/3962#issuecomment-733653714

The Raspberry Pi 3B does not have this problem, the hardware supports much more USB sound cards.

__8 sound cards limit of the Raspberry Pi OS Linux kernel__

Raspberry Pi OS has by default a limitation of 8 sound cards. Fortunately it's easy to increase this by changing the kernel configurations and building the kernel yourself.  

Instructions: https://www.raspberrypi.com/documentation/computers/linux_kernel.html

Change the following parameters: ``CONFIG_SND_DYNAMIC_MINORS=Y`` and ``CONFIG_MAX_CARDS=32`` (or set it to any value you need).
You can use ``menuconfig`` to do this, the settings can be found here: ``Device Drivers/Sound card support/Advanced Linux Sound Architecture/Dynamic device file minor numbers`` and ``Max number of sound cards``.

NB: Ubuntu 64 bit for Raspberry Pi already has a 32 sound card limit by default. 

## USB Hub

The Raspberry Pi has 4 USB ports, so if you want to stream and have more than 4 transmitters you need one or more USB hubs. 

Since most if not all USB Sound cards are operating at no more than Full Speed, you need a hub that support Multiple Transaction Translators (MTT). If your hub only has a Single Transaction Translator (STT), sound will be distorted when using approx 3,4 USB sound cards or more. Therefore you need a MTT hub.

You can check if your hub has MTT or STT with ``lsusb -v``. Warning: the build-in hub of the Pi will also be displayed, don't mix them up. Run the command first without the external USB hub and writing down the Bus and Device numbers of the internal hub. 
Look for ``bDeviceProtocol``, value 2 is MTT (good). Example from the BIG7 from UUgear:
```
lsusb -v

...

Bus 001 Device 004: ID 1a40:0201 Terminus Technology Inc. FE 2.1 7-port Hub
Couldn't open device, some information will be missing
Device Descriptor:
  bLength                18
  bDescriptorType         1
  bcdUSB               2.00
  bDeviceClass            9 Hub
  bDeviceSubClass         0
  bDeviceProtocol         2 TT per port
  bMaxPacketSize0        64
  idVendor           0x1a40 Terminus Technology Inc.
  idProduct          0x0201 FE 2.1 7-port Hub
  bcdDevice            1.00
  iManufacturer           0
  iProduct                1 USB 2.0 Hub [MTT]
  iSerial                 0
  bNumConfigurations      1
  Configuration Descriptor:


  ...
```

Unfortunately many vendors don't specify whether the hub is STT or MTT or the name of the chipset used so that you can look it up yourself in the datasheet. USB3 is also no guarantee for MTT capabilities. USB Hubs that do work, so with MTT, and also fit nicely on a Raspberry Pi (optional, but nice bonus) are the Waveshare USB 3.2 HUB HAT (5 ports) and UUgear BIG7 (7 ports). 

## Map ALSA card names to USB ports

With the help of UDEV rules, fixed ALSA card names can be assigned to USB ports. This prevents mingling your audio streams after reboots or plugging in/out USB sound cards.

### Step 1
First, check the device path (DEVPATH) for each of your USB sound cards by running the command below and then plugging in your USB devices one by one. Copy the value of each device path to a text editor, you'll need that in the next step.

```
pi@multifmberry:~ $ udevadm monitor --kernel --property --subsystem-match=sound
monitor will print the received events for:
KERNEL - the kernel uevent

KERNEL[646.140751] add      /devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.1/1-1.1.3.1:1.0/sound/card1 (sound)
ACTION=add
DEVPATH=/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.1/1-1.1.3.1:1.0/sound/card1
SUBSYSTEM=sound
SEQNUM=1678

...
````
### Step 2

Use the DEVPATH found in the previous step to create a file ``/etc/udev/rules.d/85-my-usb-audio.rules`` similar to the one below. Use your favorite text editor, for example ``sudo nano /etc/udev/rules.d/85-my-usb-audio.rules``

```
SUBSYSTEM!="sound", GOTO="my_usb_audio_end"
ACTION!="add", GOTO="my_usb_audio_end"

DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.1/1-1.1.3.1:1.0/sound/card?", ATTR{id}="FMBERRY_0"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.2/1-1.1.3.2:1.0/sound/card?", ATTR{id}="FMBERRY_1"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.3/1-1.1.3.3:1.0/sound/card?", ATTR{id}="FMBERRY_2"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.4/1-1.1.3.4:1.0/sound/card?", ATTR{id}="FMBERRY_3"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.7/1-1.1.3.7:1.0/sound/card?", ATTR{id}="FMBERRY_4"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.6/1-1.1.3.6:1.0/sound/card?", ATTR{id}="FMBERRY_5"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.3/1-1.1.3.5/1-1.1.3.5:1.0/sound/card?", ATTR{id}="FMBERRY_6"
DEVPATH=="/devices/platform/soc/3f980000.usb/usb1/1-1/1-1.1/1-1.1.2/1-1.1.2:1.0/sound/card?", ATTR{id}="FMBERRY_7"

LABEL="my_usb_audio_end"
```

Note that the last character, the card number, in the DEVPATH found with udevadm is replaced with a ``?``. 

### Step 3

Check the results:
```
pi@multifmberry:~ $ cat /proc/asound/cards
 0 [FMBERRY_1      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.3.2, full speed
 1 [FMBERRY_0      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.3.1, full speed
 2 [FMBERRY_7      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.2, full speed
 3 [FMBERRY_2      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.3.3, full speed
 4 [FMBERRY_3      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.3.4, full speed
 5 [FMBERRY_6      ]: USB-Audio - USB PnP Sound Device
                      C-Media Electronics Inc. USB PnP Sound Device at usb-3f980000.usb-1.1.3.5, full
 6 [FMBERRY_5      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.3.6, full speed
 7 [FMBERRY_4      ]: USB-Audio - USB Audio Device
                      GeneralPlus USB Audio Device at usb-3f980000.usb-1.1.3.7, full speed
 8 [vc4hdmi        ]: vc4-hdmi - vc4-hdmi
                      vc4-hdmi
```
 See also https://alsa.opensrc.org/Udev.

## Streaming audio

VLC example (install with ``sudo apt install vlc``) : 
```
#When UDEV rules have been setup as described above
cvlc --http-reconnect --aout=alsa --alsa-audio-device=hw:FMBERRY_2,0 $URL
cvlc --http-reconnect --aout=alsa --alsa-audio-device=hw:FMBERRY_3,0 $OTHERURL

#Send vlc to the background
cvlc --http-reconnect --aout=alsa --alsa-audio-device=hw:FMBERRY_2,0 $URL &
cvlc --http-reconnect --aout=alsa --alsa-audio-device=hw:FMBERRY_3,0 $OTHERURL &

#When you haven't setup UDEV rules as described above
cvlc --http-reconnect --aout=alsa --alsa-audio-device=hw:2,0 $URL
cvlc --http-reconnect --aout=alsa --alsa-audio-device=hw:3,0 $OTHERURL

#this uses much more CPU so better not do this
cvlc --http-reconnect --aout=alsa --alsa-audio-device=default:CARD=Device_2 $OTHERURL
```
Where $URL and $OTHERURL are the streams of your favorite radio stations

Mplayer example:
```
#When UDEV rules have been setup as described above 
mplayer -vo null -ao alsa:device=hw=FMBERRY_2.0 $URL 

#When you haven't setup UDEV rules as described above
mplayer -vo null -ao alsa:device=hw=2.0 $URL
```
