// Include the custom PS2Keyboard with Prodikeys additions
#include "PS2Keyboard.h"

// Simple test program for new PS2Keyboard library
// Connect a PS2 keyboard to pins 3 & 4 (CLK and DATA respectively) and supply 5V to the keyboard
// For examples, see here: http://playground.arduino.cc/ComponentLib/Ps2mouse
// or                here: http://www.beyondlogic.org/keyboard/keybrd.htm
// That second article is a great place to start if you want to understand whats going on
//
// When you've compiled the code and uploaded it to the board, start a serial monitor at
// 9600bd.  Then press keys on your PS2 keyboard (the one connected to Arduino, not the one
// connected to your computer!) Try using <shift>, <ctrl> and <alt> keys
// and check that the caps_lock key sets the caps_lock light.
// Pressing <esc> key should reset the keyboard and you should see all 3 lights go on briefly.

/*

How to connect:

PS2 desc  color   Arduino
1   DATA  brown   DIO4
2   n/c
3   GND   orange  GND
4   VCC   yellow  +5V
5   CLK   green   DIO3
6   n/c

!!!!  BUGS!

  bug MIDI-note must be shifted +2
  bug note-off is weird! Some notes stay sustained, some stop immediately!
  bug navigation keys (cursors/home/end) are mapped to Num Pad numbers!
  bug properly set the LEDs
  + support the hardware "octave + -" buttons
  + support the hardware "sustain" button
  + map the volume wheel (currently report as "b" and "c")
  + map the media keys (currently: rewind=q, ffw=p, play/pause=g, stop=j, mute=d)
  + handle the harware "creative" key (currently no report)
  + handle the harware "prodikeys" key (currently no report)
  + handle the harware "favorit" key (currently no report)
  + handle the 3 harware programmable key  (currently no reports)
  + handle the harware "note" key (currently no report)




 */

#define USE_ARDUINO_UNO
//#define USE_DIGISPARK

//@FIXME: When NOT using debug, timings are off and the protocol freezes at keyboard_reset!
#define USE_SERIAL_DEBUG  // Uncomment to output debug info to serial port (this will interfere with the USB data stream if using LUFA USB controller!)


#ifdef USE_ARDUINO_UNO
  #define PS2_PIN_DATA 4
  #define PS2_PIN_CLOCK  3  // Must be interrupt capable! (Check your microcontroller)
  #define PS2_INT_CLOCK 1 // digitalPinToInterrupt(PS2_PIN_CLOCK)
  #define LED_PIN 13
  //#define SERIAL_BAUD 57600
  #define SERIAL_BAUD 115200
#endif
#ifdef USE_DIGISPARK
  #define PS2_PIN_DATA 0
  #define PS2_PIN_CLOCK  2  // P2/PB2=INT0 Must be interrupt capable! (Check your microcontroller)
  #define PS2_INT_CLOCK  0  // P2/PB2=INT0 https://digistump.com/board/index.php?topic=1060.0
  #define LED_PIN 1
#endif

PS2Keyboard keyboard;


// Debug stuff

#ifdef USE_SERIAL_DEBUG
  const char DEBUG_HEX_DIGITS[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
  #define DEBUG(s) Serial.print(s)
  #define DEBUG_HEX(s) Serial.print(DEBUG_HEX_DIGITS[s >> 4]); Serial.print(DEBUG_HEX_DIGITS[s & 0xf]) //Serial.print(s, HEX)
  #define DEBUGLN Serial.println
  #define DEBUGFLUSH Serial.flush

  #define DUMP_MAX 32
  volatile int dump_ofs = 0;
  volatile uint8_t dump_data[DUMP_MAX];

  void dump_add(uint8_t c) {
    dump_data[dump_ofs++] = c;
  }
  
  void dump_flush() {
    // Flush dump buffer
    if (dump_ofs > 0) {
      DEBUG("<< ");
      for (int i = 0; i < dump_ofs; i++) {
        if (i > 0) DEBUG(" ");
        DEBUG_HEX(dump_data[i]);
      }
      dump_ofs = 0;
      
      DEBUGLN();
      DEBUGFLUSH();
    }
  }
#else
  // Do not include debug
  #define DEBUG(s) ;
  #define DEBUG_HEX(s) ;
  #define DEBUGLN(s) ;
  #define DEBUGFLUSH ;

  #define dump_add(v) ;
  #define dump_flush() ;
#endif

int midi_transpose = 0;
int midi_channel = 0;
int midi_patch = 0;

// Internal protocol between application CPU and USB-Controller (running the custom USB LUFA firmware)
#define LUFA_MAGIC 0xAA
#define LUFA_INTF_INTERNAL 'i'
#define LUFA_INTF_KEYBOARD 'K'
#define LUFA_INTF_MIDI 'M'
#define LUFA_COMMAND_STATUS '='
#define LUFA_COMMAND_PING '?'
#define LUFA_COMMAND_PONG '!'
#define LUFA_COMMAND_ERROR 'E'
#define LUFA_COMMAND_KEYBOARD_PRESS 'P'
#define LUFA_COMMAND_KEYBOARD_RELEASE 'R'
#define LUFA_COMMAND_KEYBOARD_LEDS 'L'
//#define LUFA_COMMAND_MIDI_NOTE_ON 'N'
//#define LUFA_COMMAND_MIDI_NOTE_OFF 'n'

void lufa_send(uint8_t intf, uint8_t command, uint8_t data1, uint8_t data2) {
  uint8_t data[1+4];
  data[0] = LUFA_MAGIC;
  
  data[1] = intf;
  data[2] = command;
  data[3] = data1;
  data[4] = data2;
  Serial.write(data, sizeof(data));
  //Serial.flush(); // Arduino 1.0: Wait for data to be transmitted
}

void lufa_send_status(uint8_t data1, uint8_t data2) {
  lufa_send(LUFA_INTF_INTERNAL, LUFA_COMMAND_STATUS, data1, data2);
}
void lufa_send_error(uint8_t data1, uint8_t data2) {
  lufa_send(LUFA_INTF_INTERNAL, LUFA_COMMAND_ERROR, data1, data2);
}
void lufa_send_ping(uint8_t data1, uint8_t data2) {
  lufa_send(LUFA_INTF_INTERNAL, LUFA_COMMAND_PING, data1, data2);
}
void lufa_send_pong(uint8_t data1, uint8_t data2) {
  lufa_send(LUFA_INTF_INTERNAL, LUFA_COMMAND_PONG, data1, data2);
}

void lufa_send_keyboard_press(uint8_t usbScanCode, uint8_t usbMod) {
  lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_PRESS, usbScanCode, usbMod);
}
void lufa_send_keyboard_release(uint8_t usbScanCode, uint8_t usbMod) {
  lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_RELEASE, usbScanCode, usbMod);
}
void lufa_send_keyboard_leds(uint8_t m) {
  lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_LEDS, m, 0);
}

