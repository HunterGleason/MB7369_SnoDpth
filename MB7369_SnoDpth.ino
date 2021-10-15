/* Arduino example code for MaxBotix MB7389 HRXL-MaxSonar-WR weather resistant ultrasonic distance sensor with push button. More info: www.makerguides.com */

//TEST START VOLT: 4.1 V 3.7 LiPo
//TEST START TIME: Sep 27, 2021 @ 15:37
//TEST END VOLT:
//TEST END TIME:

#include "RTClib.h" //Needed for communication with Real Time Clock
#include <SPI.h>//Needed for working with SD card
#include <SD.h>//Needed for working with SD card

RTC_PCF8523 rtc; //Instantiate PCF8523 RTC

File dataFile;//instantiate a logging file

const byte pulsePin = 10;
const byte triggerPin = 11;
const byte donePin = 5;
const byte chipSelect = 4; //Chip select pin for MicroSD breakout
const byte led = 13; // Pin 13 LED
const String filename = "SnoDTEST.TXT";//Desired name for logfile !!!must be less than equal to 8 char!!!
const long N = 6; //Number of sensor readings to average.


long distance = 0;
long duration = 0;


long read_sensor(int N) {

  long avg_dist = 0;
  
  for (int i = 0; i < N; i++)
  {
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(30);
    duration = pulseIn(pulsePin, HIGH);
    digitalWrite(triggerPin, LOW);
    distance = duration;

    avg_dist = avg_dist + distance;

    delay(150);
  }

  avg_dist = avg_dist / N;
  
  return avg_dist;
}


void setup() {


  pinMode(pulsePin, INPUT);

  pinMode(triggerPin, OUTPUT);
  digitalWrite(triggerPin, LOW);

  pinMode(donePin, OUTPUT);


  // Start RTC
  rtc.begin();

  //Get current logging time from RTC
  DateTime now = rtc.now();

  distance = read_sensor(N);


  //Assemble a data string for logging to SD, with time and average level NTU values
  String datastring = String(now.year()) + "-" + String(now.month()) + "-" + String(now.day()) + " " + String(now.hour()) + ":" + String(now.minute()) + ":" + String(now.second()) + "," + String(distance) + ",mm ";

  //Make sure a SD is available, otherwise blink onboard LED
  while (!SD.begin(chipSelect)) {
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
  }

  //Write datastring and close logfile on SD card
  dataFile = SD.open(filename, FILE_WRITE);
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
