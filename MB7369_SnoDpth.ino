

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
#include <IridiumSBD.h>
#include <Wire.h>
#include <CSV_Parser.h>//Needed for parsing CSV data
#include <QuickStats.h>//Needed for comuting median values 
#include <Adafruit_SHT31.h>

/*Create library instances*/
RTC_PCF8523 rtc; //instantiate PCF8523 RTC
File dataFile;//instantiate a logging file
Adafruit_SHT31 sht31 = Adafruit_SHT31();//instantiate SHT30 sensor
QuickStats stats; //initialize an instance of QuickStats class
#define IridiumWire Wire//I2C for Iridium 
IridiumSBD modem(IridiumWire);// Declare the IridiumSBD object using default I2C address


/*Define pinouts*/
const byte pulsePin = 12; //Pulse width pin for reading pw from MaxBotix MB7369 ultrasonic ranger
const byte triggerPin = 11; //Range start / stop pin for MaxBotix MB7369 ultrasonic ranger
const byte donePin = 10; //Done pin for TPL5110 power breakout
const byte chipSelect = 4; //Chip select pin for MicroSD breakout
const byte led = 13; // Pin 13 led

/*Global constants*/
char **filename;//Desired name for data file !!!must be less than equal to 8 char!!!
int16_t *N; //Number of ultrasonic reange sensor readings to average.
int16_t *ultrasonic_height_mm;
int16_t *irid_freq; // Iridium transmit freqency in hours (Read from PARAM.txt)
char **start_time;// Time at which first Iridum transmission should occur (Read from PARAM.txt)
char **irid_time;// For reading timestamp from IRID.CSV
char **dist_letter_code;// Code for adding correct letter code to Iridium string, e.g., 'A' for stage 'E' for snow depth.

/*Global variables*/
int16_t distance; //Variable for holding distance read from MaxBotix MB7369 ultrasonic ranger
int16_t duration; //Variable for holding pw duration read from MaxBotix MB7369 ultrasonic ranger
float temp_deg_c; //Variable for holding SHT30 temperature
float rh_prct; //Variable for holding SHT30 humidity

