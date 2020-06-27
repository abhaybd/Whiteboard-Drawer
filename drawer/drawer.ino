#define PI 3.1415926535897932384626433832795

#include <Wire.h>
#include <Adafruit_MotorShield.h>
#include <AccelStepper.h>
#include <Servo.h>

typedef float coord_t;

const int TOOL_PIN = 9;
const int TOOL_DOWN_POS = 0;
const int TOOL_UP_POS = 15;
const int TOOL_WAIT_MS = 50;
int toolPos = TOOL_UP_POS;

const float SPOOL_RAD = 5; // in mm
const int TICKS_PER_ROT = 200;
const float TICKS_PER_MM = TICKS_PER_ROT / (2 * PI * SPOOL_RAD);

// All of these in mm
const coord_t LEFT_X = 0;
const coord_t LEFT_Y = 1000;
const coord_t RIGHT_X = 500;
const coord_t RIGHT_Y = 1000;

// Also in mm
const coord_t OFFSET_X = 50;
const coord_t OFFSET_Y = 50;

const coord_t HOME_X = (LEFT_X + RIGHT_X) / 2;
const coord_t HOME_Y = RIGHT_Y / 2;

const coord_t DEF_VEL = static_cast<coord_t>(50 * TICKS_PER_MM);

Adafruit_MotorShield shield;
Adafruit_StepperMotor *leftStepper = shield.getStepper(TICKS_PER_ROT, 1);
Adafruit_StepperMotor *rightStepper = shield.getStepper(TICKS_PER_ROT, 2);

Servo tool;

AccelStepper left([]() {
    leftStepper->step(FORWARD, DOUBLE);
}, []() {
    leftStepper->step(BACKWARD, DOUBLE);
});
AccelStepper right([]() {
    rightStepper->step(FORWARD, DOUBLE);
}, []() {
    rightStepper->step(BACKWARD, DOUBLE);
});

/**
    Calculates the target position of each stepper, in units of steps. x and y are in mm.
*/
void calculateTargetSteps(coord_t x, coord_t y, int& l1, int& l2) {
    l1 = static_cast<int>(hypot(x - OFFSET_X - LEFT_X, y + OFFSET_Y - LEFT_Y) * TICKS_PER_MM);
    l2 = static_cast<int>(hypot(x + OFFSET_X - RIGHT_X, y + OFFSET_Y - RIGHT_Y) * TICKS_PER_MM);
}

/**
    Calculates the current position of the marker, in units of mm
*/
void getPosition(coord_t& x, coord_t& y) {
    float l1 = left.currentPosition() / TICKS_PER_MM; // length of left cord
    float l2 = right.currentPosition() / TICKS_PER_MM; // length of right cord

    coord_t o = OFFSET_X * 2; // distance between anchor points
    coord_t w = RIGHT_X - LEFT_X; // width of whiteboard

    // derived from geometry/math
    float fx = sq(l1) - sq(l2) + sq(o) + sq(w) - 2 * o * w;
    fx /= 2 * (w - o);

    float fy = LEFT_Y - sqrt(sq(l1) - sq(fx));

    x = static_cast<coord_t>(fx + OFFSET_X);
    y = static_cast<coord_t>(fy - OFFSET_Y);
}

void zero() {
    int stepsToMove = static_cast<int>(TICKS_PER_MM * hypot(RIGHT_X - LEFT_X, RIGHT_Y));
    left.move(-stepsToMove);
    left.setSpeed(DEF_VEL);
    left.runSpeedToPosition();
    right.move(-stepsToMove);
    right.setSpeed(DEF_VEL);
    right.runSpeedToPosition();

    left.setCurrentPosition(0);
    right.setCurrentPosition(0);

    int l, r;
    calculateTargetSteps(HOME_X, HOME_Y, l, r);
    left.move(l);
    left.setSpeed(DEF_VEL);
    left.runSpeedToPosition();
    right.move(r);
    right.setSpeed(DEF_VEL);
    right.runSpeedToPosition();
}

float getSpoolVel(float velX, float velY, float fromSpoolX, float fromSpoolY) {
    float mag = hypot(fromSpoolX, fromSpoolY);
    fromSpoolX /= mag;
    fromSpoolY /= mag;
    return velX * fromSpoolX + velY * fromSpoolY;
}

void getTargetVel(coord_t currX, coord_t currY, coord_t x, coord_t y, float curvature, bool clockwise, float& velX, float& velY) {
    float toTargetX = x - currX;
    float toTargetY = y - currY;
    float mag = hypot(toTargetX, toTargetY);
    toTargetX /= mag;
    toTargetY /= mag;
    if (curvature == 0) {
        velX = toTargetX;
        velY = toTargetY;
    } else {
        float rad = 1 / curvature;
        coord_t midX = (currX + x) / 2;
        coord_t midY = (currY + y) / 2;
        float d = sqrt(sq(rad) - (sq(x - currX) + sq(y - currY)) / 4);

        float arcX, arcY;
        if (clockwise) {
            arcX = midX + toTargetY * d;
            arcY = midY - toTargetX * d;
        } else {
            arcX = midX - toTargetY * d;
            arcY = midY + toTargetX * d;
        }

        float toCurrX = (currX - arcX) / rad;
        float toCurrY = (currY - arcY) / rad;
        if (clockwise) {
            velX = toCurrY;
            velY = -toCurrX;
        } else {
            velX = -toCurrY;
            velY = toCurrX;
        }
    }
}

