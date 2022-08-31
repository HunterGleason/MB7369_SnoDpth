# MB7369_SnoDpth
Arduino code for using MaxBotix MB7369 ultrasonic ranging sensor [MB7329](https://www.maxbotix.com/ultrasonic_sensors/mb7369.htm) 
for measuring distance (e.g., snow depth, water level), and the [SHT30](https://www.adafruit.com/product/4099) for 
measuring temperature and humidity. Script was developed on a Feather [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/) 
but may work with other configurations. In addition to data being written to a SD card, daily observations are sent to the 
internet each day at midnight via the Sparkfun Iridium satellite modem [Sparkfun 9603N](https://www.sparkfun.com/products/16394) 
(The Iridium modem does require a monthly rental service to exchange information with the Iridium satellite network). The total 
bytes sent over Iridium each day depends on the logging interval, one observation is ~30 bytes.

# Installation
Assuming that the Arduino IDE is already installed, and configured for use with the [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/), 
this script can be installed using the following commands:

``` bash
cd ~/Arduino
git clone https://github.com/HunterGleason/MB7369_SnoDpth.git
git checkout tpl
```
This script relies on the following libraries, which can be installed using the Library Manger in the Arduino IDE:

- <RTClib.h> //Needed for communication with Real Time Clock
- <SPI.h>//Needed for working with SD card
- <SD.h>//Needed for working with SD card
- <Adafruit_SHT31.h>//Needed for SHT30 Temp/Humid sensor
- <IridiumSBD.h>//Needed for communication with the 9603N Iridium modem
- <Wire.h>//Needed for I2C communication
- <CSV_Parser.h>//Needed for parsing CSV data
- <QuickStats.h>//Needed for computing median

Once all the required libraries are present, I recommend setting the PCF8523 RTC, described in Adafruit [PCF8523](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock/). 
After the RTC has been set, obtain a micro-SD card, and using a text editor, save the 'PARAM.txt' parameter file described below with desired parameters. Using the IDE upload 
the MB7369_SnoDpth.ino to the MCU, unplug the USB, and plug in the USB adapter wired to the TPL5110 [schematic](https://github.com/HunterGleason/MB7369_SnoDpth/blob/wth_iridium_hrly/MB7369_SnoDpth.svg) 
into the MCU. If a battery is connected, logging should begin at ~ the specified interval, be sure to disconnect power before removing or installing the micro-SD card, or loading new code. 
Data will be output under the specified file name in a text file.

# Operation 
See [schematic](https://github.com/HunterGleason/MB7369_SnoDpth/blob/wth_iridium_hrly/MB7369_SnoDpth.svg) for wiring schematic, and installation section. 
Time is kept using the PCF8523 real time clock, be sure to set the RTC to the desired time before use [Adafruit PCF8523](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock/). 
Power management is done using the Sparkfun Low Power timer (TPL5110), and the logging interval is set by adjusting the switches present on the break out board [Sparkfun TPL5110](https://www.sparkfun.com/products/15353). 
A parameter file named 'PARAM.txt' is required for the operation of the logger, an example of such a file is shown in the **Parameter File** section. This file must be present on the micro-SD card.

# Parameter File
An example of the 'PARAM.txt' is shown below, this file must be saved to the micro-SD before using this script:

filename,N,irid_freq,start_time,ultrasonic_height_mm,metrics_letter_code<br/>
DATA.TXT,15,6,12:00:00,5000,EFG

There are six columns that must be present, delimited by commas. The first column named *filename* is the desired name for the data log file, **and must be less than or 
equal to 8 characters, containing only letters and numbers!** The second column *N* is the number of samples to average when taking distance readings from the MaxBotix 
MB7369 ultrasonic sensor. *irid_freq* is the Iridium transmit frequency in hours, and *start_time* is the timestamp *HH:MM:SS* seting the time for the first Iridium 
transmission. *ultrasonic_height_mm* is the height of the ultrasonic ranger and is used to compute snow depth in the case that the first letter code of 
*metrics_letter_code* equals 'E' (snow_depth_mm) opposed to 'A' (stage_mm). If 'A', this parameter is ingnored, but a value must be included anyways. 
*metrics_letter_code* is a three letter all-caps string defining the metrics being sent to the database and are defined in the *metrics_lookup* table.  

# Helpful Tables 

## Relevent Metric Letter Codes From Omineca Database 

|obs_id |metric| letter_code|
|--------|--------|--------|
|1 | stage_mm          | A|
|5 | snow_depth_mm     | E|
|6 | air_2m_temp_deg_c | F|
|7 | air_2m_rh_prct    | G|
|8 | air_6m_temp_deg_c | H|
|9 | air_6m_rh_prct    | I|
|10| air_10m_temp_deg_c| J|
|11| air_10m_rh_prct   | K|


## Common TPL5110 Log Intervals 

**DO NOT SET BELOW 5-MIN OR IRIDIUM TRANSMISSION WILL FAIL**

|Interval | Switches ON |
|--------|--------|
|~5 min| C+D+E | 
|8 min| C+D | 
|~12 min| C+E | 
|15 min| D+E | 
|30 min| C | 
|1 hr| D |
|2 hr| E |
