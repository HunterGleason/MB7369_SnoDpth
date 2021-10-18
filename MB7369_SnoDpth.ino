/* 
DESCRIPTION: Arduino code for MaxBotix MB7369 weather resistant ultrasonic distance sensor, and SHT31 temperture / humidity sensor. For use with Adafruit Feather M0 Adalogger and PCF8523 real time clock.
Power managment is done through the Sparkfun TPL5110 low-power breakout, ~logging interval is determined by arranging switches on the TPL5110 chip (https://www.sparkfun.com/products/15353). All communication is
I2C, however, MB7369 readings are read via the pulse width output pin.
AUTHOR:Hunter Gleason
AGENCY:FLNRORD
DATE:2021-10-18
*/

/*Required libriries*/
#include "RTClib.h" //Needed for communication with Real Time Clock
#include <SPI.h>//Needed for working with SD card
#include <SD.h>//Needed for working with SD card
#include "Adafruit_SHT31.h"//Needed for SHT31 Temp/Humid sensor
#include <IridiumSBD.h> 
#include <Wire.h>

/*Create librire instanceses*/
RTC_PCF8523 rtc; //instantiate PCF8523 RTC
File dataFile;//instantiate a logging file
Adafruit_SHT31 sht31 = Adafruit_SHT31();//instantiate SHT31 sensor

#define IridiumWire Wire //instantiate iridium satallite modem
IridiumSBD modem(IridiumWire);

// For USB "low current" applications
modem.setPowerProfile(IridiumSBD::USB_POWER_PROFILE);
modem.sleep(); // Put the modem to sleep
modem.enable9603Npower(false); // Disable power for the 9603N
modem.enableSuperCapCharger(false); // Disable the super capacitor charger
modem.enable841lowPower(true); // Enable the ATtiny841's low power mode 

/*Define pinouts*/
const byte pulsePin = 12; //Pulse width pin for reading pw from MaxBotix MB7369 ultrasonic ranger
const byte triggerPin = 11; //Range start / stop pin for MaxBotix MB7369 ultrasonic ranger
const byte donePin = 10; //Done pin for TPL5110 power breakout
const byte chipSelect = 4; //Chip select pin for MicroSD breakout
const byte led = 13; // Pin 13 LED

/*Global constants*/
const String filename = "SnoDTEST.TXT";//Desired name for logfile !!!must be less than equal to 8 char!!!
const long N = 6; //Number of sensor readings to average.
const int transmitHours[] = {0};
const int logging_intv_min = 5;

/*Global variables*/
long distance; //Variable for holding distance read from MaxBotix MB7369 ultrasonic ranger
long duration; //Variable for holding pw duration read from MaxBotix MB7369 ultrasonic ranger
float temp_c;
float humid_prct;

//Function for averageing N readings from MaxBotix MB7369 ultrasonic ranger
long read_sensor(int N) {

  //Varible for average distance
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


boolean check_transmit()
{
  boolean transmit = false;
  
  DateTime now = rtc.now();
  int current_hour = now.hour();
  int current_minute = now.minute();

  int ary_size = sizeof(transmitHours);

  for (int i = 0; i < (sizeof(transmitHours) / sizeof(transmitHours[0])); i++)
  {
    if (transmitHours[i] == current_hour && current_minute<=(logging_intv_min+1))
    {
      transmit = true;
    }

    delay(1);
  }

  return transmit;

}

//Code runs once upon waking up the TPL5110
void setup() {

  //Set pin modes
  
  //turn off built in LED
  pinMode(led,OUTPUT);
  digitalWrite(led,LOW);
  
  pinMode(pulsePin, INPUT);
  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);
  pinMode(donePin, OUTPUT);


  // Start RTC (1-sec flash LED means RTC did not intialize)
  while (!rtc.begin()) {
    digitalWrite(led, HIGH);
    delay(1000);
    digitalWrite(led, LOW);
    delay(1000);
  }

  //Get current logging time from RTC
  DateTime now = rtc.now();

  //Read N average ranging distance from MB7369
  distance = read_sensor(N);


  while (!sht31.begin(0x44)) {  // Start SHT31, Set to 0x45 for alternate i2c addr (2-sec flash LED means SHT31 did not initalize)
    digitalWrite(led, HIGH);
    delay(2000);
    digitalWrite(led, LOW);
    delay(2000);
  }

  temp_c = sht31.readTemperature();
  humid_prct = sht31.readHumidity();

  //If humidty is above 80% turn on SHT31 heater to evaporate condensation, retake humidty measurment 
  if (humid_prct >= 80)
  {
    sht31.heater(true);
    //Give some time for heater to warm up
    delay(5000);
    humid_prct = sht31.readHumidity();
    sht31.heater(false);
  }


  //Assemble a data string for logging to SD, with date-time, snow depth (mm), temperture (deg C) and humidity (%)
  String datastring = String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) + " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + "," + String(distance) + ",mm,"+String(temp_c)+",deg C,"+String(humid_prct)+",%";

  //Make sure a SD is available (1/2-sec flash LED means SD card did not initalize)
  while (!SD.begin(chipSelect)) {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }

  //Write datastring and close logfile on SD card
  dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile)
  {
    dataFile.println(datastring);
    dataFile.close();
  }

  //Check that the iridium modem is connected and the hour matches one of the desired transmit hours (0-24) (transmitHours[])
  if(check_transmit() && modem.isConnected())
  { 
    modem.enableSuperCapCharger(true); // Enable the super capacitor charger
    while (!modem.checkSuperCapCharger()) ; // Wait for the capacitors to charge
    modem.enable9603Npower(true); // Enable power for the 9603N
    modem.begin(); // Wake up the 9603N and prepare it for communications.
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
