#include <Wire.h>
#include <Adafruit_MotorShield.h>

Adafruit_MotorShield shield;
Adafruit_StepperMotor *stepper = shield.getStepper(200, 1);

bool forward = true;

void setup() {
    Serial.begin(9600);
    shield.begin();
}

void loop() {
    Serial.print("Forward=");
    Serial.println(forward ? "true" : "false");
    stepper->step(200, forward ? FORWARD : BACKWARD, DOUBLE);
    forward = !forward;
}