void lufa_send_midi_message(uint8_t command, uint8_t data1, uint8_t data2) {
  lufa_send(LUFA_INTF_MIDI, command, data1, data2);
}
/*
void lufa_send_midi_noteOn(uint8_t note, uint8_t velocity) {
  lufa_send(LUFA_INTF_MIDI, LUFA_COMMAND_MIDI_NOTE_ON, note, velocity);
}
void lufa_send_midi_noteOff(uint8_t note) {
  lufa_send(LUFA_INTF_MIDI, LUFA_COMMAND_MIDI_NOTE_OFF, note, 0);
}
*/



uint8_t lufa_pos = 0;
uint8_t lufa_intf = 0;
uint8_t lufa_command = 0;
uint8_t lufa_data1 = 0;
uint8_t lufa_data2 = 0;
void lufa_receive(uint8_t b) {
  switch (lufa_pos) {
    case 0:
      if (b == LUFA_MAGIC) {
        // New packet
        lufa_pos = 1;
      }
      break;
    case 1:
      lufa_intf = b;
      lufa_pos = 2;
      break;
    case 2:
      lufa_command = b;
      lufa_pos = 3;
      break;
    case 3:
      lufa_data1 = b;
      lufa_pos = 4;
      break;
    case 4:
      lufa_data2 = b;
      // End of packet!
      lufa_parse();
      lufa_pos = 0;
      break;
    default:
      lufa_pos = 0;
  }
}
void lufa_parse() {
  // Handle packet!
  switch(lufa_intf) {
    case LUFA_INTF_INTERNAL:
      switch(lufa_command) {
        case LUFA_COMMAND_STATUS:
          //@TODO: Show status
          DEBUG("App got status:"); DEBUG_HEX(lufa_data1); DEBUG(","); DEBUG_HEX(lufa_data2); DEBUGLN();
          break;
          
        case LUFA_COMMAND_PING:
          // Respond with pong
          //Serial.print("App got ping:"); Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data2, HEX);
          //Serial.print("App sends pong:"); Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data1, HEX);
          lufa_send_pong(lufa_data1, lufa_data2);
          break;
        
        case LUFA_COMMAND_PONG:
          DEBUG("App got pong:"); DEBUG_HEX(lufa_data1); DEBUG(","); DEBUG_HEX(lufa_data2); DEBUGLN();
          break;
      }
      break;
      
    case LUFA_INTF_MIDI:
    
      //@TODO: Handle MIDI events coming IN from the USB cpu/host
      // e.g. output to SoftwareSerial MIDI OUT jack
      DEBUG("App got MIDI:"); DEBUG_HEX(lufa_command); DEBUG(","); DEBUG_HEX(lufa_data1); DEBUG(","); DEBUG_HEX(lufa_data2); DEBUGLN();
      
      break;
      
    case LUFA_INTF_KEYBOARD:
      
      // Handle keyboard commands (e.g. LED on/off) coming IN from the USB cpu/host
      switch(lufa_command) {
        case LUFA_COMMAND_KEYBOARD_LEDS:
          DEBUG("App got LEDs:"); DEBUG_HEX(lufa_data1); DEBUGLN();
          
          keyboard.setLEDs(usbLedsToPs2(lufa_data1));
          break;
      }
      break;
    
    default:
      // Unknown interface!
      ;
  }
}


