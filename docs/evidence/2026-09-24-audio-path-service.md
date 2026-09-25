# Audio path service extraction: raw bench transcript

Unedited operator capture for `full_spooky_proto-8lw.3`, summarized in
[September 24 results](../bench-results-2026-09-24.md#audio-path-service-extraction-regression).
Timestamps in the Spooky Bench JSON are UTC (2026-09-25 04:12-04:21 UTC is the
evening of 2026-09-24 local time).

## UART7 (Pico bridge, 115200 8N1)

```

[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1

========================================
Spooky Box radio-to-headphone bring-up
CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo
Jack detect: line-in PA5=empty, headphone PE0=empty
========================================
[audio] volume pot PF10/ADC3 ready; initial ADC=36248

[audio] SGTL5000 I2S -> DAC -> headphone setup
[audio] PE2 MCLK approximately 12288025 Hz
[audio] PASS: CHIP_ID=0xA011; headphone path configured muted
[audio] volume ADC=36249 -23.5 dB
[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=24576050 Hz; measured FS=48004 Hz
[sai2] frame=32 bits active=16 bits slots=2 MCKDIV=16; expect 48 kHz/1.536 MHz

[radio] Si4735 digital multi-band setup
[radio] tuned FM 99100 kHz (99.100 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] digital output enabled: 48 kHz, 16-bit stereo I2S
[bridge] SAI1 PE4 measured FS=47996 Hz
[bridge] PASS: SAI2 RX DMA -> SAI1 TX DMA is running

[summary] PASS: radio audio is routed to the headphone codec
[summary] Output follows PE0 jack detect and the PF10 volume pot
[sd] CLI ready; card detect=present, initialization deferred
[record] three-channel recorder ready; default=60s

[fuel] BQ27441-G1A fuel-gauge test
[fuel] I2C2 PB10/PB11; expected address 0x55
[fuel] target configuration: 3700 mAh / 13690 mWh
[fuel] PASS: DEVICE_TYPE=0x0421 FW=0x0109 DM=0x48 CHEM_ID=0x0128
[fuel] 4167 mV, SOC=100%, 26.2 C, idle at 0 mA / 0 mW
[fuel] remaining=3352 mAh, full=3350 mAh, design=3700 mAh; SOH=89% status=1
[fuel] flags=0x0088 BAT_DET=1 ITPOR=0 CHG=0 DSG=0 FC=0 SOC1=0 SOCF=0 CFGUP=0
[fuel] CONFIG: already applied; no data-memory write needed

[mag] TMAG5273 magnetometer test
[mag] I2C2 PB10/PB11; expected address 0x35; MAG_INT PC7 unused
[mag] PASS: manufacturer=0x5449 device=0x06 variant=A2 range=+/- 133000 uT
[mag] initial: MAG X=-101uT Y=97uT Z=166uT RAW=-25,24,41 SET=1 READY=1 DIAG=0
[mag] sleeping; use MAG READ or MAG STREAM START [period-ms]

[ui] Stage 1 UI-board bring-up ready on CM7
[ui] inputs use external backplane pulls; JP9 must be fitted
[ui] 14 LED channels enabled: 2 buttons plus 12 encoder RGB
[ui] matrix uses a centered logical 9x9 area on physical columns 2..10
[ui] SSD1309 display SPI6=ready, 8-bit mode 0 at 8 MHz

[usb] USB FS CDC command console
[usb] PA9 VBUS sense, PA11 DM, PA12 DP; PA10 remains radio reset
[usb] self-powered configuration; VBUS budget advertised as 500 mA
[usb] system power source is selected by the backplane and Nucleo jumpers
[usb] PA9 monitored in GPIO; core B-session-valid override enabled
[usb] VDD33USB ready from Nucleo 3V3 (PWR_CR3=05010044)
[usb] HSI48 active; VBUS=present; waiting for host enumeration
[usb] PASS: host configured Spooky Box USB CDC CLI
[audio] headphones inserted; output enabled under volume-pot control
[radio] USB tuned FM 99000 kHz (99.000 MHz): RSSI=15 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 98900 kHz (98.900 MHz): RSSI=15 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 98800 kHz (98.800 MHz): RSSI=16 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 98700 kHz (98.700 MHz): RSSI=17 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 98600 kHz (98.600 MHz): RSSI=18 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 98500 kHz (98.500 MHz): RSSI=26 dBuV SNR=6 dB valid=1
[radio] USB tuned FM 98400 kHz (98.400 MHz): RSSI=18 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 98300 kHz (98.300 MHz): RSSI=18 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 98200 kHz (98.200 MHz): RSSI=18 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 98100 kHz (98.100 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 98000 kHz (98.000 MHz): RSSI=17 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 97900 kHz (97.900 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 97800 kHz (97.800 MHz): RSSI=16 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 97700 kHz (97.700 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 97600 kHz (97.600 MHz): RSSI=16 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 97500 kHz (97.500 MHz): RSSI=16 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 97400 kHz (97.400 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 97300 kHz (97.300 MHz): RSSI=16 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 91500 kHz (91.500 MHz): RSSI=6 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 88900 kHz (88.900 MHz): RSSI=3 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 105500 kHz (105.500 MHz): RSSI=7 dBuV SNR=1 dB valid=0
[audio] volume ADC=37554 -22.5 dB
[audio] volume ADC=38816 -21.5 dB
[audio] volume ADC=39878 -20.5 dB
[audio] volume ADC=41376 -19.5 dB
[audio] volume ADC=43038 -18.0 dB
[audio] volume ADC=44369 -17.0 dB
[audio] volume ADC=45807 -16.0 dB
[audio] volume ADC=46765 -15.0 dB
[audio] volume ADC=48031 -14.0 dB
[audio] volume ADC=47361 -15.0 dB
[audio] volume ADC=46021 -16.0 dB
[radio] USB tuned FM 99100 kHz (99.100 MHz): RSSI=14 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 99200 kHz (99.200 MHz): RSSI=15 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 99300 kHz (99.300 MHz): RSSI=15 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 99400 kHz (99.400 MHz): RSSI=15 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 99500 kHz (99.500 MHz): RSSI=15 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 99600 kHz (99.600 MHz): RSSI=15 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 99700 kHz (99.700 MHz): RSSI=15 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 99800 kHz (99.800 MHz): RSSI=15 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 99900 kHz (99.900 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 100000 kHz (100.000 MHz): RSSI=23 dBuV SNR=3 dB valid=0
[radio] USB tuned FM 100100 kHz (100.100 MHz): RSSI=17 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 100200 kHz (100.200 MHz): RSSI=16 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 100300 kHz (100.300 MHz): RSSI=15 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 100400 kHz (100.400 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 100500 kHz (100.500 MHz): RSSI=11 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 100600 kHz (100.600 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 100700 kHz (100.700 MHz): RSSI=15 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 100800 kHz (100.800 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 100900 kHz (100.900 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 101000 kHz (101.000 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 101100 kHz (101.100 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 101200 kHz (101.200 MHz): RSSI=12 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 101300 kHz (101.300 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 101400 kHz (101.400 MHz): RSSI=13 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 101500 kHz (101.500 MHz): RSSI=17 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 101600 kHz (101.600 MHz): RSSI=18 dBuV SNR=4 dB valid=0
[radio] USB tuned FM 101700 kHz (101.700 MHz): RSSI=13 dBuV SNR=2 dB valid=0
[radio] USB tuned FM 101800 kHz (101.800 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 101900 kHz (101.900 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102000 kHz (102.000 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102100 kHz (102.100 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102200 kHz (102.200 MHz): RSSI=12 dBuV SNR=1 dB valid=0
[radio] USB tuned FM 102300 kHz (102.300 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102400 kHz (102.400 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102500 kHz (102.500 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102600 kHz (102.600 MHz): RSSI=9 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102700 kHz (102.700 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102800 kHz (102.800 MHz): RSSI=10 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 102900 kHz (102.900 MHz): RSSI=11 dBuV SNR=0 dB valid=0
[fuel] update: SOC=100%, 4167 mV, idle at 0 mA / 0 mW, 3352/3350 mAh, 26.5 C
[radio] USB tuned FM 105500 kHz (105.500 MHz): RSSI=8 dBuV SNR=0 dB valid=0
[radio] switched to AM 1000 kHz (1.000 MHz): RSSI=52 dBuV SNR=0 dB valid=0
[radio] antenna path=AM/LW loop
[radio] USB tuned AM 1310 kHz (1.310 MHz): RSSI=53 dBuV SNR=14 dB valid=1
[radio] switched to FM 105500 kHz (105.500 MHz): RSSI=6 dBuV SNR=0 dB valid=0
[radio] antenna path=FM input
[record] OK RECORD START file=REC007.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
[audio] volume ADC=44809 -17.0 dB
[audio] volume ADC=43568 -18.0 dB
[audio] volume ADC=42286 -19.0 dB
[record] RECORD progress=4.9s queues=0/8,0/8 max-write=19ms
[audio] volume ADC=43193 -18.0 dB
[audio] volume ADC=44367 -17.0 dB
[audio] volume ADC=45743 -16.0 dB
[audio] volume ADC=46887 -15.0 dB
[audio] volume ADC=48223 -14.0 dB
[audio] volume ADC=49396 -13.0 dB
[audio] volume ADC=50519 -12.0 dB
[audio] volume ADC=49833 -13.0 dB
[audio] volume ADC=48566 -14.0 dB
[audio] volume ADC=47256 -15.0 dB
[audio] volume ADC=46099 -16.0 dB
[audio] volume ADC=44795 -17.0 dB
[audio] volume ADC=43587 -18.0 dB
[audio] headphones removed; output muted
[record] RECORD progress=9.9s queues=0/8,0/8 max-write=19ms
[audio] headphones inserted; output enabled under volume-pot control
[record] RECORD progress=15.0s queues=0/8,0/8 max-write=21ms
[record] RECORD progress=20.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=25.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=30.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=35.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=40.1s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=45.0s queues=1/8,0/8 max-write=23ms
[record] RECORD progress=50.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=55.1s queues=0/8,0/8 max-write=24ms
[record] OK RECORD PASS file=REC007.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60115ms
[record] RECORD DIAG queues radio=1/8 pdm=1/8 max-write=24ms peaks=1542,1546,4248
[radio] switched to AM 1310 kHz (1.310 MHz): RSSI=58 dBuV SNR=11 dB valid=1
[radio] antenna path=AM/LW loop
[record] OK RECORD START file=REC008.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
[record] RECORD progress=4.9s queues=0/8,0/8 max-write=50ms
[audio] volume ADC=42268 -19.0 dB
[audio] volume ADC=41086 -20.0 dB
[audio] volume ADC=39805 -21.0 dB
[audio] volume ADC=38554 -22.0 dB
[audio] volume ADC=37308 -23.0 dB
[audio] volume ADC=36055 -24.0 dB
[audio] volume ADC=34822 -25.0 dB
[record] RECORD progress=9.9s queues=0/8,0/8 max-write=50ms
[audio] volume ADC=33358 -26.0 dB
[audio] volume ADC=32284 -27.0 dB
[audio] volume ADC=33030 -26.0 dB
[audio] volume ADC=34375 -25.0 dB
[audio] volume ADC=35527 -24.0 dB
[audio] volume ADC=36725 -23.0 dB
[audio] volume ADC=37994 -22.0 dB
[audio] volume ADC=39360 -21.0 dB
[audio] volume ADC=40489 -20.0 dB
[audio] headphones removed; output muted
[record] RECORD progress=14.9s queues=1/8,0/8 max-write=50ms
[audio] headphones inserted; output enabled under volume-pot control
[record] RECORD progress=19.9s queues=0/8,0/8 max-write=50ms
[record] RECORD progress=25.0s queues=0/8,0/8 max-write=50ms
[record] RECORD progress=30.0s queues=0/8,0/8 max-write=50ms
[record] RECORD progress=34.9s queues=1/8,0/8 max-write=50ms
[record] RECORD progress=40.0s queues=0/8,0/8 max-write=50ms
[record] RECORD progress=45.0s queues=0/8,0/8 max-write=50ms
[record] RECORD progress=50.0s queues=1/8,0/8 max-write=50ms
[record] RECORD progress=55.0s queues=0/8,0/8 max-write=50ms
[record] OK RECORD PASS file=REC008.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60112ms
[record] RECORD DIAG queues radio=1/8 pdm=1/8 max-write=50ms peaks=4651,4648,3846
[fuel] update: SOC=100%, 4167 mV, idle at 0 mA / 0 mW, 3352/3350 mAh, 26.7 C

[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1

========================================
Spooky Box radio-to-headphone bring-up
CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo
Jack detect: line-in PA5=empty, headphone PE0=inserted
========================================
[audio] volume pot PF10/ADC3 ready; initial ADC=40535

[audio] SGTL5000 I2S -> DAC -> headphone setup
[audio] PE2 MCLK approximately 12288025 Hz
[audio] PASS: CHIP_ID=0xA011; headphone path configured muted
[audio] volume ADC=40536 -20.0 dB
[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=24576050 Hz; measured FS=48004 Hz
[sai2] frame=32 bits active=16 bits slots=2 MCKDIV=16; expect 48 kHz/1.536 MHz

[radio] Si4735 digital multi-band setup
[radio] tuned FM 99100 kHz (99.100 MHz): RSSI=13 dBuV SNR=0 dB valid=0
[radio] digital output enabled: 48 kHz, 16-bit stereo I2S
[bridge] SAI1 PE4 measured FS=47996 Hz
[bridge] PASS: SAI2 RX DMA -> SAI1 TX DMA is running

[summary] PASS: radio audio is routed to the headphone codec
[summary] Output follows PE0 jack detect and the PF10 volume pot
[sd] CLI ready; card detect=present, initialization deferred
[record] three-channel recorder ready; default=60s

[fuel] BQ27441-G1A fuel-gauge test
[fuel] I2C2 PB10/PB11; expected address 0x55
[fuel] target configuration: 3700 mAh / 13690 mWh
[fuel] PASS: DEVICE_TYPE=0x0421 FW=0x0109 DM=0x48 CHEM_ID=0x0128
[fuel] 4167 mV, SOC=100%, 26.6 C, idle at 0 mA / 0 mW
[fuel] remaining=3352 mAh, full=3350 mAh, design=3700 mAh; SOH=89% status=1
[fuel] flags=0x0088 BAT_DET=1 ITPOR=0 CHG=0 DSG=0 FC=0 SOC1=0 SOCF=0 CFGUP=0
[fuel] CONFIG: already applied; no data-memory write needed

[mag] TMAG5273 magnetometer test
[mag] I2C2 PB10/PB11; expected address 0x35; MAG_INT PC7 unused
[mag] PASS: manufacturer=0x5449 device=0x06 variant=A2 range=+/- 133000 uT
[mag] initial: MAG X=-40uT Y=64uT Z=150uT RAW=-10,16,37 SET=6 READY=1 DIAG=0
[mag] sleeping; use MAG READ or MAG STREAM START [period-ms]

[ui] Stage 1 UI-board bring-up ready on CM7
[ui] inputs use external backplane pulls; JP9 must be fitted
[ui] 14 LED channels enabled: 2 buttons plus 12 encoder RGB
[ui] matrix uses a centered logical 9x9 area on physical columns 2..10
[ui] SSD1309 display SPI6=ready, 8-bit mode 0 at 8 MHz

[usb] USB FS CDC command console
[usb] PA9 VBUS sense, PA11 DM, PA12 DP; PA10 remains radio reset
[usb] self-powered configuration; VBUS budget advertised as 500 mA
[usb] system power source is selected by the backplane and Nucleo jumpers
[usb] PA9 monitored in GPIO; core B-session-valid override enabled
[usb] VDD33USB ready from Nucleo 3V3 (PWR_CR3=05010044)
[usb] HSI48 active; VBUS=present; waiting for host enumeration
[audio] headphones inserted; output enabled under volume-pot control
[usb] PASS: host configured Spooky Box USB CDC CLI
```

## USB CDC CLI

```
Spooky Box USB CLI ready
Type HELP for commands.
down
OK RADIO BAND=FM FREQ=99000 kHz (99.000 MHz) RSSI=15 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=98900 kHz (98.900 MHz) RSSI=15 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=98800 kHz (98.800 MHz) RSSI=16 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=98700 kHz (98.700 MHz) RSSI=17 SNR=2 VALID=0
down
OK RADIO BAND=FM FREQ=98600 kHz (98.600 MHz) RSSI=18 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=98500 kHz (98.500 MHz) RSSI=26 SNR=6 VALID=1
down
OK RADIO BAND=FM FREQ=98400 kHz (98.400 MHz) RSSI=18 SNR=2 VALID=0
down
OK RADIO BAND=FM FREQ=98300 kHz (98.300 MHz) RSSI=18 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=98200 kHz (98.200 MHz) RSSI=18 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=98100 kHz (98.100 MHz) RSSI=16 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=98000 kHz (98.000 MHz) RSSI=17 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=97900 kHz (97.900 MHz) RSSI=16 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=97800 kHz (97.800 MHz) RSSI=16 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=97700 kHz (97.700 MHz) RSSI=16 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=97600 kHz (97.600 MHz) RSSI=16 SNR=1 VALID=0
down
OK RADIO BAND=FM FREQ=97500 kHz (97.500 MHz) RSSI=16 SNR=2 VALID=0
down
OK RADIO BAND=FM FREQ=97400 kHz (97.400 MHz) RSSI=16 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=97300 kHz (97.300 MHz) RSSI=16 SNR=1 VALID=0
tune 91500
OK RADIO BAND=FM FREQ=91500 kHz (91.500 MHz) RSSI=6 SNR=0 VALID=0
tune 88900
OK RADIO BAND=FM FREQ=88900 kHz (88.900 MHz) RSSI=3 SNR=0 VALID=0
tune 105500
OK RADIO BAND=FM FREQ=105500 kHz (105.500 MHz) RSSI=7 SNR=1 VALID=0
tune 99100
OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=14 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=99200 kHz (99.200 MHz) RSSI=15 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=99300 kHz (99.300 MHz) RSSI=15 SNR=2 VALID=0
up
OK RADIO BAND=FM FREQ=99400 kHz (99.400 MHz) RSSI=15 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=99500 kHz (99.500 MHz) RSSI=15 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=99600 kHz (99.600 MHz) RSSI=15 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=99700 kHz (99.700 MHz) RSSI=15 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=99800 kHz (99.800 MHz) RSSI=15 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=99900 kHz (99.900 MHz) RSSI=16 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=100000 kHz (100.000 MHz) RSSI=23 SNR=3 VALID=0
up
OK RADIO BAND=FM FREQ=100100 kHz (100.100 MHz) RSSI=17 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=100200 kHz (100.200 MHz) RSSI=16 SNR=2 VALID=0
up
OK RADIO BAND=FM FREQ=100300 kHz (100.300 MHz) RSSI=15 SNR=2 VALID=0
up
OK RADIO BAND=FM FREQ=100400 kHz (100.400 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=100500 kHz (100.500 MHz) RSSI=11 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=100600 kHz (100.600 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=100700 kHz (100.700 MHz) RSSI=15 SNR=2 VALID=0
up
OK RADIO BAND=FM FREQ=100800 kHz (100.800 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=100900 kHz (100.900 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=101000 kHz (101.000 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=101100 kHz (101.100 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=101200 kHz (101.200 MHz) RSSI=12 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=101300 kHz (101.300 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=101400 kHz (101.400 MHz) RSSI=13 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=101500 kHz (101.500 MHz) RSSI=17 SNR=2 VALID=0
up
OK RADIO BAND=FM FREQ=101600 kHz (101.600 MHz) RSSI=18 SNR=4 VALID=0
up
OK RADIO BAND=FM FREQ=101700 kHz (101.700 MHz) RSSI=13 SNR=2 VALID=0
up
OK RADIO BAND=FM FREQ=101800 kHz (101.800 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=101900 kHz (101.900 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102000 kHz (102.000 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102100 kHz (102.100 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102200 kHz (102.200 MHz) RSSI=12 SNR=1 VALID=0
up
OK RADIO BAND=FM FREQ=102300 kHz (102.300 MHz) RSSI=11 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102400 kHz (102.400 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102500 kHz (102.500 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102600 kHz (102.600 MHz) RSSI=9 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102700 kHz (102.700 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102800 kHz (102.800 MHz) RSSI=10 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=102900 kHz (102.900 MHz) RSSI=11 SNR=0 VALID=0
tune 105500
OK RADIO BAND=FM FREQ=105500 kHz (105.500 MHz) RSSI=8 SNR=0 VALID=0
band am
OK RADIO BAND=AM FREQ=1000 kHz (1.000 MHz) RSSI=52 SNR=0 VALID=0
tune 1310
OK RADIO BAND=AM FREQ=1310 kHz (1.310 MHz) RSSI=53 SNR=14 VALID=1
band fm
OK RADIO BAND=FM FREQ=105500 kHz (105.500 MHz) RSSI=6 SNR=0 VALID=0
record start 60
OK RECORD START file=REC007.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
RECORD progress=4.9s queues=0/8,0/8 max-write=19ms
RECORD progress=9.9s queues=0/8,0/8 max-write=19ms
RECORD progress=15.0s queues=0/8,0/8 max-write=21ms
RECORD progress=20.0s queues=0/8,0/8 max-write=23ms
RECORD progress=25.0s queues=0/8,0/8 max-write=23ms
RECORD progress=30.0s queues=0/8,0/8 max-write=23ms
RECORD progress=35.0s queues=0/8,0/8 max-write=23ms
RECORD progress=40.1s queues=0/8,0/8 max-write=23ms
RECORD progress=45.0s queues=1/8,0/8 max-write=23ms
RECORD progress=50.0s queues=0/8,0/8 max-write=23ms
RECORD progress=55.1s queues=0/8,0/8 max-write=24ms
OK RECORD PASS file=REC007.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60115ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=24ms peaks=1542,1546,4248
band am
OK RADIO BAND=AM FREQ=1310 kHz (1.310 MHz) RSSI=58 SNR=11 VALID=1
record start 60
OK RECORD START file=REC008.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
RECORD progress=4.9s queues=0/8,0/8 max-write=50ms
RECORD progress=9.9s queues=0/8,0/8 max-write=50ms
RECORD progress=14.9s queues=1/8,0/8 max-write=50ms
RECORD progress=19.9s queues=0/8,0/8 max-write=50ms
RECORD progress=25.0s queues=0/8,0/8 max-write=50ms
RECORD progress=30.0s queues=0/8,0/8 max-write=50ms
RECORD progress=34.9s queues=1/8,0/8 max-write=50ms
RECORD progress=40.0s queues=0/8,0/8 max-write=50ms
RECORD progress=45.0s queues=0/8,0/8 max-write=50ms
RECORD progress=50.0s queues=1/8,0/8 max-write=50ms
RECORD progress=55.0s queues=0/8,0/8 max-write=50ms
OK RECORD PASS file=REC008.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60112ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=50ms peaks=4651,4648,3846
```

## Spooky Bench WAV inspections

```
(.venv) PS C:\Users\white\projects\embedded\nucleo-h755zi-q\full_spooky_proto> host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file REC007.WAV --timeout 600
{"schema_version": 1, "command": "wav inspect", "result": "pass", "reason": null, "execution": "hardware", "metrics": {"target_health": "healthy", "final_target_state": "running", "tools": {"version": "0.6.0", "source_sha256": "47e134b71a209668eb844bc065dfc8d64b3d5ae2e754fb2abf123539a4c621ce", "python": "3.12.14", "platform": "Windows-11-10.0.26200-SP0", "pyserial": "3.5", "spookyprobe": {"package": "spookyprobe", "version": "0.1.0", "source_commit": "ce039cab6d15171aa069991dd743e1e14e64aeef", "verified_modules": ["__init__.py", "__main__.py", "capture.py", "client.py", "protocol.py"]}, "openocd": {"state": "not_checked", "reason": "not_requested"}}, "capabilities": {"capture": true, "diagnostics": true, "flash": true, "reset": true, "ipc_test": true, "power": false, "trace": false, "crash": false, "probe_counters": false, "probe": true, "sd_test": true, "wav_inspect": true}, "ports": [{"port": "COM1", "serial_number": null, "vid": null, "pid": null, "interface": null, "description": "Communications Port (COM1)"}, {"port": "COM3", "serial_number": "335A34763533", "vid": 1155, "pid": 22336, "interface": null, "description": "USB Serial Device (COM3)"}, {"port": "COM6", "serial_number": "E66540F0A345382D", "vid": 11914, "pid": 12, "interface": null, "description": "USB Serial Device (COM6)"}], "configured": true, "selected": {"probe": {"port": "COM6", "serial_number": "E66540F0A345382D", "vid": 11914, "pid": 12, "interface": null, "description": "USB Serial Device (COM6)"}, "device": {"port": "COM3", "serial_number": "335A34763533", "vid": 1155, "pid": 22336, "interface": null, "description": "USB Serial Device (COM3)"}}, "transfer": {"file": "REC007.WAV", "bytes": 17301548, "frames": 17165, "crc32":"114daeee", "sha256": "4422fd5e6de2df2859d36f916c672e91fc915a7225d38b35199f9517b93fd58c", "duration_ms": 264406}, "wav": {"container": "RIFF/WAVE", "audio_format": 1, "channels": 3, "sample_rate_hz": 48000, "bits_per_sample": 16, "block_align": 6, "byte_rate": 288000, "data_bytes": 17301504, "frames": 2883584, "duration_seconds": 60.074666666666666, "signal": {"radio_left": {"minimum": -1542, "maximum": 1501, "peak": 1542, "mean": -5.408116080544212, "rms": 320.34956419635137, "zero_samples": 3578, "clipped_samples": 0}, "radio_right": {"minimum": -1546, "maximum": 1501, "peak": 1546, "mean": -7.466222242875532, "rms": 320.4051317633955, "zero_samples": 3632, "clipped_samples": 0}, "microphone": {"minimum": -3868, "maximum": 4248, "peak": 4248, "mean": -0.013062217018821022, "rms": 21.383748626262147, "zero_samples": 453654, "clipped_samples": 0}}, "correlations": {"radio_left_radio_right": 0.999971000720459, "radio_left_microphone": -0.0023371141229940706, "radio_right_microphone": -0.002330720887449041}}, "human_required": false, "evidence_complete": true}, "artifacts": {"run_dir": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041207.934661_0000-2fb7da7d", "result": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041207.934661_0000-2fb7da7d\\test-results.json", "transfer": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041207.934661_0000-2fb7da7d\\wav-transfer.jsonl", "wav": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041207.934661_0000-2fb7da7d\\audio\\REC007.WAV"}, "timestamps": {"started_at": "2026-09-25T04:12:07.094181+00:00", "ended_at": "2026-09-25T04:16:32.358273+00:00", "duration_ms": 265265}}
(.venv) PS C:\Users\white\projects\embedded\nucleo-h755zi-q\full_spooky_proto> host/.venv/Scripts/python.exe -m spookybench --json --profile host/bench.local.json wav inspect --file REC008.WAV --timeout 600
{"schema_version": 1, "command": "wav inspect", "result": "pass", "reason": null, "execution": "hardware", "metrics": {"target_health": "healthy", "final_target_state": "running", "tools": {"version": "0.6.0", "source_sha256": "47e134b71a209668eb844bc065dfc8d64b3d5ae2e754fb2abf123539a4c621ce", "python": "3.12.14", "platform": "Windows-11-10.0.26200-SP0", "pyserial": "3.5", "spookyprobe": {"package": "spookyprobe", "version": "0.1.0", "source_commit": "ce039cab6d15171aa069991dd743e1e14e64aeef", "verified_modules": ["__init__.py", "__main__.py", "capture.py", "client.py", "protocol.py"]}, "openocd": {"state": "not_checked", "reason": "not_requested"}}, "capabilities": {"capture": true, "diagnostics": true, "flash": true, "reset": true, "ipc_test": true, "power": false, "trace": false, "crash": false, "probe_counters": false, "probe": true, "sd_test": true, "wav_inspect": true}, "ports": [{"port": "COM1", "serial_number": null, "vid": null, "pid": null, "interface": null, "description": "Communications Port (COM1)"}, {"port": "COM3", "serial_number": "335A34763533", "vid": 1155, "pid": 22336, "interface": null, "description": "USB Serial Device (COM3)"}, {"port": "COM6", "serial_number": "E66540F0A345382D", "vid": 11914, "pid": 12, "interface": null, "description": "USB Serial Device (COM6)"}], "configured": true, "selected": {"probe": {"port": "COM6", "serial_number": "E66540F0A345382D", "vid": 11914, "pid": 12, "interface": null, "description": "USB Serial Device (COM6)"}, "device": {"port": "COM3", "serial_number": "335A34763533", "vid": 1155, "pid": 22336, "interface": null, "description": "USB Serial Device (COM3)"}}, "transfer": {"file": "REC008.WAV", "bytes": 17301548, "frames": 17165, "crc32":"c897815b", "sha256": "f98a868a8e7dd03ff7e991afa7d72e5fb85d088fd69b55633ef4da59f261f19b", "duration_ms": 263578}, "wav": {"container": "RIFF/WAVE", "audio_format": 1, "channels": 3, "sample_rate_hz": 48000, "bits_per_sample": 16, "block_align": 6, "byte_rate": 288000, "data_bytes": 17301504, "frames": 2883584, "duration_seconds": 60.074666666666666, "signal": {"radio_left": {"minimum": -3748, "maximum": 4651, "peak": 4651, "mean": 22.723249608820137, "rms": 976.3546308104561, "zero_samples": 1486, "clipped_samples": 0}, "radio_right": {"minimum": -3752, "maximum": 4648, "peak": 4648, "mean": 20.210845600474965, "rms": 976.3009296390112, "zero_samples": 1381, "clipped_samples": 0}, "microphone": {"minimum": -3846, "maximum": 3480, "peak": 3846, "mean": -0.013731176202947443, "rms": 18.060166161672498, "zero_samples": 456907, "clipped_samples": 0}}, "correlations": {"radio_left_radio_right": 0.9999997427062159, "radio_left_microphone": -0.0015883144112896939, "radio_right_microphone": -0.0015882454045653767}}, "human_required": false, "evidence_complete": true}, "artifacts": {"run_dir": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041642.920990_0000-153c429d", "result": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041642.920990_0000-153c429d\\test-results.json", "transfer": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041642.920990_0000-153c429d\\wav-transfer.jsonl", "wav": "C:\\Users\\white\\AppData\\Local\\SpookyBench\\runs\\2026-09-25T041642.920990_0000-153c429d\\audio\\REC008.WAV"}, "timestamps": {"started_at": "2026-09-25T04:16:42.337112+00:00", "ended_at": "2026-09-25T04:21:06.510897+00:00", "duration_ms": 264172}}
```