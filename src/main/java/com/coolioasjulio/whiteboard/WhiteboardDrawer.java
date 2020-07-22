package com.coolioasjulio.whiteboard;

import java.io.BufferedReader;
import java.io.Closeable;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import purejavacomm.CommPortIdentifier;
import purejavacomm.SerialPort;

public class WhiteboardDrawer implements AutoCloseable, Closeable {

    public static void main(String[] args) {
        System.out.print("Connecting...");
        try (WhiteboardDrawer wb = new WhiteboardDrawer("COM4")){
            System.out.println("Done!");
            System.out.println("Printing...");
            boolean success = wb.drawFromFile(new File("duck.gcode"));
            System.out.println("Done printing! Success=" + success);
        } catch (Exception e) {
            System.out.println("Error!");
            e.printStackTrace();
        }
    }

    private final SerialPort port;
    private final BufferedReader in;
    private final PrintStream out;

    public WhiteboardDrawer(String portName) throws Exception {
        port = (SerialPort) CommPortIdentifier.getPortIdentifier(portName).open("Drawer", 1000);
        port.setSerialPortParams(9600, SerialPort.DATABITS_8,
                SerialPort.STOPBITS_1,
                SerialPort.PARITY_NONE);

        in = new BufferedReader(new InputStreamReader(port.getInputStream()));
        out = new PrintStream(port.getOutputStream());
    }

    public boolean sendCommand(String command) throws IOException {
        String response;
        do {
            System.out.println(">>> " + command);
            out.print(command + "\n"); // don't use println, since that uses windows line endings (\r\n instead of \n)
            out.flush();
            do {
                response = in.readLine();
                System.out.println("<<< " + response);
            } while (response.startsWith("//")); // skip past debug info
        } while (response.equals("rs"));

        return response.startsWith("ok");
    }

    public boolean drawFromFile(File file) throws IOException {
        BufferedReader fileIn = new BufferedReader(new FileReader(file));
        String line;
        while ((line = fileIn.readLine()) != null) {
            line = sanitize(line);
            line = cleanup(line);
            if (line.length() > 0) {
                if (!sendCommand(line)) return false;
            }
        }

        return true;
    }

    public synchronized void close() {
        if (port != null) {
            port.removeEventListener();
            port.close();
        }
    }

    /**
     * Hardware specific cleanup of command. Safely removes select arguments and commands.
     *
     * @param command The command to cleanup.
     * @return A cleaned version of the command. May be an empty string.
     */
    private String cleanup(String command) {
        if (command.equals("G19")) {
            throw new IllegalArgumentException("All units must be in millimeters!");
        }
        switch (command) {
            case "G21":
            case "M3":
            case "M5":
            case "M2":
                return "";

            default:
                return command
                        .replaceAll("F[.\\d\\-]+", "") // remove F arg
                        //.replaceAll("(\\.\\d*)0{2,}(?=\\s|$)", "$10") // Remove trailing zeros
                        .trim();
        }
    }

    private String sanitize(String command) {
        return command
                .replaceAll("\\([\\s\\S]*?\\)", "")
                .replaceAll(";[\\s\\S]*$", "")
                .replace("%", "")
                .trim();
    }
}
