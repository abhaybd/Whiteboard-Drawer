package com.coolioasjulio.whiteboard;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.util.Enumeration;
import java.util.List;
import java.util.Scanner;
import purejavacomm.CommPortIdentifier;
import purejavacomm.SerialPort;
import purejavacomm.SerialPortEvent;
import purejavacomm.SerialPortEventListener;

public class Playground implements SerialPortEventListener {

    public static void main(String[] args) throws Exception {
        Playground p = new Playground();
        Scanner in = new Scanner(System.in);
        while (true) {
            String line = in.nextLine();
            p.out.print(line + "\n");
            p.out.flush();
        }
    }


    private final SerialPort port;
    private final BufferedReader in;
    private final PrintStream out;

    public Playground() throws Exception {
        port = (SerialPort) CommPortIdentifier.getPortIdentifier("COM4").open("blah", 1000);
        port.setSerialPortParams(9600, SerialPort.DATABITS_8,
                SerialPort.STOPBITS_1,
                SerialPort.PARITY_NONE);

        in = new BufferedReader(new InputStreamReader(port.getInputStream()));
        out = new PrintStream(port.getOutputStream());

        port.addEventListener(this);
        port.notifyOnDataAvailable(true);
    }

    public void serialEvent(SerialPortEvent serialPortEvent) {
        if (serialPortEvent.getEventType() == SerialPortEvent.DATA_AVAILABLE) {
            try {
                System.out.println(in.readLine());
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    public synchronized void close() {
        if (port != null) {
            port.removeEventListener();
            port.close();
        }
    }
}
