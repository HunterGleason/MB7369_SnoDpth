/*
  DESCRIPTION: Arduino code for MaxBotix MB7369 weather resistant ultrasonic distance sensor, and SHT30 temperature / humidity sensor. For use with Adafruit Feather M0 Adalogger and PCF8523 real time clock.
  Power management is done through the Sparkfun TPL5110 low-power breakout, ~logging interval is determined by arranging switches on the TPL5110 chip (https://www.sparkfun.com/products/15353). All communication is
  I2C, however, MB7369 readings are read via the pulse width output pin. Daily min / max sensor values are transmitted over the Iridium satallite modem at midnight. A paramter file named 'snowlog.csv' must be present 
  on the micro-SD card, see (link) for paramter file details.
  AUTHOR:Hunter Gleason
  AGENCY:FLNRORD
  DATE:2021-10-18
*/

/*Required libraries*/
#include "RTClib.h" //Needed for communication with Real Time Clock
#include <SPI.h>//Needed for working with SD card
#include <SD.h>//Needed for working with SD card
#include "Adafruit_SHT31.h"//Needed for SHT30 Temp/Humid sensor
#include <IridiumSBD.h>
#include <Wire.h>
#include <CSV_Parser.h>//Needed for parsing CSV data 

/*Create library instances*/
RTC_PCF8523 rtc; //instantiate PCF8523 RTC
File dataFile;//instantiate a logging file
Adafruit_SHT31 sht31 = Adafruit_SHT31();//instantiate SHT30 sensor

#define IridiumWire Wire

// Declare the IridiumSBD object using default I2C address
IridiumSBD modem(IridiumWire);


/*Define pinouts*/
const byte pulsePin = 12; //Pulse width pin for reading pw from MaxBotix MB7369 ultrasonic ranger
const byte triggerPin = 11; //Range start / stop pin for MaxBotix MB7369 ultrasonic ranger
const byte donePin = 10; //Done pin for TPL5110 power breakout
const byte chipSelect = 4; //Chip select pin for MicroSD breakout
const byte led = 13; // Pin 13 LED



/*Global constants*/
char **filename;//Desired name for data file !!!must be less than equal to 8 char!!!
char **N; //Number of ultrasonic reange sensor readings to average.
char **logging_intv_min;//Should match settings on TPL5110 (i.e., the switch combination), when using Iridium recommended min logging interval of >=5-min do to required transmit time during periods of poor signal quality, otherwise tranmission may fail. 

/*Global variables*/
long distance; //Variable for holding distance read from MaxBotix MB7369 ultrasonic ranger
long duration; //Variable for holding pw duration read from MaxBotix MB7369 ultrasonic ranger
float temp_c; //Variable for holding SHT30 temperature 
float humid_prct; //Variable for holding SHT30 humidity 

//Function for averaging N readings from MaxBotix MB7369 ultrasonic ranger
long read_sensor(int N) {

  //Variable for average distance
  long avg_dist = 0;

  //Take N readings
  for (int i = 0; i < N; i++)
  {
    //Start ranging
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(30);
    //Get the pulse duration (TOF)
    duration = pulseIn(pulsePin, HIGH);
    //Stop ranging
    digitalWrite(triggerPin, LOW);
    //Distance = Duration for MB7369 (mm)
    distance = duration;

    avg_dist = avg_dist + distance;
  }
  //Compute the average and return
  avg_dist = avg_dist / N;
  return avg_dist;
}


