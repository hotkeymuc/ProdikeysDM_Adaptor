/*
  PS2Keyboard.cpp - PS2Keyboard library
  Copyright (c) 2007 Free Software Foundation.  All right reserved.
  Written by Christian Weichel <info@32leaves.net>

  ** Modified for use beginning with Arduino 13 by L. Abraham Smith, <n3bah@microcompdesign.com> * 

  ** Modified to include: shift, alt, caps_lock, caps_lock light, and hot-plugging a kbd  *
  ** by Bill Oldfield 22/7/09 *

  ** Modified to handle "CreativeLabs Prodikeys DM" custom data *
  ** by Bernhard "HotKey" Slawik, github.com/hotkeymuc, 2019 *

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
//#include "WProgram.h"
#include "Arduino.h"
#include "PS2Keyboard.h"

#include "binary.h"
//typedef uint8_t boolean;
typedef uint8_t byte;



/*
 * I do know this is so uncool, but I just don't see a way arround it
 * REALLY BAD STUFF AHEAD
 *
 * The variables are used for internal status management of the ISR. The're
 * not kept in the object instance because the ISR has to be as fast as anyhow
 * possible. So the overhead of a CPP method call is to be avoided.
 *
 * PLEASE DO NOT REFER TO THESE VARIABLES IN YOUR CODE AS THEY MIGHT VANISH SOME
 * HAPPY DAY.
 */
int  ps2Keyboard_DataPin;
int  ps2Keyboard_ClockPin;
byte ps2Keyboard_CurrentBuffer;
volatile byte ps2Keyboard_BitPos;
volatile byte ps2Keyboard_CharBuffer;

// variables used to remember information about key presses
volatile bool ps2Keyboard_shift;     // indicates shift key is pressed
volatile bool ps2Keyboard_ctrl;      // indicates the ctrl key is pressed
volatile bool ps2Keyboard_alt;       // indicates the alt key is pressed
volatile bool ps2Keyboard_extend;    // remembers a keyboard extended char received
volatile bool ps2Keyboard_release;   // distinguishes key presses from releases
volatile bool ps2Keyboard_caps_lock; // remembers shift lock has been pressed

volatile bool ps2Keyboard_prodikeys; // special prodikeys value
volatile byte ps2Keyboard_prodikeysFunction;
volatile byte ps2Keyboard_prodikeysPos;
volatile byte ps2Keyboard_prodikeysKey;



// Callbacks
onData_t ps2Keyboard_onData = NULL;
onError_t ps2Keyboard_onError = NULL;
onKeyPress_t ps2Keyboard_onKeyPress = NULL;
onKeyRelease_t ps2Keyboard_onKeyRelease = NULL;

// Custom for Prodikeys
onProdikeysKeyPress_t ps2Keyboard_onProdikeysKeyPress = NULL;
onProdikeysKeyRelease_t ps2Keyboard_onProdikeysKeyRelease = NULL;
onProdikeysMidiPress_t ps2Keyboard_onProdikeysMidiPress = NULL;
onProdikeysMidiRelease_t ps2Keyboard_onProdikeysMidiRelease = NULL;
onProdikeysPitchBend_t ps2Keyboard_onProdikeysPitchBend = NULL;



// vairables used in sending command bytes to the keyboard, eg caps_lock light
volatile boolean cmd_in_progress;
volatile int     cmd_count;
         byte    cmd_value;
volatile byte    cmd_ack_value;
         byte    cmd_parity;
volatile boolean cmd_ack_byte_ok;

// sending command bytes to the keybaord needs proper parity (otherwise the keyboard
// just asks you to repeat the byte)
byte odd_parity(byte val) {
  int i, count = 1;  // start with 0 for even parity
  for (i=0; i<8; i++) {
    if (val&1) count++;
    val = val>>1;
  }
  return count & 1; // bottom bit of count is parity bit
}

