# Codec/volume service extraction: raw bench transcript

Unedited operator capture for `full_spooky_proto-8lw.1`, summarized in
[September 24 results](2026-09-24-bench-results.md#codec-and-volume-service-extraction-regression).
The CDC section shows local echo interleaved with asynchronous progress lines.

## UART7 (Pico bridge, 115200 8N1)

```

[uart] Spooky Probe console on UART7: PE8 TX, PE7 RX, 115200 8N1

========================================
Spooky Box radio-to-headphone bring-up
CM7 64 MHz HSI; FM/AM/SW/LW USB-tunable, 48 kHz stereo
Jack detect: line-in PA5=empty, headphone PE0=empty
========================================
[audio] volume pot PF10/ADC3 ready; initial ADC=44193

[audio] SGTL5000 I2S -> DAC -> headphone setup
[audio] PE2 MCLK approximately 12288025 Hz
[audio] PASS: CHIP_ID=0xA011; headphone path configured muted
[audio] volume ADC=44194 -17.5 dB
[sai2] PD11 RX, PD12 FS, PD13 SCK; kernel=24576050 Hz; measured FS=47994 Hz
[sai2] frame=32 bits active=16 bits slots=2 MCKDIV=16; expect 48 kHz/1.536 MHz

[radio] Si4735 digital multi-band setup
[radio] tuned FM 99100 kHz (99.100 MHz): RSSI=17 dBuV SNR=1 dB valid=0
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
[fuel] 4168 mV, SOC=100%, 26.9 C, idle at 0 mA / 0 mW
[fuel] remaining=3352 mAh, full=3350 mAh, design=3700 mAh; SOH=89% status=1
[fuel] flags=0x0088 BAT_DET=1 ITPOR=0 CHG=0 DSG=0 FC=0 SOC1=0 SOCF=0 CFGUP=0
[fuel] CONFIG: already applied; no data-memory write needed

[mag] TMAG5273 magnetometer test
[mag] I2C2 PB10/PB11; expected address 0x35; MAG_INT PC7 unused
[mag] PASS: manufacturer=0x5449 device=0x06 variant=A2 range=+/- 133000 uT
[mag] initial: MAG X=-52uT Y=60uT Z=170uT RAW=-13,15,42 SET=0 READY=1 DIAG=0
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
[audio] headphones removed; output muted
[audio] headphones inserted; output enabled under volume-pot control
[audio] headphones removed; output muted
[audio] headphones inserted; output enabled under volume-pot control
[audio] volume ADC=42842 -18.5 dB
[audio] volume ADC=41725 -19.5 dB
[audio] volume ADC=40118 -20.5 dB
[audio] volume ADC=39180 -21.5 dB
[audio] volume ADC=37959 -22.5 dB
[audio] volume ADC=36671 -23.5 dB
[audio] volume ADC=35432 -24.5 dB
[audio] volume ADC=34091 -25.5 dB
[audio] volume ADC=32957 -26.5 dB
[audio] volume ADC=31393 -27.5 dB
[audio] volume ADC=30298 -28.5 dB
[audio] volume ADC=29101 -29.5 dB
[audio] volume ADC=27893 -30.5 dB
[audio] volume ADC=26587 -31.5 dB
[audio] volume ADC=25034 -32.5 dB
[audio] volume ADC=23971 -33.5 dB
[audio] volume ADC=22604 -34.5 dB
[audio] volume ADC=21622 -35.5 dB
[audio] volume ADC=20422 -36.5 dB
[audio] volume ADC=19172 -37.5 dB
[audio] volume ADC=17752 -38.5 dB
[audio] volume ADC=16452 -39.5 dB
[audio] volume ADC=15331 -40.5 dB
[audio] volume ADC=14051 -41.5 dB
[audio] volume ADC=12641 -42.5 dB
[audio] volume ADC=11639 -43.5 dB
[audio] volume ADC=10352 -44.5 dB
[audio] volume ADC=9026 -45.5 dB
[audio] volume ADC=7912 -46.5 dB
[audio] volume ADC=6610 -47.5 dB
[audio] volume ADC=5320 -48.5 dB
[audio] volume ADC=3837 -49.5 dB
[audio] volume ADC=2681 -50.5 dB
[audio] volume ADC=1572 -51.5 dB
[audio] volume ADC=923 MUTED
[audio] volume ADC=1143 -51.5 dB
[audio] volume ADC=2508 -50.5 dB
[audio] volume ADC=3549 -49.5 dB
[audio] volume ADC=4837 -48.5 dB
[audio] volume ADC=6074 -47.5 dB
[audio] volume ADC=7299 -46.5 dB
[audio] volume ADC=8625 -45.5 dB
[audio] volume ADC=10016 -44.5 dB
[audio] volume ADC=11047 -43.5 dB
[audio] volume ADC=12327 -42.5 dB
[audio] volume ADC=13583 -41.5 dB
[audio] volume ADC=15000 -40.5 dB
[audio] volume ADC=16379 -39.5 dB
[audio] volume ADC=17514 -38.5 dB
[audio] volume ADC=18670 -37.5 dB
[audio] volume ADC=19985 -36.5 dB
[audio] volume ADC=21081 -35.5 dB
[audio] volume ADC=22321 -34.5 dB
[audio] volume ADC=23670 -33.5 dB
[audio] volume ADC=25032 -32.5 dB
[audio] volume ADC=26241 -31.5 dB
[audio] volume ADC=27390 -30.5 dB
[audio] volume ADC=28718 -29.5 dB
[audio] volume ADC=30111 -28.5 dB
[audio] volume ADC=31195 -27.5 dB
[audio] volume ADC=32780 -26.5 dB
[audio] volume ADC=33831 -25.5 dB
[audio] volume ADC=35236 -24.5 dB
[audio] volume ADC=36200 -23.5 dB
[audio] volume ADC=37371 -22.5 dB
[audio] volume ADC=38801 -21.5 dB
[audio] volume ADC=40157 -20.5 dB
[audio] volume ADC=41412 -19.5 dB
[audio] volume ADC=42370 -18.5 dB
[audio] volume ADC=43648 -17.5 dB
[audio] volume ADC=45008 -16.5 dB
[audio] volume ADC=46193 -15.5 dB
[audio] volume ADC=47537 -14.5 dB
[audio] volume ADC=48714 -13.5 dB
[audio] volume ADC=49946 -12.5 dB
[audio] volume ADC=51130 -11.5 dB
[audio] volume ADC=50428 -12.5 dB
[audio] volume ADC=49216 -13.5 dB
[audio] volume ADC=47951 -14.5 dB
[audio] volume ADC=46699 -15.5 dB
[audio] volume ADC=45447 -16.5 dB
[audio] volume ADC=44135 -17.5 dB
[audio] volume ADC=42977 -18.5 dB
[audio] volume ADC=41715 -19.5 dB
[radio] switched to AM 1000 kHz (1.000 MHz): RSSI=63 dBuV SNR=0 dB valid=0
[radio] antenna path=AM/LW loop
[radio] switched to FM 99100 kHz (99.100 MHz): RSSI=17 dBuV SNR=0 dB valid=0
[radio] antenna path=FM input
[audio] volume ADC=40270 -20.5 dB
[audio] volume ADC=39110 -21.5 dB
[audio] volume ADC=37971 -22.5 dB
[audio] volume ADC=36628 -23.5 dB
[audio] volume ADC=35244 -24.5 dB
[audio] volume ADC=33860 -25.5 dB
[audio] volume ADC=32693 -26.5 dB
[audio] volume ADC=31372 -27.5 dB
[audio] volume ADC=30047 -28.5 dB
[audio] volume ADC=28884 -29.5 dB
[audio] volume ADC=27907 -30.5 dB
[audio] volume ADC=26116 -31.5 dB
[audio] volume ADC=25276 -32.5 dB
[audio] volume ADC=23366 -34.0 dB
[audio] volume ADC=21484 -35.5 dB
[audio] volume ADC=19881 -36.5 dB
[audio] volume ADC=18613 -37.5 dB
[audio] volume ADC=17601 -38.5 dB
[audio] volume ADC=16510 -39.5 dB
[audio] volume ADC=15294 -40.5 dB
[audio] volume ADC=14152 -41.5 dB
[audio] volume ADC=12692 -42.5 dB
[audio] volume ADC=11308 -43.5 dB
[audio] volume ADC=9883 -44.5 dB
[audio] volume ADC=9039 -45.5 dB
[audio] volume ADC=7222 -47.0 dB
[audio] volume ADC=5614 -48.0 dB
[audio] volume ADC=4331 -49.0 dB
[audio] volume ADC=3316 -50.0 dB
[audio] volume ADC=2222 -51.0 dB
[audio] volume ADC=997 MUTED
[radio] switched to AM 1000 kHz (1.000 MHz): RSSI=58 dBuV SNR=0 dB valid=0
[radio] antenna path=AM/LW loop
[audio] volume ADC=1236 -51.5 dB
[audio] volume ADC=2324 -50.5 dB
[audio] volume ADC=3575 -49.5 dB
[audio] volume ADC=4874 -48.5 dB
[audio] volume ADC=6133 -47.5 dB
[audio] volume ADC=7292 -46.5 dB
[audio] volume ADC=8714 -45.5 dB
[audio] volume ADC=10111 -44.5 dB
[audio] volume ADC=11098 -43.5 dB
[audio] volume ADC=12475 -42.5 dB
[audio] volume ADC=13594 -41.5 dB
[audio] volume ADC=14971 -40.5 dB
[audio] volume ADC=16259 -39.5 dB
[audio] volume ADC=17452 -38.5 dB
[audio] volume ADC=18657 -37.5 dB
[audio] volume ADC=19870 -36.5 dB
[audio] volume ADC=21348 -35.5 dB
[audio] volume ADC=22549 -34.5 dB
[audio] volume ADC=23768 -33.5 dB
[audio] volume ADC=24899 -32.5 dB
[audio] volume ADC=26122 -31.5 dB
[audio] volume ADC=27459 -30.5 dB
[audio] volume ADC=28839 -29.5 dB
[audio] volume ADC=30003 -28.5 dB
[audio] volume ADC=31161 -27.5 dB
[audio] volume ADC=32411 -26.5 dB
[audio] volume ADC=33926 -25.5 dB
[audio] volume ADC=35008 -24.5 dB
[audio] volume ADC=36134 -23.5 dB
[audio] volume ADC=37355 -22.5 dB
[audio] volume ADC=38621 -21.5 dB
[audio] volume ADC=40082 -20.5 dB
[audio] volume ADC=41445 -19.5 dB
[audio] volume ADC=42687 -18.5 dB
[audio] volume ADC=43883 -17.5 dB
[audio] volume ADC=44937 -16.5 dB
[audio] volume ADC=46120 -15.5 dB
[audio] volume ADC=45478 -16.5 dB
[audio] volume ADC=44223 -17.5 dB
[audio] volume ADC=42900 -18.5 dB
[audio] headphones removed; output muted
[radio] switched to FM 99100 kHz (99.100 MHz): RSSI=17 dBuV SNR=1 dB valid=0
[radio] antenna path=FM input
[audio] headphones inserted; output enabled under volume-pot control
[record] OK RECORD START file=REC005.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
[record] RECORD progress=4.9s queues=0/8,0/8 max-write=17ms
[audio] volume ADC=41652 -19.5 dB
[audio] volume ADC=40311 -20.5 dB
[audio] volume ADC=39198 -21.5 dB
[audio] volume ADC=37713 -22.5 dB
[audio] volume ADC=36581 -23.5 dB
[audio] volume ADC=35430 -24.5 dB
[audio] volume ADC=34217 -25.5 dB
[audio] volume ADC=32949 -26.5 dB
[audio] volume ADC=33688 -25.5 dB
[audio] volume ADC=34968 -24.5 dB
[audio] volume ADC=36251 -23.5 dB
[audio] volume ADC=37545 -22.5 dB
[audio] volume ADC=38715 -21.5 dB
[audio] volume ADC=40033 -20.5 dB
[audio] volume ADC=41352 -19.5 dB
[audio] volume ADC=42462 -18.5 dB
[audio] volume ADC=43744 -17.5 dB
[audio] volume ADC=44933 -16.5 dB
[audio] volume ADC=44071 -17.5 dB
[audio] volume ADC=42777 -18.5 dB
[audio] volume ADC=41592 -19.5 dB
[audio] volume ADC=40386 -20.5 dB
[audio] volume ADC=39160 -21.5 dB
[audio] volume ADC=37951 -22.5 dB
[audio] volume ADC=36619 -23.5 dB
[audio] volume ADC=37597 -22.5 dB
[audio] volume ADC=38662 -21.5 dB
[record] RECORD progress=9.9s queues=0/8,0/8 max-write=17ms
[audio] headphones removed; output muted
[audio] headphones inserted; output enabled under volume-pot control
[audio] headphones removed; output muted
[record] RECORD progress=15.0s queues=0/8,0/8 max-write=23ms
[audio] headphones inserted; output enabled under volume-pot control
[record] RECORD progress=19.9s queues=1/8,0/8 max-write=23ms
[record] RECORD progress=25.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=30.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=35.0s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=40.1s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=45.1s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=50.1s queues=0/8,0/8 max-write=23ms
[record] RECORD progress=55.1s queues=1/8,0/8 max-write=23ms
[record] OK RECORD PASS file=REC005.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60111ms
[record] RECORD DIAG queues radio=1/8 pdm=1/8 max-write=23ms peaks=1781,1780,6069
[record] OK RECORD IDLE last-file=REC005.WAV frames=2883584 max-write=23ms
[fuel] update: SOC=100%, 4168 mV, idle at 0 mA / 0 mW, 3352/3350 mAh, 26.7 C
```

## USB CDC CLI

```
Spooky Box USB CLI ready
Type HELP for commands.
band am
OK RADIO BAND=AM FREQ=1000 kHz (1.000 MHz) RSSI=63 SNR=0 VALID=0
band fm
OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=17 SNR=0 VALID=0
band am
OK RADIO BAND=AM FREQ=1000 kHz (1.000 MHz) RSSI=58 SNR=0 VALID=0
band fm
OK RADIO BAND=FM FREQ=99100 kHz (99.100 MHz) RSSI=17 SNR=1 VALID=0
record start 60
OK RECORD START file=REC005.WAV duration=60s format=48000Hz/16-bit/3ch [radio-L,radio-R,mic]
RECORD progress=4.9s queues=0/8,0/8 max-write=17ms
RECORD progress=9.9s queues=0/8,0/8 max-write=17ms
RECORD progress=15.0s queues=0/8,0/8 max-write=23ms
rRECORD progress=19.9s queues=1/8,0/8 max-write=23ms
recoreRECORD progress=25.0s queues=0/8,0/8 max-write=23ms                RECORD progress=30.0s queues=0/8,0/8 max-write=23ms
RECORD progress=35.0s queues=0/8,0/8 max-write=23ms
recorRECORD progress=40.1s queues=0/8,0/8 max-write=23ms                   RECORD progress=45.1s queues=0/8,0/8 max-write=23ms
band RECORD progress=50.1s queues=0/8,0/8 max-write=23ms
am
ERR RADIO tuning disabled while recording
band am
ERR RADIO tuning disabled while recording
RECORD progress=55.1s queues=1/8,0/8 max-write=23ms
OK RECORD PASS file=REC005.WAV frames=2883584 bytes=17301504 audio=60.074s elapsed=60111ms
RECORD DIAG queues radio=1/8 pdm=1/8 max-write=23ms peaks=1781,1780,6069
record status
OK RECORD IDLE last-file=REC005.WAV frames=2883584 max-write=23ms
```