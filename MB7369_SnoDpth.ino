/*
   Date: 2022-07-11
   Contact: Hunter Gleason (Hunter.Gleason@alumni.unbc.ca)
   Organization: Ministry of Forests (MOF)
   Description: This script is written for the purpose of gathering hydrometric data, including the real time transmission of the data via the Iridium satallite network.
   Using the HYDROS21 probe () water level, temperature and electrical conductivity are measured and stored to a SD card at a specified logging interval. Hourly averages
   of the data are transmitted over Iridium at a specified transmission frequency. The string output transmitted over Iridium is formatted to be compatible with digestion
   into a database run by MOF. The various parameters required by the script are specified by a TXT file on the SD card used for logging, for more details on usage of this
   script please visit the GitHub repository ().
*/


/*Include the libraries we need*/
#include "RTClib.h" //Needed for communication with Real Time Clock
#include <SPI.h> //Needed for working with SD card
#include <SD.h> //Needed for working with SD card
#include "ArduinoLowPower.h" //Needed for putting Feather M0 to sleep between samples
#include <IridiumSBD.h> //Needed for communication with IRIDIUM modem 
#include <CSV_Parser.h> //Needed for parsing CSV data
#include <Adafruit_SHT31.h>
#include <QuickStats.h>

/*Define global constants*/
const byte led = 13; // Built in led pin
const byte chipSelect = 4; // Chip select pin for SD card
const byte irid_pwr_pin = 6; // Power base PN2222 transistor pin to Iridium modem
const byte PeriSetPin = 5; //Power relay set pin for all 3.3V peripheral
const byte PeriUnsetPin = 9; //Power relay unset pin for all 3.3V peripheral
const byte pulsePin = 12; //Pulse width pin for reading pw from MaxBotix MB7369 ultrasonic ranger
const byte triggerPin = 10; //Range start / stop pin for MaxBotix MB7369 ultrasonic ranger


/*Define global vars */
char **filename; // Name of log file(Read from PARAM.txt)
char **start_time;// Time at which first Iridum transmission should occur (Read from PARAM.txt)
String filestr; // Filename as string
int16_t *sample_intvl; // Sample interval in minutes (Read from PARAM.txt)
int16_t *irid_freq; // Iridium transmit freqency in hours (Read from PARAM.txt)
uint32_t irid_freq_hrs; // Iridium transmit freqency in hours
uint32_t sleep_time;// Logger sleep time in milliseconds
DateTime transmit_time;// Datetime varible for keeping IRIDIUM transmit time
DateTime present_time;// Var for keeping the current time
int err; //IRIDIUM status var
int16_t *N; //Number of ultrasonic reange sensor readings to average.
int sample_n; //same as N[0]
int16_t *ultrasonic_height_mm;
char **metrics_letter_code;// Code for adding correct letter code to Iridium string, e.g., 'A' for stage 'E' for snow depth.
String metrics; //String for representing dist_letter_code
int16_t distance; //Variable for holding distance read from MaxBotix MB7369 ultrasonic ranger
int16_t duration; //Variable for holding pw duration read from MaxBotix MB7369 ultrasonic ranger
float temp_deg_c; //Variable for holding SHT30 temperature
float rh_prct; //Variable for holding SHT30 humidity



/*Define Iridium seriel communication as Serial1 */
#define IridiumSerial Serial1



/*Create library instances*/
RTC_PCF8523 rtc; // Setup a PCF8523 Real Time Clock instance
File dataFile; // Setup a log file instance
IridiumSBD modem(IridiumSerial); // Declare the IridiumSBD object
Adafruit_SHT31 sht31 = Adafruit_SHT31();//instantiate SHT30 sensor
QuickStats stats; //initialize an instance of QuickStats class


//Function for averaging N readings from MaxBotix MB7369 ultrasonic ranger
int16_t read_sensor(int sample_n) {

  //Variable for average distance
  float values[sample_n];
  digitalWrite(triggerPin, HIGH);
  delay(2000);

  //Take N readings
  for (int16_t i = 0; i < sample_n; i++)
  {

    //Get the pulse duration (TOF)
    duration = pulseIn(pulsePin, HIGH);
    //Stop ranging
    //Distance = Duration for MB7369 (mm)
    distance = duration;
    values[i] = distance;
    delay(150);

  }

  digitalWrite(triggerPin, LOW);

  int16_t med_distance = round(stats.median(values, sample_n));

  return med_distance;
}


/*Function reads data from a .csv logfile, and uses Iridium modem to send all observations
   since the previous transmission over satellite at midnight on the RTC.
*/
int send_hourly_data()
{

  // For capturing Iridium errors
  int err;

  // Provide power to Iridium Modem
  digitalWrite(irid_pwr_pin, HIGH);
  // Allow warm up
  delay(200);

  // Start the serial port connected to the satellite modem
  IridiumSerial.begin(19200);

  // Set paramters for parsing the log file
  CSV_Parser cp("sdff", true, ',');


  // Varibles for holding data fields
  char **datetimes;
  int16_t *distances;
  float *air_temps;
  float *rhs;

  // Read HOURLY.CSV file
  cp.readSDfile("/HOURLY.CSV");

  int num_rows = cp.getRowsCount();


  //Populate data arrays from logfile
  datetimes = (char**)cp["datetime"];
  if (metrics.charAt(0) == 'E')
  {
    distances = (int16_t*)cp["snow_depth_mm"];
  } else {
    distances = (int16_t*)cp["stage_mm"];
  }
  air_temps = (float*)cp["air_temp_deg_c"];
  rhs = (float*)cp["rh_prct"];

  //Formatted for CGI script >> sensor_letter_code:date_of_first_obs:hour_of_first_obs:data
  String datastring = metrics + ":" + String(datetimes[0]).substring(0, 10) + ":" + String(datetimes[0]).substring(11, 13) + ":";


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
        float snow_depth = (float) distances[i];
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
      mean_rh = (mean_rh / N) *10.0;


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

  // Prevent from trying to charge to quickly, low current setup
  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);

  // Begin satellite modem operation, blink led (1-sec) if there was an issue
  err = modem.begin();

  if (err == ISBD_IS_ASLEEP)
  {
    modem.begin();
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

  digitalWrite(irid_pwr_pin, LOW);


  //Remove previous daily values CSV as long as send was succesfull

  SD.remove("/HOURLY.CSV");


  //Return err code
  return err;


}