void kbd_send_command(byte val, bool waitAck) {
  // stop interrupt routine from receiving characters so that we can use it
  // to send a byte
  cmd_in_progress = true;
  cmd_count       = 0;

  // set up the byte to shift out and initialise the ack bit
  cmd_value      = val;
  cmd_ack_value  = 1;    // the kbd will clear this bit on receiving the byte
  cmd_parity     = odd_parity(val);

  // set the data pin as an output, ready for driving
  digitalWrite(ps2Keyboard_DataPin, HIGH);
  pinMode(ps2Keyboard_DataPin, OUTPUT);

  // drive clock pin low - this is going to generate the first
  // interrupt of the shifting out process
  pinMode(ps2Keyboard_ClockPin, OUTPUT);
  digitalWrite(ps2Keyboard_ClockPin, LOW);

  // wait at least one clock cycle (in case the kbd is mid transmission)
  delayMicroseconds(60);

  // set up the 0 start bit
  digitalWrite(ps2Keyboard_DataPin, LOW);
  // let go of clock - the kbd takes over driving the clock from here
  digitalWrite(ps2Keyboard_ClockPin, HIGH);
  pinMode(ps2Keyboard_ClockPin, INPUT);

  
  // wait for interrupt routine to shift out byte, parity and receive ack bit
  if (waitAck) {
    while (cmd_ack_value!=0) ;

    // switch back to the interrupt routine receiving characters from the kbd
    cmd_in_progress = false;
  }

}

void PS2Keyboard::reset() {
  kbd_send_command(0xFF, true);   // send the kbd reset code to the kbd: 3 lights
                            // should flash briefly on the kbd

  // reset all the global variables
  ps2Keyboard_CurrentBuffer = 0;
  ps2Keyboard_CharBuffer    = 0;
  ps2Keyboard_BitPos     = 0;
  ps2Keyboard_shift         = false;
  ps2Keyboard_ctrl          = false;
  ps2Keyboard_alt           = false;
  ps2Keyboard_extend        = false;
  ps2Keyboard_release       = false;
  ps2Keyboard_caps_lock     = false;

  ps2Keyboard_prodikeys = false;
  
  cmd_in_progress           = false;
  cmd_count                 = 0;
  cmd_value                 = 0;
  cmd_ack_value             = 1;
}

// val : bit_2=caps_lock, bit_1=num_lock, bit_0=scroll_lock
void kbd_set_lights(byte val) {
  // When setting the lights with the 0xED command the keyboard responds
  // with an "ack byte", 0xFA. This is NOT the same as the "ack bit" that
  // follows the succesful shifting of each command byte. See this web
  // page for a good description of all this:
  // http://www.beyondlogic.org/keyboard/keybrd.htm
  cmd_ack_byte_ok = false;   // initialise the ack byte flag
  kbd_send_command(0xED, true);    // send the command byte
  while (!cmd_ack_byte_ok) ; // ack byte from keyboard sets this flag
  kbd_send_command(val, true);     // now send the data
}
uint8_t kbd_read_extra() {
  return (ps2Keyboard_caps_lock<<3) |
         (ps2Keyboard_shift<<2) |
         (ps2Keyboard_alt<<1) |
          ps2Keyboard_ctrl;
}

