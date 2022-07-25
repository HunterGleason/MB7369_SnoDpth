/*
  DESCRIPTION:See https://github.com/HunterGleason/MB7369_SnoDpth/tree/wth_iridium
  AUTHOR:Hunter Gleason
  AGENCY:FLNRORD
  DATE:2022-04-06
*/

/*Required libraries*/
#include "RTClib.h" //Needed for communication with Real Time Clock
#include <SPI.h>//Needed for working with SD card
#include <SD.h>//Needed for working with SD card
#include "Adafruit_SHT31.h"//Needed for SHT30 Temp/Humid sensor
#include <IridiumSBD.h>
#include <Wire.h>
#include <CSV_Parser.h>//Needed for parsing CSV data 
#include "QuickStats.h"

/*Create library instances*/
RTC_PCF8523 rtc; //instantiate PCF8523 RTC
File dataFile;//instantiate a logging file
Adafruit_SHT31 sht31 = Adafruit_SHT31();//instantiate SHT30 sensor
QuickStats stats; //initialize an instance of stats

#define IridiumWire Wire

// Declare the IridiumSBD object using default I2C address
IridiumSBD modem(IridiumWire);


/*Define pinouts*/
const byte pwPin = 9; //Pulse width pin for reading pw from MaxBotix MB7369 ultrasonic ranger
const byte rangePin = 12; //Range start / stop pin for MaxBotix MB7369 ultrasonic ranger
const byte donePin = 10; //Done pin for TPL5110 power breakout
const byte chipSelect = 4; //Chip select pin for MicroSD breakout
const byte led = 13; // Pin 13 LED



/*Global constants*/
char **filename;//Desired name for data file !!!must be less than equal to 8 char!!!
char **N; //Number of ultrasonic reange sensor readings to average.
char **sensor_height_mm; //Height of ultrasonic sensor above ground in mm

/*Global variables*/
float distance; //Variable for holding sample distance read from MaxBotix MB7369 ultrasonic ranger
float med_dist;//Variable for holding median distance read from MaxBotix MB7369 ultrasonic ranger
float depth; //Variable for holding snow depth computed from distance (i.e., sensor height - distance)
float temp_c; //Variable for holding SHT30 temperature
float humid_prct; //Variable for holding SHT30 humidity

//Function for reading MaxBotix MB7369 ultrasonic ranger using pulse width method
void read_sensor(){
  digitalWrite(rangePin,HIGH);
  delayMicroseconds(50); 
  distance = pulseIn(pwPin, HIGH);
  digitalWrite(rangePin,LOW);
}


