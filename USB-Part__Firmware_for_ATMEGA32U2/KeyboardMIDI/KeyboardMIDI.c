/*
						 LUFA Library
		 Copyright (C) Dean Camera, 2017.

	dean [at] fourwalledcubicle [dot] com
					 www.lufa-lib.org
*/

/*
	Copyright 2017  Dean Camera (dean [at] fourwalledcubicle [dot] com)
	Copyright 2010  Matthias Hullin (lufa [at] matthias [dot] hullin [dot] net)

	Permission to use, copy, modify, distribute, and sell this
	software and its documentation for any purpose is hereby granted
	without fee, provided that the above copyright notice appear in
	all copies and that both that the copyright notice and this
	permission notice and warranty disclaimer appear in supporting
	documentation, and that the name of the author not be used in
	advertising or publicity pertaining to distribution of the
	software without specific, written prior permission.

	The author disclaims all warranties with regard to this
	software, including all implied warranties of merchantability
	and fitness.  In no event shall the author be liable for any
	special, indirect or consequential damages or any damages
	whatsoever resulting from loss of use, data or profits, whether
	in an action of contract, negligence or other tortious action,
	arising out of or in connection with the use or performance of
	this software.
*/

/** \file

This is my attempt to create a combined HID-Keyboard and MIDI USB device.

This file contains the implementation based on pure LUFA.
It is meant to be flashed onto an ATMEGA32u2 (USB driver chip) of an Arduino UNO to make it report as a HID+MIDI device.

Then, using the hardware USART, the main ATMEGA328P processor can give instructions to the ATMEGA32u2 using high-level commands without having to take care of USB level stuff.

2019-06-20 Bernhard "HotKey" Slawik

 */

#include "KeyboardMIDI.h"

/** LUFA MIDI Class driver interface configuration and state information. This structure is
 *  passed to all MIDI Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_MIDI_Device_t Keyboard_MIDI_Interface = {
	.Config = {
		.StreamingInterfaceNumber = INTERFACE_ID_AudioStream,
		.DataINEndpoint           = {
			.Address          = MIDI_STREAM_IN_EPADDR,
			.Size             = MIDI_STREAM_EPSIZE,
			.Banks            = 1,
		},
		.DataOUTEndpoint          = {
			.Address          = MIDI_STREAM_OUT_EPADDR,
			.Size             = MIDI_STREAM_EPSIZE,
			.Banks            = 1,
		},
	},
};

/** Buffer to hold the previously generated Keyboard HID report, for comparison purposes inside the HID class driver. */
static uint8_t PrevKeyboardHIDReportBuffer[sizeof(USB_KeyboardReport_Data_t)];

/** LUFA HID Class driver interface configuration and state information. This structure is
 *  passed to all HID Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */
USB_ClassInfo_HID_Device_t Keyboard_HID_Interface = {
	.Config = {
		.InterfaceNumber              = INTERFACE_ID_Keyboard,
		.ReportINEndpoint             =
			{
				.Address              = KEYBOARD_EPADDR,
				.Size                 = KEYBOARD_EPSIZE,
				.Banks                = 1,
			},
		.PrevReportINBuffer           = PrevKeyboardHIDReportBuffer,
		.PrevReportINBufferSize       = sizeof(PrevKeyboardHIDReportBuffer),
	},
};



//UART:
// Glue when using an intermediate atmega8u2 for USB and a second ATMEGA as application processor
// Circular buffer to hold data from the host before it is sent to the device via the serial port.
RingBuff_t USBtoUSART_Buffer;



#ifdef SERIAL_USE_ISR
// Circular buffer to hold data from the serial port before it is sent to the host.
RingBuff_t USARTtoUSB_Buffer;
#endif

//MIDI:
//MIDI_EventPacket_t midiEvent;
struct {
	uint8_t command;
	uint8_t channel;
	uint8_t data2;
	uint8_t data3;
} midiMsg;
//int led1_ticks = 0;
//int led2_ticks = 0;
//#define LED_ON_TICKS 10000  /* Number of ticks to leave LEDs on */

//Keyboard:
//@TODO: Handle multiple key presses!
uint8_t Keyboard_keyCode = 0;
uint8_t Keyboard_modifiers = 0;

