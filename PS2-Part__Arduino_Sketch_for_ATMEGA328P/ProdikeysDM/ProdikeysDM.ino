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
TODO:

-> LUFA_MIDIandKeyboard

 */

/*
PS2 desc  color   Arduino
1   DATA  brown   DIO4
2   n/c
3   GND   orange  GND
4   VCC   yellow  +5V
5   CLK   green   DIO3
6   n/c

 */

#define USE_ARDUINO_UNO
//#define USE_DIGISPARK


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


// Internal protocol between application CPU and LUFA CPU

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

void lufa_send_keyboard_press(uint8_t c, uint8_t m) {
  lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_PRESS, c, m);
}
void lufa_send_keyboard_release(uint8_t c, uint8_t m) {
  lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_RELEASE, c, m);
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


/*
void serialEvent() {
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
          Serial.print("App got status:"); Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data2, HEX);
          break;
          
        case LUFA_COMMAND_PING:
          // Respond with pong
          //Serial.print("App got ping:"); Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data2, HEX);
          //Serial.print("App sends pong:"); Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data1, HEX);
          lufa_send_pong(lufa_data1, lufa_data2);
          break;
        
        case LUFA_COMMAND_PONG:
          Serial.print("App got pong:"); Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data2, HEX);
          break;
      }
      break;
      
    case LUFA_INTF_MIDI:
    
      //@TODO: Handle MIDI events coming IN from the USB cpu/host
      // e.g. output to SoftwareSerial MIDI OUT jack
      Serial.print("App got MIDI:"); Serial.print(lufa_command, HEX); Serial.print(",");Serial.print(lufa_data1, HEX); Serial.print(","); Serial.println(lufa_data2, HEX);
      
      break;
      
    case LUFA_INTF_KEYBOARD:
      // Handle keyboard commands (e.g. LED on/off) coming IN from the USB cpu/host
      switch(lufa_command) {
        case LUFA_COMMAND_KEYBOARD_LEDS:
          Serial.print("App got LEDs:"); Serial.println(lufa_data1, HEX);
          keyboard.setLEDs(lufa_data1);
          break;
      }
      break;
    
    default:
      // Unknown interface!
      ;
  }
}




// Callbacks of PS2Keyboard
void keyboard_onData(uint8_t b) {
  // Show each and every PS2 uint8_t (beware! This blocks the interrupt!)
  
  Serial.println(b, HEX);
  //delayMicroseconds(100);
}
void keyboard_onError(uint8_t e) {
  // Some protocol error has occured
  Serial.print("Error:");
  Serial.println((char)e);
}

void keyboard_onKeyPress(uint8_t c, uint8_t m) {
  // Keyboard key press
  //Serial.print("KeyPress"); Serial.println(c, HEX);
  lufa_send_keyboard_press(c, m);
}
void keyboard_onKeyRelease(uint8_t c) {
  // Keyboard key release
  //Serial.print("KeyRelease"); Serial.println(c, HEX);
  lufa_send_keyboard_release(c, keyboard.read_extra());
}
void keyboard_onProdikeysKeyPress(uint8_t k, uint8_t m) {
  // Prodikeys special key press
  
  Serial.print("FuncPress"); Serial.println(k, HEX);
  switch(k) {
    case 0x7b:  // "CREATIVE"
      break;
    case 0x7c:  // "Prodikeys"
      Serial.print("App sends ping:"); Serial.print(k, HEX); Serial.print(","); Serial.println(m, HEX);
      lufa_send_ping(k, m);
      break;
    case 0x7d:  // "Favorit"
      break;
  }
  
  //lufa_send_keyboard_press(c, m);
}
void keyboard_onProdikeysKeyRelease(uint8_t k) {
  // Prodikeys special key release
  Serial.print("FuncRelease"); Serial.println(k, HEX);
  //lufa_send_keyboard_release(c, keyboard.read_extra());
}
void keyboard_onProdikeysMidiPress(uint8_t n, uint8_t velocity) {
  // Prodikeys MIDI note press
  //Serial.print("MidiPress");  Serial.print(n);  Serial.print(",");  Serial.println(velocity);
  
  //lufa_send_midi_noteOn(n, velocity);
  lufa_send_midi_message(144 + 0, n, velocity);
}
void keyboard_onProdikeysMidiRelease(uint8_t n) {
  // Prodikeys MIDI note release
  //Serial.print("MidiRelease");  Serial.println(n);
  
  //lufa_send_midi_noteOff(n);
  lufa_send_midi_message(128 + 0, n, 64);
}
void keyboard_onProdikeysPitchBend(int8_t pitch) {
  // Prodikeys pitch bend
  //Serial.print("PitchBend"); Serial.println(pitch);
  
  uint16_t v = 8192 + (pitch * 8192 / 64);
  lufa_send_midi_message(224 + 0, v >> 7, v % 128);
}

void keyboard_reset() {
  // Do the whole thing

  keyboard.setCallbacks(
    keyboard_onData,
    keyboard_onError,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL);
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
  
  //delay(200);
  keyboard.begin(PS2_PIN_CLOCK, PS2_INT_CLOCK, PS2_PIN_DATA);
  delay(10);
  keyboard.signalBusy();
  delay(10);
  keyboard.signalReady();
  delay(10);
  //Serial.print("Resetting...");
  //keyboard.reset();
  keyboard.resetAndWait();
  //Serial.println("AA");
  delay(500);
  
  
  // Simulate idle
  Serial.print("Idle...");
  for(int i = 0; i < 5; i++) {
    Serial.print("[F1]");
    keyboard.sendCommand(0xF1);
    // Should answer 0xEF
    delay(200);
  }
  

  // Send init sequence
  Serial.println("Init ProdiKeys DM...");

  /*
  Serial.print("[E4][70]");
  keyboard.sendCommandAndWaitAck(0xE4);
  keyboard.sendCommandAndWaitAck(0x70);
  //Serial.print(".");
  delay(80);
  //Serial.print("[EA]");
  //keyboard.sendCommand(0xEA);
  //delay(100);
  // 0x35, 0x42, 0x33, 0x3D, 0x35, 0x7f, 0x35, 0x05

  Serial.print("[E9]"); keyboard.sendCommandAndWaitAck(0xE9); delay(1);
  Serial.print("[E9]"); keyboard.sendCommandAndWaitAck(0xE9); delay(1);

  
  //Serial.print("Starting ProdiKeys DM...");
  / *
  // Settings? (Pitch bend range, octave, shortcuts...?)
  
  Serial.print("[E4][5C]");
  keyboard.sendCommandAndWaitAck(0xE4);
  keyboard.sendCommandAndWaitAck(0x5C);
  //Serial.print(".");
  //delay(10);
  Serial.print("(FD)"); keyboard.sendCommand(0xFD);
  Serial.print("(A6)"); keyboard.sendCommand(0xA6);// delay(1);
  
  Serial.print("(8F)"); keyboard.sendCommand(0x8F);// delay(1);
  
  Serial.print("(A6)"); keyboard.sendCommand(0xA6);// delay(1);
  Serial.print("(AD)"); keyboard.sendCommand(0xAD);// delay(1);

  Serial.print("(A6)"); keyboard.sendCommand(0xA6);// delay(1);
  Serial.print("(A1)"); keyboard.sendCommand(0xA1);// delay(1);

  Serial.print("(A6)"); keyboard.sendCommand(0xA6);// delay(1);
  Serial.print("(AD)"); keyboard.sendCommand(0xAD);// delay(1);
  
  
  delay(10);
  Serial.print("(EC)"); keyboard.sendCommand(0xEC); delay(1);
  //...
  * /
  
  Serial.print("[E9]"); keyboard.sendCommandAndWaitAck(0xE9); delay(2);
  */  
  
  //delay(500);
  
  // Minimal sequence to activate MIDI
  Serial.print("[E8]");
  keyboard.sendCommand(0xE8); delay(50);  // should answer 0x23
  
  Serial.print("[E8]");
  keyboard.sendCommand(0xE8); delay(50);  // should answer 0x23

  //Serial.print("Start [E4][04]");
  //keyboard.sendCommandAndWaitAck(0xE4);
  //keyboard.sendCommandAndWaitAck(0x04);
  
  //Serial.print("Wake [E4][05]");
  //keyboard.sendCommandAndWait(0xE4);
  //keyboard.sendCommandAndWait(0x05);

  Serial.print("Resume [E4][15]");
  keyboard.sendCommandAndWaitAck(0xE4);
  keyboard.sendCommandAndWaitAck(0x15);

  //Serial.print("Shutdown [E4][16]");
  //keyboard.sendCommandAndWaitAck(0xE4);
  //keyboard.sendCommandAndWaitAck(0x16);
  
  delay(100);

  keyboard.setCallbacks(
    NULL, //keyboard_onData,
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
  Serial.println("PS2KeyboardExt2");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  keyboard_reset();
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("Ready.");
  
}

#define is_printable(c) (!(c&0x80))   // don't print if top bit is set



void loop() {

  if (Serial.available()) {
    uint8_t c = Serial.read();
    lufa_receive(c);
  }
  
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
