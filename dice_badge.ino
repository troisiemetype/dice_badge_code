#include "avr/sleep.h"

const uint16_t SRAM_START = 0x000;		// SRAM start is 0x60, but registers can be used as well, some have undecided reset value.
const uint16_t SRAM_END = 0x25F;		// 0x0DF, 0x15F, 0x25F for Attiny 25, 45, 85 resp.

const uint8_t PIN_LED1 = 0;
const uint8_t PIN_LED2 = 1;
const uint8_t PIN_LED4 = 2;
const uint8_t PIN_LED6 = 3;
const uint8_t PIN_BTN = 4;

// Sleep delay in ms. How long will the dice display its number before to go to sleep.
const uint16_t SLEEP_DELAY = 8000;

uint32_t seed;
uint32_t lastDebounce;
uint32_t lastClic;
bool btnLastState;
bool btnChange;

uint8_t pinState = 0;

enum{
	IDLE = 0,
	DIM_UP,
	DISPLAY,
	DIM_DOWN,
};

// Interrupt service routine.
// simply disable sleep. Program then resumes where the sleep was launch.
ISR(PCINT0_vect){
	MCUCR &= ~(1 << SE);
}

// using fading on leds.
ISR(TIMER1_OVF_vect){
	// Clearing all four leds)
	PORTB &= (0b1111);
}

ISR(TIMER1_COMPA_vect){
	// Set all four leds to their predefined values
	PORTB |= pinState;
}

// Disable unwanted peripherals to save current.
void disablePeripherals(){
	// Disable ADC.
	ADCSRA &= ~(1 << ADEN);
	// Disable analog comparator
	ACSR |= (1 << ACD);
	// Disable brownout detector in sleep modes
	MCUCR |= (1 << BODS) | (1 << BODSE);
	MCUCR |= (1 << BODS);
	MCUCR &= (1 << BODSE);
	// Disable watchdog timer
	WDTCR |= (1 << WDCE);
	WDTCR &= (1 << WDE);
}

void makeSeed(){
	uint8_t *sramData;
	for(uint16_t i = SRAM_START; i < SRAM_END; ++i){
		sramData = i;
		seed ^= (uint32_t)(*sramData) << ((i % 4) * 8);
	}

	randomSeed(seed);
}

void initSystem(){
	disablePeripherals();

	pinMode(PIN_LED1, OUTPUT);
	pinMode(PIN_LED2, OUTPUT);
	pinMode(PIN_LED4, OUTPUT);
	pinMode(PIN_LED6, OUTPUT);
	pinMode(PIN_BTN, INPUT_PULLUP);

	lastClic = lastDebounce = millis();
	btnLastState = digitalRead(PIN_BTN);

	// Timer setting for led dimming
}

void deinitSystem(){
	pinMode(PIN_LED1, INPUT);
	pinMode(PIN_LED2, INPUT);
	pinMode(PIN_LED4, INPUT);
	pinMode(PIN_LED6, INPUT);

	// Timer unsetting for led dimming (maybe not needed as timer interrupt cannot wakeup the CPU in sleep modes)
}

void displayNumber(uint8_t output){
	bool isPar = 0;
	isPar = output % 2;

	pinMode(PIN_LED1, isPar?1:0);
	pinMode(PIN_LED2, (output > 1)?1:0);
	pinMode(PIN_LED4, (output > 3)?1:0);
	pinMode(PIN_LED6, (output > 5)?1:0);

/*
	// dim mode
	pinState = 0;
	if(isPar) pinState |= (1 << 0);
	if(output > 1) pinState |= (1 << 1);
	if(output > 3) pinState |= (1 << 2);
	if(output > 5) pinState |= (1 << 3);
*/
}

void displayClear(){
	pinMode(PIN_LED1, 0);
	pinMode(PIN_LED2, 0);
	pinMode(PIN_LED4, 0);
	pinMode(PIN_LED6, 0);
}

bool debounceButton(uint32_t now){

	bool btnNow = digitalRead(PIN_BTN);


	if(btnNow != btnLastState){
		btnLastState = btnNow;
		lastDebounce = now;
		btnChange = true;
	}

	if(btnChange && !btnNow && (now - lastDebounce) > 2){
		btnChange = false;
		return true;
	}

	return false;
}

bool testSleep(uint32_t now){
	if((now - lastClic) > SLEEP_DELAY) return true;

	return false;
}

void goToSleep(){
	// First enable interrupt on button (pin 4)
	GIMSK |= (1 << PCIE);
	PCMSK = (1 << PCINT4);

	// Then setup sleep mode
	MCUCR |= (1 << SM1);
	MCUCR &= ~(1 << SM0);
	MCUCR |= (1 << SE);
	deinitSystem();
	
	//Finally launch sleep
//	__asm__ __volatile__ ( "sleep" "\n\t"::);
	sleep_mode();

	initSystem();
}

void setup(){
	makeSeed();
	initSystem();
}

void loop(){
	uint32_t now = millis();

	if(debounceButton(now)){
		uint8_t result = random(6) + 1;
		displayNumber(result);
	}

	if(testSleep(now)){
		goToSleep();
	}
}