//Code runs once upon waking up the TPL5110
void setup() {


  //Set pin modes

  //turn off built in LED
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  pinMode(pwPin, INPUT);
  pinMode(rangePin, OUTPUT);
  digitalWrite(rangePin, LOW);
  pinMode(donePin, OUTPUT);


  // Start the I2C wire port connected to the satellite modem
  Wire.begin();

  // Check that the Qwiic Iridium is attached (5-sec flash LED means Qwiic Iridium did not initialize)
  while (!modem.isConnected())
  {
    digitalWrite(led, HIGH);
    delay(10000);
    digitalWrite(led, LOW);
    delay(10000);
  }

  // For USB "low current" applications
  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
  modem.enable9603Npower(false); // Disable power for the 9603N
  modem.enableSuperCapCharger(false); // Disable the super capacitor charger
  modem.enable841lowPower(true); // Enable the ATtiny841's low power mode
  modem.sleep(); // Put the modem to sleep


  //Make sure a SD is available (1-sec flash LED means SD card did not initialize)
  while (!SD.begin(chipSelect))
  {
    digitalWrite(led, HIGH);
    delay(5000);
    digitalWrite(led, LOW);
    delay(5000);
  }

  //Set paramters for parsing the parameter file
  CSV_Parser cp(/*format*/ "sss", /*has_header*/ true, /*delimiter*/ ',');

  //Read the parameter file off SD card (snowlog.csv), 1/4-sec flash means file is not available
  while (!cp.readSDfile("/snowlog.csv"))
  {
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  //Read values from snowlog.csv into global varibles
  filename = (char**)cp["filename"];
  N = (char**)cp["N"];
  
  //Get the sensor height from snowlog.csv
  sensor_height_mm = (char**)cp["sensor_height_mm"];

  // Start RTC (10-sec flash LED means RTC did not initialize)
  while (!rtc.begin())
  {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

  //Get current logging time from RTC
  DateTime now = rtc.now();

  //Read N average ranging distance from MB7369
  int n = String(N[0]).toInt();
  float samples[n];
  int i = 0;

  float sensor_height = String(sensor_height_mm[0]).toFloat();
  while (i < n)
  {
    read_sensor();

    samples[i] = distance;
    i++;

    delay(500);
  }

  med_dist = stats.median(samples, n);

  depth = sensor_height - med_dist;



  while (!sht31.begin(0x44))
  { // Start SHT30, Set to 0x45 for alternate i2c addr (2-sec flash LED means SHT30 did not initialize)
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

  temp_c = sht31.readTemperature();
  humid_prct = sht31.readHumidity();

  //If humidity is above 80% turn on SHT31 heater to evaporate condensation, retake humidity measurement
  if (humid_prct >= 80)
  {
    sht31.heater(true);
    //Give some time for heater to warm up (no documentation on required time, 2 sec adaquate?)
    delay(3000);
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
  String datastring = yr_str + "-" + mnth_str + "-" + day_str + " " + hr_str + ":" + min_str + ":" + sec_str + "," + String(distance) + "," + String(depth) + "," + String(temp_c) + "," + String(humid_prct);

  //Write header if first time writing to the file
  if (!SD.exists(filename[0]))
  {
    dataFile = SD.open(filename[0], FILE_WRITE);
    if (dataFile)
    {
      dataFile.println("datetime,distance_mm,depth_mm,temp_deg_c,rh_prct");
      dataFile.close();
    }

  }

  //Write datastring and close logfile on SD card
  dataFile = SD.open(filename[0], FILE_WRITE);
  if (dataFile)
  {
    dataFile.println(datastring);
    dataFile.close();
  }

  //Check that the iridium modem is connected and the the clock has just reached midnight (i.e.,current time is within one logging interval of midnight)
  if ((int) now.hour() == 0)
  {

    if (!SD.exists("IRID.CSV"))
    {
      dataFile = SD.open("IRID.CSV", FILE_WRITE);
      dataFile.println("day,day1");
      dataFile.println(String(now.day()) + "," + String(now.day()));
      dataFile.close();

    }


    CSV_Parser cp(/*format*/ "s-", /*has_header*/ true, /*delimiter*/ ',');


    while (!cp.readSDfile("/IRID.CSV"))
    {
      digitalWrite(led, HIGH);
      delay(100);
      digitalWrite(led, LOW);
      delay(100);
    }

    char **irid_day = (char**)cp["day"];

    if (String(irid_day[0]).toInt() == (int) now.day())
    {

      //Update IRID.CSV with new day
      SD.remove("IRID.CSV");
      dataFile = SD.open("IRID.CSV", FILE_WRITE);
      dataFile.println("day,day1");
      DateTime next_day = (DateTime(now.year(), now.month(), now.day()) + TimeSpan(1, 0, 0, 0));
      dataFile.println(String(next_day.day()) + "," + String(next_day.day()));
      dataFile.close();


      modem.enableSuperCapCharger(true); // Enable the super capacitor charger
      while (!modem.checkSuperCapCharger()) ; // Wait for the capacitors to charge
      modem.enable9603Npower(true); // Enable power for the 9603N
      modem.begin(); // Wake up the 9603N and prepare it for communications.

      /*Need to obtain the daily observations from the SD card, caclulate daily min / max for snow depth, temperture and RH, and send results over Iridium*/

      //Get the datetime of the days start (i.e., 24 hours previous to current time)
      DateTime days_start (now - TimeSpan(1, 0, 0, 0));

      //Set paramters for parsing the log file
      CSV_Parser cp("s-sss", true, ',');

      //Varibles for holding data fields
      char **datetimes;
      char **snowdepths;
      char **temps;
      char **rhs;

      //Parse the logfile
      cp.readSDfile(filename[0]);

      //Populate data arrays from logfile
      datetimes = (char**)cp["datetime"];
      snowdepths = (char**)cp["depth_mm"];
      temps = (char**)cp["temp_deg_c"];
      rhs = (char**)cp["rh_prct"];

      //Declare daily min / max varibles, set to limit values
      long min_depth = 6000; //Max distance is 5000mm
      long max_depth = -1; //Min distance is 0 mm
      float min_temp = 100; //100 deg C should not be reached in outdoor conditions
      float max_temp = -100; //-100 deg C should not be reached in outdoor conditions
      float min_rh = 101; //Max RH is 100 %
      float max_rh = -1; //Min RH is 0 %

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
          long snowdepth = String(snowdepths[i]).toInt();
          float temp = String(temps[i]).toFloat();
          float rh = String(rhs[i]).toFloat();

          //Update min snow depth
          if (snowdepth < min_depth)
          {
            min_depth = snowdepth;
          }
          //Update max snow depth
          if (snowdepth > max_depth)
          {
            max_depth = snowdepth;
          }
          //Update min temp
          if (temp < min_temp)
          {
            min_temp = temp;
          }
          //Update max temp
          if (temp > max_temp)
          {
            max_temp = temp;
          }
          //Update min rh
          if (rh < min_rh)
          {
            min_rh = rh;
          }
          //Update max rh
          if (rh > max_rh)
          {
            max_rh = rh;
          }

        }

      }

      //A data string with daily min / max results for sending over Iridium
      datastring = "{" + yr_str + "-" + mnth_str + "-" + day_str + " " + hr_str + ":" + min_str + ":" + sec_str + "," + String(min_depth) + ":min_depth_mm," + String(max_depth) + ":max_depth_mm," + String(min_temp) + ":min_temp_degC," + String(max_temp) + ":max_temp_degC," + String(min_rh) + ":min_rh_prct," + String(max_rh) + ":max_rh_prct}";

      modem.sendSBDText(datastring.c_str()); // Send datastring message
      modem.sleep(); // Put the modem to sleep
      modem.enable9603Npower(false); // Disable power for the 9603N
      modem.enableSuperCapCharger(false); // Disable the super capacitor charger
      modem.enable841lowPower(true); // Enable the ATtiny841's low power mode (optional)

    }
  }
}

void loop()
{


  // We're done!
  // It's important that the donePin is written LOW and THEN HIGH. This shift
  // from low to HIGH is how the TPL5110 Nano Power Timer knows to turn off the
  // microcontroller.
  digitalWrite(donePin, LOW);
  delay(10);
  digitalWrite(donePin, HIGH);
  delay(10);

}
