# Radio control service extraction: raw bench transcript

Unedited operator capture for `full_spooky_proto-8lw.2`, summarized in
[September 24 results](2026-09-24-bench-results.md#radio-control-service-extraction-regression).

## UART7 (Pico bridge, 115200 8N1)

```
[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1

========================================
Spooky Box radio-to-headphone bring-up
CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo
Jack detect: line-in PA5=empty, headphone PE0=inserted
========================================
[audio] volume pot PF10/ADC3 ready; initial ADC=33237

[audio] SGTL5000 I2S -> DAC -> headphone setup
[audio] PE2 MCLK approximately 12288025 Hz
[audio] PASS: CHIP_ID=0xA011; headphone path configured muted
[audio] volume ADC=33239 -26.0 dB
[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=24576050 Hz; measured FS=47990 Hz
[sai2] frame=32 bits active=16 bits slots=2 MCKDIV=16; expect 48 kHz/1.536 MHz

[radio] Si4735 digital multi-band setup
[radio] tuned FM 99100 kHz (99.100 MHz): RSSI=15 dBuV SNR=0 dB valid=0
[radio] digital output enabled: 48 kHz, 16-bit stereo I2S
[bridge] SAI1 PE4 measured FS=47994 Hz
[bridge] PASS: SAI2 RX DMA -> SAI1 TX DMA is running

[summary] PASS: radio audio is routed to the headphone codec
[summary] Output follows PE0 jack detect and the PF10 volume pot
[sd] CLI ready; card detect=present, initialization deferred
[record] three-channel recorder ready; default=60s

[fuel] BQ27441-G1A fuel-gauge test
[fuel] I2C2 PB10/PB11; expected address 0x55
[fuel] target configuration: 3700 mAh / 13690 mWh
[fuel] PASS: DEVICE_TYPE=0x0421 FW=0x0109 DM=0x48 CHEM_ID=0x0128
[fuel] 4167 mV, SOC=100%, 26.1 C, idle at 0 mA / 0 mW
[fuel] remaining=3352 mAh, full=3350 mAh, design=3700 mAh; SOH=89% status=1
[fuel] flags=0x0088 BAT_DET=1 ITPOR=0 CHG=0 DSG=0 FC=0 SOC1=0 SOCF=0 CFGUP=0
[fuel] CONFIG: already applied; no data-memory write needed

[mag] TMAG5273 magnetometer test
[mag] I2C2 PB10/PB11; expected address 0x35; MAG_INT PC7 unused
[mag] PASS: manufacturer=0x5449 device=0x06 variant=A2 range=+/- 133000 uT
[mag] initial: MAG X=-28uT Y=20uT Z=142uT RAW=-7,5,35 SET=6 READY=1 DIAG=0
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
[audio] volume ADC=34367 -25.0 dB
[audio] volume ADC=35624 -24.0 dB
[audio] volume ADC=36796 -23.0 dB
[audio] volume ADC=38033 -22.0 dB
[audio] volume ADC=37328 -23.0 dB
[radio] USB tuned FM 101500 kHz (101.500 MHz): RSSI=19 dBuV SNR=3 dB valid=0
[radio] USB tuned FM 108000 kHz (108.000 MHz): RSSI=6 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 108000 kHz (108.000 MHz): RSSI=7 dBuV SNR=0 dB valid=0
[radio] USB tuned FM 107900 kHz (107.900 MHz): RSSI=6 dBuV SNR=0 dB valid=0
[radio] switched to SW 6000 kHz (6.000 MHz): RSSI=0 dBuV SNR=0 dB valid=0
[radio] antenna path=SW whip
[radio] switched to LW 198 kHz (0.198 MHz): RSSI=35 dBuV SNR=0 dB valid=0
[radio] antenna path=AM/LW loop
[radio] switched to AM 1000 kHz (1.000 MHz): RSSI=56 dBuV SNR=0 dB valid=0
[radio] antenna path=AM/LW loop
[radio] USB tuned AM 1500 kHz (1.500 MHz): RSSI=31 dBuV SNR=0 dB valid=0
[radio] switched to FM 107900 kHz (107.900 MHz): RSSI=6 dBuV SNR=0 dB valid=0
[radio] antenna path=FM input
[radio] switched to AM 1500 kHz (1.500 MHz): RSSI=26 dBuV SNR=0 dB valid=0
[radio] antenna path=AM/LW loop
[record] OK RECORD START file=REC006.WAV duration=20s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
[record] RECORD progress=4.9s queues=0/8,0/8 max-write=21ms
[record] RECORD progress=9.9s queues=0/8,0/8 max-write=21ms
[record] RECORD progress=15.0s queues=0/8,0/8 max-write=21ms
[record] RECORD progress=19.9s queues=1/8,0/8 max-write=26ms
[record] OK RECORD PASS file=REC006.WAV frames=962560 bytes=5775360 audio=20.053s elapsed=20094ms
[record] RECORD DIAG queues radio=1/8 pdm=1/8 max-write=26ms peaks=300,298,106
[audio] volume ADC=36315 -23.5 dB

[sleep] preparing low-power charging monitor
[sleep] reporting once before shutdown
[fuel] update: SOC=100%, 4167 mV, idle at 0 mA / 0 mW, 3352/3350 mAh, 26.2 C
[sleep] USB CDC stopped; AUX UART7 remains active
[sleep] RTC wake self-test in 10 seconds, then reports every 5 minutes
[sleep] 3V3_VSYS turns on only while reading the gauge
[sleep] 5V_VSYS and the Babysitter power path remain on
[sleep] press the blue USER button or RESET to reboot
[sleep] 3V3_VSYS disabled; CM7 entering SLEEP mode
[sleep] RTC wake self-test passed; five-minute cadence armed
[fuel] update: SOC=100%, 4167 mV, idle at 0 mA / 0 mW, 3352/3350 mAh, 26.2 C

[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1

========================================
Spooky Box radio-to-headphone bring-up
CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo
Jack detect: line-in PA5=empty, headphone PE0=inserted
========================================
[audio] volume pot PF10/ADC3 ready; initial ADC=36305

[audio] SGTL5000 I2S -> DAC -> headphone setup
[audio] PE2 MCLK approximately 12288025 Hz
[audio] PASS: CHIP_ID=0xA011; headphone path configured muted
[audio] volume ADC=36305 -23.5 dB
[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=24576050 Hz; measured FS=47995 Hz
[sai2] frame=32 bits active=16 bits slots=2 MCKDIV=16; expect 48 kHz/1.536 MHz

[radio] Si4735 digital multi-band setup
[radio] tuned FM 99100 kHz (99.100 MHz): RSSI=16 dBuV SNR=0 dB valid=0
[radio] digital output enabled: 48 kHz, 16-bit stereo I2S
[bridge] SAI1 PE4 measured FS=47990 Hz
[bridge] PASS: SAI2 RX DMA -> SAI1 TX DMA is running

[summary] PASS: radio audio is routed to the headphone codec
[summary] Output follows PE0 jack detect and the PF10 volume pot
[sd] CLI ready; card detect=present, initialization deferred
[record] three-channel recorder ready; default=60s

[fuel] BQ27441-G1A fuel-gauge test
[fuel] I2C2 PB10/PB11; expected address 0x55
[fuel] target configuration: 3700 mAh / 13690 mWh
[fuel] PASS: DEVICE_TYPE=0x0421 FW=0x0109 DM=0x48 CHEM_ID=0x0128
[fuel] 4167 mV, SOC=100%, 26.3 C, idle at 0 mA / 0 mW
[fuel] remaining=3352 mAh, full=3350 mAh, design=3700 mAh; SOH=89% status=1
[fuel] flags=0x0088 BAT_DET=1 ITPOR=0 CHG=0 DSG=0 FC=0 SOC1=0 SOCF=0 CFGUP=0
[fuel] CONFIG: already applied; no data-memory write needed

[mag] TMAG5273 magnetometer test
[mag] I2C2 PB10/PB11; expected address 0x35; MAG_INT PC7 unused
[mag] PASS: manufacturer=0x5449 device=0x06 variant=A2 range=+/- 133000 uT
[mag] initial: MAG X=-48uT Y=81uT Z=170uT RAW=-12,20,42 SET=2 READY=1 DIAG=0
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
status
OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=15 SNR=0 VALID=0
band
OK RADIO BAND=FM RANGE=87500..108000 kHz STEP=100 kHz
tune 101504
OK RADIO BAND=FM FREQ=101500 kHz (101.500 MHz) RSSI=19 SNR=3 VALID=0
tune 108004
ERR FM range: 87500..108000 kHz
tune abc
ERR usage: TUNE <frequency-kHz>
tune 108000
OK RADIO BAND=FM FREQ=108000 kHz (108.000 MHz) RSSI=6 SNR=0 VALID=0
up
OK RADIO BAND=FM FREQ=108000 kHz (108.000 MHz) RSSI=7 SNR=0 VALID=0
down
OK RADIO BAND=FM FREQ=107900 kHz (107.900 MHz) RSSI=6 SNR=0 VALID=0
band sw
OK RADIO BAND=SW FREQ=6000 kHz (6.000 MHz) RSSI=0 SNR=0 VALID=0
band lw
OK RADIO BAND=LW FREQ=198 kHz (0.198 MHz) RSSI=35 SNR=0 VALID=0
band am
OK RADIO BAND=AM FREQ=1000 kHz (1.000 MHz) RSSI=56 SNR=0 VALID=0
tune 1500
OK RADIO BAND=AM FREQ=1500 kHz (1.500 MHz) RSSI=31 SNR=0 VALID=0
band fm
OK RADIO BAND=FM FREQ=107900 kHz (107.900 MHz) RSSI=6 SNR=0 VALID=0
band am
OK RADIO BAND=AM FREQ=1500 kHz (1.500 MHz) RSSI=26 SNR=0 VALID=0
band xx
ERR usage: BAND FM|AM|SW|LW
record start 20
OK RECORD START file=REC006.WAV duration=20s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
up
ERR RADIO tuning disabled while recording
tune 99100
ERR RADIO tuning disabled while recording
RECORD progress=4.9s queues=0/8,0/8 max-write=21ms
band fm
ERR RADIO tuning disabled while recording
RECORD progress=9.9s queues=0/8,0/8 max-write=21ms
RECORD progress=15.0s queues=0/8,0/8 max-write=21ms
RECORD progress=19.9s queues=1/8,0/8 max-write=26ms
OK RECORD PASS file=REC006.WAV frames=962560 bytes=5775360 audio=20.053s elapsed=20094ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=26ms peaks=300,298,106
volume
OK VOLUME ADC=36315 LEVEL=55% ATTEN=-23.5 dB MUTED=0
sleep start
OK SLEEP START; CDC will disconnect; updates continue on AUX UART7
```