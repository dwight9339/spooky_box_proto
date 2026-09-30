# UI service split hands-on and visual addendum

Date: 2026-09-30  
Beads issue: `full_spooky_proto-8lw.9`  
Base evidence: [UI service split bench qualification](2026-09-30-ui-service-split.md)

The operator was present for a second hands-on UI session using the same
IpcSmoke image and source snapshot identified in the base evidence.

## Input checks

`UI WATCH START` produced no spontaneous event before the controls were moved.
The live capture then reported a press and release for each of BTN0, BTN1,
ENC0_BTN, ENC1_BTN, ENC2_BTN and ENC3_BTN. It also reported at least one
completed clockwise and counterclockwise detent for each of ENC0 through ENC3.
Every reported detent completed at AB=11. The watch was stopped and final
`UI STATUS` reported all controls released, all renderers idle, and
`DROPPED=0`.

This passes the physical input portion and the no-spurious-event acceptance
check for the `UI WATCH START` critical-section fix.

## Visual checks

The target again reported successful matrix probe, all 14 LED steps, the
81-pixel matrix animation, SSD1309 test transfers at offsets 0 and 2, display
off, safe-off and final idle status. While the sequence ran, the operator
confirmed that:

- all 14 LED channels appeared correctly;
- the centered 9x9 matrix animation appeared correctly; and
- the OLED border and asymmetric corner test pattern appeared correctly.

The target ended with watch, LED chase, matrix and display disabled, and with
zero dropped UI messages. The board remains on the IpcSmoke image.
