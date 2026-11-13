// Serial format: class,topProbability,secondProbability
// Processing arms the round with: 1,1.0000,0.0000

#include <Adafruit_NeoPixel.h>
#include <stdlib.h>
#include <string.h>

const int MOTOR1_PINS[4] = {7, 8, 9, 10};
const int MOTOR2_PINS[4] = {3, 4, 5, 6};
const int STEP_SEQUENCE[4][4] = {
  {1, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 1},
  {1, 0, 0, 1}
};

const int LED_COUNT = 12;
Adafruit_NeoPixel ledRing(LED_COUNT, 11, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel ledRing2(LED_COUNT, 13, NEO_GRB + NEO_KHZ800);

const int MOTOR2_UP_STEPS = 1400;
const int MOTOR2_DOWN_STEPS = 600;
const unsigned long MOTOR1_STEP_MS = 4;
const unsigned long MOTOR2_STEP_MS = 3;
const unsigned long MOTOR_PAUSE_MS = 5;
const unsigned long LED_INTERVAL_MS = 80;
const unsigned long GREEN_BLINK_INTERVAL_MS = 600;
const unsigned long PLACEMENT_LED_INTERVAL_MS = 40;
const unsigned long SLOW_LED_INTERVAL_MS = 140;
const unsigned long OBSERVATION_EYE_INTERVAL_MS = 450;
const unsigned long PLACEMENT_MS = 4000;
const unsigned long OBSERVATION_MS = 2000;
const unsigned long CLASS1_CANCEL_MS = 3000;
const unsigned long CANCEL_FLASH_MS = 150;

enum RoundState {
  WAITING_FOR_ARM,
  READY_FOR_TRIGGER,
  WAITING_FOR_PLACEMENT,
  OBSERVING_RESULT,
  LOCKED_AFTER_RESULT
};

RoundState roundState = WAITING_FOR_ARM;
unsigned long roundStateStartedAt = 0;
int votes[5];
float topProbabilitySums[5];
float secondProbabilitySums[5];
int latestObservedClass = -1;

int motor1Phase = 0;
int motor2Phase = 0;
int motor1Position = 0;
bool class1SequenceActive = false;
unsigned long class1SequenceStartedAt = 0;
bool abortRequested = false;
bool resultActionComplete = false;
bool holdLargeLeds = false;
bool holdGreenLeds = false;
int heldLedIndex = 0;
int heldLedIndex2 = 0;
unsigned long lastLedUpdate = 0;
unsigned long lastGreenBlinkUpdate = 0;
bool greenBlinkOn = true;
unsigned long lastPlacementLedUpdate = 0;
unsigned long lastObservationEyeUpdate = 0;
bool observationLookingRight = false;
char serialLine[48];
byte serialLength = 0;

void setup() {
  Serial.begin(9600);

  for (int pin : MOTOR1_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  for (int pin : MOTOR2_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  ledRing.begin();
  ledRing.setBrightness(40);
  ledRing2.begin();
  ledRing2.setBrightness(40);
  turnAllLedsOff();

  clearObservation();
  randomSeed(analogRead(A2) ^ micros());
  Serial.println("WAITING_FOR_ARM");
}

void loop() {
  updateRoundState();
  updatePlacementFade();
  updateObservationEyes();
  refreshHeldLed();
  readSerialCommand();
  if (abortRequested) cancelFalseTrigger();
  delay(1);
}

void readSerialCommand() {
  int bytesRemaining = min(Serial.available(), 48);
  while (bytesRemaining-- > 0 && Serial.available() > 0) {
    char incoming = Serial.read();
    if (incoming == '\r') continue;

    if (incoming == '\n') {
      if (serialLength > 0) {
        serialLine[serialLength] = 0;
        handleSerialLine(serialLine);
        serialLength = 0;
      }
      continue;
    }

    if (serialLength < sizeof(serialLine) - 1) {
      serialLine[serialLength++] = incoming;
    } else {
      serialLength = 0;
      Serial.println("ERROR line too long");
    }
  }
}

void handleSerialLine(char *line) {
  char *classText = strtok(line, ",");
  char *topText = strtok(NULL, ",");
  char *secondText = strtok(NULL, ",");
  char *extraText = strtok(NULL, ",");
  if (!classText || !topText || !secondText || extraText) return;

  int resultClass = atoi(classText);
  float topProbability = atof(topText);
  float secondProbability = atof(secondText);
  if (resultClass < 1 || resultClass > 4 ||
      topProbability < 0.0 || topProbability > 1.0 ||
      secondProbability < 0.0 || secondProbability > 1.0) return;

  if (resultClass == 1) {
    if (roundState == LOCKED_AFTER_RESULT && resultActionComplete) {
      armNewRound();
      return;
    }
    if (roundState == WAITING_FOR_PLACEMENT ||
        roundState == OBSERVING_RESULT ||
        roundState == LOCKED_AFTER_RESULT) {
      updateClass1Sequence(true);
    } else {
      armNewRound();
    }
    return;
  }

  updateClass1Sequence(false);

  if (roundState == READY_FOR_TRIGGER) {
    clearObservation();
    resultActionComplete = false;
    roundState = WAITING_FOR_PLACEMENT;
    roundStateStartedAt = millis();
    lastPlacementLedUpdate = 0;
    turnAllLedsOff();
    Serial.println("PLACEMENT 4s");
  } else if (roundState == OBSERVING_RESULT) {
    recordObservation(resultClass, topProbability, secondProbability);
  }
}

void updateRoundState() {
  unsigned long now = millis();

  if (roundState == WAITING_FOR_PLACEMENT &&
      now - roundStateStartedAt >= PLACEMENT_MS) {
    clearObservation();
    roundState = OBSERVING_RESULT;
    roundStateStartedAt = now;
    observationLookingRight = false;
    lastObservationEyeUpdate = now;
    showObservationEyes();
    Serial.println("OBSERVING 2s");
    return;
  }

  if (roundState == OBSERVING_RESULT &&
      now - roundStateStartedAt >= OBSERVATION_MS) {
    finishObservation();
  }
}

void armNewRound() {
  roundState = READY_FOR_TRIGGER;
  clearObservation();
  class1SequenceActive = false;
  abortRequested = false;
  resultActionComplete = false;
  holdLargeLeds = false;
  holdGreenLeds = false;
  turnAllLedsOff();
  returnMotor1ToCenter();
  releaseMotor(MOTOR2_PINS);
  Serial.println("ARMED");
}

void clearObservation() {
  for (int resultClass = 0; resultClass < 5; resultClass++) {
    votes[resultClass] = 0;
    topProbabilitySums[resultClass] = 0;
    secondProbabilitySums[resultClass] = 0;
  }
  latestObservedClass = -1;
}

void recordObservation(int resultClass, float topProbability, float secondProbability) {
  if (resultClass < 2 || resultClass > 4) return;
  votes[resultClass]++;
  topProbabilitySums[resultClass] += topProbability;
  secondProbabilitySums[resultClass] += secondProbability;
  latestObservedClass = resultClass;
}

int majorityClass() {
  int winner = -1;
  for (int resultClass = 2; resultClass <= 4; resultClass++) {
    if (votes[resultClass] == 0) continue;
    if (winner == -1 || votes[resultClass] > votes[winner] ||
        (votes[resultClass] == votes[winner] && resultClass == latestObservedClass)) {
      winner = resultClass;
    }
  }
  return winner;
}

void finishObservation() {
  roundState = LOCKED_AFTER_RESULT;
  holdLargeLeds = false;
  holdGreenLeds = false;
  releaseMotor(MOTOR1_PINS);
  releaseMotor(MOTOR2_PINS);
  turnAllLedsOff();
  Serial.println("LOCKED");
  Serial.flush();

  int winner = majorityClass();
  if (winner == -1) {
    Serial.println("NO_RESULT");
    resultActionComplete = true;
    Serial.println("DONE");
    return;
  }

  float topProbability = topProbabilitySums[winner] / votes[winner];
  float secondProbability = secondProbabilitySums[winner] / votes[winner];
  Serial.print("DECISION class=");
  Serial.print(winner);
  Serial.print(" top=");
  Serial.print(topProbability, 4);
  Serial.print(" second=");
  Serial.println(secondProbability, 4);

  executeDecision(winner, topProbability, secondProbability);
  if (abortRequested) {
    cancelFalseTrigger();
    return;
  }
  resultActionComplete = true;
  Serial.println("DONE");
}

void executeDecision(int resultClass, float topProbability, float secondProbability) {
  if (topProbability > 0.8) {
    Serial.println("High confidence: direct motor action");
  } else if (topProbability >= 0.6) {
    Serial.println("Medium-high confidence: 3 eye laps");
    runEyeLaps(3);
    if (abortRequested) return;
    turnAllLedsOff();
  } else if (topProbability >= 0.4) {
    Serial.println("Medium-low confidence: 5 eye laps with slow Motor 1 sway");
    runEyeLapsWithSlowSway(5);
    if (abortRequested) return;
    turnAllLedsOff();
  } else {
    Serial.println("Low confidence: unknown gesture");
    runEyeLaps(5);
    if (abortRequested) return;
    swayMotor1(2, 100, 12);
    if (abortRequested) return;
    swayMotor1(3, 60, 3);
    if (abortRequested) return;
    holdUnfocusedEyes();
    return;
  }

  holdDecisionGreen();
  runMotorAction(resultClass);
}

void runMotorAction(int resultClass) {
  if (resultClass == 2) {
    moveMotor(MOTOR1_PINS, motor1Phase, 1, 350);
  } else if (resultClass == 3) {
    moveMotor(MOTOR1_PINS, motor1Phase, -1, 230);
  }

  if (abortRequested) return;

  for (int cycle = 0; cycle < 2; cycle++) {
    moveMotor2(-1, MOTOR2_UP_STEPS);
    if (abortRequested) return;
    delay(MOTOR_PAUSE_MS);
    moveMotor2(1, MOTOR2_DOWN_STEPS);
    if (abortRequested) return;
    delay(MOTOR_PAUSE_MS);
  }
}

void returnMotor1ToCenter() {
  if (motor1Position > 0) {
    moveMotorAtSpeed(MOTOR1_PINS, motor1Phase, -1,
                     motor1Position, MOTOR1_STEP_MS, false);
  } else if (motor1Position < 0) {
    moveMotorAtSpeed(MOTOR1_PINS, motor1Phase, 1,
                     -motor1Position, MOTOR1_STEP_MS, false);
  } else {
    releaseMotor(MOTOR1_PINS);
  }
  motor1Position = 0;
}

void moveMotor(const int motorPins[4], int &phase, int direction, int totalSteps) {
  moveMotorAtSpeed(motorPins, phase, direction, totalSteps, MOTOR1_STEP_MS, true);
}

void moveMotor2(int direction, int totalSteps) {
  moveMotorAtSpeed(MOTOR2_PINS, motor2Phase, direction,
                   totalSteps, MOTOR2_STEP_MS, true);
}

void moveMotorAtSpeed(const int motorPins[4], int &phase, int direction,
                      int totalSteps, unsigned long stepDelayMs, bool canAbort) {
  for (int step = 0; step < totalSteps; step++) {
    stepMotorOnce(motorPins, phase, direction);
    refreshHeldLed();
    if (canAbort) {
      readSerialCommand();
      if (abortRequested) break;
    }
    delay(stepDelayMs);
  }
  releaseMotor(motorPins);
}

void stepMotorOnce(const int motorPins[4], int &phase, int direction) {
  phase += direction;
  if (phase > 3) phase = 0;
  if (phase < 0) phase = 3;
  for (int coil = 0; coil < 4; coil++) {
    digitalWrite(motorPins[coil], STEP_SEQUENCE[phase][coil]);
  }
  if (motorPins == MOTOR1_PINS) motor1Position += direction;
}

void releaseMotor(const int motorPins[4]) {
  for (int coil = 0; coil < 4; coil++) digitalWrite(motorPins[coil], LOW);
}

void runEyeLaps(int laps) {
  holdLargeLeds = false;
  holdGreenLeds = false;
  for (int lap = 0; lap < laps; lap++) {
    for (int pixel = 0; pixel < LED_COUNT; pixel++) {
      showLargeLed(pixel);
      waitActionDelay(SLOW_LED_INTERVAL_MS);
      if (abortRequested) return;
    }
  }
}

void runEyeLapsWithSlowSway(int laps) {
  holdLargeLeds = false;
  holdGreenLeds = false;
  const int swayAmplitude = 100;
  const int totalMotorSteps = swayAmplitude * 2;
  int totalFrames = laps * LED_COUNT;
  int completedMotorSteps = 0;

  for (int frame = 0; frame < totalFrames; frame++) {
    showLargeLed(frame % LED_COUNT);

    int targetMotorSteps = (long)(frame + 1) * totalMotorSteps / totalFrames;
    int stepsThisFrame = targetMotorSteps - completedMotorSteps;
    unsigned long waitPerStep = stepsThisFrame > 0
      ? SLOW_LED_INTERVAL_MS / stepsThisFrame
      : SLOW_LED_INTERVAL_MS;
    while (completedMotorSteps < targetMotorSteps) {
      int direction = completedMotorSteps < swayAmplitude ? 1 : -1;
      stepMotorOnce(MOTOR1_PINS, motor1Phase, direction);
      completedMotorSteps++;
      waitActionDelay(waitPerStep);
      if (abortRequested) {
        releaseMotor(MOTOR1_PINS);
        return;
      }
    }
  }
  releaseMotor(MOTOR1_PINS);
}

void swayMotor1(int cycles, int amplitude, unsigned long stepDelayMs) {
  for (int cycle = 0; cycle < cycles; cycle++) {
    for (int step = 0; step < amplitude; step++) {
      stepMotorOnce(MOTOR1_PINS, motor1Phase, 1);
      waitActionDelay(stepDelayMs);
      if (abortRequested) {
        releaseMotor(MOTOR1_PINS);
        return;
      }
    }
    for (int step = 0; step < amplitude; step++) {
      stepMotorOnce(MOTOR1_PINS, motor1Phase, -1);
      waitActionDelay(stepDelayMs);
      if (abortRequested) {
        releaseMotor(MOTOR1_PINS);
        return;
      }
    }
  }
  releaseMotor(MOTOR1_PINS);
}

void holdUnfocusedEyes() {
  turnAllLedsOff();
  heldLedIndex = 1;
  heldLedIndex2 = 7;
  holdLargeLeds = true;
  holdGreenLeds = false;
  showUnfocusedRedEyes();
  lastLedUpdate = millis();
}

void refreshHeldLed() {
  if (!holdLargeLeds && !holdGreenLeds) return;
  unsigned long now = millis();
  if (holdGreenLeds) {
    if (now - lastGreenBlinkUpdate < GREEN_BLINK_INTERVAL_MS) return;
    lastGreenBlinkUpdate = now;
    greenBlinkOn = !greenBlinkOn;
    if (greenBlinkOn) showDecisionGreen();
    else turnAllLedsOff();
    return;
  }
  if (now - lastLedUpdate < LED_INTERVAL_MS) return;
  lastLedUpdate = now;
  showUnfocusedRedEyes();
}

void holdDecisionGreen() {
  holdLargeLeds = false;
  holdGreenLeds = true;
  greenBlinkOn = true;
  lastGreenBlinkUpdate = millis();
  showDecisionGreen();
  lastLedUpdate = millis();
}

void showDecisionGreen() {
  for (int pixel = 0; pixel < LED_COUNT; pixel++) {
    ledRing.setPixelColor(pixel, ledRing.Color(90, 255, 90));
    ledRing2.setPixelColor(pixel, ledRing2.Color(90, 255, 90));
  }
  ledRing.show();
  ledRing2.show();
}

void showUnfocusedRedEyes() {
  ledRing.clear();
  ledRing2.clear();
  ledRing.setPixelColor(heldLedIndex, ledRing.Color(255, 0, 0));
  ledRing2.setPixelColor(heldLedIndex2, ledRing2.Color(255, 0, 0));
  ledRing.show();
  ledRing2.show();
}

void showLargeLed(int pixel) {
  showEyePose(pixel, pixel);
}

void showEyePose(int leftPixel, int rightPixel) {
  ledRing.clear();
  ledRing2.clear();
  ledRing.setPixelColor(leftPixel, ledRing.Color(255, 255, 255));
  ledRing2.setPixelColor(rightPixel, ledRing2.Color(255, 255, 255));
  ledRing.show();
  ledRing2.show();
}

void updateObservationEyes() {
  if (roundState != OBSERVING_RESULT) return;
  unsigned long now = millis();
  if (now - lastObservationEyeUpdate < OBSERVATION_EYE_INTERVAL_MS) return;
  observationLookingRight = !observationLookingRight;
  lastObservationEyeUpdate = now;
  showObservationEyes();
}

void showObservationEyes() {
  showLargeLed(observationLookingRight ? 8 : 3);
}

void updateClass1Sequence(bool isClass1) {
  if (!isClass1) {
    class1SequenceActive = false;
    return;
  }

  unsigned long now = millis();
  if (!class1SequenceActive) {
    class1SequenceActive = true;
    class1SequenceStartedAt = now;
  } else if (now - class1SequenceStartedAt >= CLASS1_CANCEL_MS) {
    abortRequested = true;
  }
}

void waitActionDelay(unsigned long durationMs) {
  unsigned long startedAt = millis();
  while (millis() - startedAt < durationMs) {
    readSerialCommand();
    refreshHeldLed();
    if (abortRequested) return;
    delay(1);
  }
}

void cancelFalseTrigger() {
  bool needsLockMessage = roundState != LOCKED_AFTER_RESULT;
  roundState = LOCKED_AFTER_RESULT;
  if (needsLockMessage) {
    Serial.println("LOCKED");
    Serial.flush();
  }

  releaseMotor(MOTOR1_PINS);
  releaseMotor(MOTOR2_PINS);
  holdLargeLeds = false;
  holdGreenLeds = false;
  flashWhiteTwice();

  abortRequested = false;
  resultActionComplete = false;
  class1SequenceActive = false;
  returnMotor1ToCenter();
  turnAllLedsOff();
  clearObservation();
  roundState = WAITING_FOR_ARM;
  Serial.println("CANCELLED false trigger");
  Serial.println("DONE");
}

void flashWhiteTwice() {
  for (int flash = 0; flash < 2; flash++) {
    for (int pixel = 0; pixel < LED_COUNT; pixel++) {
      ledRing.setPixelColor(pixel, ledRing.Color(255, 255, 255));
      ledRing2.setPixelColor(pixel, ledRing2.Color(255, 255, 255));
    }
    ledRing.show();
    ledRing2.show();
    delay(CANCEL_FLASH_MS);
    turnAllLedsOff();
    delay(CANCEL_FLASH_MS);
  }
}

void updatePlacementFade() {
  if (roundState != WAITING_FOR_PLACEMENT) return;
  unsigned long now = millis();
  if (now - lastPlacementLedUpdate < PLACEMENT_LED_INTERVAL_MS) return;
  lastPlacementLedUpdate = now;

  unsigned long elapsed = now - roundStateStartedAt;
  if (elapsed > PLACEMENT_MS) elapsed = PLACEMENT_MS;
  byte level = (unsigned long)elapsed * 255 / PLACEMENT_MS;
  for (int pixel = 0; pixel < LED_COUNT; pixel++) {
    ledRing.setPixelColor(pixel, ledRing.Color(level, level, level));
    ledRing2.setPixelColor(pixel, ledRing2.Color(level, level, level));
  }
  ledRing.show();
  ledRing2.show();
}

void turnAllLedsOff() {
  ledRing.clear();
  ledRing2.clear();
  ledRing.show();
  ledRing2.show();
}
