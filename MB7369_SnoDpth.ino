/*Required libraries*/
#include "RTClib.h" //For communication with Real Time Clock
#include <SPI.h>//For working with SD card
#include <SD.h>//For working with SD card
#include <IridiumSBD.h>//For communcating with IRIDIUM modem 
#include <Wire.h>//For I2C communication 
#include <CSV_Parser.h>//For parsing CSV data
#include <QuickStats.h>//For computing median values 
#include <Adafruit_SHT31.h>//For communicating with SHT30 probe 

/*Create library instances*/
RTC_PCF8523 rtc; //Instantiate PCF8523 RTC
File dataFile;//Instantiate a logging file
Adafruit_SHT31 sht31 = Adafruit_SHT31();//Instantiate SHT30 sensor
QuickStats stats; //Instantiate QuickStats
#define IridiumWire Wire//Instantiate Wire 
IridiumSBD modem(IridiumWire);//Instantiate IridiumSBD as modem


/*Define pinouts*/
const byte pulsePin = 12; //Pulse width pin for reading pw from MaxBotix ultrasonic rangers
const byte triggerPin = 11; //Range start / stop pin for MaxBotix ultrasonic rangers
const byte donePin = 10; //Done pin for TPL5110 power breakout
const byte chipSelect = 4; //Chip select pin for MicroSD breakout
const byte led = 13; // Pin 13 is built in led

/*Global constants*/
char **filename;//Desired name for data file !!!must be less than or equal to 8 chars!!!
int16_t *N; //Number of ultrasonic range sensor readings to compute median.
int16_t *ultrasonic_height_mm; //Reqiired for comuting snow depth, ignored if configured as stage_mm
int16_t *irid_freq; // Iridium transmit freqency in hours
char **start_time;// Time at which first Iridum transmission should occur
char **irid_time;// For keeping track of IRIDIUM transmit times
char **metrics_letter_code;// Code for adding correct letter code to Iridium string, e.g., 'AFG', (see 'metrics' table in database for letter codes)
String metrics;// String representation of metrics_letter_code
boolean report_raw_dist = true;// Boolean indicating if raw distance (stage) or depth (snow) should be logged

/*Global variables*/
int16_t distance; //Variable for holding distance read from MaxBotix MB7369 ultrasonic ranger
int16_t duration; //Variable for holding pw duration read from MaxBotix MB7369 ultrasonic ranger
float temp_deg_c; //Variable for holding SHT30 temperature
float rh_prct; //Variable for holding SHT30 humidity
DateTime irid_time_; //Varible for holding Iridium transmit time

/*Define User Functions*/
int16_t read_sensor(int N)  //Function for sampling MaxBotix ultrasonic ranger, returns N median distance in mm
{
  float values[N];//Array for holding sampled distances
  digitalWrite(triggerPin, HIGH);//Drive ranging pin HIGH
  delay(1000);//Let instrument settle

  for (int i = 0; i < N; i++)//Loop N times
  {
    duration = pulseIn(pulsePin, HIGH);//Get the pulse duration (TOF)
    distance = (float) duration;//Distance = Duration for MB7369 (mm)
    values[i] = distance;
    delay(150);//Avoids sampling to quickly
  }

  digitalWrite(triggerPin, LOW);//Terminate ranging

  int16_t med_distance = round(stats.median(values, N));//Compute median

  return med_distance;//Return median
}