// The ISR for the external interrupt
// This may look like a lot of code for an Interrupt routine, but the switch
// statements are fast and the path through the routine is only ever a few
// simple lines of code.
void ps2interrupt (void) {
  int value = digitalRead(ps2Keyboard_DataPin);
  //byte value = (PIND & B00010000) >> 4; // FAST read (digitalRead is too slow for Host->Keyboard communication and can miss the first bit)

  // This is the code to send a byte to the keyboard. Actually its 12 bits:
  // a start bit, 8 data bits, 1 parity, 1 stop bit, 1 ack bit (from the kbd)
  if (cmd_in_progress) {
    cmd_count++;          // cmd_count keeps track of the shifting
    switch (cmd_count) {
      case 1: // start bit
        digitalWrite(ps2Keyboard_DataPin, LOW);
        break;
      case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
        // data bits to shift
        digitalWrite(ps2Keyboard_DataPin, cmd_value&1);
        cmd_value = cmd_value>>1;
        break;
      case 10:  // parity bit
        digitalWrite(ps2Keyboard_DataPin,cmd_parity);
        break;
      case 11:  // stop bit
        // release the data pin, so stop bit actually relies on pull-up
        // but this ensures the data pin is ready to be driven by the kbd for
        // for the next bit.
        digitalWrite(ps2Keyboard_DataPin, HIGH);
        pinMode(ps2Keyboard_DataPin, INPUT);
        break;
      case 12: // ack bit - driven by the kbd, so we read its value
        cmd_ack_value = digitalRead(ps2Keyboard_DataPin);
        cmd_in_progress = false;  // done shifting out
        break;
    }
    return; // don't fall through to the receive section of the ISR
  }

  // receive section of the ISR
  // shift the bits in
  if ((ps2Keyboard_BitPos == 0) && (value == 1)) { //value == HIGH)) {
    // Discard "HIGH" start bit!
    return;
  }
  
  if(ps2Keyboard_BitPos > 0 && ps2Keyboard_BitPos < 11) {
    ps2Keyboard_CurrentBuffer |= (value << (ps2Keyboard_BitPos - 1));
  }
  ps2Keyboard_BitPos++; // keep track of shift-in position

  if (ps2Keyboard_BitPos == 11) { // a complete character received

    //Serial.println(ps2Keyboard_CurrentBuffer, HEX);
    if (ps2Keyboard_onData != NULL) ps2Keyboard_onData(ps2Keyboard_CurrentBuffer);

    // Prodikeys:
    if (ps2Keyboard_prodikeysFunction > 0) {

      if (ps2Keyboard_prodikeysFunction == 0x31) {
        // Button / Pitch Bend

        if (ps2Keyboard_prodikeysPos == 0) {
          if (ps2Keyboard_CurrentBuffer == 0xf0) {  // Release event
            ps2Keyboard_release = true;
          } else {
            
            if ((ps2Keyboard_CurrentBuffer >= 0x31) && (ps2Keyboard_CurrentBuffer <= 0x6f)) {
              // Center is 0x50
              //Serial.print("Prodikeys: PITCH ");
              //Serial.println(map(ps2Keyboard_CurrentBuffer, 0x31, 0x6f, -63, 63));
              if (ps2Keyboard_onProdikeysPitchBend != NULL) {
                ps2Keyboard_onProdikeysPitchBend(map(ps2Keyboard_CurrentBuffer, 0x31, 0x6f, -64, 64));
              }
              
            } else {
              //ps2Keyboard_prodikeysKey = ps2Keyboard_CurrentBuffer;
              //Serial.print("Prodikeys: FUNC ");
              //Serial.println(ps2Keyboard_prodikeysKey, HEX);
              if (ps2Keyboard_onProdikeysKeyPress != NULL)
                ps2Keyboard_onProdikeysKeyPress(ps2Keyboard_CurrentBuffer, kbd_read_extra());
            }
            
            ps2Keyboard_prodikeysFunction = 0; // Back to normal
          }
        } else {
          if (ps2Keyboard_release) {
            //ps2Keyboard_prodikeysKey = ps2Keyboard_CurrentBuffer;
            //Serial.print("Prodikeys: FUNC-RELEASE ");
            //Serial.println(ps2Keyboard_prodikeysKey, HEX);
            if (ps2Keyboard_onProdikeysKeyRelease != NULL)
              ps2Keyboard_onProdikeysKeyRelease(ps2Keyboard_CurrentBuffer);
          }
          ps2Keyboard_prodikeysFunction = 0; // Back to normal
        }

        
      } else
      if (ps2Keyboard_prodikeysFunction == 0x51) {
        
        // MIDI event
        switch (ps2Keyboard_prodikeysPos) {
          
          case 0:
            if (ps2Keyboard_CurrentBuffer == 0xf0) {  // Release event
              ps2Keyboard_release = true;
            } else {
              ps2Keyboard_prodikeysKey = ps2Keyboard_CurrentBuffer;
            }
            break;
          
          case 1:
            if (ps2Keyboard_release) {
              ps2Keyboard_prodikeysKey = ps2Keyboard_CurrentBuffer;

              if (ps2Keyboard_onProdikeysMidiRelease != NULL)
                ps2Keyboard_onProdikeysMidiRelease(ps2Keyboard_prodikeysKey, 0);
                
              ps2Keyboard_prodikeysFunction = 0; // Back to normal
            
            } else {
              //Serial.print("Prodikeys: PRESS ");
              //Serial.print(ps2Keyboard_prodikeysKey, HEX);
              //Serial.print(" velocity=");
              //Serial.println(ps2Keyboard_CurrentBuffer);
              if (ps2Keyboard_onProdikeysMidiPress != NULL)
                ps2Keyboard_onProdikeysMidiPress(ps2Keyboard_prodikeysKey, ps2Keyboard_CurrentBuffer);
              
              ps2Keyboard_prodikeysFunction = 0; // Back to normal
            }
            break;
          /*
          case 2:
            if (ps2Keyboard_release) {
              //Serial.print("Prodikeys: RELEASE ");
              //Serial.println(ps2Keyboard_prodikeysKey, HEX);
              if (ps2Keyboard_onProdikeysMidiRelease != NULL)
                ps2Keyboard_onProdikeysMidiRelease(ps2Keyboard_prodikeysKey, ps2Keyboard_CurrentBuffer);
            }
            ps2Keyboard_prodikeysFunction = 0; // Back to normal
            break;
          */
          default:
            //Serial.println("Prodikeys flow error");
            if (ps2Keyboard_onError != NULL) ps2Keyboard_onError('X');
            ps2Keyboard_prodikeysFunction = 0; // Back to normal
        }
        
      } else {
        //Serial.println("Prodikeys function error");
        if (ps2Keyboard_onError != NULL) ps2Keyboard_onError('F');
        ps2Keyboard_prodikeysFunction = 0; // Back to normal
      }
      
      ps2Keyboard_prodikeysPos++;
      
    } else
    
    // Normal keyboard operation
    switch (ps2Keyboard_CurrentBuffer) {
      case 0xF0: { // key release char
        ps2Keyboard_release = true;
        ps2Keyboard_extend = false;
        break;
      }
      case 0xFA: { // command acknowlegde byte
        cmd_ack_byte_ok = true;
        break;
      }
      case 0xFC:
      case 0xFD:
      { // Self-test failed
        //kbd_send_command(0xff, false);
        //reset();
        if (ps2Keyboard_onError != NULL) ps2Keyboard_onError('S');
        break;
      }
      case 0xFE: { // Error
        cmd_ack_byte_ok = false; //@FIXME
        cmd_in_progress = false;
        cmd_count = 0;
        break;
      }
      case 0xAA: { // Self-test OK
        cmd_ack_byte_ok = true;
        //cmd_in_progress = false;
        break;
      }
  
      case 0xE0: { // extended char set
        ps2Keyboard_extend = true;
        break;
      }
      case 0x12:   // left shift
      case 0x59: { // right shift
        /*
        if (ps2Keyboard_release) {
          if (ps2Keyboard_onKeyRelease != NULL)
            ps2Keyboard_onKeyRelease(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend);
        } else {
          if (ps2Keyboard_onKeyPress != NULL)
            ps2Keyboard_onKeyPress(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend, kbd_read_extra());
        }
        */
            
        ps2Keyboard_shift = ps2Keyboard_release? false : true;
        ps2Keyboard_release = false;
        break;
      }
      case 0x11: { // alt key (right alt is extended 0x11)
        /*
        if (ps2Keyboard_release) {
          if (ps2Keyboard_onKeyRelease != NULL)
            ps2Keyboard_onKeyRelease(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend);
        } else {
          if (ps2Keyboard_onKeyPress != NULL)
            ps2Keyboard_onKeyPress(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend, kbd_read_extra());
        }
        */

        ps2Keyboard_alt = ps2Keyboard_release? false : true;
        ps2Keyboard_release = false;
        break;
      }
      case 0x14: { // ctrl key (right ctrl is extended 0x14)
        /*
        if (ps2Keyboard_release) {
          if (ps2Keyboard_onKeyRelease != NULL)
            ps2Keyboard_onKeyRelease(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend);
        } else {
          if (ps2Keyboard_onKeyPress != NULL)
            ps2Keyboard_onKeyPress(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend, kbd_read_extra());
        }
        */

        ps2Keyboard_ctrl = ps2Keyboard_release? false : true;
        ps2Keyboard_release = false;
        break;
      }
      case 0x58: { // caps lock key
        /*
        if (ps2Keyboard_release) {
          if (ps2Keyboard_onKeyRelease != NULL)
            ps2Keyboard_onKeyRelease(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend);
        } else {
          if (ps2Keyboard_onKeyPress != NULL)
            ps2Keyboard_onKeyPress(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend, kbd_read_extra());
        }
        */

        if (!ps2Keyboard_release) {
        	ps2Keyboard_caps_lock = ps2Keyboard_caps_lock? false : true;
        	// allow caps lock code through to enable light on and off
          ps2Keyboard_CharBuffer = ps2Keyboard_CurrentBuffer;
        }
        else {
  	      ps2Keyboard_release = false;
        }
        break;
      }
      
      default: { // a real key
        
        if ((ps2Keyboard_extend) && ((ps2Keyboard_CurrentBuffer == 0x51) || (ps2Keyboard_CurrentBuffer == 0x31))) {
          // Prodikeys event
          ps2Keyboard_prodikeysFunction = ps2Keyboard_CurrentBuffer;
          ps2Keyboard_prodikeysPos = 0;
          ps2Keyboard_release = false;  // We need that
          
          
        } else
        // Normal key event
        if (ps2Keyboard_release) { // although ignore if its just released
          
          //Serial.print("Key: RELEASE "); Serial.println(ps2Keyboard_CurrentBuffer);
          if (ps2Keyboard_onKeyRelease != NULL)
            ps2Keyboard_onKeyRelease(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend);
          
          ps2Keyboard_release = false;
        }
        else { // real keys go into CharBuffer
          //Serial.print("Key: PRESS "); Serial.println(ps2Keyboard_CurrentBuffer);
          if (ps2Keyboard_onKeyPress != NULL)
            ps2Keyboard_onKeyPress(ps2Keyboard_CurrentBuffer, ps2Keyboard_extend, kbd_read_extra());
          
          ps2Keyboard_CharBuffer = ps2Keyboard_CurrentBuffer;
        }
      }
    }
    ps2Keyboard_CurrentBuffer = 0;
    ps2Keyboard_BitPos = 0;
  }
}

