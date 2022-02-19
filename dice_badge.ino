#include "avr/sleep.h"

const uint16_t SRAM_START = 0x000;		// SRAM start is 0x60, but registers can be used as well, some have undecided reset value.
const uint16_t SRAM_END = 0x25F;		// 0x0DF, 0x15F, 0x25F for Attiny 25, 45, 85 resp.

const uint8_t PIN_LED1 = 0;
const uint8_t PIN_LED2 = 1;
const uint8_t PIN_LED4 = 3;
const uint8_t PIN_LED6 = 2;
const uint8_t PIN_BTN = 4;

// Sleep delay in ms. How long will the dice display its number before to go to sleep.
const uint16_t SLEEP_DELAY = 8000;

const uint8_t FADE_FAST = 8;
const uint8_t FADE_MID = 24;
const uint8_t FADE_SLOW = 64;

const uint8_t QUEUE_SIZE = 24;

const uint8_t DEBOUNCE_DELAY = 1;
const uint16_t LONG_DELAY = 800;

uint32_t seed;

uint8_t testValue;

volatile uint8_t pinState;

// Those are the states to be queued.
enum{
	IDLE = 0,
	FADE,
	WAIT,
	CHANGE_NUMBER,
	SLEEP,
};

// Those are the different modes.
enum{
	MODE_DICE = 0,
	MODE_HEARTBEAT,
	MODE_PULSE,
	MODE_DEMO,
};

// Structures for different states, to be inserted into the global state structure.
struct wait_t{
	uint16_t delay;
	uint32_t start;
};

struct fade_t{
	int8_t direction;
	uint8_t speed;
	uint8_t limit;
};

struct state_t{
	state_t *next;
	volatile uint8_t state;
	union{
		fade_t fade;
		wait_t wait;
		uint8_t value;
	};
};

state_t *queueIn;
state_t *volatile queueOut;

uint8_t mode;
volatile bool overflow;
uint16_t overflowCounter;
uint8_t fadeSpeed;

struct buttonState_t{
	uint32_t lastChange;
	bool state : 1;
	bool pState : 1;
	bool longState : 1;
	bool pLongState : 1;
	bool now : 1;
	bool prev : 1;
	bool changed : 1;
} btn;

// Interrupt service routine for PCINT, on which the button pin is the only one activated.
// simply disable sleep. Program then resumes where the sleep was launch.
// Also disable interrupt on button pin, we don't want it to fire each time the button is pressed.
ISR(PCINT0_vect){
	MCUCR &= ~(1 << SE);
	GIMSK &= ~(1 << PCIE);
}

// Both timer interrupts for fading leds.
// using fading on leds.
ISR(TIMER1_OVF_vect){
	// Clearing all four leds)
	PORTB &= ~(0b1111);
//	PORTB |= pinState;
	// overflow counter
	if(++overflowCounter == queueOut->fade.speed){
		overflowCounter = 0;
		overflow = true;
	}
}