int send_hourly_data() //Function reads contents of HOURLY.CSV and sends hourly average data values over IRIDIUM formatted to be digested by database CGI script
{

  int err;// For capturing Iridium error code

  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);// Prevent from trying to charge to quickly, low current setup
  modem.enable9603Npower(true); // Enable power for the 9603N
  modem.enableSuperCapCharger(true); // Enable the super capacitor charger

  err = modem.begin();// Begin satellite modem operation, blink led twice for (1-sec) if there was an issue

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

  CSV_Parser cp("sdff", true, ',');  // Set paramters for parsing the log file

  char **datetimes; //pointer for datetimes
  int16_t *distances;//pointer for distances
  float *air_temps;//pointer for air temps
  float *rhs;//pointer for RHs

  cp.readSDfile("/HOURLY.CSV");// Read HOURLY.CSV file

  int num_rows = cp.getRowsCount();//Get # of rows

  datetimes = (char**)cp["datetime"];//populate datetimes
  if (report_raw_dist == true)//if raw distance then stage
  {
    distances = (int16_t*)cp["stage_mm"];//populate stages
  } else {//if now raw distance then snow
    distances = (int16_t*)cp["snow_depth_mm"];//populate snow
  }
  air_temps = (float*)cp["air_temp_deg_c"];//populate air temps
  rhs = (float*)cp["rh_prct"];//populate RHs

  String datastring = metrics + ":" + String(datetimes[0]).substring(0, 10) + ":" + String(datetimes[0]).substring(11, 13) + ":";  //Formatted for CGI script >> sensor_letter_code:date_of_first_obs:hour_of_first_obs:data

  int start_year = String(datetimes[0]).substring(0, 4).toInt();//parse year from first datestamp
  int start_month = String(datetimes[0]).substring(5, 7).toInt();//parse month from first datestamp
  int start_day = String(datetimes[0]).substring(8, 10).toInt();//parse day from first datestamp
  int start_hour = String(datetimes[0]).substring(11, 13).toInt();//parse hour from first datestamp
  int end_year = String(datetimes[num_rows - 1]).substring(0, 4).toInt();//parse year from last datestamp
  int end_month = String(datetimes[num_rows - 1]).substring(5, 7).toInt();//parse month from last datestamp
  int end_day = String(datetimes[num_rows - 1]).substring(8, 10).toInt();//parse day from last datestamp
  int end_hour = String(datetimes[num_rows - 1]).substring(11, 13).toInt();//parse hour from last datestamp

  DateTime start_dt = DateTime(start_year, start_month, start_day, start_hour, 0, 0);//Set start time as first datetime floor(@ the hour)
  DateTime end_dt = DateTime(end_year, end_month, end_day, end_hour + 1, 0, 0);//Set start time as last datetime @ the hour + 1
  DateTime intvl_dt;//For keeping track of the datetime at the end of each hourly interval

  while (start_dt < end_dt)//While the start_dt is less than the end_dt
  {
    intvl_dt = start_dt + TimeSpan(0, 1, 0, 0);//Set the intvl_dt as start_dt + 1 hour

    float mean_depth;//var for mean depth / distance
    float mean_temp;//var for mean air temp
    float mean_rh;//var for mean air RH
    boolean is_first_obs = true;//boolean for indicating first observation in HOURLY.CSV
    int N = 0;//var for # of obs

    for (int i = 0; i < num_rows; i++) {//For each observation in the HOURLY.CSV

      String datetime = String(datetimes[i]);//get the datestamp at row and parse datetime
      int dt_year = datetime.substring(0, 4).toInt();
      int dt_month = datetime.substring(5, 7).toInt();
      int dt_day = datetime.substring(8, 10).toInt();
      int dt_hour = datetime.substring(11, 13).toInt();
      int dt_min = datetime.substring(14, 16).toInt();
      int dt_sec = datetime.substring(17, 19).toInt();

      DateTime obs_dt = DateTime(dt_year, dt_month, dt_day, dt_hour, dt_min, dt_sec);//Create a date time from datestamp at row i

      if (obs_dt >= start_dt && obs_dt <= intvl_dt)//If the observations is within the current hourly interval
      {

        float snow_depth = (float) distances[i];//snow depth (or stage) is distances at i
        float air_temp = air_temps[i];// air temp is air_temps at i
        float rh = rhs[i];// air RH is rhs at i


        if (is_first_obs == true)//Check if this is the first observation for the hour
        {

          mean_depth = snow_depth;//set avg equal to first obs
          mean_temp = air_temp;
          mean_rh = rh;
          is_first_obs = false;//update state of is_first_obs to false
          N++;//increment obs count
        } else {
          mean_depth = mean_depth + snow_depth;//add value at i to mean depth sum
          mean_temp = mean_temp + air_temp;
          mean_rh = mean_rh + rh;
          N++;//increment obs count
        }

      }
    }

    if (N > 0)//Check if there were any observations for the hour
    {
      mean_depth = mean_depth / N;//compute mean depth (distance)
      mean_temp = (mean_temp / N) * 10.0;//compute mean temp, convert to integer
      mean_rh = (mean_rh / N) * 10.0;//compute mean RH, convert to integer

      datastring = datastring + String(round(mean_depth)) + ',' + String(round(mean_temp)) + ',' + String(round(mean_rh)) + ':';//Assemble the data string

    }

    start_dt = intvl_dt;//update start_dt to equal intvl_dt,i.e.,next hour start
  }

  uint8_t dt_buffer[340];//Binary bufffer for iridium transmission (max allowed buffer size 340 bytes)

  int message_bytes = datastring.length();// Total bytes in Iridium message

  int buffer_idx = 0; //Set buffer index to zero

  for (int i = 0; i < message_bytes; i++)//For each byte in the message (i.e. each char)
  {
    dt_buffer[buffer_idx] = datastring.charAt(i);//Update the buffer at buffer index with corresponding char

    buffer_idx++;//increment the buffer index
  }

  SD.remove("/HOURLY.CSV");//Remove previous HOURLY.CSV so that only new values will be sent next transmit

  irid_time_ =  irid_time_ + TimeSpan(0, irid_freq[0], 0, 0);//Increment the Iridium transmit time by the Iridium transmit freq.

  SD.remove("IRID.CSV");//Remove the old IRID.CSV file

  dataFile = SD.open("IRID.CSV", FILE_WRITE);//Create new IRID.CSV file
  if (dataFile)
  {
    dataFile.println("irid_time");//write header
    dataFile.print(irid_time_.timestamp());//write iridium transmit time
    dataFile.close();//close file
  }

  digitalWrite(led, HIGH);//Indicate the modem is trying to send with led

  err = modem.sendSBDBinary(dt_buffer, buffer_idx);//transmit binary buffer data via iridium

  if (err != 0 && err != 13)// If first attemped failed try once more with increased timeout
  {
    err = modem.begin();
    modem.adjustSendReceiveTimeout(500);
    err = modem.sendSBDBinary(dt_buffer, buffer_idx);
  }
  digitalWrite(led, LOW);//indicate no longer trying to send

  return err;//Return err code

}