// USB Scan Codes: http://www.fourwalledcubicle.com/files/LUFA/Doc/140928/html/_h_i_d_class_common_8h.html
#define USB_ALPHA_OFS (0x04 - 'a')


uint8_t ps2ScanCodeToUsb(uint8_t ps2ScanCode, bool ps2Ext, uint8_t ps2Mod) {
  uint8_t result = 0;

  // More USB scan codes:
  // Name        USB  PS/2
  // CAPS_LOCK  0x39
  // F1...F12   0x3A..0x45
  // NUM_LOCK   0x53  
  // SCR_LOCK   0x47
  // PrintScr   0x46
  // Pause      0x48
  // POWER      0x66
  // APPLICAT.  0x65

  // See USB scan codes: https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2
  
  switch(ps2ScanCode) {
    case 0x05: result = (ps2Ext ? 0x00 : 0x3a); break;  // ps2Ext ? -- : F1
    case 0x06: result = (ps2Ext ? 0x00 : 0x3b); break;  // ps2Ext ? -- : F2
    case 0x04: result = (ps2Ext ? 0x00 : 0x3c); break;  // ps2Ext ? -- : F3
    case 0x0c: result = (ps2Ext ? 0x00 : 0x3d); break;  // ps2Ext ? -- : F4
    case 0x03: result = (ps2Ext ? 0x00 : 0x3e); break;  // ps2Ext ? -- : F5
    case 0x0b: result = (ps2Ext ? 0x00 : 0x3f); break;  // ps2Ext ? -- : F6
    case 0x83: result = (ps2Ext ? 0x00 : 0x40); break;  // ps2Ext ? -- : F7
    case 0x0a: result = (ps2Ext ? 0x00 : 0x41); break;  // ps2Ext ? -- : F8
    case 0x01: result = (ps2Ext ? 0x00 : 0x42); break;  // ps2Ext ? -- : F9
    case 0x09: result = (ps2Ext ? 0x00 : 0x43); break;  // ps2Ext ? -- : F10
    case 0x78: result = (ps2Ext ? 0x00 : 0x44); break;  // ps2Ext ? -- : F11
    case 0x07: result = (ps2Ext ? 0x00 : 0x45); break;  // ps2Ext ? -- : F12
    case 0x0e: result = (ps2Ext ? 0x00 : 0x32); break;  // ps2Ext ? -- : ^
    
    case 0x1C: result = 'a' + USB_ALPHA_OFS; break; // a = 0x04
    case 0x32: result = (ps2Ext ? 0x80 /*ed*/ : ('b' + USB_ALPHA_OFS)); break; // b = 0x05, volumeup=0x80/ed
    case 0x21: result = (ps2Ext ? 0x81 /*ee*/ : ('c' + USB_ALPHA_OFS)); break; // c = 0x06, volumedown=0x81/ee)
    case 0x23: result = (ps2Ext ? 0x7f /*ef*/ : ('d' + USB_ALPHA_OFS)); break; // MUTE=0x7f/ef / d
    case 0x24: result = 'e' + USB_ALPHA_OFS; break;
    case 0x2B: result = 'f' + USB_ALPHA_OFS; break;
    case 0x34: result = (ps2Ext ? 0xe8 : ('g' + USB_ALPHA_OFS)); break; // PlayPause / g
    case 0x33: result = 'h' + USB_ALPHA_OFS; break;
    case 0x43: result = 'i' + USB_ALPHA_OFS; break;
    case 0x3B: result = (ps2Ext ? 0xf3 /*0xe9 / */ : ('j' + USB_ALPHA_OFS)); break; // Stop / j
    case 0x42: result = 'k' + USB_ALPHA_OFS; break;
    case 0x4B: result = 'l' + USB_ALPHA_OFS; break;
    case 0x3A: result = 'm' + USB_ALPHA_OFS; break;
    case 0x31: result = 'n' + USB_ALPHA_OFS; break;
    case 0x44: result = 'o' + USB_ALPHA_OFS; break;
    case 0x4D: result = (ps2Ext ? 0xeb : ('p' + USB_ALPHA_OFS)); break; // Next / p
    case 0x15: result = (ps2Ext ? 0xea : ('q' + USB_ALPHA_OFS)); break; // Previous / q
    case 0x2D: result = 'r' + USB_ALPHA_OFS; break;
    case 0x1B: result = 's' + USB_ALPHA_OFS; break;
    case 0x2C: result = 't' + USB_ALPHA_OFS; break;
    case 0x3C: result = 'u' + USB_ALPHA_OFS; break;
    case 0x2A: result = 'v' + USB_ALPHA_OFS; break;
    case 0x1D: result = 'w' + USB_ALPHA_OFS; break;
    case 0x22: result = 'x' + USB_ALPHA_OFS; break;
    case 0x35: result = 'y' + USB_ALPHA_OFS; break;
    case 0x1A: result = 'z' + USB_ALPHA_OFS; break;

    case 0x41: result = 0x36; break;  // , <
    case 0x49: result = 0x37; break;  // . >
    case 0x4A: result = 0x38; break;  // / ?
    case 0x4c: result = 0x33; break;  // DE: ö
    case 0x52: result = 0x34; break;  // DE: ä
    case 0x54: result = 0x2f; break;  // { [  // DE: "ü"
    case 0x5B: result = 0x30; break;  // } ]
    case 0x4E: result = 0x2d; break;  // - _  // DE: ß
    case 0x55: result = 0x2e; break;  // = +
    case 0x29: result = 0x2c; break;  // SPACE
  
    case 0x45: result = 0x27; break;  // 0
    case 0x16: result = 0x1e; break;  // 1
    case 0x1E: result = 0x1F; break;  // 2
    case 0x1f: result = (ps2Ext ? 0xe3 : 0x00); break;  // ps2Ext ? Win_left : ---
    case 0x27: result = (ps2Ext ? 0xe7 : 0x00); break;  // ps2Ext ? Win_right : ---
    case 0x26: result = 0x20; break;  // 3
    case 0x25: result = 0x21; break;  // 4
    case 0x2E: result = 0x22; break;  // 5
    case 0x36: result = 0x23; break;  // 6
    case 0x3D: result = 0x24; break;  // 7
    case 0x3E: result = 0x25; break;  // 8
    case 0x46: result = 0x26; break;  // 9
  
    case 0x0D: result = 0x2b; break;  // TAB
    case 0x5A: result = (ps2Ext ? 0x58 : 0x28); break;  // ps2Ext ? KP_ENTER : ???ENTER \n
    case 0x66: result = 0x2A; break;  // Backspace
    
    
    // Num Pad
    case 0x69: result = 0x59; break;  //ps2Ext ? PS2_KC_END   : '1';
    case 0x6B: result = (ps2Ext ? 0x50 : 0x5c); break;  //ps2Ext ? PS2_KC_LEFT  : '4';
    case 0x6C: result = (ps2Ext ? 0x4a : 0x5f); break;  //ps2Ext ? PS2_KC_HOME  : '7';
    case 0x70: result = (ps2Ext ? 0x49 : 0x62); break;  //ps2Ext ? PS2_KC_INS   : KP'0';
    case 0x71: result = (ps2Ext ? 0x4c : 0x63); break;  //ps2Ext ? PS2_KC_DEL   : '.';
    case 0x72: result = (ps2Ext ? 0x51 : 0x5a); break;  //ps2Ext ? PS2_KC_DOWN  : '2';
    case 0x73: result = 0x5d; break;  //'5'; break;
    case 0x74: result = (ps2Ext ? 0x4f : 0x5e); break;  //ps2Ext ? PS2_KC_RIGHT : '6';
    case 0x75: result = (ps2Ext ? 0x52 : 0x60); break;  //ps2Ext ? PS2_KC_UP    : '8';
    case 0x76: result = 0x29; break;  //ESC
    case 0x79: result = 0x57; break;  //'+'; break;
    case 0x7A: result = (ps2Ext ? 0x4e : 0x5b); break;  //ps2Ext ? PS2_KC_PGDN  : '3';
    case 0x7B: result = 0x56; break;  //'-'; break;
    case 0x7C: result = (ps2Ext ? 0x46 : 0x55); break;  //ps2Ext ? SYSRQ : '*'; break;
    case 0x7D: result = (ps2Ext ? 0x4b : 0x61); break;  //ps2Ext ? PS2_KC_PGUP  : '9';
    case 0x7e: result = (ps2Ext ? 0x00 : 0x47); break;  //ps2Ext ? ---  : SCROLL_LOCK;

    case 0x77: result = (ps2Ext ? 0x53 : 0x53); break;  // ps2Ext ? --- : NUM_LOCK

    case 0x11: result = (ps2Ext ? 0xe2 : 0xe6); break;  // ps2Ext ? Alt_right_gr : Alt_left
    case 0x12: result = (ps2Ext ? 0x00 : 0xe1); break;  // ps2Ext ? --- : Shift_left
    case 0x59: result = (ps2Ext ? 0x00 : 0xe5); break;  // ps2Ext ? --- : Shift_right
    case 0x14: result = (ps2Ext ? 0xe4 : 0xe0); break;  // ps2Ext ? Ctrl_right : Ctrl_left
    
    case 0x2f: result = (ps2Ext ? 0x76 : 0x00); break;  // ps2Ext ? ContextMenu : ---
    case 0x61: result = (ps2Ext ? 0x65 /* compose */ : 0x00); break;  // ps2Ext ? SUSTAIN! : ---
    case 0x60: result = (ps2Ext ? 0x00 : 0x00); break;  // ps2Ext ? SUSTAIN! : ---
    
    case 0x62:  // ps2Ext ? Octave+ : ---
      if (ps2Ext) {
        if (ps2Mod == 4) {  // Shift = channel
          if (midi_channel < 15) midi_channel ++;
        } else
        if (ps2Mod == 2) {  // Alt = Fine
          if (midi_transpose < 127) midi_transpose += 1;
        } else
        if (ps2Mod == 1) {  // Ctrl = Patch
          if (midi_patch < 127) midi_patch++;
          lufa_send_midi_message(0xc0 + midi_channel, midi_patch, 0x00);
        } else {
          // Regular = octave
          if (midi_transpose+12 < 128) midi_transpose += 12;
        }
      }
      //result = (ps2Ext ? 0x00 : 0x00);
      result = 0;
      break;
    case 0x63:  // ps2Ext ? Octave- : ---
      if (ps2Ext) {
        if (ps2Mod == 4) {  // Shift = channel
          if (midi_channel > 0) midi_channel--;
        } else
        if (ps2Mod == 2) {  // Alt = Fine
          if (midi_transpose > -127) midi_transpose --;
        } else
        if (ps2Mod == 1) {  // Ctrl = Patch
          if (midi_patch > 0) midi_patch--;
          lufa_send_midi_message(0xc0 + midi_channel, midi_patch, 0x00);
        } else {
          // Regular = octave
          if (midi_transpose-12 > -127) midi_transpose -= 12;
        }
      }
      //result = (ps2Ext ? 0x00 : 0x00);
      result = 0;
      break;  
    
    /*
    case 0x58:
      // setting the keyboard lights is done here.
      result = ps2Keyboard_caps_lock? PS2_KC_CLON : PS2_KC_CLOFF;
      if (ps2Keyboard_caps_lock) kbd_set_lights(4);
      else                       kbd_set_lights(0);
    break;
    */

  }

  // shift a-z chars here (less code than in the switch statement)
  //if (((result>='a') && (result<='z')) && (ps2Keyboard_shift)) result = result + ('A'-'a');

  return result;
}