void setPosition(coord_t x, coord_t y, coord_t vel, float curvature, bool clockwise) {
    coord_t currX, currY;
    int lTarget, rTarget;
    calculateTargetSteps(x, y, lTarget, rTarget);
    bool done = left.currentPosition() == lTarget && right.currentPosition() == rTarget;
    while (!done) {
        getPosition(currX, currY);
        // create a velocity vector pointing in the desired direction
        float velX = 0, velY = 0;
        getTargetVel(currX, currY, x, y, curvature, clockwise, velX, velY);
        velX *= vel;
        velY *= vel;

        // the ROC of the spool is the dot product of the velocity vector with the normalized vector from the spool to the anchor
        float fromLX = x - OFFSET_X - LEFT_X;
        float fromLY = y + OFFSET_Y - LEFT_Y;
        float leftVel = getSpoolVel(velX, velY, fromLX, fromLY);

        float fromRX = x + OFFSET_X - RIGHT_X;
        float fromRY = y + OFFSET_Y - RIGHT_Y;
        float rightVel = getSpoolVel(velX, velY, fromRX, fromRY);

        Serial.print("// Pos=(" + String(currX) + "," + currY + "), Target=(" + x + "," + y + "), Speed=" + vel + ", ");
        Serial.print("Speeds: Left=");
        Serial.print(leftVel);
        Serial.print(", Right=");
        Serial.println(rightVel);

        bool lDone = abs(left.currentPosition() - lTarget) <= 5;
        bool rDone = abs(right.currentPosition() - rTarget) <= 5;

        if (lDone) {
            left.stop();
        } else {
            left.setSpeed(leftVel * TICKS_PER_MM);
            left.runSpeed();
        }

        if (rDone) {
            right.stop();
        } else {
            right.setSpeed(rightVel * TICKS_PER_MM);
            right.runSpeed();
        }
        done = lDone && rDone;
    }

    left.stop();
    right.stop();
}

void setToolUp(bool up) {
    int target = up ? TOOL_UP_POS : TOOL_DOWN_POS;
    // If we're not already at the target position, set it now
    if (target != toolPos) {
        toolPos = target;
        tool.write(target);
        delay(TOOL_WAIT_MS); // delay to give the servo time to get there
    }
}

void setup() {
    Serial.begin(9600);
    shield.begin();
    tool.attach(TOOL_PIN);

    toolPos = TOOL_UP_POS;
    tool.write(toolPos);

    // configure motor inversions
    left.setPinsInverted(true, false, false);
    right.setPinsInverted(false, false, false);
}

coord_t targetX = 0;
coord_t targetY = 0;
coord_t targetZ = 0;

#define numParts 6
#define commandLen 100
void loop() {
    // wait for data to become available
    while (Serial.available() == 0) {
        delay(10);
    }

    // read command from serial
    char* command = new char[commandLen]; // we'll allocate to the heap so we can free it earlier
    size_t len = Serial.readBytesUntil('\n', command, commandLen - 1);
    command[len] = '\0'; // make sure string is null terminated
    Serial.print("// Recieved command: ");
    Serial.println(command);

    // Tokenize into space delimited parts
    char* parts[numParts] = {};
    char* part = strtok(command, " \n");
    for (size_t i = 0; i < numParts && part != nullptr; i++) {
        parts[i] = part;
        part = strtok(nullptr, " \n");
    }

    delete [] command; // free the allocated memory on the heap

    // Execute g-code command
    char* type = parts[0];
    if (type == nullptr) {
        Serial.println("// Invalid command! Failing silently...");
    } else if (strcmp(type, "G28") == 0) { // If it's a home command
        targetX = HOME_X;
        targetY = HOME_Y;
        targetZ = 0;
        setToolUp(true);
        zero();
    } else if (type[0] == 'G') {
        coord_t arcI = 0;
        coord_t arcJ = 0;
        for (int i = 1; i < numParts && parts[i] != nullptr; i++) {
            if (parts[i][0] == 'X') {
                targetX = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            } else if (parts[i][0] == 'Y') {
                targetY = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            } else if (parts[i][0] == 'Z') {
                targetZ = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            } else if (parts[i][0] == 'I') {
                arcI = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            } else if (parts[i][1] == 'J') {
                arcJ = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            }
        }
        setToolUp(targetZ >= 0);
        long code = strtol(&type[1], nullptr, 10);
        if (code == 0 || code == 1) {
            setPosition(targetX, targetY, DEF_VEL, 0, false);
        } else if (code == 2 || code == 3) {
            if (arcI != 0 || arcJ != 0) {
                float rad = hypot(arcI, arcJ);
                setPosition(targetX, targetY, DEF_VEL, 1 / rad, code == 2);
            } else {
                Serial.println("// Invalid G2/G3 command!");
                Serial.println("!!");
                return;
            }
        } else {
            Serial.print("// Unsupported g-code: G");
            Serial.println(code);
        }
    } else {
        Serial.println("// Invalid command! Failing silently...");
    }

    Serial.println("ok");
}
