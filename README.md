# MB7369_SnoDpth
Arduino code for using MaxBotix MB7369 ultrasonic ranging sensor [MB7329](https://www.maxbotix.com/ultrasonic_sensors/mb7369.htm) (or similar) for measuring 
distance (e.g., snow depth / water level), and the [SHT30](https://www.adafruit.com/product/4099) for measuring temperature and humidity. Script 
was developed on a Feather [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/) but may work with other configurations. 

# Installation
Assuming that the Arduino IDE is already installed, and configured for use with the [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/), 
this script can be installed using the following commands:

``` bash
cd ~/Arduino
git clone https://github.com/HunterGleason/MB7369_SnoDpth.git
git checkout main
```
This script relies on the following libraries, which can be installed using the Library Manger in the Arduino IDE:

- <RTClib.h> //Needed for communication with Real Time Clock
- <SPI.h> //Needed for working with SD card
- <SD.h> //Needed for working with SD card
- <ArduinoLowPower.h> //Needed for putting Feather M0 to sleep between samples
- <IridiumSBD.h> //Needed for communication with IRIDIUM modem 
- <CSV_Parser.h> //Needed for parsing CSV data
- <Adafruit_SHT31.h>//Needed for communication with SHT30 probe
- <QuickStats.h>//Needed for commuting median

# Operation 
See [schematic](https://github.com/HunterGleason/MB7369_SnoDpth/blob/main/MB7369_SnoDpth.svg) for wiring schematic, and installation section. Once all the required 
libraries are present, we recommend setting the PCF8523 RTC, described in Adafruit [PCF8523](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock/). 
After the RTC has been set, obtain a micro-SD card, and using a text editor, save the 'PARAM.txt' parameter file described below with desired parameters. Usign the IDE upload 
the MB7369_SnoDpth.ino to the MCU. If power is provided and no SD card is inserted the MCU should blink at a 2-sec interval, this is expected. With a battery connected make sure the
power switch is off on the Terminal Block, load a SD card with a PARAM.txt file present (see below). If succsessfull the logger should take a sample, idicated by a breif flash of 
the built in LED and a audible click of the latching relay. If there was an issue, the LED should blink, see the error code section. If the *start_time* + *irid_freq* datetime is in the 
past in relation to the current time on the RTC, the MCU will go directly into trying to send data over IRIDIUM, as there will be no data to send, expect a blank message, or several until
the Iridium send time is greater than the present time on the RTC. 

# Parameter File
An example of the 'PARAM.txt' is shown below, this file must be saved to the micro-SD before using this script:

<br>filename,sample_intvl,irid_freq,start_time,N,ultrasonic_height_mm,metrics_letter_code<br/>
DATA.txt,600,6,12:00:00,15,5000,EFG

There are seven columns that must be present, delimited by commas. The first column named *filename* is the desired name for the data log file, **and must be less than or equal 
to 8 characters, containing only letters and numbers!**. The second column is *sample_intvl*, and is the delay time in seconds between readings. The third column *irid_freq* defines
the interval at which data is sent over Iridium in hours. The *start_time* (HH:MM:SS) indicates the desired 24-hr start time for the first Iridium transmission, the actual start 
time will be equal to *start_time* + *irid_freq*. *N* is the number of samples to take the median of from the ultrasonic ranger during each log. The *ultrasonic_height_mm* parameter is
only used when the first letter of the *metrics_letter_code* is 'E', i.e., snow_depth_mm. It is used to compute depth from distance, however it must not be ommited even if depth is not
being measured, it will be ignored however, a good default is 5000 (max range). The *metrics_letter_code* parameter is used to construct the Iridium string so that the data is ingested
correctly by the Omineca CGI script, see the **Helpful Tables** tables section for letter code to metric translation. Must be three chars long, as three measurments are being logged. 

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