//Function for averaging N readings from MaxBotix MB7369 ultrasonic ranger
int16_t read_sensor(int N) {

  //Variable for average distance
  float values[N];

  //Start ranging
  digitalWrite(triggerPin, HIGH);
  delay(100);

  //Take N readings
  for (int i = 0; i < N; i++)
  {
    //Get the pulse duration (TOF)
    duration = pulseIn(pulsePin, HIGH);
    //Stop ranging
    //Distance = Duration for MB7369 (mm)
    distance = (float) duration;
    values[i] = distance;
    delay(100);
  }

  digitalWrite(triggerPin, LOW);

  //Compute the median and return

  int16_t med_distance = round(stats.median(values, N));

  return med_distance;
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

  // Begin satellite modem operation, blink led (1-sec) if there was an issue
  err = modem.begin();
  if (err != ISBD_SUCCESS)
  {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
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

  int num_rows = cp.getRowsCount();


  //Populate data arrays from logfile
  datetimes = (char**)cp["datetime"];
  snow_depths = (int16_t*)cp["snow_depth_mm"];
  air_temps = (float*)cp["air_temp_deg_c"];
  rhs = (float*)cp["rh_prct"];

  //Formatted for CGI script >> sensor_letter_code:date_of_first_obs:hour_of_first_obs:data
  String datastring = String(dist_letter_code[0])+"FG:" + String(datetimes[0]).substring(0, 10) + ":" + String(datetimes[0]).substring(11, 13) + ":";


  //Get start and end date information from HOURLY.CSV time series data
  int start_year = String(datetimes[0]).substring(0, 4).toInt();
  int start_month = String(datetimes[0]).substring(5, 7).toInt();
  int start_day = String(datetimes[0]).substring(8, 10).toInt();
  int start_hour = String(datetimes[0]).substring(11, 13).toInt();
  int end_year = String(datetimes[num_rows - 1]).substring(0, 4).toInt();
  int end_month = String(datetimes[num_rows - 1]).substring(5, 7).toInt();
  int end_day = String(datetimes[num_rows - 1]).substring(8, 10).toInt();
  int end_hour = String(datetimes[num_rows - 1]).substring(11, 13).toInt();

  //Set the start time to rounded first datetime hour in CSV
  DateTime start_dt = DateTime(start_year, start_month, start_day, start_hour, 0, 0);
  //Set the end time to end of last datetime hour in CSV
  DateTime end_dt = DateTime(end_year, end_month, end_day, end_hour + 1, 0, 0);
  //For keeping track of the datetime at the end of each hourly interval
  DateTime intvl_dt;

  while (start_dt < end_dt)
  {


    intvl_dt = start_dt + TimeSpan(0, 1, 0, 0);

    //Declare average vars for each HYDROS21 output
    float mean_depth;
    float mean_temp;
    float mean_rh;
    boolean is_first_obs = true;
    int N = 0;

    //For each observation in the HOURLY.CSV
    for (int i = 0; i < num_rows; i++) {

      //Read the datetime and hour
      String datetime = String(datetimes[i]);
      int dt_year = datetime.substring(0, 4).toInt();
      int dt_month = datetime.substring(5, 7).toInt();
      int dt_day = datetime.substring(8, 10).toInt();
      int dt_hour = datetime.substring(11, 13).toInt();
      int dt_min = datetime.substring(14, 16).toInt();
      int dt_sec = datetime.substring(17, 19).toInt();

      DateTime obs_dt = DateTime(dt_year, dt_month, dt_day, dt_hour, dt_min, dt_sec);

      //If the hour matches day hour
      if (obs_dt >= start_dt && obs_dt <= intvl_dt)
      {

        //Get data
        float snow_depth = (float) snow_depths[i];
        float air_temp = air_temps[i];
        float rh = rhs[i];

        //Check if this is the first observation for the hour
        if (is_first_obs == true)
        {
          //Update average vars
          mean_depth = snow_depth;
          mean_temp = air_temp;
          mean_rh = rh;
          is_first_obs = false;
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

    start_dt = intvl_dt;
  }



  //Binary bufffer for iridium transmission (max allowed buffer size 340 bytes)
  uint8_t dt_buffer[340];

  // Total bytes in Iridium message
  int message_bytes = datastring.length();


  //Set buffer index to zero
  int buffer_idx = 0;

  //For each byte in the message (i.e. each char)
  for (int i = 0; i < message_bytes; i++)
  {
    //Update the buffer at buffer index with corresponding char
    dt_buffer[buffer_idx] = datastring.charAt(i);

    buffer_idx++;
  }

  //Indicate the modem is trying to send with led
  digitalWrite(led, HIGH);

  //transmit binary buffer data via iridium
  err = modem.sendSBDBinary(dt_buffer, buffer_idx);

  // If first attemped failed try once more 
  if (err != 0 && err != 13)
  {
    err = modem.begin();
    modem.adjustSendReceiveTimeout(500);
    err = modem.sendSBDBinary(dt_buffer, buffer_idx);
  }

  digitalWrite(led, LOW);


  //Remove previous daily values CSV as long as send was succesfull

  SD.remove("/HOURLY.CSV");


  //Return err code
  return err;


}


//Code runs once upon waking up the TPL5110
void setup() {

  // Start the I2C wire port connected to the satellite modem
  Wire.begin();

  // Check that the Qwiic Iridium is attached (5-sec flash led means Qwiic Iridium did not initialize)
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

  //Set pin modes
  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);
  pinMode(pulsePin, INPUT);
  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);
  pinMode(donePin, OUTPUT);


  // Start RTC (1-sec flash led means RTC did not initialize)
  while (!rtc.begin()) {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

  //Make sure a SD is available (1/2-sec flash led means SD card did not initialize)
  while (!SD.begin(chipSelect)) {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

  //Set paramters for parsing the parameter file
  CSV_Parser cp(/*format*/ "sddsds", /*has_header*/ true, /*delimiter*/ ',');

  //Read the parameter file off SD card (snowlog.csv), see README.md
  cp.readSDfile("/PARAM.txt");

  //Read values from SNOW_PARAM.TXT into global varibles
  filename = (char**)cp["filename"];
  N = (int16_t*)cp["N"];
  irid_freq = (int16_t*)cp["irid_freq"];
  start_time = (char**)cp["start_time"];
  ultrasonic_height_mm = (int16_t*)cp["ultrasonic_height_mm"];
  dist_letter_code = (char**)cp["dist_letter_code"];

  

  //Log file name
  String filestr = String(filename[0]);

  //Get current logging time from RTC
  DateTime dt = rtc.now();

  //Write header if first time writing to the DAILY file
  if (!SD.exists("IRID.CSV"))
  {
    //Write datastring and close logfile on SD card
    dataFile = SD.open("IRID.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.println("irid_time");
      dataFile.print(dt.timestamp(DateTime::TIMESTAMP_DATE) + "T" + start_time[0]);
      dataFile.close();
    }
  }

  CSV_Parser cp1("s", true, ',');

  cp1.readSDfile("/IRID.CSV");

  irid_time = (char**)cp1["irid_time"];

  int irid_year = String(irid_time[0]).substring(0, 4).toInt();
  int irid_month =  String(irid_time[0]).substring(5, 7).toInt();
  int irid_day = String(irid_time[0]).substring(8, 10).toInt();
  int irid_hr = String(irid_time[0]).substring(11, 13).toInt();
  int irid_min = String(irid_time[0]).substring(14, 16).toInt();
  int irid_sec = String(irid_time[0]).substring(17, 19).toInt();

  DateTime irid_time_ = DateTime(irid_year, irid_month, irid_day, irid_hr, irid_min, irid_sec);

  if (dt >= irid_time_)
  {
    send_hourly_data();
    irid_time_ =  irid_time_ + TimeSpan(0, irid_freq[0], 0, 0);

    SD.remove("IRID.CSV");

    //Write datastring and close logfile on SD card
    dataFile = SD.open("IRID.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.println("irid_time");
      dataFile.print(irid_time_.timestamp());
      dataFile.close();
    }

  }

  //Read N average ranging distance from MB7369
  distance = read_sensor(N[0]);
  int16_t snow_depth_mm = ultrasonic_height_mm[0] - distance;


  while (!sht31.begin(0x44)) {  // Start SHT30, Set to 0x45 for alternate i2c addr (2-sec flash led means SHT30 did not initialize)
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  temp_deg_c = sht31.readTemperature();
  rh_prct = sht31.readHumidity();

  //If humidity is above 80% turn on SHT31 heater to evaporate condensation, retake humidity measurement
  if (rh_prct >= 80 )
  {
    sht31.heater(true);
    //Give some time for heater to warm up
    delay(5000);
    rh_prct = sht31.readHumidity();
    sht31.heater(false);
  }

  String datastring = dt.timestamp() + "," + snow_depth_mm + "," + temp_deg_c + "," + rh_prct;

  //Write header if first time writing to the logfile
  if (!SD.exists(filestr.c_str()))
  {
    dataFile = SD.open(filestr.c_str(), FILE_WRITE);
    if (dataFile)
    {
      dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");
      dataFile.close();
    }

  } else {
    //Write datastring and close logfile on SD card
    dataFile = SD.open(filestr.c_str(), FILE_WRITE);
    if (dataFile)
    {
      dataFile.println(datastring);
      dataFile.close();
    }

  }

  //Write header if first time writing to the DAILY file
  if (!SD.exists("HOURLY.CSV"))
  {
    //Write datastring and close logfile on SD card
    dataFile = SD.open("HOURLY.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");
      dataFile.close();
    }
  } else {

    //Write datastring and close logfile on SD card
    dataFile = SD.open("HOURLY.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.println(datastring);
      dataFile.close();
    }
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
