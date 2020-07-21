#define PI 3.1415926535897932384626433832795

#include <Wire.h>
#include <Adafruit_MotorShield.h>
#include <AccelStepper.h>
#include <MultiStepper.h>
#include <Servo.h>

typedef float coord_t;

const int TOOL_PIN = 10;
const int TOOL_DOWN_POS = 90;
const int TOOL_UP_POS = 25;
const int TOOL_WAIT_MS = 100;
int toolPos = TOOL_UP_POS;

const float SPOOL_RAD = 7.5; // in mm
const int TICKS_PER_ROT = 200;
const float TICKS_PER_MM = TICKS_PER_ROT / (2 * PI * SPOOL_RAD);

// All of these in mm
const coord_t LEFT_X = 0;
const coord_t LEFT_Y = 400;//953;
const coord_t RIGHT_X = 660;
const coord_t RIGHT_Y = LEFT_Y;

// Also in mm
const coord_t OFFSET_X = 41.5;
const coord_t OFFSET_Y = 40;

const coord_t HOME_X = (LEFT_X + RIGHT_X) / 2;
const coord_t HOME_Y = RIGHT_Y / 2;

const coord_t MAX_SEG_LEN = 5;

const long DEF_VEL = static_cast<long>(10 * TICKS_PER_MM); // in ticks per second

Adafruit_MotorShield shield;
Adafruit_StepperMotor *leftStepper = shield.getStepper(TICKS_PER_ROT, 1);
Adafruit_StepperMotor *rightStepper = shield.getStepper(TICKS_PER_ROT, 2);

Servo tool;

AccelStepper left([]() {
    leftStepper->onestep(FORWARD, SINGLE);
}, []() {
    leftStepper->onestep(BACKWARD, SINGLE);
});
AccelStepper right([]() { // this is inverted to invert the motor
    rightStepper->onestep(BACKWARD, SINGLE);
}, []() {
    rightStepper->onestep(FORWARD, SINGLE);
});

MultiStepper ms;

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
    float leftAnchorX = (sq(l1) - sq(l2) + sq(o) + sq(w) - 2 * o * w) / (2 * (w - o));
    float leftAnchorY = LEFT_Y - sqrt(sq(l1) - sq(leftAnchorX));

    x = static_cast<coord_t>(leftAnchorX + OFFSET_X);
    y = static_cast<coord_t>(leftAnchorY - OFFSET_Y);
}

void moveBothTo(int l, int r) {
    long positions[] = {l, r};
    ms.moveTo(positions);
    ms.runSpeedToPosition();
    //    left.moveTo(l);
    //    right.moveTo(r);
    //    while (left.currentPosition() != left.targetPosition() || right.currentPosition() != right.targetPosition()) {
    //        left.run();
    //        right.run();
    //    }
}

void zero() {
    left.setCurrentPosition(0);
    right.setCurrentPosition(0);

    int l, r;
    calculateTargetSteps(HOME_X, HOME_Y, l, r);
    Serial.println("// Homing...");
    moveBothTo(l, r);
}

float arcLength(coord_t currX, coord_t currY, coord_t x, coord_t y, float curvature) {
    float dx = x - currX;
    float dy = y - currY;

    if (curvature == 0) return hypot(dx, dy);
    else {
        float dist = hypot(dx, dy);
        float angle = 2 * asin(dist * curvature / 2);
        return angle / curvature;
    }
}

void interpolate(float t, coord_t currX, coord_t currY, coord_t targetX, coord_t targetY, float curvature, bool clockwise, coord_t& interpX, coord_t& interpY) {
    if (curvature == 0) {
        // If this is a line, interpolation is easy peasy
        interpX = currX + t * (targetX - currX);
        interpY = currY + t * (targetY - currY);
    } else {
        // Get vector from current to target
        float dx = targetX - currX;
        float dy = targetY - currY;
        float dist = hypot(dx, dy);

        // normalize that vector
        float dxNorm = dx / dist;
        float dyNorm = dy / dist;

        // This is the distance between the circle center and the chord
        float d = sqrt(sq(1 / curvature) - sq(dist / 2));

        // Calculate the circle center (find midpoint between curr and target and add normalized vector rotated CW or CCW 90 deg and scaled by d)
        float cX, cY;
        if (clockwise) {
            cX = currX + dx / 2 + dyNorm * d;
            cY = currY + dy / 2 - dxNorm * d;
        } else {
            cX = currX + dx / 2 - dyNorm * d;
            cY = currY + dy / 2 + dxNorm * d;
        }

        // Get vector from circle center to current position
        float fromCX = currX - cX;
        float fromCY = currY - cY;
        // This is the angle subtended by the chord from current to target
        float angle = 2 * asin(dist * curvature / 2) * t;
        if (!clockwise) angle = -angle; // Negative angle means CCW
        // Apply a CW rotation by angle (if angle < 0 then rotation is CCW)
        float x = cos(angle) * fromCX + sin(angle) * fromCY;
        float y = -sin(angle) * fromCX + cos(angle) * fromCY;
        // Add back to circle center to get interpolated point
        interpX = cX + x;
        interpY = cY + y;
    }
}

