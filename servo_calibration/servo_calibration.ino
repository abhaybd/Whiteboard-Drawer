#include <Servo.h>
#include <Wire.h>

Servo servo;

void setup() {
    Serial.begin(9600);
    servo.attach(10);
}

void loop() {
    while (Serial.available() == 0) {
        delay(10);
    }

    char posTarget[5] = {};
    size_t len = Serial.readBytesUntil('\n', posTarget, 4);
    posTarget[len] = '\0';

    long target = strtol(posTarget, nullptr, 10);
    Serial.print("// Recieved target: ");
    Serial.println(target);

    servo.write(target);
    delay(100);
}
