/*
 * Original firmware
 * =================
 *
 * Flickering LED Candle
 * (C) 2012 Akafugu Corporation
 *
 *
 * Alternative firmware with 8-bit PWM
 * ===================================
 *
 * 2013 - Robert Spitzenpfeil
 *
 *
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 */

/*
 * Pinout:
 * PB0 (pin 5) - Button (INT0 interrupt)
 * PB1 (pin 6) - LED (PWM)
 * PB2 (pin 7) - LED
 * PB3 (pin 2) - LED
 * PB4 (pin 3) - LED
 * PB5 (pin 1) - Reset
 *
 * 1k resistor per pin. Suitable for running from a 3V CR2032 cell
 *
 *
 * Inspired by similar LED candle projects on Instructables:
 *
 * http://www.instructables.com/id/YAFLC-Yet-Another-Flickering-LED-Candle/
 * http://www.instructables.com/id/Realistic-Fire-Effect-with-Arduino-and-LEDs/
 *
 */

#define DEBUG

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <stdbool.h>

#define sbi(var, mask)   ((var) |= (uint8_t)(1 << mask))
#define cbi(var, mask)   ((var) &= (uint8_t)~(1 << mask))

// button control
volatile bool button_held = false;

// defines for on time (in seconds)
#define ON_1H 3600
#define ON_2H 15
#define ON_3H 10800
#define ON_4H 14400
#define ON_5h 18000

// how long to stay on
uint16_t off_timer = ON_2H;

// for hold to turn off feature
volatile bool off_flag = false;	// has the button been held long enough to turn off?

// for flickering intensity
volatile bool fast_flicker = true;

// random number seed (will give same flicker sequence each time)
uint32_t lfsr = 0xbeefcace;

uint32_t rand(void)
{
	// http://en.wikipedia.org/wiki/Linear_feedback_shift_register
	// Galois LFSR: taps: 32 31 29 1; characteristic polynomial: x^32 + x^31 + x^29 + x + 1 */
	lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
	return lfsr;
}

uint8_t brightness = 0;

void set_brightness(uint8_t value)
{
	brightness = value;
}

volatile uint8_t sleep_requested = 0;

// Enter sleep mode: Wake on INT0 interrupt
void do_sleep(void)
{
	cli();
	GIMSK |= _BV(PCIE);	// Pin change interrupt enabled
	#ifdef DEBUG
	PCMSK |= _BV(PCINT2);	// PB2 
	#else
	PCMSK |= _BV(PCINT0);	// PB0
	#endif
	sleep_requested = 0;
	PORTB &= ~_BV(PB0);
	set_sleep_mode(_BV(SM1));	// Power down sleep mode (sets MCUCR)
	sei();
	sleep_mode();
	off_timer = ON_2H;
}

// Interrupt signal PCINT0: On PB0 (PB2 for DEBUG, different board)
ISR(PCINT0_vect)
{
	PCMSK = 0;
	GIFR |= _BV(PCIF); // clear PCIF
}

// Flicker by randomly setting pins PB1~PB4 on and off
void flicker(void)
{
	uint32_t r;
	uint8_t temp1;
	uint8_t temp2;

	r = rand();
	temp1 = (uint8_t) (r >> 28);

	if (off_flag) {
		set_brightness(0);
	} else {
		temp2 = (uint8_t) r;	// use lowermost 8 bits to set values for the LED pins
		set_brightness(0);
		_delay_loop_2(temp1 << 7);
		set_brightness(temp2);
		_delay_loop_2(temp1 << 7);
	}
}

ISR(TIM0_COMPA_vect)
{
	static uint16_t bitmask = 0b0000000000000001;
	static uint16_t sec_counter = 0;
	static uint16_t PWM_cycle_counter = 0;
	static uint16_t button_held_counter = 0;
	uint8_t OCR0A_next;

	// increase system-clock
	CLKPR = _BV(CLKPCE);
	CLKPR = 0;		// set system-clock prescaler to 1 --> full 9.6MHz

	if ( brightness & bitmask ) {
		#ifdef DEBUG
			PORTB |= (0b00000001); // PB0
		#else
			PORTB |= 0b00011110;	// PB1...PB4
		#endif
	} else {
		#ifdef DEBUG
			PORTB &= ~(0b00000001); // PB0
		#else
			PORTB &= ~(0b00011110);	// PB1...PB4
		#endif
	}

	OCR0A_next = bitmask;
	bitmask = bitmask << 1;

	if (bitmask == _BV(9)) {
		bitmask = 0x0001;
		OCR0A_next = 2;
		PWM_cycle_counter++;
	}

	OCR0A = OCR0A_next;	// when to run next time
	TCNT0 = 0;		// clear timer to compensate for code runtime above
	TIFR0 = _BV(OCF0A);	// clear interrupt flag to kill any erroneously pending interrupt in the queue

	if (PWM_cycle_counter == 293) {	// count seconds
		sec_counter++;
		PWM_cycle_counter = 0;
	}

	#ifdef DEBUG
	if (PINB & _BV(PB2)) { 
	#else
	if (PINB & _BV(PB0)) {	// button is not held
	#endif
		if (button_held_counter > 0) {	// button up detected
			fast_flicker = !fast_flicker;
		}

		button_held = false;
		button_held_counter = 0;
	} else {		// button is held
		button_held = true;
		button_held_counter++;
	}

	// time to go to sleep?
	if (off_timer != 0 && sec_counter >= off_timer && !button_held) {
		sec_counter = 0;
		off_flag = false;
		button_held_counter = 0;
		sleep_requested = 1;
		return;
	}
	// holding the button for 3 seconds turns the device off
	if (button_held_counter == 7032) {
		off_flag = true;
		return;
	}
	// when off flag is set, wait for key up to go to sleep (otherwise the interrupt pin will wake the device up again)
	if (off_flag && !button_held) {
		off_flag = false;
		sleep_requested = 1;
	}
	
	// decrease system-clock
	CLKPR = _BV(CLKPCE);
	CLKPR = _BV(CLKPS2);	// set system-clock prescaler to 1/16 --> 9.6MHz / 16 = 600kHz
}

void main(void) __attribute__ ((noreturn));

void main(void)
{
	// set system-clock prescaler to 1/16
	// 9.6MHz RC-oscillator --> 600kHz system-clock
	CLKPR = _BV(CLKPCE);
	CLKPR = _BV(CLKPS2);

	// configure TIMER0
	TCCR0A = _BV(WGM01);	// set CTC mode
	TCCR0B = ( _BV(CS01) );	// set prescaler to 8
	// enable COMPA isr
	TIMSK0 = _BV(OCIE0A);
	// set top value for TCNT0
	OCR0A = 10;		// just some start value

	#ifdef DEBUG
		// pull-up on PB2
		PORTB |= _BV(PB2);
		// PB0 as output, rest as input
		DDRB = 0b00000001;
		PORTB |= _BV(PB0);
	#else
		// pull-up on PB0
		PORTB |= _BV(PB0);
		// PB0 as input, PB1...PB4 as output
		DDRB = 0b00011110;
	#endif

	// globally enable interrupts
	// necessary to wake up from sleep via pin-change interrupt
	sei();

	while (1) {
		if(sleep_requested == 1) {
			do_sleep();
		}
		flicker();
	}
}