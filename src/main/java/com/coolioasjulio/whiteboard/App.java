package com.coolioasjulio.whiteboard;

import java.awt.event.ActionEvent;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.lang.reflect.InvocationTargetException;
import java.util.Enumeration;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.function.Consumer;
import javax.swing.*;
import purejavacomm.CommPortIdentifier;
import purejavacomm.SerialPort;
import purejavacomm.SerialPortEvent;

public class App {
    public static void main(String[] args) {
        App app = new App();
        app.start();
    }

    private static final String DEF_PORT_ITEM = "Select Port";

    private JTextField inputField;
    private JComboBox<String> portDropDown;
    private JButton submitButton;
    private JTextArea outputArea;
    private JButton resetButton;
    private JButton selectFileButton;
    private JButton startButton;
    private JLabel positionLabel;
    private JPanel root;
    private JLabel selectedFileLabel;
    private JFrame frame;

    private final Object serialLock = new Object();
    private SerialPort serialPort;
    private InputConsumer input;
    private PrintStream out;
    private File gcodeFile;

    public App() {
        frame = new JFrame("CNC Whiteboard");
        frame.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        frame.add(root);

        portDropDown.addItem(DEF_PORT_ITEM);

        Enumeration<CommPortIdentifier> ports = CommPortIdentifier.getPortIdentifiers();
        while (ports.hasMoreElements()) {
            CommPortIdentifier id = ports.nextElement();
            portDropDown.addItem(id.getName());
        }

        portDropDown.setSelectedIndex(0);

        portDropDown.addActionListener(this::onPortSelected);
        inputField.addActionListener(this::onSubmit);
        submitButton.addActionListener(this::onSubmit);
        resetButton.addActionListener(this::onReset);
        selectFileButton.addActionListener(this::onChooseFile);
        startButton.addActionListener(this::onDrawFile);

        setInputEnabled(false);

        Runtime.getRuntime().addShutdownHook(new Thread(this::close));
    }

    public void start() {
        frame.pack();
        frame.setVisible(true);
    }

    public void close() {
        synchronized (serialLock) {
            if (serialPort != null) {
                serialPort.removeEventListener();
                serialPort.close();
            }
        }
    }

    private void onPortSelected(ActionEvent e) {
        String port = (String) portDropDown.getSelectedItem();
        if (port == null) return;
        if (port.equals(DEF_PORT_ITEM)) {
            synchronized (serialLock) {
                if (serialPort != null) {
                    serialPort.close();
                    serialPort = null;
                }
            }
            return;
        }
        try {
            SerialPort serialPort = (SerialPort) CommPortIdentifier.getPortIdentifier(port).open("Drawer", 1000);
            serialPort.setSerialPortParams(9600, SerialPort.DATABITS_8,
                    SerialPort.STOPBITS_1,
                    SerialPort.PARITY_NONE);
            synchronized (serialLock) {
                if (this.serialPort != null) {
                    this.serialPort.removeEventListener();
                    this.serialPort.close();
                }
                this.serialPort = serialPort;
                input = new InputConsumer(serialPort.getInputStream());
                out = new PrintStream(serialPort.getOutputStream());
            }
            setInputEnabled(true);
        } catch (Exception ex) {
            ex.printStackTrace();
            showError("There was an error connecting to " + port);
        }
    }

    private void onChooseFile(ActionEvent e) {
        JFileChooser chooser = new JFileChooser();

        int ret = chooser.showOpenDialog(root);
        if (ret == JFileChooser.APPROVE_OPTION) {
            File file = chooser.getSelectedFile();
            if (file.isFile()) {
                gcodeFile = file;
                selectedFileLabel.setText(String.format("<html>Selected file:<br>%s</html>", file.getName()));
            } else {
                showError("Error selecting file!");
            }
        }
    }

    private void onReset(ActionEvent e) {
        outputArea.setText("");
        synchronized (serialLock) {
            if (serialPort != null) {
                serialPort.setDTR(true);
                try {
                    Thread.sleep(30);
                } catch (InterruptedException ex) {
                    ex.printStackTrace();
                }
                serialPort.setDTR(false);
            }
        }
    }

    private void onDrawFile(ActionEvent e) {
        if (gcodeFile == null) {
            showError("No file selected!");
            return;
        }

        setInputEnabled(false);
        new Thread(() -> {
            synchronized (serialLock) {
                if (serialPort != null) {
                    try {
                        BufferedReader fileIn = new BufferedReader(new FileReader(gcodeFile));
                        String line;
                        while ((line = fileIn.readLine()) != null) {
                            line = sanitize(line);
                            if (line.length() > 0) {
                                sendCommand(line);
                            }
                        }
                    } catch (IOException ex) {
                        ex.printStackTrace();
                        showError("An unexpected error occurred!");
                    }
                }
            }
            SwingUtilities.invokeLater(() -> setInputEnabled(true));
        }).start();
    }

    private void setInputEnabled(boolean enabled) {
        startButton.setEnabled(enabled);
        inputField.setEnabled(enabled);
        submitButton.setEnabled(enabled);
    }