PS2Keyboard::PS2Keyboard() {
  // nothing to do here	
}

void PS2Keyboard::begin(int clockPin, int clockInt, int dataPin) {
  // Prepare the global variables
  ps2Keyboard_ClockPin      = clockPin;
  ps2Keyboard_DataPin       = dataPin;
  ps2Keyboard_CurrentBuffer = 0;
  ps2Keyboard_CharBuffer    = 0;
  ps2Keyboard_BitPos     = 0;
  ps2Keyboard_shift         = false;
  ps2Keyboard_ctrl          = false;
  ps2Keyboard_alt           = false;
  ps2Keyboard_extend        = false;
  ps2Keyboard_release       = false;
  ps2Keyboard_caps_lock     = false;
  ps2Keyboard_prodikeysFunction = 0x00;
  cmd_in_progress           = false;
  cmd_count                 = 0;
  cmd_value                 = 0;
  cmd_ack_value             = 1;

  // initialize the pins
  pinMode(clockPin, INPUT);
  pinMode(dataPin, INPUT);
  signalRebooting();
  
  /*
  // CLK=HIGH, DATA=HIGH: "PC is ready to take data"
  digitalWrite(clockPin, HIGH);
  digitalWrite(dataPin, HIGH);
  signalReady();
  */

  attachInterrupt(clockInt, ps2interrupt, FALLING);
#if 0
  // Global Enable INT1 interrupt
  EIMSK |= ( 1 << INT1);
  // Falling edge triggers interrupt
  EICRA |= (0 << ISC10) | (1 << ISC11);
#endif
  //signalReady();
  
}

