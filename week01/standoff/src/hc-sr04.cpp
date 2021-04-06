/*
 * Code for interfacing a 32U4 with the SR-HR04 ultrasonic sensor. 
 * 
 * This uses the Input Capture feature of the ATmega32U4 (e.g., Leonardo) to get precision readings.
 * Specifically, you must connect the pulse width pin to pin 13 (ICP3) on the 32U4.
 * You are welcome to use whatever pin you want for triggering a ping, just be sure to change it from the default.
 * 
 * The input capture first looks for a rising edge, then a falling edge
 * The difference between the two is the pulse width, which is a direct measurement 
 * of the (round trip) timer counts to hear the echo.
 * 
 * But note that the timing is in timer counts, which must be converted to time.
 */

#include <Romi32U4.h>
#include <Arduino.h>
#include <stdlib.h>
#include <math.h>

volatile uint16_t pulseStart = 0;
volatile uint16_t pulseEnd = 0;

//define the states for the echo capture
enum PULSE_STATE {PLS_IDLE, PLS_WAITING_LOW, PLS_WAITING_HIGH, PLS_CAPTURED};

//and initialize to IDLE
volatile PULSE_STATE pulseState = PLS_IDLE;

//this may be most any pin, connect the pin to Trig on the sensor
const uint8_t trigPin = 12;

//for scheduling pings
uint32_t lastPing = 0;
const uint32_t PING_INTERVAL = 100; //ms

/*
 * Commands the ultrasonic to take a reading
 */
void CommandPing(int trigPin)
{
  cli(); //disable interrupts

  TIFR3 = 0x20; //clear any interrupt flag that might be there

  TIMSK3 |= 0x20; //enable the input capture interrupt
  TCCR3B |= 0xC0; //set to capture the rising edge on pin 13; enable noise cancel

  sei(); //re-enable interrupts

  //update the state and command a ping
  pulseState = PLS_WAITING_LOW;
  
  digitalWrite(trigPin, HIGH); //command a ping by bringing TRIG HIGH
  delayMicroseconds(10);      //we'll allow a delay here for convenience; it's only 10 us
  digitalWrite(trigPin, LOW);  //must bring the TRIG pin back LOW to get it to send a ping
}

int cmpfunc (const void * a, const void * b) {
  return ( *(uint32_t*)a - *(uint32_t*)b );
}

uint32_t median(uint32_t *values) {
  qsort(values, 5, sizeof(uint32_t), cmpfunc);
  return values[2];
}

uint32_t mean(uint32_t *values) {
  uint32_t sum = values[0] + 
              values[1] + 
              values[2] + 
              values[3] + 
              values[4];
  uint32_t f = sum / 5.0;
  return f;
}

Romi32U4Motors motors;

void setup()
{
  Serial.begin(115200);
  // while(!Serial) {} //you must open the Serial Monitor to get past this step!
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

  pinMode(trigPin, OUTPUT);
  pinMode(13, INPUT); //explicitly make 13 an input, since it defaults to OUTPUT in Arduino World (LED)

  lastPing = millis();

  Serial.println("/setup");
}

uint32_t filterValues[5] = {0, 0, 0, 0, 0};
int currIndex = 0;

void loop() 
{
  //schedule pings roughly every PING_INTERVAL milliseconds
  uint32_t currTime = millis();
  if((currTime - lastPing) >= PING_INTERVAL && pulseState == PLS_IDLE)
  {
    lastPing = currTime;
    CommandPing(trigPin); //command a ping
  }
  
  if(pulseState == PLS_CAPTURED) //we got an echo
  {
    //update the state to IDLE
    pulseState = PLS_IDLE;

    /*
     * Calculate the length of the pulse (in timer counts!). Note that we turn off
     * interrupts for a VERY short period so that there is no risk of the ISR changing
     * pulseEnd or pulseStart. The way the state machine works, this wouldn't 
     * really be a problem, but best practice is to ensure that no side effects can occur.
     */
    noInterrupts();
    uint16_t pulseLengthTimerCounts = pulseEnd - pulseStart;
    interrupts();
    
    //EDIT THIS LINE: convert pulseLengthTimerCounts, which is in timer counts, to time, in us
    //You'll need the clock frequency and the pre-scaler to convert timer counts to time
    // Timer 3 is 16-bit timer
    // Clock Frequency is 16MHz
    // Prescaler is /64 so 64 clock cycles = +1 timer count
    // 16 clock cycles per us
    // http://medesign.seas.upenn.edu/index.php/Guides/MaEvArM-timer3
    uint32_t pulseLengthUS = pulseLengthTimerCounts * 4; //pulse length in us
// /4 instead

    //EDIT THIS LINE AFTER YOU CALIBRATE THE SENSOR: put your formula in for converting us -> cm
    // speed of sound is 340m/s or 29us/cm
    // half of total distance ping travels is the distance to the object
    float distancePulse = pulseLengthUS / 58.0;    //distance in cm

    // Filter garbage values, compute running average
    filterValues[currIndex] = pulseLengthUS;
    currIndex += (currIndex == 4) ? -4 : 1;

    float filteredDistance = mean(filterValues) / 58.0;


    // Kp works very well here, so we have no need for Ki or Kd
    float err = filteredDistance - 20.0;
    static float kp = 10.0, ki = 1.0;
    float speed = kp * err;

    // Speed threshhold
    if(abs(speed) <= 5) {
      speed = 0;
    }

    motors.setEfforts(speed, speed);

    Serial.print(millis());
    Serial.print('\t');
    Serial.print(pulseLengthTimerCounts);
    Serial.print('\t');
    Serial.print(pulseLengthUS);
    Serial.print('\t');
    Serial.print(distancePulse);
    Serial.print("\tm:\t");
    Serial.print(filteredDistance);
    Serial.print("\ts:\t");
    Serial.print(speed);
    Serial.print('\n');
  }
}

/*
 * ISR for input capture on pin 13. We can precisely capture the value of TIMER3
 * by setting TCCR3B to capture either a rising or falling edge. This ISR
 * then reads the captured value (stored in ICR3) and copies it to the appropriate
 * variable.
 */
ISR(TIMER3_CAPT_vect)
{
  if(pulseState == PLS_WAITING_LOW) //we're waiting for a rising edge
  {
    pulseStart = ICR3; //copy the input capture register (timer count)
    TCCR3B &= 0xBF;    //now set to capture falling edge on pin 13
    pulseState = PLS_WAITING_HIGH;
  }

  else if(pulseState == PLS_WAITING_HIGH) //waiting for the falling edge
  {
    pulseEnd = ICR3;
    pulseState = PLS_CAPTURED; //raise a flag to indicate that we have data
  }
}