//Code runs once upon waking up the TPL5110
void setup() {

  //Set pin modes

  //turn off built in LED
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);

  pinMode(pulsePin, INPUT);
  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);
  pinMode(donePin, OUTPUT);

  // Start the I2C wire port connected to the satellite modem
  Wire.begin();

  // Check that the Qwiic Iridium is attached (5-sec flash LED means Qwiic Iridium did not initialize)
  while(!modem.isConnected())
  {
    digitalWrite(led, HIGH);
    delay(5000);
    digitalWrite(led, LOW);
    delay(5000);
  }

  // For USB "low current" applications
  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
  modem.sleep(); // Put the modem to sleep
  modem.enable9603Npower(false); // Disable power for the 9603N
  modem.enableSuperCapCharger(false); // Disable the super capacitor charger
  modem.enable841lowPower(true); // Enable the ATtiny841's low power mode


  //Make sure a SD is available (1/2-sec flash LED means SD card did not initialize)
  while (!SD.begin(chipSelect))
  {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

  //Set paramters for parsing the parameter file
  CSV_Parser cp(/*format*/ "sss", /*has_header*/ true, /*delimiter*/ ',');

  //Read the parameter file off SD card (snowlog.csv), see README.md 
  cp.readSDfile("/snowlog.csv");

  //Read values from SNOW_PARAM.TXT into global varibles 
  filename = (char**)cp["filename"];
  N = (char**)cp["N"];
  logging_intv_min = (char**)cp["log_intv"];

  // Start RTC (1-sec flash LED means RTC did not initialize)
  while (!rtc.begin()) {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

  //Get current logging time from RTC
  DateTime now = rtc.now();

  //Read N average ranging distance from MB7369
  distance = read_sensor(String(N[0]).toInt());


  while (!sht31.begin(0x44)) {  // Start SHT30, Set to 0x45 for alternate i2c addr (2-sec flash LED means SHT30 did not initialize)
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  temp_c = sht31.readTemperature();
  humid_prct = sht31.readHumidity();

  //If humidity is above 80% turn on SHT31 heater to evaporate condensation, retake humidity measurement
  if (humid_prct >= 80)
  {
    sht31.heater(true);
    //Give some time for heater to warm up
    delay(5000);
    humid_prct = sht31.readHumidity();
    sht31.heater(false);
  }

  //Format current date time values for writing to SD
  String yr_str = String(now.year());
  String mnth_str;
  if (now.month() >= 10)
  {
    mnth_str = String(now.month());
  } else {
    mnth_str = "0" + String(now.month());
  }

  String day_str;
  if (now.day() >= 10)
  {
    day_str = String(now.day());
  } else {
    day_str = "0" + String(now.day());
  }

  String hr_str;
  if (now.hour() >= 10)
  {
    hr_str = String(now.hour());
  } else {
    hr_str = "0" + String(now.hour());
  }

  String min_str;
  if (now.minute() >= 10)
  {
    min_str = String(now.minute());
  } else {
    min_str = "0" + String(now.minute());
  }


  String sec_str;
  if (now.second() >= 10)
  {
    sec_str = String(now.second());
  } else {
    sec_str = "0" + String(now.second());
  }

  //Assemble a data string for logging to SD, with date-time, snow depth (mm), temperature (deg C) and humidity (%)
  String datastring = yr_str + "-" + mnth_str + "-" + day_str + " " + hr_str + ":" + min_str + ":" + sec_str + "," + String(distance) + ",mm," + String(temp_c) + ",deg C," + String(humid_prct) + ",%";

  //Write datastring and close logfile on SD card
  dataFile = SD.open(String(filename[0]), FILE_WRITE);
  if (dataFile)
  {
    dataFile.println(datastring);
    dataFile.close();
  }

  //Check that the iridium modem is connected and the the clock has just reached midnight (i.e.,current time is within one logging interval of midnight)
  if (now.hour() == 0 && now.minute() <= String(logging_intv_min[0]).toInt() && modem.isConnected())
  {
   

    modem.enableSuperCapCharger(true); // Enable the super capacitor charger
    while (!modem.checkSuperCapCharger()) ; // Wait for the capacitors to charge
    modem.enable9603Npower(true); // Enable power for the 9603N
    modem.begin(); // Wake up the 9603N and prepare it for communications.

    /*Need to obtain the daily observations from the SD card, caclulate daily min / max for snow depth, temperture and RH, and send results over Iridium*/ 
    
    //Get the datetime of the days start (i.e., 24 hours previous to current time)
    DateTime days_start (now - TimeSpan(1,0,0,0));

    //Set paramters for parsing the log file
    CSV_Parser cp("ss-s-s-", false, ',');

    //Varibles for holding data fields 
    char **datetimes;
    long *snowdepths;
    float *temps;
    float *rh;

    //Parse the logfile
    cp.readSDfile(filename[0]);

    //Populate data arrays from logfile
    datetimes = (char**)cp[0];
    snowdepths = (long*)cp[1];
    temps = (float*)cp[3];
    rh = (float*)cp[5];

    //Daily min / max varibles, set to limit values 
    long min_depth=6000;
    long max_depth=-1;
    float min_temp=100;
    float max_temp=-100;
    float min_rh=101;
    float max_rh=-1;

    //For each observation in the CSV 
    for (int i = 0; i < cp.getRowsCount(); i++) {

      //Get the datetime stamp as string 
      String datetime = String(datetimes[i]);

      //Get the observations year, month, day
      int dt_year = datetime.substring(0, 4).toInt();
      int dt_month = datetime.substring(5, 7).toInt();
      int dt_day = datetime.substring(8, 10).toInt();

      //Check if the observations datetime occured during the day being summarised  
      if (dt_year == days_start.year() && dt_month == days_start.month() && dt_day == days_start.day())
      {
        //Update min snow depth
        if(snowdepths[i]<min_depth)
        {
          min_depth=snowdepths[i];
        }
        //Update max snow depth
        if(snowdepths[i]>max_depth)
        {
          max_depth=snowdepths[i];
        }
        //Update min temp
        if(temps[i]<min_temp)
        {
          min_temp=temps[i];
        }
        //Update max temp
        if(temps[i]>max_temp)
        {
          max_temp=temps[i];
        }
        //Update min rh
        if(rh[i]<min_rh)
        {
          min_rh=rh[i];
        }
        //Update max rh
        if(rh[i]>max_rh)
        {
          max_rh=rh[i];
        }

      }

    }

    //A data string with daily min / max results for sending over Iridium 
    datastring = "{" + yr_str + "-" + mnth_str + "-" + day_str + " " + hr_str + ":" + min_str + ":" + sec_str + "," + String(min_depth) + "," + String(max_depth) + "," + String(min_temp) + "," + String(max_temp) + "," + String(min_rh) + "," + String(max_rh) + "}";   

    //Delete this block, trouble shooting 
    dataFile = SD.open(String(filename[0]), FILE_WRITE);
    if (dataFile)
    {
      dataFile.println(datastring);
      dataFile.close();
    }

    
    modem.sendSBDText(datastring.c_str()); // Send a message
    modem.sleep(); // Put the modem to sleep
    modem.enable9603Npower(false); // Disable power for the 9603N
    modem.enableSuperCapCharger(false); // Disable the super capacitor charger
    modem.enable841lowPower(true); // Enable the ATtiny841's low power mode (optional)
 
  }


}

void loop() {


  // We're done!
  // It's important that the donePin is written LOW and THEN HIGH. This shift
  // from low to HIGH is how the TPL5110 Nano Power Timer knows to turn off the
  // microcontroller.
  digitalWrite(donePin, LOW);
  digitalWrite(donePin, HIGH);
  delay(10);

}