uint8_t ps2ModToUsb(uint8_t ps2Mod) {
  uint8_t usbMod = 0;

  /*
PS2:
  1=ctrl
  2=alt
  4=shift
  
#define KEY_MOD_LCTRL  0x01
#define KEY_MOD_LSHIFT 0x02
#define KEY_MOD_LALT   0x04
#define KEY_MOD_LMETA  0x08
#define KEY_MOD_RCTRL  0x10
#define KEY_MOD_RSHIFT 0x20
#define KEY_MOD_RALT   0x40
#define KEY_MOD_RMETA  0x80
   */
  if (ps2Mod & 1) usbMod |= 1;  // LeftCtrl
  if (ps2Mod & 2) usbMod |= 4;  // LeftAlt
  if (ps2Mod & 4) usbMod |= 2;  // LeftShift
  
  return usbMod;
}

uint8_t usbLedsToPs2(uint8_t usbLeds) {
  uint8_t ps2Leds = 0;

  //if (usbLeds & 1) ps2Leds |= ???;  // Num
  if (usbLeds & 2) ps2Leds |= 4;  // Caps
  //if (usbLeds & 4) ps2Leds |= ???;  // Scroll
  
  //if (usbLeds & 8) ps2Leds |= ???;  // Compose
  //if (usbLeds & 16) ps2Leds |= ???;  // Kana
  
  return ps2Leds;
}



