# Test plan (first real test)

Tick each step. Stop at the first failure and note what happened.

## A. Bench test (no PS4)
- [ ] Sketch uploads, Serial Monitor prints an IP
- [ ] Web page opens at that IP
- [ ] IR LED blinks purple on a phone camera when a test button is pressed
- [ ] Strip turns on/off with the On and Off buttons
- [ ] Red, Green, Blue, White test buttons show the right colours
- [ ] Other colour buttons look like their names (note any that don't)
- [ ] Settings save and survive a reboot

## B. PS4 plugin
- [ ] GoldHEN plugin loader enabled
- [ ] Companion app installed, plugin installed and enabled
- [ ] Set up: ESP32 IP, port 4048, small LED counts, RGB
- [ ] Test Strip: packet count on the web page increases
- [ ] Strip changes colour while Test Strip runs

## C. In a game / app
- [ ] Colours follow the screen roughly
- [ ] Dark scenes switch the strip off
- [ ] Any flicker? Try a larger minimum interval
- [ ] Any game stutter or crash? (this is the plugin side, report it upstream too)

## Notes
(write results here, then update README status)
