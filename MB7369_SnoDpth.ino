/*Include required libraries*/
#include "RTClib.h" //Needed for communication with Real Time Clock
#include <SPI.h> //Needed for working with SD card
#include <SD.h> //Needed for working with SD card
#include "ArduinoLowPower.h" //Needed for putting Feather M0 to sleep between samples
#include <IridiumSBD.h> //Needed for communication with IRIDIUM modem 
#include <CSV_Parser.h> //Needed for parsing CSV data
#include <Adafruit_AHTX0.h> //Needed for communicating with  AHT20
#include <QuickStats.h>//Needed for commuting median

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
int16_t *ultrasonic_height_mm;//Height of ultrasonic sensor,used to compute depth, ignored if stage_mm being logged
int16_t height_mm;//Same as ultrasonic_height_mm[0]
char **metrics_letter_code;// Three letter code for Iridium string, e.g., 'A' for stage 'E' for snow depth (see metrics_lookup in database)
String metrics; //String for representing dist_letter_code
int32_t distance; //Variable for holding distance read from MaxBotix MB7369 ultrasonic ranger
int32_t duration; //Variable for holding pulse width duration read from MaxBotix ultrasonic ranger
sensors_event_t rh_prct, temp_deg_c; //Variable for holding AHT20 temp and RH vals


/*Define Iridium seriel communication as Serial1 */
#define IridiumSerial Serial1



/*Create library instances*/
RTC_DS3231 rtc; // Setup a PCF8523 Real Time Clock instance
File dataFile; // Setup a log file instance
IridiumSBD modem(IridiumSerial); // Declare the IridiumSBD object
Adafruit_AHTX0 aht;//instantiate AHT20 object
QuickStats stats; //initialize an instance of QuickStats class


/*Define User Functions*/

int16_t read_sensor(int sample_n) {//Function for averaging N readings from MaxBotix ultrasonic ranger

  float values[sample_n];//Array for storing sampled distances
  digitalWrite(triggerPin, HIGH);//Write the ranging trigger pin HIGH
  delay(2000);//Let sensor settle

  for (int16_t i = 0; i < sample_n; i++)//Take N samples
  {

    duration = pulseIn(pulsePin, HIGH);//Get the pulse duration (i.e.,time of flight)
    distance = duration;//Distance = Duration for MB7369 (mm)
    values[i] = distance;
    delay(150);//Dont sample too quickly < 7.5 Htz >

  }

  digitalWrite(triggerPin, LOW);//Stop continious ranging

  int16_t med_distance = round(stats.median(values, sample_n));//Copute median distance

  return med_distance;//Return the median distance, average too noisy with ultrasonics
}


