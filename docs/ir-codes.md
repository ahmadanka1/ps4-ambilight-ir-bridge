# IR codes (24-key remote)

Captured with IRremoteESP8266 `IRrecvDumpV2`. All are NEC, 32 bit.
Your remote may differ. Capture your own codes and edit the `keys[]` table in the sketch.

| Row | Keys |
|---|---|
| 1 | `F700FF` Bright+, `F7807F` Bright-, `F740BF` Off, `F7C03F` On |
| 2 | `F720DF` R, `F7A05F` G, `F7609F` B, `F7E01F` W |
| 3 | `F710EF` red, `F7906F` green, `F750AF` blue, `F7D02F` Flash |
| 4 | `F730CF` orange, `F7B04F` teal, `F7708F` purple, `F7F00F` Strobe |
| 5 | `F708F7` light orange, `F78877` blue, `F748B7` violet, `F7C837` Fade |
| 6 | `F728D7` yellow, `F7A857` dark teal, `F76897` pink, `F7E817` Smooth |

Colour names are read from the key caps. The RGB values the sketch uses to match
colours are estimates and may need tuning.
