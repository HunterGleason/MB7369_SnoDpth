# MB7369_SnoDpth
Arduino code for using MaxBotix MB7369 ultrasonic ranging sensor [MB7329](https://www.maxbotix.com/ultrasonic_sensors/mb7369.htm) for measuring distance (e.g., snow depth, water level), and the [SHT30](https://www.adafruit.com/product/4099) for measuring temperature and humidity. Script was developed on a Feather [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/) but may work with other configurations. In addition to data being written to a SD card, daily observations are sent to the internet each day at midnight via the Sparkfun Iridium satellite modem [Sparkfun 9603N](https://www.sparkfun.com/products/16394) (The Iridium modem does require a monthly rental service to exchange information with the Iridium satellite network). The total bytes sent over Iridium each day depends on the logging interval, one observation is ~30 bytes.

# Installation
Assuming that the Arduino IDE is already installed, and configured for use with the [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/), this script can be installed using the following commands:

``` bash
cd ~/Arduino
git clone https://github.com/HunterGleason/MB7369_SnoDpth.git
git checkout wth_iridium
```
This script relies on the following libraries, which can be installed using the Library Manger in the Arduino IDE:

- <RTClib.h> //Needed for communication with Real Time Clock
- <SPI.h>//Needed for working with SD card
- <SD.h>//Needed for working with SD card
- <Adafruit_SHT31.h>//Needed for SHT30 Temp/Humid sensor
- <IridiumSBD.h>//Needed for communication with the 9603N Iridium modem
- <Wire.h>//Needed for I2C communication
- <CSV_Parser.h>//Needed for parsing CSV data

Once all the required libraries are present, I recommend setting the PCF8523 RTC, described in Adafruit [PCF8523](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock/). After the RTC has been set, obtain a micro-SD card, and using a text editor, save the 'snowlog.csv' parameter file described below with desired parameters. Check that the switches on [Sparkfun TPL5110](https://www.sparkfun.com/products/15353) match the desired logging interval specified in 'snowlog.csv'. Usign the IDE upload the MB7369_SnoDpth.ino to the MCU, unplug the USB, and plug in the USB adapter wired to the TPL5110 [schematic](https://github.com/HunterGleason/MB7369_SnoDpth/blob/wth_iridium_hrly/MB7369_SnoDpth.svg) into the MCU. If a battery is connected, logging should begin at ~ the specified interval, be sure to disconnect power before removing or installing the micro-SD card. Data will be output under the specified file name in a CSV.

# Operation 
See [schematic](https://github.com/HunterGleason/MB7369_SnoDpth/blob/wth_iridium_hrly/MB7369_SnoDpth.svg) for wiring schematic, and installation section. Time is kept using the PCF8523 real time clock, be sure to set the RTC to the desired time before use [Adafruit PCF8523](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock/). Power management is done using the Sparkfun Low Power timer (TPL5110), and the logging interval is set by adjusting the switches present on the break out board [Sparkfun TPL5110](https://www.sparkfun.com/products/15353). A parameter file named 'snowlog.csv' is required for the operation of the logger, an example of such a file is shown in the **Parameter File** section. This file must be present on the micro-SD card.

# Parameter File
An example of the 'snowlog.csv' is shown below, this file must be saved to the micro-SD before using this script:

filename,N,log_intv<br/>
snowtest.csv,6,5

There are three columns that must be present, delimited by commas. The first column named *filename* is the desired name for the data log file, **and must be less than or equal to 8 characters, containing only letters and numbers!** The second column *N* is the number of samples to average when taking distance readings from the MaxBotix MB7369 ultrasonic sensor. Finally, *log_intv* is the approximate logging interval (in minutes), which is determined by the switch configuration on the Sparkfun TPL5110 breakout board. For proper Iridium communication it is important that the *log_intv* value corresponds with the switch configuration on the TPL5110. If using a *log_intv* less than 5-minutes then Iridium communication may fail as communication time may exceed the TPL5110 power cycle interval.  
