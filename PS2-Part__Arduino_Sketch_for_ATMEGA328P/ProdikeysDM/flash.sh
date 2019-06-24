#!/bin/sh
#avrdude -cusbtiny -v -patmega328p -cusbtiny -Uflash:w:ProdikeysDM.ino.with_bootloader.standard.hex:i 
avrdude -cusbtiny -v -V -patmega328p -cusbtiny -Uflash:w:ProdikeysDM.ino.standard.hex:i 
