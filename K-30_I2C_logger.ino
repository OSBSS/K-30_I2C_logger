// OSBSS CO2 data logger based on SenseAir K-30 1% CO2 sensor
// Communication with sensor using I2C protocol
// Updated on: 3/13/2015

#include <EEPROM.h>
#include <DS3234lib3.h>
#include <PowerSaver.h>
#include <Wire.h>
#include <SdFat.h>

// Launch Variables   ******************************
long interval = 60;  // set logging interval in SECONDS, eg: set 300 seconds for an interval of 5 mins
int dayStart = 13, hourStart = 21, minStart = 31;    // define logger start time: day of the month, hour, minute
char filename[15] = "log.csv";    // Set filename Format: "12345678.123". Cannot be more than 8 characters in length, contain spaces or begin with a number

// Global objects and variables   ******************************
PowerSaver chip;  	// declare object for PowerSaver class
DS3234 RTC;    // declare object for DS3234 class
SdFat sd; 		// declare object for SdFat class
SdFile file;		// declare object for SdFile class

#define POWER 3    // pin 3 supplies power to microSD card breakout
#define LED 7  // pin 7 controls LED
int SDcsPin = 9; // pin 9 is CS pin for MicroSD breakout

int CO2ppm = 0;

// ISR ****************************************************************
ISR(PCINT0_vect)  // Interrupt Vector Routine to be executed when pin 8 receives an interrupt.
{
  //PORTB ^= (1<<PORTB1);
  asm("nop");
}

// setup ****************************************************************
void setup()
{
  Serial.begin(19200); // open serial at 19200 bps
  
  pinMode(LED, OUTPUT); // set output pins
  pinMode(POWER, OUTPUT);
  
  digitalWrite(POWER, HIGH);  // turn on SD card
  delay(1);    // give some delay to ensure RTC and SD are initialized properly
  
  Wire.begin();  // initialize I2C using Wire.h library
  
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))  // initialize SD card on the SPI bus
  {
    delay(10);
    SDcardError();
  }
  else
  {
    delay(10);
    file.open(filename, O_CREAT | O_APPEND | O_WRITE);  // open file in write mode and append data to the end of file
    delay(1);
    String time = RTC.timeStamp();    // get date and time from RTC
    file.println();
    file.print("Date/Time,CO2(ppm)");    // Print header to file
    file.println();
    PrintFileTimeStamp();
    file.close();    // close file - very important
                     // give some delay by blinking status LED to wait for the file to properly close
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
  }
  RTC.checkInterval(hourStart, minStart, interval); // Check if the logging interval is in secs, mins or hours
  RTC.alarm2set(dayStart, hourStart, minStart);  // Configure begin time
  RTC.alarmFlagClear();  // clear alarm flag
  
  chip.sleepInterruptSetup();    // setup sleep function on the ATmega328p. Power-down mode is used here
}

// loop ****************************************************************
void loop()
{
  digitalWrite(POWER, LOW);
  delay(1);  // give some delay for SD card power to be low before processor sleeps to avoid it being stuck
  
  chip.turnOffADC();    // turn off ADC to save power
  chip.turnOffSPI();  // turn off SPI bus to save power
  //chip.turnOffWDT();  // turn off WatchDog Timer to save power (does not work for Pro Mini - only works for Uno)
  chip.turnOffBOD();    // turn off Brown-out detection to save power
  
  
  chip.goodNight();    // put processor in extreme power down mode - GOODNIGHT!
                       // this function saves previous states of analog pins and sets them to LOW INPUTS
                       // average current draw on Mini Pro should now be around 0.195 mA (with both onboard LEDs taken out)
                       // Processor will only wake up with an interrupt generated from the RTC, which occurs every logging interval
                       
  chip.turnOnADC();    // enable ADC after processor wakes up
  chip.turnOnSPI();   // turn on SPI bus once the processor wakes up
  
  digitalWrite(POWER, HIGH);
  delay(1);    // important delay to ensure SPI bus is properly activated
  
  RTC.alarmFlagClear();    // clear alarm flag

  RTC.checkDST();  // check and account for Daylight Saving Time in US
  
  CO2ppm = GetCO2(0x68); // default address for K-30 CO2 sensor is 0x68
  delay(50); // give some delay to ensure CO2 data is properly received from sensor
  
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))    // very important - reinitialize SD card on the SPI bus
  {
    delay(10);
    SDcardError();
  }
  else
  {
    delay(10);
    file.open(filename, O_WRITE | O_AT_END);  // open file in write mode
    delay(1);
    
    String time = RTC.timeStamp();    // get date and time from RTC
    SPCR = 0;  // reset SPI control register
    
    file.print(time);
    file.print(",");
    file.print(CO2ppm);  // print temperature upto 3 decimal places
    file.println();
    PrintFileTimeStamp();
    file.close();    // close file - very important
                     // give some delay by blinking status LED to wait for the file to properly close
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
  }
  RTC.setNextAlarm();      //set next alarm before sleeping
  delay(1);
}

// Get CO2 concentration ****************************************************************
int GetCO2(int address)
{
  byte recieved[4] = {0,0,0,0}; // create an array to store bytes received from sensor
  
  Wire.beginTransmission(address);
  Wire.write(0x22);
  Wire.write(0x00);
  Wire.write(0x08);
  Wire.write(0x2A);
  Wire.endTransmission();
  delay(20); // give delay to ensure transmission is complete
  
  Wire.requestFrom(address,4);
  delay(10);
  
  byte i=0;
  while(Wire.available())
  {
    recieved[i] = Wire.read();
    i++;
  }
  
  byte checkSum = recieved[0] + recieved[1] + recieved[2];
  CO2ppm = (recieved[1] << 8) + recieved[2];
  
  if(checkSum == recieved[3])
    return CO2ppm;
  else
    return -1;
}

// file timestamps
void PrintFileTimeStamp() // Print timestamps to data file. Format: year, month, day, hour, min, sec
{ 
  file.timestamp(T_WRITE, RTC.year, RTC.month, RTC.day, RTC.hour, RTC.minute, RTC.second);    // edit date modified
  file.timestamp(T_ACCESS, RTC.year, RTC.month, RTC.day, RTC.hour, RTC.minute, RTC.second);    // edit date accessed
}

// SD card Error response ****************************************************************
void SDcardError()
{
    for(int i=0;i<3;i++)   // blink LED 3 times to indicate SD card write error
    {
      digitalWrite(LED, HIGH);
      delay(50);
      digitalWrite(LED, LOW);
      delay(150);
    }
}

//****************************************************************
