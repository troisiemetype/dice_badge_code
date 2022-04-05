# Dice badge
This is the code running on the dice badge which design is shared [here](https://github.com/troisiemetype/Dice_badge).

## Use
### Modes
There are six modes available on this badge. Changing mode is as simple as pushing the switch more than a second. The mode number will blink three times to confirm.
- Dice. In this mode, each time the tact swith is pressed, several numbers are displayed, simulating a throw. The last number stays for a few seconds, then the badge goes to a deep-sleep mode.
- Heartbeat, normal. In this mode the badge blinks a random number, simulating a heartbeat, with a pulse every 3 seconds or so.
- Heartbeat, slow. Same as above, but the pulse is once every six seconds.
- Pulse, normal. In this mode the badge simply blinks (on then off) a random number, every three seconds.
- Pulse, slow. Same as above, but the pulse is once every six seconds.
- Random / demo. In this mode the leds blinks totaly randomly, with random speed.

### Deep sleep
CPU sleeping is used as much a possible to save battery. In dice mode (mode 1), once the number as been displayed a few seconds, it goes to sleep. In all other modes (mode 2 - 6), a short press on the switch will shut off the leds and put the badge to sleep. Then another press will wake the badge up and come back to the mode it was before.

### Random numbers
Even if there is no need for a true random generator on the dice, I wanted to do better than Arduino's bare 'random' function, and the associated 'randomSeed'. In my experience thery are not that random, at least if you use the classic trick of initializing it with one or several 'analogRead'. A while ago I read an [article on Hackaday](https://hackaday.com/2015/06/29/true-random-number-generator-for-a-true-hacker/), associated with a [project on hackaday.io](https://hackaday.io/project/5588-hardware-password-manager) (scroll-down to projects logs), about seeding random with the content of uninitialized RAM.
The basic idea is that most of the RAM bytes are always on the same state on power-up, but some are randomly 0 or 1. All the RAM content is read on program start, before variables are initialized, and is XORed byte to byte to form a 32 byte int that is used as seed. I invite you to read the hackaday article if you want more details about this technique.
The algorithm used to generate random number from this seed is called xorshift. Why this one ? Mostly because it is considered to be quite efficient, it takes three lines of code, and more importantly I understand what it does on first sight. :) [Ask wikipedia for more info](https://en.wikipedia.org/wiki/Xorshift).