void keyboard_send_and_wait(const char *text, uint8_t cmd, bool expectAck) {
  DEBUG(">> ");
  DEBUG_HEX(cmd);
  DEBUG("\t");
  DEBUG(text);
  DEBUGLN("");
  
  
  if (expectAck) {
    keyboard.sendCommandAndWaitAck(cmd);
  } else {
    //keyboard.sendCommandAndWait(cmd);
    keyboard.sendCommand(cmd);
    delay(5);
  }
  
  dump_flush();
}



// Callbacks of PS2Keyboard

void keyboard_onData(uint8_t b) {
  // Show each and every PS2 uint8_t
  // Beware! This blocks the interrupt!!!
  
  //DEBUG_HEX(b);
  
  // Asynchronous dump
  //dump_data[dump_ofs++] = b;
  dump_add(b);
  
  //delayMicroseconds(100);
}
void keyboard_onError(uint8_t e) {
  // Some protocol error has occured
  DEBUG("Error:");
  DEBUGLN((char)e);
  //DEBUG_HEX(e);
  DEBUGLN();
}

void keyboard_onKeyPress(uint8_t ps2ScanCode, bool ps2Ext, uint8_t ps2Mod) {
  // Keyboard key press
  DEBUG("KeyPress");
  DEBUG(": ps2scan=0x"); DEBUG_HEX(ps2ScanCode);
  DEBUG(", ps2Ext=0x"); DEBUG_HEX(ps2Ext);
  DEBUG(", ps2Mod=0x"); DEBUG_HEX(ps2Mod);
  DEBUGLN();
  
  uint8_t usbScanCode = ps2ScanCodeToUsb(ps2ScanCode, ps2Ext, ps2Mod);
  uint8_t usbMod = ps2ModToUsb(ps2Mod);
  lufa_send_keyboard_press(usbScanCode, usbMod);
}
void keyboard_onKeyRelease(uint8_t ps2ScanCode, bool ps2Ext) {
  // Keyboard key release
  //DEBUG("KeyRelease"); DEBUG_HEX(c); DEBUGLN();
  
  uint8_t usbScanCode = ps2ScanCodeToUsb(ps2ScanCode, ps2Ext, 0x00);
  uint8_t usbMod = ps2ModToUsb(keyboard.read_extra());
  lufa_send_keyboard_release(usbScanCode, usbMod);
}
void keyboard_onProdikeysKeyPress(uint8_t k, uint8_t m) {
  // Prodikeys special key press
  
  DEBUG("FuncPress");
  DEBUG(": k=0x"); DEBUG_HEX(k);
  DEBUG(", m=0x"); DEBUG_HEX(m);
  DEBUGLN();
  switch(k) {
    case 0x70:
      DEBUGLN("Programmable 1");
      break;
    case 0x71:
      DEBUGLN("Programmable 2");
      break;
    case 0x72:
      DEBUGLN("Programmable 3");
      break;
      
    case 0x7b:  // "CREATIVE"
      break;
    case 0x7c:  // "Prodikeys"
      DEBUG("App sends ping:"); DEBUG_HEX(k); DEBUG(","); DEBUG_HEX(m); DEBUGLN();
      lufa_send_ping(k, m);
      break;
    case 0x7d:  // "Favorit"
      break;
  }
  
  //lufa_send_keyboard_press(usbScanCode, usbMod);
}
void keyboard_onProdikeysKeyRelease(uint8_t k) {
  // Prodikeys special key release
  DEBUG("FuncRelease k=0x"); DEBUG_HEX(k); DEBUGLN();
  //lufa_send_keyboard_release(usbScanCode, usbMod);
}