void PS2Keyboard::signalRebooting() {
  // CLK=LOW, DATA=LOW: "PC is rebooting"
  digitalWrite(ps2Keyboard_DataPin, LOW);
  digitalWrite(ps2Keyboard_ClockPin, LOW);
}
void PS2Keyboard::signalReady() {
  // CLK=HIGH, DATA=HIGH: "PC is ready to take data"
  digitalWrite(ps2Keyboard_DataPin, HIGH);
  delay(1);
  digitalWrite(ps2Keyboard_ClockPin, HIGH);
  
}
void PS2Keyboard::signalBusy() {
  // CLK=LOW, DATA=HIGH: "PC is busy"
  digitalWrite(ps2Keyboard_DataPin, HIGH);
  digitalWrite(ps2Keyboard_ClockPin, LOW);
  
}

bool PS2Keyboard::available() {
  return ps2Keyboard_CharBuffer != 0;
}

// This routine allows a calling program to see if other other keys are held
// down when a character is received: ie <ctrl>, <alt>, <shift> or <shift_lock>
// Note that this routine must be called after available() has returned true,
// but BEFORE read(). The read() routine clears the buffer and allows another
// character to be received so these bits can change anytime after the read().
byte PS2Keyboard::read_extra() {
  return kbd_read_extra();
}

