/*
 * Code for interfacing a 32U4 with the Sharp IR Sensor
 */

#include <Arduino.h>

volatile uint16_t pulseStart = 0;
volatile uint16_t pulseEnd = 0;


//for scheduling readings
uint32_t lastRead = 0;
const uint32_t READ_INTERVAL = 100; //ms


void setup()
{
  Serial.begin(115200);
  while(!Serial) {} //you must open the Serial Monitor to get past this step!
  Serial.println("setup");

  noInterrupts(); //disable interupts while we mess with the control registers
  
  //sets timer 3 to normal mode (16-bit, fast counter)
  TCCR3A = 0; 
  
  interrupts(); //re-enable interrupts

  //note that the Arduino machinery has already set the prescaler elsewhere
  //so we'll print out the value of the register to figure out what it is
  Serial.print("TCCR3B = ");
  Serial.println(TCCR3B, HEX);
  // HEX 3 = 0011 BIN
  // /64 Prescaler
  // 1 time every 64 microseconds

  // Pin 18 is A0
  pinMode(18, INPUT); //explicitly make 18 an input
  pinMode(30, INPUT); //make pin 30 input for button B

  lastRead = millis();

  Serial.print("finished setup after ");
  Serial.print(lastRead);
  Serial.println(" ms");
}

int i = 0;
void loop() 
{
  //schedule pings roughly every PING_INTERVAL milliseconds
  uint32_t currTime = millis();

  // Reset read count on button A press
  // (to get multiple sets of readings without reboot)
  if(i > 195 && !digitalRead(30)) {
    Serial.println("Reset");
    i = 0;
  }

  if(currTime - lastRead >= READ_INTERVAL && i < 200) {
    i++;
    lastRead = currTime;
    float adcValue = analogRead(18);

    float voltage = adcValue * 5.0 / 1024.0;

    //EDIT THIS LINE AFTER YOU CALIBRATE THE SENSOR: put your formula in for converting us -> cm
    // speed of sound is 340m/s or 29us/cm
    // half of total distance ping travels is the distance to the object
    float distance = 0.0;    //distance in cm

    Serial.print(millis());
    Serial.print('\t');
    Serial.print(adcValue);
    Serial.print('\t');
    Serial.print(voltage);
    Serial.print('\t');
    Serial.print(distance);
    Serial.print('\n');
  }

}
