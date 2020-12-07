#Welcome to my little project that adds a bit of retro flavour to a smart light bulb!


This relies on the Tasmota project. Follow the Tasmota guides to downloading and building the Tasmota project.
Then you should be able to paste these files into the tasmota folder.

In the platformio.ini file:
I'm assuming you're using an ESP8266 based device or board. I've not tried this on anything else, but there is nothing specific to using the ESP8266 - it should work as well, if not better on a ESP32.

Change this to represent what hardware you have - for me the Arlec GLD120HA RGBWW light bulb has a 2M flash chip in it,
so I allocate a 256k section to store the disk files.

board_build.ldscript        = eagle.flash.2m256.ld

Add

board_build.filesystem = littlefs

## After you've built the code, loaded it onto a device and set up Tasmota to connect to a wifi access point.

##What does this do?

Firstly, we have a 6502 emulator done by 'mikeroolz' that was posted on the Arduino forum back on 2013. I can only assume
mikeroolz was happy for it to be public domain. If you're mikeroolz, apart from thanks for a great piece of code, you might 
want to inform me what license you'd like associated with it.

We than add the roms of the legendary VIC20.This gives use the KERNAL and basic. I've then put some traps in the 6502 emulator to 
fudge up an emulation of the iec port so I can do disk and a device to talk to the Tasmota command line.

Creates a tcp socket at port 8880. You can use Telnet to connect to this.
telnet 192.168.1.123 8880  


The disk emulation is rudimentary:

LOAD "TEST",8   load the file "TEST"
SAVE "TEST",8   saves the current basic program to the file "TEST"
you can, of course, have different filenames. These are stored in a LittleFS partition on the serial flash of the ESP8266 module.

Then, to talk to the tasmota command line:

OPEN 1,4,5                  opens file #1 to device 4
PRINT#1,"POWER ON"          send the POWER ON command to Tasmota (should turn the light on)
PRINT#1,"COLOR 6"           changes the color to #6. you have a choice of 1..12

refer to the TASMOTA docs as to what commands you have available and the VIC20 docs for how to tun the basic.
The STOP key is ctrl-g.

Alas there is no graphics or sound that the real VIC20 had - a bit hard to map that hardware onto a lightbulb :(

Note that this has just been hacked together - it 'works' but don't expect a flawless experience.

So, there it is. Hopefully if you're adventurous and want to have a play. Go crazy. Feel free to add features etc.








