# Whiteboard-Drawer
Robot powered by an arduino that can draw on a whiteboard.

Written to accept arbitrary G-Code, which means it can draw pretty much anything.

The code for the robot is in `drawer`, and the code to calibrate the servo is in `servo_calibration`. There is also Java code that will stream a G-Code file over serial to the robot, which allows it to draw arbitrary designs.
For the most part, this robot adheres to the guidelines/rules of G-Code, and behaves like a CNC machine.

<img src="https://github.com/abhaybd/Whiteboard-Drawer/blob/master/hello%20world.gif" width="200">    <img src="https://github.com/abhaybd/Whiteboard-Drawer/blob/master/duck.gif" width="200">