void keyboard_onProdikeysMidiPress(uint8_t n, uint8_t velocity) {
  // Prodikeys MIDI note press
  DEBUG("MidiPress n=0x"); DEBUG_HEX(n);
  DEBUG(", v=0x"); DEBUG_HEX(velocity);
  DEBUGLN();
  
  //lufa_send_midi_noteOn(n, velocity);
  lufa_send_midi_message(0x90 + midi_channel, n + midi_transpose, velocity);
}

void keyboard_onProdikeysMidiRelease(uint8_t n, uint8_t velocity) {
  // Prodikeys MIDI note release
  DEBUG("MidiRelease n=0x"); DEBUG_HEX(n); DEBUGLN();

  //uint8_t velocity = 0;
  //lufa_send_midi_noteOff(n);
  lufa_send_midi_message(0x80 + midi_channel, n + midi_transpose, velocity);
}

void keyboard_onProdikeysPitchBend(int8_t pitch) {
  // Prodikeys pitch bend
  DEBUG("PitchBend p="); DEBUG_HEX(pitch); DEBUGLN();
  
  uint16_t v = 8191 + ((int16_t)pitch << 7); // * 8191, /64
  lufa_send_midi_message(0xe0 + midi_channel, v % 128, v >> 7);
}

void keyboard_reset() {
  // Reset PS/2, start keyboard, init Prodikeys
  int i;
  

  // Start off with default callbacks, do not set the Prodikeys-callbacks, YET.
  keyboard.setCallbacks(
    keyboard_onData,
    keyboard_onError,
    
    NULL,
    NULL,
    
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  );
  /*
  keyboard.setCallbacks(
    keyboard_onData,
    keyboard_onError,
    
    keyboard_onKeyPress,
    keyboard_onKeyRelease,
    
    keyboard_onProdikeysKeyPress,
    keyboard_onProdikeysKeyRelease,
    keyboard_onProdikeysMidiPress,
    keyboard_onProdikeysMidiRelease,
    keyboard_onProdikeysPitchBend
  );
  */

  DEBUGLN("keyboard.begin...");
  //delay(200);
  keyboard.begin(PS2_PIN_CLOCK, PS2_INT_CLOCK, PS2_PIN_DATA);

  dump_flush();
  
  DEBUGLN("Busy/Ready...");
  delay(100);
  dump_flush();

  
  //keyboard.signalRebooting();
  //delay(1000);
  //dump_flush();
  
  
  keyboard.signalBusy();
  delay(100);
  dump_flush();
  
  keyboard.signalReady();
  delay(100);
  dump_flush();
  
  
  DEBUGLN(">> Reset, expect FA, AA");
  //keyboard.reset();
  keyboard.resetAndWait();
  //dump_flush();
  
  delay(500);
  dump_flush();
  
  /*
  // Simulate idle (computer is booting)
  DEBUGLN("Cold boot idle...");
  for(i = 0; i < 3; i++) {
    delay(200);
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }
  */

  //keyboard_send_and_wait("BIOS x0: Disable scanning", 0xf5, true);  // expect 0xFA
  
  // System is booting:
  keyboard_send_and_wait("BIOS 1: Read ID (expect FA, AB 83)", 0xf2, true);  // expect FA, AB 83
  delay(10); dump_flush();
  
  keyboard_send_and_wait("BIOS 2: Set LEDs", 0xed, true);  // expect 0xFA
  keyboard_send_and_wait("BIOS 3: LEDs", 0, true);  // 1=ScrollLock, 2=NumLock, 4=CapsLock, expect 0xFA
  
  /*
  keyboard_send_and_wait("BIOS x4a: Set scan...", 0xf0, false);  // expect 0xFE
  keyboard_send_and_wait("BIOS x4b: 02", 0x02, false);  // expect 0xFE
  
  keyboard_send_and_wait("BIOS x5a: Set Typematic", 0xf3, true);  // expect 0xFA
  keyboard_send_and_wait("BIOS x5b: Set Typematic", 0x2b, true);  // expect 0xFA
  keyboard_send_and_wait("BIOS x5c: Set Typematic", 0xf4, true);  // expect 0xFA
  delay(50);
  keyboard_send_and_wait("BIOS x5d: Set Typematic", 0xff, false);  // expect 0xFA,AA
  delay(300); dump_flush();

  
  keyboard_send_and_wait("BIOS x6a: Set Typematic", 0xf3, true);  // expect 0xFA
  keyboard_send_and_wait("BIOS x6b: Set Typematic", 0x20, true);  // expect 0xFA
  
  delay(200);
  keyboard_send_and_wait("BIOS x6c: Set Typematic", 0xfd, true);  // expect 0xFA
  keyboard_send_and_wait("BIOS x6d: Set Typematic", 0x00, true);  // expect 0xFA
  */
  
  /*
  keyboard_send_and_wait("BIOS 4", 0xef, false);  // expect 0xFA !!!!!!!!!!!!
  delay(1);
  keyboard_send_and_wait("BIOS 5", 0x00, false);  // expect 0xFA
  */

  /*
  DEBUGLN("Boot idle...");
  for(i = 0; i < 5; i++) {
    delay(200);
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }

  // Windows
  DEBUGLN("Windows...");
  keyboard_send_and_wait("Win 1", 0xe8, false);  // expect 0x23
  keyboard_send_and_wait("Win 2 reset ", 0xff, false);  // expect 0xFA, AA
  delay(500);
  dump_flush();
  
  keyboard_send_and_wait("Win 3", 0xf3, true);  // expect 0xFA
  keyboard_send_and_wait("Win 4", 0x00, true);  // expect 0xFA
  
  keyboard_send_and_wait("Win 5", 0xed, true);  // expect 0xFA
  keyboard_send_and_wait("Win 6", 0x00, true);  // expect 0xFA
  
  keyboard_send_and_wait("Win 7", 0xed, true);  // expect 0xFA
  keyboard_send_and_wait("Win 8", 0x00, true);  // expect 0xFA

  DEBUGLN("Win idle...");
  for(i = 0; i < 5; i++) {
    delay(200);
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }

  // Desktop
  keyboard_send_and_wait("Desk 1", 0xf3, true);  // expect 0xFA
  keyboard_send_and_wait("Desk 2", 0x20, true);  // expect 0xFA
  
  for(i = 0; i < 3; i++) {
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }
  keyboard_send_and_wait("Desk 3", 0xf3, true);  // expect 0xFA
  keyboard_send_and_wait("Desk 4", 0x20, true);  // expect 0xFA

  for(i = 0; i < 3; i++) {
    delay(200);
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }
  */
  
  // ##############
  
  /*
  // Send init sequence
  DEBUGLN("Starting ProdLoad...");

  // When sending the following bytes, the init sequence must be finished correctly or stuff goes bad
  keyboard_send_and_wait("ProdLoad 1", 0xe4, true);  // expect 0xFA
  keyboard_send_and_wait("ProdLoad 2: Init", 0x70, true);  // expect 0xFA
  
  DEBUG("Waiting for 9 bytes of data (~100ms)...");
  // 0xEA, 0x35, 0x42, 0x33, 0x3D, 0x35, 0x7f, 0x35, 0x05
  delay(150);  dump_flush();
  
  
  //delay(100);
  
  keyboard_send_and_wait("ProdLoad 3", 0xE9, true);  // expect 0xFA
  keyboard_send_and_wait("ProdLoad 4", 0xE9, true);  // expect 0xFA

  
  keyboard_send_and_wait("ProdLoad 5", 0xE4, true);  // expect 0xFA
  
  
  //delay(100); dump_flush();
  
  // Settings? (Pitch bend range, octave, shortcuts...?)
  DEBUGLN("CT FUNCTION 0x5c, 0xfd, 0xa6 (mapping?), expect 0x7F");
  
  keyboard.sendCommand(0x5c);
  delayMicroseconds(100);
  //dump_flush();
  keyboard.sendCommand(0xfd);
  delayMicroseconds(100);
  //dump_flush();
  //keyboard_send_and_wait("ProdLoad CTb", 0xa6, false);  // expect 0x7F
  keyboard.sendCommand(0xa6);
  delayMicroseconds(100);
  // expect 0x7F
  delay(50);
  dump_flush();
  
   
  keyboard_send_and_wait("ProdLoad 6", 0x8f, false);  // expect 0x7F
  
  keyboard_send_and_wait("ProdLoad 7", 0xa6, false);  // expect 0x7F

  delay(50);
  DEBUGLN("ProdLoad8: AD, 7F, A6, 7F, A1, 7F, expect 0xff");

  //keyboard_send_and_wait("ProdLoad 8a", 0xad, false);  // expect 0x7F?
  keyboard.sendCommand(0xad);
  delayMicroseconds(100);
  keyboard.sendCommand(0x7f);
  delayMicroseconds(100);

  //keyboard_send_and_wait("ProdLoad 8b", 0xa6, false);  // expect 0x7F?
  keyboard.sendCommand(0xa6);
  delayMicroseconds(100);
  keyboard.sendCommand(0x7f);
  delayMicroseconds(100);

  //keyboard_send_and_wait("ProdLoad 8c", 0xa1, false);  // expect 0x7F?
  keyboard.sendCommand(0xa1);
  delayMicroseconds(100);
  keyboard.sendCommand(0x7f);
  delayMicroseconds(100);

  //keyboard_send_and_wait("ProdLoad 8d", 0xa6, false);  // expect 0x7F?
  keyboard.sendCommand(0xa6);
  delayMicroseconds(100);
  keyboard.sendCommand(0x7f);
  delayMicroseconds(100);

  //keyboard_send_and_wait("ProdLoad 8e", 0xad, false);  // expect 0x7F
  keyboard.sendCommand(0xad);
  delayMicroseconds(100);

  // expect 0xFF
  delay(10);
  dump_flush();
  

  
  DEBUGLN("Waiting for 9 bytes of data (~1 sec)...");
  // expect : 0xEC, 0x33, 0x46, 0x33, 0x1F, 0x35, 0x50, 0x35, 0x16
  // but got: 0xEA, 0x35, 0x24, 0x33, 0x5B, 0x35, 0x7F, 0x33, 0x49
  for(i = 0; i < 6; i++) {
    delay(200);
    dump_flush();
  }

  
  //keyboard_send_and_wait("ProdLoad 9", 0xe9, true);  // expect 0xFA
  
  
  for(i = 0; i < 5; i++) {
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }
  */
  
  //delay(500);
  
  // Minimal sequence to activate MIDI starts here
  keyboard_send_and_wait("Start 0", 0xe8, false);  // expect 0x23
  keyboard_send_and_wait("Start 1", 0xe8, false);  // expect 0x23


  //DEBUGLN(">> Start [E4][04]");
  //keyboard_send_and_wait("Start 2", 0xe4, true);  // expect 0xFA
  //keyboard_send_and_wait("Start 3", 0x04, true);  // expect 0xFA

  
  //DEBUGLN(">> Wake [E4][05]");
  //keyboard.sendCommandAndWait(0xE4);
  //keyboard.sendCommandAndWait(0x05);

  // this one works quite well:
  DEBUGLN(">> Resume [E4][15]");
  keyboard_send_and_wait("Resume 2", 0xe4, true);  // expect 0xFA
  keyboard_send_and_wait("Resume 3", 0x15, true);  // expect 0xFA


  //DEBUGLN(">> Shutdown [E4][16]");
  //keyboard.sendCommandAndWaitAck(0xE4);
  //keyboard.sendCommandAndWaitAck(0x16);

  /*
  for(i = 0; i < 5; i++) {
    delay(100);
    keyboard_send_and_wait("idle", 0xf1, false); // expect 0xfe
    delay(200);
  }
  dump_flush();
  */
  
  
  // Now that the Prodikeys is initialized, we can set the remaining callbacks
  keyboard.setCallbacks(
    //NULL, // disable debugging of RAW data, not to interfere with interrupts
    keyboard_onData,  // NULL, // disable debugging of RAW data, not to interfere with interrupts
    
    keyboard_onError,
    
    keyboard_onKeyPress,
    keyboard_onKeyRelease,
    
    keyboard_onProdikeysKeyPress,
    keyboard_onProdikeysKeyRelease,
    keyboard_onProdikeysMidiPress,
    keyboard_onProdikeysMidiRelease,
    keyboard_onProdikeysPitchBend
  );
}