    private void onSubmit(ActionEvent e) {
        setInputEnabled(false);
        new Thread(() -> {
            sendCommand(inputField.getText());
            updatePosition();
            SwingUtilities.invokeLater(() -> {
                setInputEnabled(true);
                inputField.requestFocusInWindow();
            });
        }).start();
    }

    private void sendCommand(String c) {
        synchronized (serialLock) {
            if (serialPort != null) {
                final String command = sanitize(c);
                inputField.setText("");
                if (command.length() > 0) {
                    addLine(">>> " + command);
                    // Register input with the input consumer. This needs to happen BEFORE sending the output.
                    // That way, as soon as the response comes it knows to send the input back here.
                    Future<String> future = input.getResponse(false);
                    out.print(command + "\n"); // don't use println, since that uses windows line endings (\r\n instead of \n)
                    out.flush();
                    try {
                        future.get();
                    } catch (InterruptedException | ExecutionException e) {
                        e.printStackTrace();
                    }
                }
            }
        }
    }

    private void runOnEDT(Runnable r) {
        if (SwingUtilities.isEventDispatchThread()) {
            r.run();
        } else {
            try {
                SwingUtilities.invokeAndWait(r);
            } catch (InterruptedException | InvocationTargetException e) {
                e.printStackTrace();
            }
        }
    }

    private void addLine(String line) {
        System.out.println(line);
        runOnEDT(() -> outputArea.setText(outputArea.getText() + line + "\n"));
    }

    private void showError(String message) {
        runOnEDT(() -> JOptionPane.showMessageDialog(root, message, "Error!", JOptionPane.ERROR_MESSAGE));
    }

    private String sanitize(String command) {
        command = command.replaceAll("\\([\\s\\S]*?\\)", "")
                .replaceAll(";[\\s\\S]*$", "")
                .replace("%", "")
                .trim();

        switch (command) {
            case "G21":
            case "M3":
            case "M5":
            case "M2":
            case "G19":
                return "";

            default:
                return command
                        .replaceAll("F[.\\d\\-]+", "") // remove F arg
                        .trim();
        }
    }

    private double parse(String s) {
        return s.equalsIgnoreCase("nan") ? Double.NaN : Double.parseDouble(s);
    }

    private void updatePosition() {
        synchronized (serialLock) {
            if (serialPort != null) {
                try {
                    Future<String> future = input.getResponse(true);
                    out.print("M118\n");
                    out.flush();
                    String response = future.get();
                    if (response == null) {
                        SwingUtilities.invokeLater(() -> {
                            positionLabel.setText("Current Position: N/A");
                            showError("Lost connection unexpectedly!");
                        });
                    } else {
                        String[] parts = response.split(" "); // response in form "ok X:123 Y:123 Z:123"
                        try {
                            double x = parse(parts[1].substring(2));
                            double y = parse(parts[2].substring(2));
                            double z = parse(parts[3].substring(2));

                            System.out.printf("Pos - X: %.3f, Y: %.3f, Z: %.3f\n", x, y, z);
                            SwingUtilities.invokeLater(() ->
                                    positionLabel.setText(
                                            String.format("<html>Current Position:<br>X: %.3f<br>Y: %.3f<br>Z: %.3f</html>", x, y, z)));
                        } catch (NumberFormatException e) {
                            e.printStackTrace(); // don't crash for a malformed response, but still report it
                        }
                    }
                } catch (InterruptedException | ExecutionException e) {
                    e.printStackTrace();
                    SwingUtilities.invokeLater(() -> {
                        positionLabel.setText("Current Position: N/A");
                        showError("Error communicating with machine!");
                    });
                }
            } else {
                positionLabel.setText("Current Position: N/A");
            }
        }
    }

    private class InputConsumer {
        private BufferedReader in;
        private BlockingQueue<InputTask> queue;
        public InputConsumer(InputStream in) {
            queue = new LinkedBlockingQueue<>();
            this.in = new BufferedReader(new InputStreamReader(in));
            Thread inputThread = new Thread(this::inputTask);
            inputThread.start();
        }

        private void inputTask() {
            InputTask activeTask = null;
            try {
                String response;
                while ((response = in.readLine()) != null) {
                    if (activeTask == null) activeTask = queue.poll();

                    if (activeTask == null || !activeTask.silent) {
                        addLine("<<< " + response);
                    }

                    if (activeTask != null && response.startsWith("ok")) {
                        activeTask.callback.accept(response);
                        activeTask = null;
                    }
                }
            } catch (IOException e) {
                // ignored
            } finally {
                if (activeTask != null) {
                    activeTask.callback.accept(null);
                }
                for (InputTask task : queue) {
                    task.callback.accept(null);
                }
            }
        }

        public Future<String> getResponse(boolean silent) {
            CompletableFuture<String> future = new CompletableFuture<>();
            InputTask task = new InputTask();
            task.silent = silent;
            task.callback = future::complete;
            queue.add(task);
            return future;
        }

        private class InputTask {
            private Consumer<String> callback;
            private boolean silent;
        }
    }


}