void setPosition(coord_t x, coord_t y, float curvature, bool clockwise) {
    coord_t currX, currY;
    getPosition(currX, currY); // get the current position
    if (hypot(x - currX, y - currY) <= 0.1) return; // Return early if we're already at target
    Serial.println("// Setting position!");
    float arcLen = arcLength(currX, currY, x, y, curvature);
    int numSegs = ceil(arcLen / MAX_SEG_LEN); // calculate the number of sub-segments to use based on arc length
    Serial.print("// ArcLen=");
    Serial.print(arcLen);
    Serial.print(", numSegs=");
    Serial.println(numSegs);
    // Step along each segment
    for (int i = 0; i < numSegs; i++) {
        float t = static_cast<float>(i + 1) / numSegs;
        coord_t interpX, interpY;
        interpolate(t, currX, currY, x, y, curvature, clockwise, interpX, interpY); // calculate target x and y positions
        int left, right;
        calculateTargetSteps(interpX, interpY, left, right); // from x and y positions, calculate target motor steps
        moveBothTo(left, right); // move both motors uncoordinatedly
    }
    Serial.println("// Move done!");
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

    left.setAcceleration(DEF_VEL * 2);
    right.setAcceleration(DEF_VEL * 2);
    left.setMaxSpeed(DEF_VEL);
    right.setMaxSpeed(DEF_VEL);

    ms.addStepper(left);
    ms.addStepper(right);

    Serial.println("// Hello world!");
}

coord_t targetX = HOME_X;
coord_t targetY = HOME_Y;
coord_t targetZ = 0;

#define numParts 6
#define commandLen 100
void loop() {
    // wait for data to become available
    while (Serial.available() == 0) {
        delay(10);
    }

    // read command from serial
    char command[commandLen] = {};
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

    // Execute g-code command
    char* type = parts[0];
    if (type == nullptr) {
        Serial.println("// Empty command! Failing silently...");
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
            } else if (parts[i][0] == 'J') {
                arcJ = static_cast<coord_t>(strtod(&parts[i][1], nullptr));
            }
        }
        if (targetX < OFFSET_X + 5) { // Clamp to prevent invalid x target
            targetX = OFFSET_X + 5;
        }
        setToolUp(targetZ >= 0);
        long code = strtol(&type[1], nullptr, 10);
        if (code == 0 || code == 1) {
            setPosition(targetX, targetY, 0, false);
        } else if (code == 2 || code == 3) {
            if (arcI != 0 || arcJ != 0) {
                float rad = hypot(arcI, arcJ);
                setPosition(targetX, targetY, 1 / rad, code == 2);
            } else {
                Serial.println("// Invalid G2/G3 command!");
                Serial.println("!!");
                return;
            }
        } else {
            Serial.print("// Unsupported g-code: G");
            Serial.println(code);
        }
    } else if (type[0] == 'M') {
        long code = strtol(&type[1], nullptr, 10);
        if (code == 118) {
            coord_t x, y;
            getPosition(x, y);
            Serial.print("// X:");
            Serial.print(x);
            Serial.print(" Y:");
            Serial.print(y);
            Serial.print(" Z:");
            Serial.println(toolPos == TOOL_UP_POS ? 1 : -1);
            long l = left.currentPosition(); // length of left cord
            long r = right.currentPosition(); // length of right cord
            Serial.print("// L:");
            Serial.print(l);
            Serial.print(" R:");
            Serial.println(r);
        } else if (code == 18) {
            leftStepper->release();
            rightStepper->release();
        }
    } else if (type[0] == 'L') {
        long pos = strtol(parts[1], nullptr, 10);
        moveBothTo(pos, right.currentPosition());
    } else if (type[0] == 'R') {
        long pos = strtol(parts[1], nullptr, 10);
        moveBothTo(left.currentPosition(), pos);
    } else {
        Serial.println("// Invalid command! Failing silently...");
    }

    Serial.println("ok");
}