void setup() {//Code runs once upon waking up of the TPL5110 power timer

  Wire.begin();// Start the I2C wire port connected to the satellite modem

  while (!modem.isConnected())  // Check that the Iridium is attached (5-sec flash led means Qwiic Iridium did not initialize)
  {
    digitalWrite(led, HIGH);
    delay(5000);
    digitalWrite(led, LOW);
    delay(5000);
  }

  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);// For USB "low current" applications
  modem.sleep(); // Put the modem to sleep
  modem.enable9603Npower(false); // Disable power for the 9603N
  modem.enableSuperCapCharger(false); // Disable the super capacitor charger

  pinMode(led, OUTPUT);//Set led pin as output
  digitalWrite(led, LOW);//drive led low
  pinMode(pulsePin, INPUT);//set pulse width pin as input
  pinMode(triggerPin, OUTPUT);//set ranging trigger pin as output
  digitalWrite(triggerPin, LOW);//drive ranging trigger pin HIGH
  pinMode(donePin, OUTPUT);//set done pin as outout

  while (!rtc.begin()) {// Start RTC (1-sec flash led means RTC did not initialize)
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

  while (!SD.begin(chipSelect)) {//Make sure a SD is available (1/2-sec flash led means SD card did not initialize)
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

  CSV_Parser cp(/*format*/ "sddsds", /*has_header*/ true, /*delimiter*/ ',');//Set parameters for parsing the parameter file

  cp.readSDfile("/PARAM.txt");//Read the PARAM.txt file

  filename = (char**)cp["filename"];//Get the filename parameter
  N = (int16_t*)cp["N"];//Get the sample N parameter
  irid_freq = (int16_t*)cp["irid_freq"];//Get the iridium frequency parameter
  start_time = (char**)cp["start_time"];//Get the start time parameter
  ultrasonic_height_mm = (int16_t*)cp["ultrasonic_height_mm"];//Get the ultrasonic ranger height parameter
  metrics_letter_code = (char**)cp["metrics_letter_code"];//Get the metrics letter code parameter

  metrics = String(metrics_letter_code[0]);//Get metrics letter code as Arduino String

  if (metrics.charAt(0) == 'E')//Check if first letter in letter code is 'E', i.e., snow depth (see database)
  {
    report_raw_dist = false;//If snow depth, do not report raw distance value,set false
  }

  String filestr = String(filename[0]);//get name of logfile

  DateTime dt = rtc.now();//Get datetime from RTC

  if (!SD.exists("IRID.CSV"))//Check if IRID.CSV exists
  {
    dataFile = SD.open("IRID.CSV", FILE_WRITE);//Open a IRID.CSV file for writing
    if (dataFile)
    {
      dataFile.println("irid_time");//write header
      dataFile.print(dt.timestamp(DateTime::TIMESTAMP_DATE) + "T" + start_time[0]);//write the next transmit time
      dataFile.close();//close the file
    }
  }

  CSV_Parser cp1("s", true, ',');//Parser for IRIDIUM.CSV

  cp1.readSDfile("/IRID.CSV");//Parse IRIDIUM.CSV

  irid_time = (char**)cp1["irid_time"];//Get the transmit time and conver to DateTime
  int irid_year = String(irid_time[0]).substring(0, 4).toInt();
  int irid_month =  String(irid_time[0]).substring(5, 7).toInt();
  int irid_day = String(irid_time[0]).substring(8, 10).toInt();
  int irid_hr = String(irid_time[0]).substring(11, 13).toInt();
  int irid_min = String(irid_time[0]).substring(14, 16).toInt();
  int irid_sec = String(irid_time[0]).substring(17, 19).toInt();

  irid_time_ = DateTime(irid_year, irid_month, irid_day, irid_hr, irid_min, irid_sec);//Construct Iridium transmit time

  if (dt >= irid_time_)//Check if the current time is greater then or equal to the iridium transmit time
  {
    send_hourly_data();//Send the hourly averages over Iridium
  }

  distance = read_sensor(N[0]);//Read N average ranging distance from MaxBotix ultrasonic ranger

  if (report_raw_dist == false)//if not reporting raw distance (stage), report depth(snow)
  {
    distance = ultrasonic_height_mm[0] - distance;//Compute depth as height of instrument - distance
  }


  while (!sht31.begin(0x44)) {  // Start SHT30, Set to 0x45 for alternate i2c addr (2-sec flash led means SHT30 did not initialize)
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  temp_deg_c = sht31.readTemperature();//Get temp. reading from SHT30
  rh_prct = sht31.readHumidity();//Get RH reading from SHT30

  float t_dew = temp_deg_c -  ((100.0 - rh_prct) / 5.0); //Approx. T-dew

  if (temp_deg_c <= t_dew )//If condensation, activate heater and retake humidity measurement
  {
    sht31.heater(true);//Turn on SHT30 heater
    delay(5000);//Give some time for heater to warm up
    sht31.heater(false);//Turn of heater
    delay(1000);
    rh_prct = sht31.readHumidity();//Retake RH sample
  }

  String datastring = dt.timestamp() + "," + distance + "," + temp_deg_c + "," + rh_prct;//construct data string

  if (!SD.exists(filestr.c_str()))//Write header if first time writing to the logfile
  {
    dataFile = SD.open(filestr.c_str(), FILE_WRITE);//open log file
    if (dataFile)
    {
      if (report_raw_dist == false)//if snow
      {
        dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");//print snow header
      } else {//if stage
        dataFile.println("datetime,stage_mm,air_temp_deg_c,rh_prct");//print stage header
      }
      dataFile.close();//close log file
    }

  } else {//Write datastring and close logfile on SD card

    dataFile = SD.open(filestr.c_str(), FILE_WRITE);//open log file
    if (dataFile)
    {
      dataFile.println(datastring);//print data string
      dataFile.close();//close log file
    }

  }

  if (!SD.exists("HOURLY.CSV"))//HOURLY.CSV duplicates the logfile
  {
    dataFile = SD.open("HOURLY.CSV", FILE_WRITE);
    if (dataFile)
    {
      if (report_raw_dist == false)
      {
        dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");
      } else {
        dataFile.println("datetime,stage_mm,air_temp_deg_c,rh_prct");
      }
      dataFile.close();
    }
  } else {
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