byte PS2Keyboard::read() {
  byte result;

  // read the raw data from the keyboard
  result = ps2Keyboard_CharBuffer;

  // Use a switch for the code to character conversion.
  // This is fast and actually only uses 4 bytes per simple line
  switch (result) {
  case 0x1C: result = 'a'; break;
  case 0x32: result = 'b'; break;
  case 0x21: result = 'c'; break;
  case 0x23: result = 'd'; break;
  case 0x24: result = 'e'; break;
  case 0x2B: result = 'f'; break;
  case 0x34: result = 'g'; break;
  case 0x33: result = 'h'; break;
  case 0x43: result = 'i'; break;
  case 0x3B: result = 'j'; break;
  case 0x42: result = 'k'; break;
  case 0x4B: result = 'l'; break;
  case 0x3A: result = 'm'; break;
  case 0x31: result = 'n'; break;
  case 0x44: result = 'o'; break;
  case 0x4D: result = 'p'; break;
  case 0x15: result = 'q'; break;
  case 0x2D: result = 'r'; break;
  case 0x1B: result = 's'; break;
  case 0x2C: result = 't'; break;
  case 0x3C: result = 'u'; break;
  case 0x2A: result = 'v'; break;
  case 0x1D: result = 'w'; break;
  case 0x22: result = 'x'; break;
  case 0x35: result = 'y'; break;
  case 0x1A: result = 'z'; break;

    // note that caps lock only used on a-z
  case 0x41: result = ps2Keyboard_shift? '<' : ','; break;
  case 0x49: result = ps2Keyboard_shift? '>' : '.'; break;
  case 0x4A: result = ps2Keyboard_shift? '?' : '/'; break;
  case 0x54: result = ps2Keyboard_shift? '{' : '['; break;
  case 0x5B: result = ps2Keyboard_shift? '}' : ']'; break;
  case 0x4E: result = ps2Keyboard_shift? '_' : '-'; break;
  case 0x55: result = ps2Keyboard_shift? '+' : '='; break;
  case 0x29: result = ' '; break;

  case 0x45: result = ps2Keyboard_shift? ')' : '0'; break;
  case 0x16: result = ps2Keyboard_shift? '!' : '1'; break;
  case 0x1E: result = ps2Keyboard_shift? '@' : '2'; break;
  case 0x26: result = ps2Keyboard_shift? '\xf5' : '3'; break;
  case 0x25: result = ps2Keyboard_shift? '$' : '4'; break;
  case 0x2E: result = ps2Keyboard_shift? '%' : '5'; break;
  case 0x36: result = ps2Keyboard_shift? '^' : '6'; break;
  case 0x3D: result = ps2Keyboard_shift? '&' : '7'; break;
  case 0x3E: result = ps2Keyboard_shift? '*' : '8'; break;
  case 0x46: result = ps2Keyboard_shift? '(' : '9'; break;

  case 0x0D: result = '\t'; break;
  case 0x5A: result = '\n'; break;
  case 0x66: result = PS2_KC_BKSP;  break;
  case 0x69: result = ps2Keyboard_extend? PS2_KC_END   : '1'; break;
  case 0x6B: result = ps2Keyboard_extend? PS2_KC_LEFT  : '4'; break;
  case 0x6C: result = ps2Keyboard_extend? PS2_KC_HOME  : '7'; break;
  case 0x70: result = ps2Keyboard_extend? PS2_KC_INS   : '0'; break;
  case 0x71: result = ps2Keyboard_extend? PS2_KC_DEL   : '.'; break;
  case 0x72: result = ps2Keyboard_extend? PS2_KC_DOWN  : '2'; break;
  case 0x73: result = '5'; break;
  case 0x74: result = ps2Keyboard_extend? PS2_KC_RIGHT : '6'; break;
  case 0x75: result = ps2Keyboard_extend? PS2_KC_UP    : '8'; break;
  case 0x76: result = PS2_KC_ESC; break;
  case 0x79: result = '+'; break;
  case 0x7A: result = ps2Keyboard_extend? PS2_KC_PGDN  : '3'; break;
  case 0x7B: result = '-'; break;
  case 0x7C: result = '*'; break;
  case 0x7D: result = ps2Keyboard_extend? PS2_KC_PGUP  : '9'; break;

  case 0x58:
    // setting the keyboard lights is done here. Ideally it would be done
    // in the interrupt routine itself and the key codes associated wth
    // caps lock key presses would never be passed on as characters.
    // However it would make the interrupt routine very messy with lots
    // of extra state associated with the control of a caps_lock
    // key code causing a cmd byte to transmit, causing an ack_byte to
    // be received, then a data byte to transmit. Much easier done here.
    // The downside, however, is that the light going on or off at the
    // right time relies on the calling program to be checking for
    // characters on a regular basis. If the calling program stops
    // polling for characters at any point pressing the caps lock key
    // will not change the state of the caps lock light while polling
    // is not happening.
    result = ps2Keyboard_caps_lock? PS2_KC_CLON : PS2_KC_CLOFF;
    if (ps2Keyboard_caps_lock) kbd_set_lights(4);
    else                       kbd_set_lights(0);
    break;

    // Reset the shift counter for unexpected values, to get back into sink
    // This allows for hot plugging a keyboard in and out
  default:  delay(500); // but wait a bit in case part way through a shift
            ps2Keyboard_BitPos  = 0;
            ps2Keyboard_shift      = false;
            ps2Keyboard_ctrl       = false;
            ps2Keyboard_alt        = false;
            ps2Keyboard_extend     = false;
            ps2Keyboard_release    = false;
            ps2Keyboard_caps_lock  = false;
            ps2Keyboard_prodikeysFunction = 0x00;
  } // end switch(result)

  // shift a-z chars here (less code than in the switch statement)
  if (((result>='a') && (result<='z')) &&
      ((ps2Keyboard_shift && !ps2Keyboard_caps_lock) ||
       (!ps2Keyboard_shift && ps2Keyboard_caps_lock))) {
    result = result + ('A'-'a');
  }

  // done with the character
  ps2Keyboard_CharBuffer = 0;

  return(result);
}

