# MB7369_SnoDpth
Arduino code for using MaxBotix MB7369 ultrasonic ranging sensor [MB7329](https://www.maxbotix.com/ultrasonic_sensors/mb7369.htm) for measuring distance (e.g., snow depth, water level), and the [SHT30](https://www.adafruit.com/product/4099) for measuring temperature and humidity. Script was developed on a Feather [Adalogger M0](https://learn.adafruit.com/adafruit-feather-m0-adalogger/) but may work with other configurations. In addition to data being written to a SD card, daily minimum and maximum values for each measurement are computed each day at midnight and sent to the internet using a Sparkfun Iridium satellite modem [Sparkfun 9603N](https://www.sparkfun.com/products/16394) (The Iridium modem does require a monthly rental service to exchange information with the Iridium satellite network).

# Operation 
See [schematic](https://github.com/HunterGleason/MB7369_SnoDpth/blob/wth_iridium/MB7369_SnoDpth.svg) for wiring schematic. Time is kept using the PCF8523 real time clock, be sure to set the RTC to the desired time before use [Adafruit PCF8523](https://learn.adafruit.com/adafruit-pcf8523-real-time-clock/). Power management is done using the Sparkfun Low Power timer (TPL5110), and the logging interval is set by adjusting the switches present on the break out board [Sparkfun TPL5110](https://www.sparkfun.com/products/15353). A parameter file named 'snowlog.csv' is required for the operation of the logger, an example of such a file is shown in the **Parameter File** section. This file must be present on the micro-SD card.

# Parameter File
An example of the 'snowlog.csv' is shown below, this file must be saved to the micro-SD before using this script:

filename,N,log_intv<br/>
snowtest.csv,6,5

There are three columns that must be present, delimited by commas. The first column named *filename* is the desired name for the data log file, **and must be less than or equal to 8 characters, containing only letters and numbers!** The second column *N* is the number of samples to average when taking distance readings from the MaxBotix MB7369 ultrasonic sensor. Finally, *log_intv* is the approximate logging interval, which is determined by the switch configuration on the Sparkfun TPL5110 breakout board. For proper Iridium communication it is important that the *log_intv* value corresponds with the switch configuration on the TPL5110. If using a *log_intv* less than 5-minutes then Iridium communication may fail as communication time may exceed the TPL5110 power cycle interval.  
