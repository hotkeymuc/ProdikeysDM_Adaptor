#!/bin/sh
# Read flash contents of the ATMEGA32u2 (which does the USB tasks on an Arduino UNO)
avrdude -p atmega32u2 -c usbtiny -U flash:r:flash_ATMEGA32U2.hex:r