void MIDI_send(uint8_t midiCommand, uint8_t midiData1, uint8_t midiData2);


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
	
	//Serial.write(data, sizeof(data));
	//Serial.flush(); // Arduino 1.0: Wait for data to be transmitted
	for (int i = 0; i < sizeof(data); i++) {
		RingBuffer_Insert(&USBtoUSART_Buffer, data[i]);
	}
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

/*
void lufa_send_keyboard_press(uint8_t c, uint8_t m) {
	lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_PRESS, c, m);
}
void lufa_send_keyboard_release(uint8_t c, uint8_t m) {
	lufa_send(LUFA_INTF_KEYBOARD, LUFA_COMMAND_KEYBOARD_RELEASE, c, m);
}
*/
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
void lufa_parse() {
	// Handle commands coming from the application CPU to the USB Host (e.g. type a key, enter midi note)
	
	switch(lufa_intf) {
		case LUFA_INTF_INTERNAL:
			
			switch(lufa_command) {
				case LUFA_COMMAND_STATUS:
					//@TODO: Show status
					//Serial.print("Status:"); Serial.print(lufa_data1, HEX); Serial.print(", "); Serial.println(lufa_data2, HEX);
					break;
					
				case LUFA_COMMAND_PING:
					// Respond with pong
					lufa_send_pong(lufa_data1, lufa_data2);
					break;
			}
			break;
		
		case LUFA_INTF_MIDI:
			MIDI_send(lufa_command, lufa_data1, lufa_data2);
			break;
		
		case LUFA_INTF_KEYBOARD:
			switch(lufa_command) {
				//@TODO: Handle multiple key presses!
				case LUFA_COMMAND_KEYBOARD_PRESS:
					//Keyboard_press(lufa_data1, lufa_data2);
					Keyboard_keyCode = lufa_data1;
					Keyboard_modifiers = lufa_data2;
					break;
				case LUFA_COMMAND_KEYBOARD_RELEASE:
					Keyboard_keyCode = 0;
					Keyboard_modifiers = lufa_data2;
					break;
				case LUFA_COMMAND_KEYBOARD_LEDS:
					// Application CPU wants the system LEDs to change
					//@TODO: Generate a HID Report for that?
					// The application CPU needs to send actual Lock-Key presses in order to trigger that
					break;
			}
			
			break;
		
		default:
			// Unknown interface!
			;
	}
}

