/*
  DESCRIPTION: See https://github.com/HunterGleason/MB7369_SnoDpth/blob/wth_iridium_hrly/MB7369_SnoDpth.ino
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
int16_t *N; //Number of ultrasonic reange sensor readings to average.
int16_t *irid_freq; // Iridium transmit freqency in hours (Read from PARAM.txt)
char **start_time;// Time at which first Iridum transmission should occur (Read from PARAM.txt)
char **irid_time;// For reading timestamp from IRID.CSV

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

/*Function reads data from a .csv logfile, and uses Iridium modem to send all observations
   since the previous transmission over satellite at midnight on the RTC.
*/
int send_hourly_data()
{

  // For capturing Iridium errors
  int err;


  // Prevent from trying to charge to quickly, low current setup
  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
  modem.enable9603Npower(true); // Enable power for the 9603N
  modem.enableSuperCapCharger(true); // Enable the super capacitor charger

  // Begin satellite modem operation, blink LED (1-sec) if there was an issue
  err = modem.begin();
  if (err != ISBD_SUCCESS)
  {
    digitalWrite(LED, HIGH);
    delay(1000);
    digitalWrite(LED, LOW);
    delay(1000);
    digitalWrite(LED, HIGH);
    delay(1000);
    digitalWrite(LED, LOW);
    delay(1000);
  }

  // Set paramters for parsing the log file
  CSV_Parser cp("sdff", true, ',');

  // Varibles for holding data fields
  char **datetimes;
  int16_t *snow_depths;
  float *air_temps;
  float *rhs;

  // Read HOURLY.CSV file
  cp.readSDfile("/HOURLY.CSV");


  //Populate data arrays from logfile
  datetimes = (char**)cp["datetime"];
  snow_depths = (int16_t*)cp["snow_depth_mm"];
  air_temps = (float*)cp["air_temp_deg_c"];
  rhs = (float*)cp["rh_prct"];

  //Formatted for CGI script >> sensor_letter_code:date_of_first_obs:hour_of_first_obs:data
  String datastring = "EFG:" + String(datetimes[0]).substring(0, 10) + ":" + String(datetimes[0]).substring(11, 13) + ":";


  //For each hour 0-23
  for (int day_hour = 0; day_hour < 24; day_hour++)
  {

    //Declare average vars for each HYDROS21 output
    float mean_depth;
    float mean_temp;
    float mean_rh;
    boolean is_first_obs = false;
    int N = 0;

    //For each observation in the HOURLY.CSV
    for (int i = 0; i < cp.getRowsCount(); i++) {

      //Read the datetime and hour
      String datetime = String(datetimes[i]);
      int dt_hour = datetime.substring(11, 13).toInt();

      //If the hour matches day hour
      if (dt_hour == day_hour)
      {

        //Get data
        float snow_depth = (float) snow_depths[i];
        float air_temp = air_temps[i];
        float rh = rhs[i];

        //Check if this is the first observation for the hour
        if (is_first_obs == false)
        {
          //Update average vars
          mean_depth = snow_depth;
          mean_temp = air_temp;
          mean_rh = rh;
          is_first_obs = true;
          N++;
        } else {
          //Update average vars
          mean_depth = mean_depth + snow_depth;
          mean_temp = mean_temp + air_temp;
          mean_rh = mean_rh + rh;
          N++;
        }

      }
    }

    //Check if there were any observations for the hour
    if (N > 0)
    {
      //Compute averages
      mean_depth = mean_depth / N;
      mean_temp = (mean_temp / N) * 10.0;
      mean_rh = mean_rh / N;


      //Assemble the data string
      datastring = datastring + String(round(mean_depth)) + ',' + String(round(mean_temp)) + ',' + String(round(mean_rh)) + ':';

    }
  }


  
  //Binary bufffer for iridium transmission (max allowed buffer size 340 bytes)
  uint8_t dt_buffer[340];

  // Total bytes in Iridium message 
  int message_bytes = datastring.length();

  //Set buffer index to zero
  int buffer_idx = 0;

  // A boolean var for keeping track of any send attempts that may have failed 
  boolean failed = false;

  //For each byte in the message (i.e. each char)
  for (int i = 0; i < message_bytes; i++)
  {

    //Update the buffer at buffer index with corresponding char
    dt_buffer[buffer_idx] = datastring.charAt(i);

    // Check 340 bytes has been reached, or the end of the message 
    if (buffer_idx == 339 || i == message_bytes)
    {
      
      //Indicate the modem is trying to send with LED
      digitalWrite(LED, HIGH);

      //transmit binary buffer data via iridium
      err = modem.sendSBDBinary(dt_buffer, buffer_idx);
      
      //Will attempt 3 times before giving up 
      int attempt = 1;

      // While message failed to send, or 3 attempts have been exceeded
      while (err != 0 && attempt <= 3)
      {
        // Send the Iridium message
        err = modem.begin();
        err = modem.sendSBDBinary(dt_buffer, buffer_idx);
        attempt = attempt + 1;

      }

      // If all three attempts failed, mark as failed 
      if (err != 0)
      {
        failed = true;
      }

      //Reset buffer index 
      buffer_idx = 0;
      digitalWrite(LED, LOW);

    }else{
      
      //increment buffer index 
      buffer_idx++;
    }

    
  }
  

  //Remove previous daily values CSV as long as send was succesfull
  if (failed == false)
  {
    SD.remove("/HOURLY.CSV");
  }

  //Return err code
  return err;


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
  while (!modem.isConnected())
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


  // Start RTC (1-sec flash LED means RTC did not initialize)
  while (!rtc.begin()) {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

    //Make sure a SD is available (1/2-sec flash LED means SD card did not initialize)
  while (!SD.begin(chipSelect)) {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

   //Set paramters for parsing the parameter file
  CSV_Parser cp(/*format*/ "sss", /*has_header*/ true, /*delimiter*/ ',');

  //Read the parameter file off SD card (snowlog.csv), see README.md 
  cp.readSDfile("/PARAM.txt");

  //Read values from SNOW_PARAM.TXT into global varibles 
  filename = (char**)cp["filename"];
  N = (int16_t*)cp["N"];
  irid_freq = (int16_t*)cp["irid_freq"];
  start_time = (char**)cp["start_time"];

    //Get current logging time from RTC
  DateTime now = rtc.now();

    //Write header if first time writing to the DAILY file
  if (!SD.exists("IRID.CSV"))
  {
    //Write datastring and close logfile on SD card
    dataFile = SD.open("IRID.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.print(now.timestamp(DateTime::TIMESTAMP_DATE)+" "+start_time[0]);
      dataFile.close();
    }
  } 

  CSV_Parser cp("s",true,',');

  cp.readSDfile("/IRID.CSV");

  irid_time = (char**)cp["irid_time"];

  int irid_year = String(irid_time[0]).substring(0,3).toInt();
  int irid_month =  String(irid_time[0]).substring(5,6).toInt();
  int irid_day = String(irid_time[0]).substring(8,9).toInt();
  int irid_hr = String(irid_time[0]).substring(11,12).toInt();
  int irid_min = String(irid_time[0]).substring(14,15).toInt();
  int irid_sec = String(irid_time[0]).substring(17,18).toInt();

  DateTime irid_time_ = DateTime(irid_year,irid_month,irid_day,irid_hr,irid_min,irid_sec);

  if(now >= irid_time_)
  {
    send_hourly_data();
    irid_time_ =  irid_time_+TimeSpan(0,irid_freq[0],0,0);

    SD.remove("IRID.CSV");
    
    //Write datastring and close logfile on SD card
    dataFile = SD.open("IRID.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.print(irid_time_.timestamp(DateTime::TIMESTAMP_FULL));
      dataFile.close();
    }
    
  }

  //Read N average ranging distance from MB7369
  distance = read_sensor(N[0]);


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



  //Assemble a data string for logging to SD, with date-time, snow depth (mm), temperature (deg C) and humidity (%)
  String datastring = yr_str + "-" + mnth_str + "-" + day_str + " " + hr_str + ":" + min_str + ":" + sec_str + "," + String(distance) + ",mm," + String(temp_c) + ",deg C," + String(humid_prct) + ",%";

  //Write datastring and close logfile on SD card
  dataFile = SD.open(filename[0], FILE_WRITE);
  if (dataFile)
  {
    dataFile.println(datastring);
    dataFile.close();
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
