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

### Deep sleep.
CPU sleeping is used as much a possible to save battery. In dice mode (mode 1), once the number as been displayed a few seconds, it goes to sleep. In all other modes (mode 2 - 6), a short press on the switch will shut off the leds and put the badge to sleep. Then another press will wake the badge up and come back to the mode it was before.