void lufa_receive(uint8_t b) {
	switch (lufa_pos) {
		case 0:
			if (b == LUFA_MAGIC) {
				// New packet begins
				lufa_pos = 1;
			} else {
				// Ignore / discard
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

////////////////////////////////////////


//UART:
//http://fourwalledcubicle.com/files/LUFA/Doc/120219/html/group___group___serial___a_v_r8.html#ga696c089404bd217a831466f063f961c1

void UART_setup() {
	
	// Init ringbuffers
	RingBuffer_InitBuffer(&USBtoUSART_Buffer);
	
	#ifdef SERIAL_USE_ISR
	RingBuffer_InitBuffer(&USARTtoUSB_Buffer);
	#endif
	
	
	// Hardware Initialization
	
	#ifndef SERIAL_USE_ISR
	// Without ISR
	Serial_Init(SERIAL_BAUD, true);
	#endif
	
	#ifdef SERIAL_USE_ISR
	// Do everything manually
	
	//cli();
	
	// Start the flush timer so that overflows occur rapidly to push received bytes to the USB interface
	TCCR0B = (1 << CS02);
	
	// Must turn off USART before reconfiguring it, otherwise incorrect operation may occur
	UCSR1B = 0;
	UCSR1A = 0;
	UCSR1C = 0;
	
	UBRR1  = SERIAL_2X_UBBRVAL(SERIAL_BAUD);
	
	UCSR1C = ((1 << UCSZ11) | (1 << UCSZ10));
	UCSR1A = (1 << U2X1);
	UCSR1B = ((1 << RXCIE1) | (1 << TXEN1) | (1 << RXEN1));	// RXIE1 = enable receive interrupt
	
	//sei();
	#endif
	
	lufa_pos = 0;
}

void UART_loop() {
	// Get data from USART (application processor)
	
	#ifdef SERIAL_USE_ISR
	// Get UART data from Ring buffer (filled by ISR)
	RingBuff_Count_t BufferCount = RingBuffer_GetCount(&USARTtoUSB_Buffer);
	
	// See if we have UART data
	while (BufferCount >= 1) {
		// Parse "lufa" serial protocol
		RingBuff_Data_t c = RingBuffer_Remove(&USARTtoUSB_Buffer);
		lufa_receive(c);
		BufferCount--;
	}
	#endif
	
	#ifndef SERIAL_USE_ISR
	// Polling UART (not using ISR) - you MUST NOT enable ISR via RXEN1"
	//if (Serial_IsCharReceived()) {
		//uint8_t c = UDR1;
		int c = Serial_ReceiveByte();	// This will disconnect the USB device!
		if (c >= 0) {
			
			//@FIXME: Just for debugging
			//RingBuffer_Insert(&USBtoUSART_Buffer, c);
			//Serial_SendByte(c);
			
			lufa_receive((uint8_t)c);
		}
	//}
	#endif
	
	// any data to send to main processor?
	if (!(RingBuffer_IsEmpty(&USBtoUSART_Buffer))) {
		//Serial_TxByte(RingBuffer_Remove(&USBtoUSART_Buffer));
		Serial_SendByte(RingBuffer_Remove(&USBtoUSART_Buffer));
	}
}

//MIDI:
void MIDI_loop() {
	
	//MIDI:
	/*
	//TODO: Have fun here!
	static uint8_t PrevJoystickStatus;
	
	uint8_t MIDICommand = 0;
	uint8_t MIDIPitch;
	
	// Get current joystick mask, XOR with previous to detect joystick changes
	uint8_t JoystickStatus  = Joystick_GetStatus();
	uint8_t JoystickChanges = (JoystickStatus ^ PrevJoystickStatus);
	
	// Get board button status - if pressed use channel 10 (percussion), otherwise use channel 1
	uint8_t Channel = ((Buttons_GetStatus() & BUTTONS_BUTTON1) ? MIDI_CHANNEL(10) : MIDI_CHANNEL(1));
	
	if (JoystickChanges & JOY_LEFT) {
		MIDICommand = ((JoystickStatus & JOY_LEFT)? MIDI_COMMAND_NOTE_ON : MIDI_COMMAND_NOTE_OFF);
		MIDIPitch   = 0x3C;
	}
	if (JoystickChanges & JOY_UP) {
		MIDICommand = ((JoystickStatus & JOY_UP)? MIDI_COMMAND_NOTE_ON : MIDI_COMMAND_NOTE_OFF);
		MIDIPitch   = 0x3D;
	}
	if (JoystickChanges & JOY_RIGHT) {
		MIDICommand = ((JoystickStatus & JOY_RIGHT)? MIDI_COMMAND_NOTE_ON : MIDI_COMMAND_NOTE_OFF);
		MIDIPitch   = 0x3E;
	}
	if (JoystickChanges & JOY_DOWN) {
		MIDICommand = ((JoystickStatus & JOY_DOWN)? MIDI_COMMAND_NOTE_ON : MIDI_COMMAND_NOTE_OFF);
		MIDIPitch   = 0x3F;
	}
	if (JoystickChanges & JOY_PRESS) {
		MIDICommand = ((JoystickStatus & JOY_PRESS)? MIDI_COMMAND_NOTE_ON : MIDI_COMMAND_NOTE_OFF);
		MIDIPitch   = 0x3B;
	}
	if (MIDICommand) {
		MIDI_EventPacket_t MIDIEvent = (MIDI_EventPacket_t)
			{
				.Event       = MIDI_EVENT(0, MIDICommand),
				.Data1       = MIDICommand | Channel,
				.Data2       = MIDIPitch,
				.Data3       = MIDI_STANDARD_VELOCITY,
			};
		MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, &MIDIEvent);
		MIDI_Device_Flush(&Keyboard_MIDI_Interface);
	}
	PrevJoystickStatus = JoystickStatus;
	
	MIDI_EventPacket_t ReceivedMIDIEvent;
	while (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &ReceivedMIDIEvent)) {
		if ((ReceivedMIDIEvent.Event == MIDI_EVENT(0, MIDI_COMMAND_NOTE_ON)) && (ReceivedMIDIEvent.Data3 > 0))
			LEDs_SetAllLEDs(ReceivedMIDIEvent.Data2 > 64 ? LEDS_LED1 : LEDS_LED2);
		else
			LEDs_SetAllLEDs(LEDS_NO_LEDS);
	}
	*/
	
	// See if there is a MIDI event from USB
	MIDI_EventPacket_t midiEventIn;
	if (MIDI_Device_ReceiveEventPacket(&Keyboard_MIDI_Interface, &midiEventIn)) {
		RingBuff_Count_t count = RingBuffer_GetCount(&USBtoUSART_Buffer);
		
		// Room to send a message?
		if ((BUFFER_SIZE - count) >= sizeof(midiMsg)) {
			/*
			//midiMsg.command = midiEvent.Command << 4;
			midiMsg.command = midiEvent.Event << 4;
			midiMsg.channel = (midiEvent.Data1 & 0x0F) + 1;
			midiMsg.data2 = midiEvent.Data2;
			midiMsg.data3 = midiEvent.Data3;
			*/
			
			// Send to application CPU (maye it has a hardware MIDI-OUT jack?)
			lufa_send_midi_message(midiEventIn.Data1, midiEventIn.Data2, midiEventIn.Data3);
			
			/*
			// Turn on the RX led and start its timer
			LEDs_TurnOnLEDs(LEDS_LED2);
			led2_ticks = LED_ON_TICKS;
			*/
			
		} else {
			// Turn on the RX led and leave it on to indicate the buffer is full and the sketch is not reading it fast enough.
			//LEDs_TurnOnLEDs(LEDS_LED2);
		}
		
		// if there's no room in the serial buffer the message gets dropped
	}
}

MIDI_EventPacket_t midiEventOut;
void MIDI_send(uint8_t midiCommand, uint8_t midiData1, uint8_t midiData2) {
	// Build a midi event to send via USB
	
	//midiEvent.CableNumber = 0;
	//midiEvent.Command = midiMsg.command >> 4;
	midiEventOut.Event = midiCommand >> 4;	//midiMsg.command >> 4;
	
	midiEventOut.Data1 = midiCommand;	//(midiMsg.command & 0xF0) | ((midiMsg.channel-1) & 0x0F);
	midiEventOut.Data2 = midiData1;
	midiEventOut.Data3 = midiData2;
	
	MIDI_Device_SendEventPacket(&Keyboard_MIDI_Interface, &midiEventOut);
	MIDI_Device_Flush(&Keyboard_MIDI_Interface);
	
	// Turn on the TX led and starts its timer
	//LEDs_TurnOnLEDs(LEDS_LED1);
	//led1_ticks = LED_ON_TICKS;
}

void Keyboard_loop() {
	//Keyboard
}





// LUFA: Configures the board hardware and chip peripherals for the demo's functionality.
void SetupHardware(void) {
#if (ARCH == ARCH_AVR8)
	
	// Disable watchdog if enabled by bootloader/fuses
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	// Disable clock division
	clock_prescale_set(clock_div_1);
	
#elif (ARCH == ARCH_XMEGA)
	// Start the PLL to multiply the 2MHz RC oscillator to 32MHz and switch the CPU core to run from it
	XMEGACLK_StartPLL(CLOCK_SRC_INT_RC2MHZ, 2000000, F_CPU);
	XMEGACLK_SetCPUClockSource(CLOCK_SRC_PLL);

	// Start the 32MHz internal RC oscillator and start the DFLL to increase it to 48MHz using the USB SOF as a reference
	XMEGACLK_StartInternalOscillator(CLOCK_SRC_INT_RC32MHZ);
	XMEGACLK_StartDFLL(CLOCK_SRC_INT_RC32MHZ, DFLL_REF_INT_USBSOF, F_USB);

	PMIC.CTRL = PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
#endif
	
	// Hardware Initialization
	LEDs_Init();
	
	UART_setup();
	
	//Joystick_Init();
	//Buttons_Init();
	//Dataflash_Init();
	USB_Init();
	
	// Check if the Dataflash is working, abort if not
	//if (!(DataflashManager_CheckDataflashOperation())) {
	//	LEDs_SetAllLEDs(LEDMASK_USB_ERROR);
	//	for(;;);
	//}
	
	// Clear Dataflash sector protections, if enabled
	//DataflashManager_ResetDataflashProtections();
}

void setup() {
	
	SetupHardware();
	wdt_reset();
	
	//LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
	
	GlobalInterruptEnable();
	//sei();
}


void loop() {
	
	UART_loop();
	
	//MassStorage:
	// MS_Device_USBTask(&Disk_MS_Interface);
	
	//MIDI:
	MIDI_loop();
	MIDI_Device_USBTask(&Keyboard_MIDI_Interface);
	
	//Keyboard:
	Keyboard_loop();
	HID_Device_USBTask(&Keyboard_HID_Interface);
	
	/*
	// Turn off the Tx LED when the tick count reaches zero
	if (led1_ticks) {
		led1_ticks--;
		if (led1_ticks == 0) {
			LEDs_TurnOffLEDs(LEDS_LED1);
		}
	}
	
	// Turn off the RX LED when the tick count reaches zero
	if (led2_ticks) {
		led2_ticks--;
		if (led2_ticks == 0) {
			LEDs_TurnOffLEDs(LEDS_LED2);
		}
	}
	*/
	
	//General:
	USB_USBTask();
}



/** Main program entry point. This routine contains the overall program flow, including initial
 *  setup of all components and the main program loop.
 */
int main(void) {
	
	setup();
	
	lufa_send_status('O', 'K');
	
	for (;;) {
		loop();
	}
}



//General:
// Event handler for the library USB Connection event.
void EVENT_USB_Device_Connect(void) {
	LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

// Event handler for the library USB Disconnection event.
void EVENT_USB_Device_Disconnect(void) {
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

// Event handler for the library USB Configuration Changed event.
void EVENT_USB_Device_ConfigurationChanged(void) {
	bool ConfigSuccess = true;
	
	ConfigSuccess &= HID_Device_ConfigureEndpoints(&Keyboard_HID_Interface);
	ConfigSuccess &= MIDI_Device_ConfigureEndpoints(&Keyboard_MIDI_Interface);
	
	// Enable Start-Of-Frame event
	USB_Device_EnableSOFEvents();
	
	LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

// Event handler for the library USB Control Request reception event.
void EVENT_USB_Device_ControlRequest(void) {
	//MS_Device_ProcessControlRequest(&Disk_MS_Interface);
	MIDI_Device_ProcessControlRequest(&Keyboard_MIDI_Interface);
	HID_Device_ProcessControlRequest(&Keyboard_HID_Interface);
}

/** Mass Storage class driver callback function the reception of SCSI commands from the host, which must be processed.
 *
 *  \param[in] MSInterfaceInfo  Pointer to the Mass Storage class interface configuration structure being referenced
 */
/*
bool CALLBACK_MS_Device_SCSICommandReceived(USB_ClassInfo_MS_Device_t* const MSInterfaceInfo)
{
	bool CommandSuccess;

	LEDs_SetAllLEDs(LEDMASK_USB_BUSY);
	CommandSuccess = SCSI_DecodeSCSICommand(MSInterfaceInfo);
	LEDs_SetAllLEDs(LEDMASK_USB_READY);

	return CommandSuccess;
}
*/

// Event handler for the USB device Start Of Frame event.
void EVENT_USB_Device_StartOfFrame(void) {
		HID_Device_MillisecondElapsed(&Keyboard_HID_Interface);
}

/** HID class driver callback function for the creation of HID reports to the host.
 *
 *  \param[in]     HIDInterfaceInfo  Pointer to the HID class interface configuration structure being referenced
 *  \param[in,out] ReportID    Report ID requested by the host if non-zero, otherwise callback should set to the generated report ID
 *  \param[in]     ReportType  Type of the report to create, either HID_REPORT_ITEM_In or HID_REPORT_ITEM_Feature
 *  \param[out]    ReportData  Pointer to a buffer where the created report should be stored
 *  \param[out]    ReportSize  Number of bytes written in the report (or zero if no report is to be sent)
 *
 *  \return Boolean \c true to force the sending of the report, \c false to let the library determine if it needs to be sent
 */
bool CALLBACK_HID_Device_CreateHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo, uint8_t* const ReportID, const uint8_t ReportType, void* ReportData, uint16_t* const ReportSize) {
	
	// Structure is: [mod] [0] [k0][k1][k2][k3][k4][k5]
	USB_KeyboardReport_Data_t* KeyboardReport = (USB_KeyboardReport_Data_t*)ReportData;
	
	//TODO: Have fun here!
	//if (Keyboard_keyCode != 0) {
		// Press a key
		//KeyboardReport->Modifier = HID_KEYBOARD_MODIFIER_LEFTSHIFT;
		//KeyboardReport->KeyCode[0] = HID_KEYBOARD_SC_A;
		
		KeyboardReport->Modifier = Keyboard_modifiers;
		KeyboardReport->KeyCode[0] = Keyboard_keyCode;
	//}
	
	
	*ReportSize = sizeof(USB_KeyboardReport_Data_t);
	return false;
}

/** HID class driver callback function for the processing of HID reports from the host.
 *
 *  \param[in] HIDInterfaceInfo  Pointer to the HID class interface configuration structure being referenced
 *  \param[in] ReportID    Report ID of the received report from the host
 *  \param[in] ReportType  The type of report that the host has sent, either HID_REPORT_ITEM_Out or HID_REPORT_ITEM_Feature
 *  \param[in] ReportData  Pointer to a buffer where the received report has been stored
 *  \param[in] ReportSize  Size in bytes of the received HID report
 */
void CALLBACK_HID_Device_ProcessHIDReport(USB_ClassInfo_HID_Device_t* const HIDInterfaceInfo, const uint8_t ReportID, const uint8_t ReportType, const void* ReportData, const uint16_t ReportSize) {
	
	uint8_t* LEDReport = (uint8_t*)ReportData;
	
	// Report to application CPU
	lufa_send_keyboard_leds(*LEDReport);
	
	/*
	uint8_t  LEDMask   = LEDS_NO_LEDS;
	
	if (*LEDReport & HID_KEYBOARD_LED_NUMLOCK)
		LEDMask |= LEDS_LED1;
	
	if (*LEDReport & HID_KEYBOARD_LED_CAPSLOCK)
		LEDMask |= LEDS_LED3;
	
	if (*LEDReport & HID_KEYBOARD_LED_SCROLLLOCK)
		LEDMask |= LEDS_LED4;
	
	LEDs_SetAllLEDs(LEDMask);
	*/
}



#ifdef SERIAL_USE_ISR
/** ISR to manage the reception of data from the serial port, placing received bytes into a circular buffer
 *  for later transmission to the host.
 */

ISR(USART1_RX_vect, ISR_BLOCK) {
	uint8_t ReceivedByte = UDR1;
	
	//if (USB_DeviceState == DEVICE_STATE_Configured)
	//if ((USB_DeviceState == DEVICE_STATE_Configured) && !RingBuffer_IsFull(&USARTtoUSB_Buffer)) {
	if (!RingBuffer_IsFull(&USARTtoUSB_Buffer)) {
		RingBuffer_Insert(&USARTtoUSB_Buffer, ReceivedByte);
	}
}
#endif


/** Event handler for the CDC Class driver Line Encoding Changed event.
 *
 *  \param[in] CDCInterfaceInfo  Pointer to the CDC class interface configuration structure being referenced
 */
/*
void EVENT_CDC_Device_LineEncodingChanged(USB_ClassInfo_CDC_Device_t* const CDCInterfaceInfo) {
	uint8_t ConfigMask = 0;
	
	switch (CDCInterfaceInfo->State.LineEncoding.ParityType) {
		case CDC_PARITY_Odd:
			ConfigMask = ((1 << UPM11) | (1 << UPM10));
			break;
		case CDC_PARITY_Even:
			ConfigMask = (1 << UPM11);
			break;
	}
	
	if (CDCInterfaceInfo->State.LineEncoding.CharFormat == CDC_LINEENCODING_TwoStopBits)
		ConfigMask |= (1 << USBS1);
	
	switch (CDCInterfaceInfo->State.LineEncoding.DataBits) {
		case 6:
			ConfigMask |= (1 << UCSZ10);
			break;
		case 7:
			ConfigMask |= (1 << UCSZ11);
			break;
		case 8:
			ConfigMask |= ((1 << UCSZ11) | (1 << UCSZ10));
			break;
	}
	
	// Keep the TX line held high (idle) while the USART is reconfigured
	PORTD |= (1 << 3);
	
	// Must turn off USART before reconfiguring it, otherwise incorrect operation may occur
	UCSR1B = 0;
	UCSR1A = 0;
	UCSR1C = 0;
	
	// Set the new baud rate before configuring the USART
	UBRR1  = SERIAL_2X_UBBRVAL(CDCInterfaceInfo->State.LineEncoding.BaudRateBPS);
	
	// Reconfigure the USART in double speed mode for a wider baud rate range at the expense of accuracy
	UCSR1C = ConfigMask;
	UCSR1A = (1 << U2X1);
	UCSR1B = ((1 << RXCIE1) | (1 << TXEN1) | (1 << RXEN1));
	
	// Release the TX line after the USART has been reconfigured
	PORTD &= ~(1 << 3);
}

*/