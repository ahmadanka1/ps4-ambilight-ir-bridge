# Changelog

## 0.1.0 - first tested version
- Tested on a real PS4 with an ESP32, an IR emitter on GPIO 4 and a 24-key RGB strip: works.
- Only the default web settings were tested.
- Default IR pin changed to GPIO 4.
- IR pin is now a variable (`DEFAULT_IR_PIN`, default 15) and adjustable on the web page.
- Dominant vibrant colour mode (hue histogram), hue-based matching with hysteresis.
- HSV smoothing (time based), glide through neighbouring keys, one IR code per 80 ms,
  newest-packet-only processing so there is no lag build-up.
- Web page: speed/transition sliders, PS4 frames per second and IR sends per second.
- Redesigned dark, mobile friendly web page with live swatches, sliders, coloured test
  buttons, IR sent counter and last code.
- Vivid colour mode, colour boost and hue-based key matching (grey scenes no longer
  always pick White).
- First PS4 test: packets received, strip response not yet confirmed.
- First version of the ESP32 IR bridge: DDP receiver on UDP 4048, colour averaging,
  nearest-key matching for a 24-key NEC remote, web page with live status,
  settings and IR test buttons, fallback hotspot.