ISR(TIMER1_COMPA_vect){
	// Set all four leds to their predefined values
//	PORTB &= ~(0b1111);
	if(OCR1A != 255) PORTB |= pinState;
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

// Read uninitialized RAM values to generate a seed that is different on every startup and on every board.
void makeSeed(){
	uint8_t *sramData;
	for(uint16_t i = SRAM_START; i < SRAM_END; ++i){
		sramData = i;
		seed ^= (uint32_t)(*sramData) << ((i % 4) * 8);
	}
//	srand(seed);
	randomSeed(seed);
}

// Alternative to the Arduino random() function.
uint32_t xorshift(uint32_t max = 0){
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;

	if(max != 0) return (uint64_t)seed * max / ((uint32_t)(-1));
	else return seed;
}

// Everything that has to be initialized on first startup, then on every wake up from sleep.
void initSystem(){
	disablePeripherals();

	// Setting port directions
	pinMode(PIN_LED1, OUTPUT);
	pinMode(PIN_LED2, OUTPUT);
	pinMode(PIN_LED4, OUTPUT);
	pinMode(PIN_LED6, OUTPUT);
	pinMode(PIN_BTN, INPUT_PULLUP);

	// Init State
	pinState = 0;
	overflow = false;
	overflowCounter = 0;

	// button reset
	memset(&btn, 0, sizeof(buttonState_t));

	// Timer setting for led dimming
	TCCR1A = /*(1 << CS13) | */(1 << CS12) | (1 << CS11) | (1 << CS10);
	GTCCR = 0;
	OCR1A = 255;
	TIMSK |= (1 << OCIE1A) | (1 << TOIE1);

/*
	// Sleep settings.
	MCUCR |= (1 << SM1);
	MCUCR &= ~(1 << SM0);
*/
}

// Changing uC state before sleep.
void deinitSystem(){
	// Timer unsetting for led dimming (maybe not needed as timer interrupt cannot wake the CPU up in sleep modes)
	TCCR1A = 0;
	TIMSK &= ~((1 << OCIE1A) | (1 << TOIE1));

	pinMode(PIN_LED1, INPUT);
	pinMode(PIN_LED2, INPUT);
	pinMode(PIN_LED4, INPUT);
	pinMode(PIN_LED6, INPUT);
}

// Self explanatory : empty queue. memset cannot be used because in each queue entry we have to keep the link to the next entry.
void emptyQueue(){
	state_t *s = queueOut;
//	state_t *nx;
	for(uint8_t i = 0; i <= QUEUE_SIZE; ++i){
		// Here we just change the state to IDLE : if no state is specified, no function is called.
		s->state = IDLE;
		s = s->next;
/*
		nx = s->next;
		memset(s, 0, sizeof(state_t));
		s->next = nx;
		s = nx;
*/
	}

	queueOut = queueIn = s;
}

// Add a number to the queue.
void queueNumber(uint8_t output){
	bool isPar = 0;
	isPar = output % 2;
/*
	digitalWrite(PIN_LED1, isPar?1:0);
	digitalWrite(PIN_LED2, (output > 1)?1:0);
	digitalWrite(PIN_LED4, (output > 3)?1:0);
	digitalWrite(PIN_LED6, (output > 5)?1:0);
*/

	// the number we add to the queue is not the one returned by the random function, but the nibble needed for PORTB control, ready to be used.
	uint8_t out = 0;
	out = 0;
	if(isPar) out |= (1 << 0);
	if(output > 1) out |= (1 << 1);
	if(output > 3) out |= (1 << 3);
	if(output > 5) out |= (1 << 2);

	state_t *s = queueIn;
	s->state = CHANGE_NUMBER;
	s->value = out;
	queueIn = s->next;
}

void displayClear(){
/*
	digitalWrite(PIN_LED1, 0);
	digitalWrite(PIN_LED2, 0);
	digitalWrite(PIN_LED4, 0);
	digitalWrite(PIN_LED6, 0);
*/
	pinState = 0;
}

// Add a fade-in to the queue.
void queueFadeIn(uint8_t speed = FADE_FAST, uint8_t limit = 0){
	state_t *s = queueIn;
	s->state = FADE;
	s->fade.direction = -1;
	s->fade.speed = speed;
	s->fade.limit = limit;
	queueIn = s->next;
}

// Add a fade-out to the queue.
void queueFadeOut(uint8_t speed = FADE_FAST, uint8_t limit = 255){
	state_t *s = queueIn;
	s->state = FADE;
	s->fade.direction = +1;
	s->fade.speed = speed;
	s->fade.limit = limit;
	queueIn = s->next;
}

// Add a delay to the queue.
void queueWait(uint16_t delay){
	state_t *s = queueIn;
	s->state = WAIT;
	s->wait.delay = delay;
	s->wait.start = 0;
	queueIn = s->next;
}

// Add a sleep to the queue.
void queueSleep(){
	state_t *s = queueIn;
	s->state = SLEEP;
	queueIn = s->next;
}

// Handle dimming state.
void dimming(){
	state_t *s = queueOut;

	if(overflow){
		overflow = false;
		OCR1A += s->fade.direction;
		if(OCR1A == s->fade.limit){
			s->state = IDLE;
			queueOut = s->next;
		}
	}
}

// Handle waiting state.
void waiting(){
	state_t *s = queueOut;

	uint32_t now = millis();

	if(s->wait.start == 0){
		s->wait.start = now;
		return;
	}

	if((now - s->wait.start) > s->wait.delay){
		s->state = IDLE;
		queueOut = s->next;
	}
}

// Handle changing state.
void changing(){
	state_t *s = queueOut;

	pinState = s->value;
	s->state = IDLE;
	queueOut = s->next;
}

// Handle sleeping state. Set the sleep options and put the uC to sleep.
void sleeping(){
	state_t *s = queueOut;
	s->state = IDLE;
	queueOut = s->next;

	// First enable interrupt on button (pin 4)
	GIMSK |= (1 << PCIE);
	PCMSK = (1 << PCINT4);


	// Then setup sleep mode
	MCUCR |= (1 << SM1);
	MCUCR &= ~(1 << SM0);
	MCUCR |= (1 << SE);

	deinitSystem();
	
	//Finally launch sleep
	sleep_mode();

	initSystem();

	// On wake-up, some modes have to have something done.
	switch(mode){
		case MODE_DICE:
			launchDice();
			break;
		default:
			break;
	}
}

// Launch a new dice. Queue several fade-out / new number / fade-in.
void launchDice(){
	for(uint8_t i = 0; i < 3; ++i){
		queueFadeOut(FADE_FAST);
		queueNumber(xorshift(6) + 1);
		queueFadeIn(FADE_FAST);
	}
	queueWait(5000);
	queueFadeOut(FADE_SLOW);
	queueSleep();
}

// Loop for dice mode.
void loopDice(){

}

// Loop for pulse mode.
void loopPulse(){
	if(queueOut->state != IDLE) return;

	queueNumber(xorshift(6) + 1);

	queueFadeIn(FADE_MID);
	queueWait(400);

	queueFadeOut(FADE_SLOW);
	queueWait(2000);
}

// Loop for heartbeat mode.
void loopHeartBeat(){
	if(queueOut->state != IDLE) return;
	queueNumber(xorshift(6) + 1);

	queueFadeIn(FADE_FAST, 0);
	queueFadeOut(FADE_FAST);

	queueFadeIn(FADE_FAST, 100);
	queueFadeOut(FADE_SLOW);

	queueWait(1200);

}

// Loop for demo mode.
void loopDemo(){
	if(queueOut->state != IDLE) return;

//	queueNumber(xorshift(6) + 1);
	// Here we don't care if the display is a dice, we just wanna blink ; any combinaison is ok, and we directly change pinState instead of queueing.
	pinState = xorshift(16);
	queueFadeIn(xorshift(32));
	queueFadeOut(xorshift(32));
}

void handleButton(){

	uint32_t now = millis();

	// Button is active LOW ; we invert the reading so boolean logic is more intuitive.
	btn.prev = btn.now;
	btn.now = !digitalRead(PIN_BTN);

	// Check if there is a difference, so we can reinit the button timer.
	if(btn.now != btn.prev){
		btn.lastChange = now;
		return;
	}

	// We check for debounce.
	if((btn.state != btn.now) && ((now - btn.lastChange) > DEBOUNCE_DELAY)){
		// Change the actual state of the button.
		btn.pState = btn.state;
		btn.state = btn.now;
		btn.changed = true;

		// We check for polarity. If the button is depressed we want to update long state.
		if(!btn.state){
			btn.pLongState = btn.longState;
			btn.longState = false;
		}
	}

	if(btn.state && !btn.longState && ((now - btn.lastChange) > LONG_DELAY)){
		btn.longState = true;
		btn.changed = true;
	}
/*	
	// Test for detecting short and long clics.
	if(btn.longState && btn.changed){
		btn.changed = false;
		pinState = 1;
		OCR1A = 0;
		delay(1000);
		OCR1A = 255;
	} else if(!btn.state && !btn.pLongState && btn.changed){
		btn.changed = false;
		pinState = 1;
		OCR1A = 0;
		delay(200);
		OCR1A = 255;
	}	
	return;
*/
	// Now we make something with this button.
	// First long press.
	if(btn.longState && btn.changed){
		btn.changed = false;
		// handle long press : change mode
		mode++;
		if(mode > MODE_DEMO) mode = MODE_DICE;

		emptyQueue();
		queueFadeOut(4);
		queueNumber(mode + 1);
		for(uint8_t i = 0; i < 2; ++i){
			
			queueFadeIn(4);
			queueWait(400);
			queueFadeOut(4);
			queueWait(300);
		}
	// Then short clic, i.e. release if it was shorter than a long press.
	} else if(!btn.state && !btn.pLongState && btn.changed){
		btn.changed = false;

		switch(mode){
			case MODE_DEMO:
			case MODE_PULSE:
				emptyQueue();
				queueFadeOut();
				queueSleep();
				break;
			case MODE_DICE:
				emptyQueue();
				launchDice();
				break;
			default:
				break;
		}
	}
}

// Initialisation. The bulk is needed on each wake-up, and is in a dedicated function. Queue is initialized only once, here.
void setup(){
	// Very first thing to do, before any variable initilization : reading RAM for seed generation.
	makeSeed();

	mode = MODE_DICE;

	// Initializaing the display queue.
	state_t *queue = new state_t[QUEUE_SIZE];

	memset(queue, 0, sizeof(state_t) * QUEUE_SIZE);

	for(uint8_t i = 0; i < QUEUE_SIZE; ++i){
		if(i == (QUEUE_SIZE - 1)){
			queue[i].next = &queue[0];
		} else {
			queue[i].next = &queue[i + 1];
		}
	}

	queueIn = queueOut = queue;

	// call for wake-up and reset settings.
	initSystem();
}

void loop(){
	handleButton();

	switch(queueOut->state){
		case FADE:
			dimming();
			break;
		case WAIT:
			waiting();
			break;
		case CHANGE_NUMBER:
			changing();
			break;
		case SLEEP:
			sleeping();
			break;
		case IDLE:
		default:
			break;
	}
return;
	switch (mode){
		case MODE_DEMO:
			loopDemo();
			break;
		case MODE_HEARTBEAT:
			loopHeartBeat();
			break;
		case MODE_PULSE:
			loopPulse();
			break;
		case MODE_DICE:
		default:
			loopDice();
			break;
	}
}