/*
   The setup function. We only start the sensors, RTC and SD here
*/
void setup(void)
{
  // Set pin modes
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  pinMode(PeriSetPin, OUTPUT);
  digitalWrite(PeriSetPin, LOW);
  pinMode(PeriUnsetPin, OUTPUT);
  digitalWrite(PeriUnsetPin, HIGH);
  delay(30);
  digitalWrite(PeriUnsetPin, LOW);
  pinMode(irid_pwr_pin, OUTPUT);
  digitalWrite(irid_pwr_pin, LOW);
  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);
  pinMode(pulsePin, INPUT);


  //Make sure a SD is available (2-sec flash led means SD card did not initialize)
  while (!SD.begin(chipSelect)) {
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  //Set paramters for parsing the parameter file PARAM.txt
  CSV_Parser cp("sddsdds", true, ',');


  //Read the parameter file 'PARAM.txt', blink (1-sec) if fail to read
  while (!cp.readSDfile("/PARAM.txt"))
  {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }


  //Populate data arrays from parameter file PARAM.txt
  filename = (char**)cp["filename"];
  sample_intvl = (int16_t*)cp["sample_intvl"];
  irid_freq = (int16_t*)cp["irid_freq"];
  start_time = (char**)cp["start_time"];
  N = (int16_t*)cp["N"];
  sample_n = N[0];
  ultrasonic_height_mm = (int16_t*)cp["ultrasonic_height_mm"];
  metrics_letter_code = (char**)cp["metrics_letter_code"];

  metrics = String(metrics_letter_code[0]);

  //Sleep time between samples in miliseconds
  sleep_time = sample_intvl[0] * 1000;

  //Log file name
  filestr = String(filename[0]);

  //Iridium transmission frequency in hours
  irid_freq_hrs = irid_freq[0];

  //Get logging start time from parameter file
  int start_hour = String(start_time[0]).substring(0, 3).toInt();
  int start_minute = String(start_time[0]).substring(3, 5).toInt();
  int start_second = String(start_time[0]).substring(6, 8).toInt();

  // Make sure RTC is available
  while (!rtc.begin())
  {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }


  //Get the present time
  present_time = rtc.now();

  //Update the transmit time to the start time for present date
  transmit_time = DateTime(present_time.year(),
                           present_time.month(),
                           present_time.day(),
                           start_hour + irid_freq_hrs,
                           start_minute,
                           start_second);




}

/*
   Main function, sample HYDROS21 and sample interval, log to SD, and transmit hourly averages over IRIDIUM at midnight on the RTC
*/
void loop(void)
{

  //Get the present datetime
  present_time = rtc.now();

  //If the presnet time has reached transmit_time send all data since last transmission averaged hourly
  if (present_time >= transmit_time)
  {
    // Send the hourly data over Iridium
    int send_status = send_hourly_data();

    //Update next Iridium transmit time by 'irid_freq_hrs'
    transmit_time = (transmit_time + TimeSpan(0, irid_freq_hrs, 0, 0));
  }

  //Read N average ranging distance from MB7369
  distance = read_sensor(sample_n);

  if (metrics.charAt(0) == 'E')
  {
    distance = ultrasonic_height_mm[0] - distance;
  }

  digitalWrite(PeriSetPin, HIGH);
  delay(50);
  digitalWrite(PeriSetPin, LOW);
  delay(200);

  if (!sht31.begin(0x44)) { // Start SHT30, Set to 0x45 for alternate i2c addr (2-sec flash led means SHT30 did not initialize)
    digitalWrite(led, HIGH);
    delay(5000);
    digitalWrite(led, LOW);
    delay(5000);
  }


  temp_deg_c = sht31.readTemperature();
  rh_prct = sht31.readHumidity();

  //If humidity is above 80% turn on SHT31 heater to evaporate condensation, retake humidity measurement
  if (rh_prct >= 80.0 )
  {
    sht31.heater(true);
    //Give some time for heater to warm up
    delay(5000);
    rh_prct = sht31.readHumidity();
    sht31.heater(false);
  }

  digitalWrite(PeriUnsetPin, HIGH);
  delay(50);
  digitalWrite(PeriUnsetPin, LOW);

  String datastring = present_time.timestamp() + "," + distance + "," + temp_deg_c + "," + rh_prct;

  //Write header if first time writing to the logfile
  if (!SD.exists(filestr.c_str()))
  {
    dataFile = SD.open(filestr.c_str(), FILE_WRITE);
    if (dataFile)
    {
      if (metrics.charAt(0) == 'E')
      {
        dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");
      } else {
        dataFile.println("datetime,stage_mm,air_temp_deg_c,rh_prct");
      }

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
      if (metrics.charAt(0) == 'E')
      {
        dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");
      } else {
        dataFile.println("datetime,stage_mm,air_temp_deg_c,rh_prct");
      }
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

  //Flash led to idicate a sample was just taken
  digitalWrite(led, HIGH);
  delay(250);
  digitalWrite(led, LOW);
  delay(250);

  //Put logger in low power mode for lenght 'sleep_time'
  LowPower.sleep(sleep_time);


}