int send_hourly_data()//Function reads HOURLY.CSV and sends hourly averages over IRIDIUM, formatted as to be ingested by the Omineca CGI script / database
{

  int err;  // For capturing Iridium errors

  digitalWrite(irid_pwr_pin, HIGH);  // Provide power to Iridium Modem
  delay(200); // Allow warm up

  IridiumSerial.begin(19200);// Start the serial port connected to the satellite modem

  CSV_Parser cp("sdff", true, ',');// Set paramters for parsing the log file

  char **datetimes;//pointer to datetimes
  int16_t *distances;//pointer to distances
  float *air_temps;//pointer to air temps
  float *rhs;//pointer to RHs

  cp.readSDfile("/HOURLY.CSV");// Read HOURLY.CSV file from SD

  int num_rows = cp.getRowsCount();//Get # of rows

  datetimes = (char**)cp["datetime"];//populate datetimes
  if (metrics.charAt(0) == 'E')//If measuring snow depth
  {
    distances = (int16_t*)cp["snow_depth_mm"];//populate snow depths
  } else {//If measuring stage
    distances = (int16_t*)cp["stage_mm"];//populate stage
  }
  air_temps = (float*)cp["air_temp_deg_c"];//populate air temps
  rhs = (float*)cp["rh_prct"];//populate RHs

  String datastring = metrics + ":" + String(datetimes[0]).substring(0, 10) + ":" + String(datetimes[0]).substring(11, 13) + ":";//Formatted for CGI script >> sensor_letter_code:date_of_first_obs:hour_of_first_obs:data

  int start_year = String(datetimes[0]).substring(0, 4).toInt();//year of first obs
  int start_month = String(datetimes[0]).substring(5, 7).toInt();//month of first obs
  int start_day = String(datetimes[0]).substring(8, 10).toInt();//day of first obs
  int start_hour = String(datetimes[0]).substring(11, 13).toInt();//hour of first obs
  int end_year = String(datetimes[num_rows - 1]).substring(0, 4).toInt();//year of last obs
  int end_month = String(datetimes[num_rows - 1]).substring(5, 7).toInt();//month of last obs
  int end_day = String(datetimes[num_rows - 1]).substring(8, 10).toInt();//day of last obs
  int end_hour = String(datetimes[num_rows - 1]).substring(11, 13).toInt();//hour of last obs

  DateTime start_dt = DateTime(start_year, start_month, start_day, start_hour, 0, 0);//Set start date time to first observation time rounded down @ the hour
  DateTime end_dt = DateTime(end_year, end_month, end_day, end_hour + 1, 0, 0);//Set the end datetime to last observation + 1 hour
  DateTime intvl_dt;//For keeping track of the datetime at the end of each hourly interval

  while (start_dt < end_dt)//while the start datetime is < end datetime
  {
    intvl_dt = start_dt + TimeSpan(0, 1, 0, 0);//intvl_dt is equal to start_dt + 1 hour

    float mean_depth = -9999.0; //mean depth / distance
    float mean_temp;//mean air temp
    float mean_rh;//mean air RH
    boolean is_first_obs = true;//Boolean indicating first hourly observation
    boolean is_first_obs_snow = true;//Boolean for first houry snow observation
    int N = 0;//Sample N counter
    int N_snow = 0;

    for (int i = 0; i < num_rows; i++) { //For each observation in the HOURLY.CSV

      String datetime = String(datetimes[i]);//Datetime and row i
      int dt_year = datetime.substring(0, 4).toInt();
      int dt_month = datetime.substring(5, 7).toInt();
      int dt_day = datetime.substring(8, 10).toInt();
      int dt_hour = datetime.substring(11, 13).toInt();
      int dt_min = datetime.substring(14, 16).toInt();
      int dt_sec = datetime.substring(17, 19).toInt();

      DateTime obs_dt = DateTime(dt_year, dt_month, dt_day, dt_hour, dt_min, dt_sec);//Construct DateTime for obs at row i

      if (obs_dt >= start_dt && obs_dt <= intvl_dt)//If obs datetime is withing the current hour interval
      {

        float snow_depth = (float) distances[i];//depth / distance at row i
        float air_temp = air_temps[i];//air temp at row i
        float rh = rhs[i];//RH at row i

        //Need to treat snow diffrent as a good reading is not guarteed
        if (is_first_obs_snow == true)
        {
          //Check that depth is greater than zero (ie not negative) with 100mm of tolerance 
          if (snow_depth > -100.0)
          {
            mean_depth = snow_depth;//mean depth / distance equal to depth at i
            N_snow++;
          }
        } else {
          if (snow_depth > -100.0)
          {
            mean_depth = mean_depth + snow_depth;//Add depth / distance at i to mean_depth
            N_snow++;
          }
        }

        if (is_first_obs == true)//Check if this is the first observation for the hour
        {

          mean_temp = air_temp;//mean_air temp equal to air temp at i
          mean_rh = rh;//mean RH equal to RH at i
          is_first_obs = false;//No longer first obs
          N++;//Increment sample N
        } else {
          mean_temp = mean_temp + air_temp;//Add air temp at i to mean temp
          mean_rh = mean_rh + rh;//Add RH at i to mean RH
          N++;//Increment sample N
        }

      }
    }

    if (N_snow > 0)
    {
      mean_depth = mean_depth / N;//mean depth / distance
    }

    if (N > 0)//Check if there were any observations for the hour
    {
      mean_temp = (mean_temp / N) * 10.0;//mean air temp, convert to int
      mean_rh = (mean_rh / N) * 10.0; //mean RH, convert to int

      datastring = datastring + String(round(mean_depth)) + ',' + String(round(mean_temp)) + ',' + String(round(mean_rh)) + ':';//Assemble the data string

    }

    start_dt = intvl_dt;//Set intvl_dt to start_dt,i.e., next hour
  }

  uint8_t dt_buffer[340];//Binary bufffer for iridium transmission (max allowed buffer size 340 bytes)

  int message_bytes = datastring.length();// Total bytes in Iridium message

  int buffer_idx = 0;//Set buffer index to zero

  for (int i = 0; i < message_bytes; i++)//For each byte in the message (i.e. each char)
  {
    dt_buffer[buffer_idx] = datastring.charAt(i);//Update the buffer at buffer index with corresponding char
    buffer_idx++;//increment buffer index
  }

  modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);// Prevent from trying to charge to quickly, low current setup

  err = modem.begin();// Begin satellite modem operation, blink led (1-sec) if there was an issue

  if (err == ISBD_IS_ASLEEP)//Check if modem is asleep for whatever reason
  {
    modem.begin();
  }

  digitalWrite(led, HIGH); //Indicate the modem is trying to send with led

  err = modem.sendSBDBinary(dt_buffer, buffer_idx); //transmit binary buffer data via iridium

  if (err != 0 && err != 13)// If first attemped failed try once more with extended timeout
  {
    err = modem.begin();
    modem.adjustSendReceiveTimeout(500);
    err = modem.sendSBDBinary(dt_buffer, buffer_idx);
  }

  digitalWrite(led, LOW);//Indicate transmission ended

  digitalWrite(irid_pwr_pin, LOW);//Kill power to modem to save pwr

  SD.remove("/HOURLY.CSV");  //Remove previous daily values CSV

  return err;  //Return err code, not used but can be used for trouble shooting

}

