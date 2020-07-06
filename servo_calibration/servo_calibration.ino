#include <Servo.h>
#include <Wire.h>

/**
    This script is used to calibrate the servo used to lift and lower the tool.
    Run this script with the servo plugged in. Type different positions into the terminal, and the servo will go to that position.
    Find appropriate positions for "up" and "down", and save them.
*/

Servo servo;

void setup() {
    Serial.begin(9600);
    servo.attach(10);
}

void loop() {
    // Wait for data
    while (Serial.available() == 0) {
        delay(10);
    }

    // Read at most 4 bytes from the terminal
    char posTarget[5] = {};
    size_t len = Serial.readBytesUntil('\n', posTarget, 4);
    posTarget[len] = '\0';

    // Parse the recieved number as a long
    long target = strtol(posTarget, nullptr, 10);
    Serial.print("// Recieved target: ");
    Serial.println(target);

    // Set the target for the servo, and wait for it to reach it
    servo.write(target);
    delay(100);
}