void setup() {

  Serial.begin(SERIAL_BAUD);
  //DEBUGLN("PS2KeyboardExt2");
  DEBUGLN("ProdikeysDM, based on PS2KeyboardExt2");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  keyboard_reset();
  
  digitalWrite(LED_PIN, LOW);

  DEBUGLN("Ready.");
  
}

#define is_printable(c) (!(c&0x80))   // don't print if top bit is set



void loop() {

  if (Serial.available()) {
    uint8_t c = Serial.read();
    lufa_receive(c);
  }

  dump_flush();
  
  /*
  if(keyboard.available()) {
    Serial.print("KEY:");
    // reading the "extra" bits is optional
    uint8_t   extra = keyboard.read_extra(); // must read extra before reading the character byte
    uint8_t       c = keyboard.read();

    boolean ctrl = extra & 1;  // <ctrl> is bit 0
    boolean  alt = extra & 2;  //  <alt> is bit 1

    if (ctrl) Serial.print('^');
    if (alt)  Serial.print('_');

    if      (c==PS2_KC_UP)      Serial.print("up\n");
    else if (c==PS2_KC_DOWN)    Serial.print("down\n");
    else if (c==PS2_KC_BKSP)    Serial.print("backspace\n");
    else if (c==PS2_KC_ESC)   { Serial.print("escape and reset\n"); keyboard.reset(); }
    else if ( is_printable(c) ) Serial.print((char)c);   // don't print any untrapped special characters
    else Serial.print(c, HEX);
    
    Serial.println();
  }
  */
  

  /*
  // Idle
  Serial.print("idle...");
  keyboard.sendCommand(0xF1);
  delay(1000);
  */ 
}
