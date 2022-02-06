MultiFMBerry - Audio
=======

USB sound cards
-----------

If you want to stream audio from your Pi to the transmitters you need USB sound cards. Cheap ones are fine, as long as they are supported by the Pi. I've used very cheap "3D Sound" USB sound cards from AliExpress (reporting themselves as ``Generalplus``)

__Limitations of Raspberry Pi 4B__

Unfortunately the Raspberry Pi 4B can only handle 4 sound cards due to limitations of the USB chipset. Using a hub is not making any difference.
See https://forums.raspberrypi.com/viewtopic.php?t=263124 and https://github.com/raspberrypi/linux/issues/3962

It should be possible to connect a USB-C hub to the USB-C power connector of the Pi. The Pi then either has to be powered to the USB-C hub (Power Delivery) or through the 5V GPIO pins. I haven't tried this myself. See https://github.com/raspberrypi/linux/issues/3962#issuecomment-733653714

The Raspberry Pi 3B does not have this problem and supports much more USB sound cards.

__8 sound cards limit of the Raspberry Pi OS Linux kernel__

Rasperry PI OS has by default a limitation of 8 sound cards. Fortunately it's easy to increase this by changing the kernel configurations and building the kernel yourself.  

Instructions: https://www.raspberrypi.com/documentation/computers/linux_kernel.html

Change to following parameters: ``CONFIG_SND_DYNAMIC_MINORS=Y`` and ``CONFIG_MAX_CARDS=32`` (or set it to any value you need)

NB: Ubuntu 64 bit for Raspberry Pi already has a 32 sound card limit by default.

USB Hub
----------

The Raspberry Pi has 4 USB ports, so if you want to stream and have more then 4 transmitters you need one or more USB hubs. 

Since most if not all USB Sound cards are operating at no more then Full Speed, you need a hub that support Multiple Transaction Translators (MTT). If your hub only has a Single Transaction Translator (STT) sound will be distorted when using approx 3,4 USB sound cards or more.

You can check if your hub has MTT or STT with ``lsusb -v`` (don't get confused by the build in hub of the Pi). Look for ``bDeviceProtocol``, value 2 is MTT (good). Example from the BIG7:
```
lsusb v

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


Unfortunately in many cases this is not specified. USB Hubs that fit nicely on a Raspberry Pi (optional, but nice bonus) and with MTT are the Waveshare USB 3.2 HUB HAT (5 ports)and UUgear BIG7 (7 ports). 

Streaming Audio
---------

Mplayer example:
```
mplayer -vo null -ao alsa:device=default=CARD=Device_3 $URL
mplayer -vo null -ao alsa:device=default=CARD=Device_4 $OTHERURL
```

VLC example: 
```
cvlc --http-reconnect --aout=alsa --alsa-audio-device=default:CARD=Device_1 $URL
cvlc --http-reconnect --aout=alsa --alsa-audio-device=default:CARD=Device_2 $OTHERURL

```
Where $URL and $OTHERURLis the stream of your favorite radio station

Mplayer uses much less CPU then VLC, install with ``sudo apt install mplayer``

