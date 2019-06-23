#!/bin/sh
# Write flash contents of the ATMEGA32u2 (which does the USB tasks on an Arduino UNO)
TARGET=KeyboardMIDI
# -v = verbose
# -V = NO verify
# -F = force
avrdude -p atmega32u2 -c usbtiny -P usb -v -v -V -U flash:w:KeyboardMIDI.hex -U eeprom:w:KeyboardMIDI.eep