void setup(void)//Setup section, runs once upon powering up the Feather M0
{
  pinMode(led, OUTPUT);//Set led pin as OUTPUT
  digitalWrite(led, LOW);//Turn off led
  pinMode(PeriSetPin, OUTPUT);//Set peripheral set pin as OUTPUT
  digitalWrite(PeriSetPin, LOW);//Drive peripheral set pin LOW
  pinMode(PeriUnsetPin, OUTPUT);//Set peripheral unset pin as OUTPUT
  digitalWrite(PeriUnsetPin, HIGH);//Drive peripheral unset pin HIGH to assure unset relay
  delay(30);//Mimimum delay for relay
  digitalWrite(PeriUnsetPin, LOW);//Drive peripheral unset pin LOW as it is a latching relay
  pinMode(irid_pwr_pin, OUTPUT);//Set iridium power pin as OUTPUT
  digitalWrite(irid_pwr_pin, LOW);//Drive iridium power pin LOW
  pinMode(triggerPin, OUTPUT);//Set ultrasonic ranging trigger pin as OUTPUT
  digitalWrite(triggerPin, LOW);//Drive ultrasonic randing trigger pin LOW
  pinMode(pulsePin, INPUT);//Set ultrasonic pulse width pin as INPUT

  while (!SD.begin(chipSelect)) {//Make sure a SD is available (2-sec flash led means SD card did not initialize)
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  CSV_Parser cp("sddsdds", true, ',');//Set paramters for parsing the parameter file PARAM.txt

  while (!cp.readSDfile("/PARAM.txt"))//Read the parameter file 'PARAM.txt', blink (1-sec) if fail to read
  {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

  filename = (char**)cp["filename"];//Get file name from parameter file
  sample_intvl = (int16_t*)cp["sample_intvl"];//Get sample interval in seconds from parameter file
  irid_freq = (int16_t*)cp["irid_freq"];//Get iridium freqency in hours from parameter file
  start_time = (char**)cp["start_time"];//Get idridium start time as timestamp from parameter file
  N = (int16_t*)cp["N"];//Get sample N from parameter file
  sample_n = N[0];//Update value of sample_n from parameter file
  ultrasonic_height_mm = (int16_t*)cp["ultrasonic_height_mm"];//Get height of ultrasonic sensor in mm from parameter file
  metrics_letter_code = (char**)cp["metrics_letter_code"];//Get metrics letter code string from parameter file

  metrics = String(metrics_letter_code[0]);//Update metrics with value from parameter file

  sleep_time = sample_intvl[0] * 1000;//Sleep time between samples in miliseconds

  filestr = String(filename[0]); //Log file name

  irid_freq_hrs = irid_freq[0];  //Iridium transmission frequency in hours

  height_mm = ultrasonic_height_mm[0];


  int start_hour = String(start_time[0]).substring(0, 3).toInt();//Parse iridium start time hour
  int start_minute = String(start_time[0]).substring(3, 5).toInt();//Parse iridium start time minute
  int start_second = String(start_time[0]).substring(6, 8).toInt();//Parse iridium start time second

  while (!rtc.begin())// Make sure RTC is available, blink at 1/2 sec if not
  {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

  present_time = rtc.now();  //Get the present datetime

  //Update the transmit time to the start time for present date
  transmit_time = DateTime(present_time.year(),
                           present_time.month(),
                           present_time.day(),
                           start_hour + irid_freq_hrs,
                           start_minute,
                           start_second);




}

void loop(void)//Code executes repeatedly until loss of power
{
  present_time = rtc.now();//Get the present datetime

  if (present_time >= transmit_time)//If the presnet time has reached transmit_time send all data since last transmission averaged hourly
  {
    int send_status = send_hourly_data();// Send the hourly data over Iridium

    transmit_time = (transmit_time + TimeSpan(0, irid_freq_hrs, 0, 0));//Update next Iridium transmit time by 'irid_freq_hrs'
  }

  digitalWrite(PeriSetPin, HIGH);//Drive set pin high
  delay(50);//Required delay
  digitalWrite(PeriSetPin, LOW);//Drive set pin LOW (latched)
  delay(200);//Let settle

  distance = read_sensor(sample_n);//Read N average ranging distance from MaxBotix ultrasonic ranger


  if (metrics.charAt(0) == 'E')//If the first letter of the metrics letter code is 'E', i.e., snow_depth_mm, otherwise its 'A', i.e., stage_mm
  {
    distance = height_mm - distance;//Compute snow depth from distance
  }


  if (!aht.begin()) { // Start AHT20 (2-sec flash led means SHT30 did not initialize)
    digitalWrite(led, HIGH);
    delay(5000);
    digitalWrite(led, LOW);
    delay(5000);
  }


  aht.getEvent(&rh_prct, &temp_deg_c);// populate temp and humidity objects with fresh data

  digitalWrite(PeriUnsetPin, HIGH);//Drive unset pin HIGH
  delay(50);//required delay
  digitalWrite(PeriUnsetPin, LOW);//Drive unset pin LOW (latched)

  String datastring = present_time.timestamp() + "," + distance + "," + temp_deg_c.temperature + "," + rh_prct.relative_humidity;//Assemble datastring

  if (!SD.exists(filestr.c_str()))//Write header if first time writing to the logfile
  {
    dataFile = SD.open(filestr.c_str(), FILE_WRITE);//Open file under filestr name from parameter file
    if (dataFile)
    {
      if (metrics.charAt(0) == 'E')//If first letter of metrics code is 'E', i.e., snow_depth_mm, write snow header
      {
        dataFile.println("datetime,snow_depth_mm,air_temp_deg_c,rh_prct");
      } else {//stage_mm, 'A'
        dataFile.println("datetime,stage_mm,air_temp_deg_c,rh_prct");//Write stage header
      }

      dataFile.close();//Close the dataFile
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

  if (!SD.exists("HOURLY.CSV"))//HOURLY.CSV duplicates the log file until deleted
  {
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

    dataFile = SD.open("HOURLY.CSV", FILE_WRITE);
    if (dataFile)
    {
      dataFile.println(datastring);
      dataFile.close();
    }
  }
  digitalWrite(led, HIGH);//Flash led to idicate a sample was just taken
  delay(250);
  digitalWrite(led, LOW);
  delay(250);

  LowPower.sleep(sleep_time);//Put logger in low power mode for length 'sleep_time'

}
