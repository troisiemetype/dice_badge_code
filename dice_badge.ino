#include "avr/sleep.h"

#include "PushButton.h"

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

state_t *stateQueueWrite;
state_t *stateQueueRead;

uint8_t mode;
volatile bool overflow;
uint16_t overflowCounter;
uint8_t fadeSpeed;
uint32_t lastClic;
bool justWake;

PushButton btn;

// Interrupt service routine for PCINT, on which the button pin is the only one activated.
// simply disable sleep. Program then resumes where the sleep was launch.
ISR(PCINT0_vect){
	MCUCR &= ~(1 << SE);
}

// Both timer interrupts for fading leds.
// using fading on leds.
ISR(TIMER1_OVF_vect){
	// Clearing all four leds)
	PORTB &= ~(0b1111);
//	PORTB |= pinState;
	// overflow counter
	if(++overflowCounter == stateQueueRead->fade.speed){
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
	
	btn.begin(PIN_BTN, INPUT_PULLUP);
	justWake = true;


	// Init State
	pinState = 0;
	overflow = false;
	overflowCounter = 0;
	lastClic = millis();

	// Timer setting for led dimming
	TCCR1A = /*(1 << CS13) | */(1 << CS12) | (1 << CS11) | (1 << CS10);
	GTCCR = 0;
	OCR1A = 255;
	TIMSK |= (1 << OCIE1A) | (1 << TOIE1);
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
	state_t *s = stateQueueRead;
	for(uint8_t i = 0; i <= QUEUE_SIZE; ++i){
		// Here we just change the state to IDLE : if no state is specified, no function is called.
		s->state = IDLE;
		s = s->next;
	}

	stateQueueRead = stateQueueWrite = s;
}

// Add a number to the queue.
void displayNumber(uint8_t output){
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

	state_t *s = stateQueueWrite;
	s->state = CHANGE_NUMBER;
	s->value = out;
	stateQueueWrite = s->next;
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
void fadeIn(uint8_t speed = FADE_FAST){
	state_t *s = stateQueueWrite;
	s->state = FADE;
	s->fade.direction = -1;
	s->fade.speed = speed;
	s->fade.limit = 0;
	stateQueueWrite = stateQueueWrite->next;
}

// Add a fade-out to the queue.
void fadeOut(uint8_t speed = FADE_FAST){
	state_t *s = stateQueueWrite;
	s->state = FADE;
	s->fade.direction = +1;
	s->fade.speed = speed;
	s->fade.limit = 255;
	stateQueueWrite = stateQueueWrite->next;
}

// Add a delay to the queue.
void wait(uint16_t delay){
	state_t *s = stateQueueWrite;
	s->state = WAIT;
	s->wait.delay = delay;
	s->wait.start = 0;
	stateQueueWrite = s->next;
}

// Add a sleep to the queue.
void goToSleep(){
	state_t *s = stateQueueWrite;
	s->state = SLEEP;
	stateQueueWrite = s->next;
}

// Handle dimming state.
void dimming(){
	state_t *s = stateQueueRead;

	if(overflow){
		overflow = false;
		OCR1A += s->fade.direction;
		if(OCR1A == s->fade.limit){
			s->state = IDLE;
			stateQueueRead = s->next;
		}
	}
}

// Handle waiting state.
void waiting(){
	state_t *s = stateQueueRead;

	uint32_t now = millis();

	if(s->wait.start == 0) s->wait.start = now;

	if((now - s->wait.start) > s->wait.delay){
		s->state = IDLE;
		stateQueueRead = s->next;
	}

}

// Handle changing state.
void changing(){
	state_t *s = stateQueueRead;

	pinState = s->value;
	s->state = IDLE;
	stateQueueRead = s->next;
}

// Launch a new dice. Queue several fade-out / new number / fade-in.
void launchDice(){
	for(uint8_t i = 0; i < 3; ++i){
//		uint8_t result = random(6) + 1;
		uint8_t result = 0;
//		result = random(6) + 1;
//		result = (rand() * 6) / (RAND_MAX + 1);
		result = xorshift(6) + 1;
		fadeOut(FADE_FAST);
		displayNumber(result);
		fadeIn(FADE_FAST);
	}
}

// Test sleep against given time.
bool testSleep(uint32_t now){
	if((now - lastClic) > SLEEP_DELAY) return true;

	return false;
}

// Handle sleeping state. Set the sleep options and put the uC to sleep.
void sleeping(){
	state_t *s = stateQueueRead;
	s->state = IDLE;
	stateQueueRead = s->next;

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
}

// Loop for dice mode.
void loopDice(){
	// If timing is over, then the system is put to sleep.
	if(testSleep(millis())){
		fadeOut(FADE_SLOW);
		goToSleep();
		// Here we wake from sleep.
		lastClic = millis();
	}
}

// Loop for pulse mode.
void loopPulse(){
	if(stateQueueRead->state != IDLE) return;

	displayNumber(xorshift(6) + 1);

	fadeIn(FADE_MID);
	wait(500);

	fadeOut(FADE_SLOW);
	wait(5000);
}

// Loop for demo mode.
void loopDemo(){
	if(stateQueueRead->state != IDLE) return;

//	displayNumber(xorshift(6) + 1);
	// Here we don't care if the display is a dice, we just wanna blink ; any combinaison is ok, and we directly change pinState instead of queueing.
	pinState = xorshift(16);
	fadeIn(xorshift(32));
	fadeOut(xorshift(32));
}

void handleButton(){
	lastClic = millis();

	if(btn.isLongPressed()){
		if(justWake){
			justWake = false;
			return;
		}	

		// handle long press : change mode
		mode++;
		if(mode > MODE_DEMO) mode = MODE_DICE;
		emptyQueue();
		fadeOut(0);
		displayNumber(mode);
		for(uint8_t i = 0; i < 4; ++i){
/*			
			fadeIn(4);
			wait(200);
			fadeOut(4);
			wait(300);
*/
			pinState = mode + 1;
			OCR1A = 0;
			delay(200);
			OCR1A = 255;
			delay(300);
		}
	} else if(btn.justReleased()){
		switch(mode){
			case MODE_DEMO:
			case MODE_PULSE:
				if(justWake){
					justWake = false;
					return;
				}	
				emptyQueue();
				fadeOut();
				goToSleep();
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

	mode = MODE_PULSE;

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

	stateQueueWrite = stateQueueRead = queue;

	// call for wake-up and reset settings.
	initSystem();
}

void loop(){
	if(btn.update()) handleButton();

	switch(stateQueueRead->state){
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

	switch (mode){
		case MODE_DEMO:
			loopDemo();
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
