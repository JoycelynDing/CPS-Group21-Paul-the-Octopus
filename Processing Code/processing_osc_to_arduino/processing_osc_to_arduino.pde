/*
  Wekinator -> Arduino bridge.
  Stable Class 1 arms a round. Once armed, every non-1 prediction is forwarded
  immediately. Arduino owns the 4-second placement and 2-second observation.
*/

import oscP5.*;
import netP5.*;
import processing.serial.*;
import java.util.Locale;

OscP5 oscP5;
Serial port;

final int WAITING_FOR_CLASS_1 = 0;
final int ARMED = 1;
final int FORWARDING = 2;
final int LOCKED_WAITING_FOR_DONE = 3;
final int WAITING_FOR_RESET = 4;
final int CLASS_1_ARM_MS = 500;
final int CLASS_1_RESET_MS = 3000;
final int FORWARD_INTERVAL_MS = 100;

int gateState = WAITING_FOR_CLASS_1;
int latestClass = -1;
float[] classProbabilities = new float[4];
int class1StartedAt = -1;
int lastForwardedAt = -FORWARD_INTERVAL_MS;

void setup() {
  size(420, 170);
  println("Available serial ports:");
  printArray(Serial.list());

  port = new Serial(this, "/dev/cu.usbmodem1101", 9600);
  port.bufferUntil('\n');
  oscP5 = new OscP5(this, 12000);
}

void draw() {
  background(28);
  fill(255);
  textSize(15);
  text("Wekinator -> Arduino", 18, 30);
  text("Latest class: " + latestClass, 18, 60);
  text("State: " + stateName(), 18, 85);
  text("Arduino owns: placement 4 s + observation 2 s", 18, 115);
  text("Serial: /dev/cu.usbmodem11301 @ 9600", 18, 145);
}

void oscEvent(OscMessage message) {
  if (message.checkAddrPattern("/wek/outputs") && message.checkTypetag("f")) {
    latestClass = (int)message.get(0).floatValue();
    return;
  }

  if (message.checkAddrPattern("/Match-Result") &&
      message.arguments().length >= 4) {
    int offset = message.arguments().length >= 5 ? 1 : 0;
    for (int i = 0; i < 4; i++) {
      classProbabilities[i] = message.get(i + offset).floatValue();
    }
    updateGate();
  }
}

void updateGate() {
  int now = millis();

  if (gateState == WAITING_FOR_CLASS_1) {
    if (latestClass != 1) {
      class1StartedAt = -1;
      return;
    }

    if (class1StartedAt < 0) class1StartedAt = now;
    if (now - class1StartedAt >= CLASS_1_ARM_MS) {
      sendArduino(1, 1.0, 0.0);
      gateState = ARMED;
      class1StartedAt = -1;
      println("Round armed by stable Class 1");
    }
    return;
  }

  if (gateState == ARMED) {
    if (latestClass >= 2 && latestClass <= 4) {
      sendCurrentPrediction();
      lastForwardedAt = now;
      gateState = FORWARDING;
      println("Non-1 detected; Arduino started placement timer");
    }
    return;
  }

  if (gateState == FORWARDING) {
    if (latestClass >= 1 && latestClass <= 4 &&
        now - lastForwardedAt >= FORWARD_INTERVAL_MS) {
      sendCurrentPrediction();
      lastForwardedAt = now;
    }
    return;
  }

  if (gateState == LOCKED_WAITING_FOR_DONE) {
    if (latestClass >= 1 && latestClass <= 4 &&
        now - lastForwardedAt >= FORWARD_INTERVAL_MS) {
      sendCurrentPrediction();
      lastForwardedAt = now;
    }
    if (latestClass == 1) {
      if (class1StartedAt < 0) class1StartedAt = now;
    } else {
      class1StartedAt = -1;
    }
    return;
  }

  if (gateState == WAITING_FOR_RESET) {
    if (latestClass != 1) {
      class1StartedAt = -1;
      return;
    }

    if (class1StartedAt < 0) class1StartedAt = now;
    if (now - class1StartedAt >= CLASS_1_RESET_MS) {
      sendArduino(1, 1.0, 0.0);
      gateState = ARMED;
      class1StartedAt = -1;
      println("Stable Class 1 for 3 s; Arduino returned to default");
    }
  }
}

void serialEvent(Serial source) {
  String response = source.readStringUntil('\n');
  if (response == null) return;
  response = trim(response);
  if (response.length() == 0) return;
  println("Arduino <- " + response);

  if (response.equals("WAITING_FOR_ARM")) {
    gateState = WAITING_FOR_CLASS_1;
    class1StartedAt = latestClass == 1 ? millis() : -1;
    println("Arduino restarted; waiting to re-arm from stable Class 1");
  } else if (response.equals("LOCKED")) {
    gateState = LOCKED_WAITING_FOR_DONE;
    class1StartedAt = -1;
    println("Arduino locked its decision; forwarding stopped");
  } else if (response.equals("DONE") && gateState == LOCKED_WAITING_FOR_DONE) {
    gateState = WAITING_FOR_RESET;
    if (latestClass == 1 && class1StartedAt < 0) class1StartedAt = millis();
    println("Arduino action complete; waiting 3 s of stable Class 1");
  }
}

void sendCurrentPrediction() {
  int[] topTwo = topTwoIndices();
  sendArduino(latestClass,
    classProbabilities[topTwo[0]], classProbabilities[topTwo[1]]);
}

int[] topTwoIndices() {
  int best = 0;
  int second = 1;
  if (classProbabilities[second] > classProbabilities[best]) {
    best = 1;
    second = 0;
  }

  for (int i = 2; i < 4; i++) {
    if (classProbabilities[i] > classProbabilities[best]) {
      second = best;
      best = i;
    } else if (classProbabilities[i] > classProbabilities[second]) {
      second = i;
    }
  }
  return new int[] { best, second };
}

void sendArduino(int resultClass, float firstProbability, float secondProbability) {
  String serialLine = String.format(Locale.US, "%d,%.4f,%.4f",
    resultClass, firstProbability, secondProbability);
  println("Arduino: " + serialLine);
  port.write(serialLine + "\n");
}

String stateName() {
  if (gateState == WAITING_FOR_CLASS_1) return "waiting for stable Class 1";
  if (gateState == ARMED) return "armed";
  if (gateState == FORWARDING) return "forwarding samples to Arduino";
  if (gateState == LOCKED_WAITING_FOR_DONE) return "Arduino locked; waiting for DONE";
  return "waiting 3 s of Class 1 to reset";
}