void PS2Keyboard::setLEDs(uint8_t m) {
  kbd_set_lights(m);
}


void PS2Keyboard::resetAndWait() {
  
  //sendCommandAndWaitAck(0xff);
  sendCommand(0xff);
  delayMicroseconds(100);
  //cmd_ack_byte_ok = false;
  //delay(1);
  //cmd_ack_byte_ok = false;
  //while (!cmd_ack_byte_ok) ; // ack byte from keyboard sets this flag
  
  //reset();

  //@FIXME!
  //delay(200);
  //cmd_in_progress = false;
  //cmd_ack_value = 1;
}
void PS2Keyboard::sendCommand(byte val) {
  kbd_send_command(val, false);
}
void PS2Keyboard::sendCommandAndWait(byte val) {
  while (cmd_in_progress) ;
  kbd_send_command(val, false);
  while (cmd_in_progress) ;
}
void PS2Keyboard::sendCommandAndWaitAck(byte val) {
  cmd_ack_byte_ok = false;   // initialise the ack byte flag
  kbd_send_command(val, true);    // send the command byte
  while (!cmd_ack_byte_ok) ; // ack byte from keyboard sets this flag
}

void PS2Keyboard::setCallbacks(
    onData_t onData,
    onError_t onError,
    onKeyPress_t onKeyPress,
    onKeyRelease_t onKeyRelease,
    
    onProdikeysKeyPress_t onProdikeysKeyPress,
    onProdikeysKeyRelease_t onProdikeysKeyRelease,
    onProdikeysMidiPress_t onProdikeysMidiPress,
    onProdikeysMidiRelease_t onProdikeysMidiRelease,
    onProdikeysPitchBend_t onProdikeysPitchBend
  ) {
  ps2Keyboard_onData = onData;
  ps2Keyboard_onError = onError;
  ps2Keyboard_onKeyPress = onKeyPress;
  ps2Keyboard_onKeyRelease = onKeyRelease;
  
  ps2Keyboard_onProdikeysKeyPress = onProdikeysKeyPress;
  ps2Keyboard_onProdikeysKeyRelease = onProdikeysKeyRelease;
  ps2Keyboard_onProdikeysMidiPress = onProdikeysMidiPress;
  ps2Keyboard_onProdikeysMidiRelease = onProdikeysMidiRelease;
  ps2Keyboard_onProdikeysPitchBend = onProdikeysPitchBend;
}
