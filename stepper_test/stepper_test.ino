#include <Wire.h>
#include <Adafruit_MotorShield.h>

Adafruit_MotorShield shield;
Adafruit_StepperMotor *stepper = shield.getStepper(200, 2);

bool forward = true;

void setup() {
    Serial.begin(9600);
    shield.begin();
}

void loop() {
    Serial.println("Forward=" + forward);
    stepper->step(200, forward ? FORWARD : BACKWARD, DOUBLE);
    forward = !forward;
}
