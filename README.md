# ProdikeysDM_Adaptor
Arduino code to use a PS/2 based "Creative Labs Prodikeys DM" MIDI keyboard on USB

## What's this?
I have seen a video about the *Creative Labs Prodikeys* range of products - these are computer keyboards with a row of piano keys attached to them. I instantly wanted to have one.
([More info on these weird things](http://prodikeys.princefolk.co.uk))

Unfortunately, when searching eBay I could only find a "Prodikeys DM" which is the PS/2 version - no USB, no DIN jacks...
There is also some proprietary "driver magic" going on: The included Windows XP software sends some "magic" commands to the keyboard in order to activate its MIDI capabilities. After that, it sends MIDI data to the Windows MIDI subsystem. This, unfortunately, does NOT work with PS/2-to-USB dongles or any non-XP operating system :-(

So: We need a specialized PS/2-to-USB adaptor. That's what this is.

## The Solution
Since PS/2 is a very simple protocol, it is easy to use an Arduino to "speak PS/2" without using any additional parts. This part is quite easy.
What's left is the whole USB MIDI/HID keyboard stuff...
[LUFA for the rescue!](http://www.lufa-lib.org)

The trick is to split the problem up into two parts: a *PS/2 part* (talking to the Prodikeys) and an *USB part* (talking to the computer). This can both be done on one regular **Arduino UNO R3**.

## How to
* Get an **Arduino UNO R3** which has an **ATMEGA32u2** USB controller. Others using a FTDI or CH340 do **not** work.
* Upload the Arduino sketch [*ProdikeysDM*](https://github.com/hotkeymuc/ProdikeysDM_Adaptor/tree/master/PS2-Part__Arduino_Sketch_for_ATMEGA328P/ProdikeysDM) onto it
* Connect an ISP programmer to the **secondary** ISP port (near the USB jack) and flash the [*KeyboardMIDI* firmware](https://github.com/hotkeymuc/ProdikeysDM_Adaptor/tree/master/USB-Part__Firmware_for_ATMEGA32U2/KeyboardMIDI) onto it. After it has been flashed, you will not be able to upload a sketch via USB any more (unless you flash the original firmware back)
* Connect the ProdikeysDM keyboard to the Arduino: CLK to PIN3, DATA to PIN4
* Plug the Arduino into a computer and it should show up as a "Prodikeys DM multifunction keyboard"
* Have fun and jam along

## Known issues
* As of know (2019-06-23) the keyboard mapping is wrong! Pressing any of the "regular" keys will produce random key strokes. But, hey!, there are keystrokes!
* Some notes on the MIDI keyboard seem to be dead. This seems to be a well-known issue with these keyboards. More investigation needs to be done here.