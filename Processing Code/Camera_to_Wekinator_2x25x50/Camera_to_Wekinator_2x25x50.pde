import processing.video.*;
import oscP5.*;
import netP5.*;

Capture camera;
OscP5 oscP5;
NetAddress wekinator;

final int CAMERA_WIDTH = 640;
final int CAMERA_HEIGHT = 480;
final int CAMERA_X = 60;
final int CAMERA_Y = 60;
final int PANEL_X = 740;
final int GRID_WIDTH = 50;
final int GRID_HEIGHT = 25;
final int PIXEL_COUNT = GRID_WIDTH * GRID_HEIGHT;
final int VALUES_PER_IMAGE = PIXEL_COUNT * 3;
final int INPUT_COUNT = VALUES_PER_IMAGE * 2;
final int SEND_EVERY_N_FRAMES = 8;
final int DEFAULT_CROP_WIDTH = 300;
final int DEFAULT_CROP_HEIGHT = 150;
final int DEFAULT_CROP_Y = 165;

float[] samples = new float[INPUT_COUNT];
color[] sampledColoursA = new color[PIXEL_COUNT];
color[] sampledColoursB = new color[PIXEL_COUNT];
boolean cameraReady = false;
boolean screenLightOn = true;

// Default boxes match the two flag positions in the test setup.
int[] cropX = {10, 330};
int[] cropY = {DEFAULT_CROP_Y, DEFAULT_CROP_Y};
int[] cropWidth = {DEFAULT_CROP_WIDTH, DEFAULT_CROP_WIDTH};
int[] cropHeight = {DEFAULT_CROP_HEIGHT, DEFAULT_CROP_HEIGHT};
int cropStartX;
int cropStartY;
boolean selectingCrop = false;
int activeCrop = 0;

void settings() {
  size(1080, 600);
  pixelDensity(1);
}

void setup() {
  println("Available cameras:");
  printArray(Capture.list());

  camera = new Capture(this, CAMERA_WIDTH, CAMERA_HEIGHT);
  camera.start();

  oscP5 = new OscP5(this, 12004);
  wekinator = new NetAddress("127.0.0.1", 6448);
}

void draw() {
  background(screenLightOn ? color(255) : color(20));

  // Keep the controls dark while the white outer margin acts as a screen light.
  noStroke();
  fill(20);
  rect(PANEL_X - 20, 40, width - PANEL_X, height - 80);

  if (camera.available()) {
    camera.read();
    cameraReady = true;
    sampleCamera();
  }

  if (cameraReady) {
    image(camera, CAMERA_X, CAMERA_Y, CAMERA_WIDTH, CAMERA_HEIGHT);
    drawCropRectangle(0, color(0, 255, 0));
    drawCropRectangle(1, color(0, 200, 255));
    drawSamplePreview(sampledColoursA, PANEL_X, 60);
    drawSamplePreview(sampledColoursB, PANEL_X, 260);

    if (frameCount % SEND_EVERY_N_FRAMES == 0) {
      sendToWekinator();
    }
  }

  fill(255);
  textSize(14);
  text("A - left flag", PANEL_X, 234);
  text("B - right flag", PANEL_X, 434);
  text("2 x 50 x 25 RGB = 7500 inputs", PANEL_X, 470);
  text("Press 1 or 2, then drag to set A or B", PANEL_X, 496);
  text("R: reset boxes   |   OSC port 6448", PANEL_X, 522);
  text("Crop ratio locked at 2:1", PANEL_X, 548);
  text("L: screen light " + (screenLightOn ? "ON" : "OFF"), PANEL_X, 574);
}

void sampleCamera() {
  camera.loadPixels();
  sampleCrop(0, 0, sampledColoursA);
  sampleCrop(1, VALUES_PER_IMAGE, sampledColoursB);
}

void sampleCrop(int cropNumber, int inputOffset, color[] previewColours) {
  for (int y = 0; y < GRID_HEIGHT; y++) {
    for (int x = 0; x < GRID_WIDTH; x++) {
      int sampleX = cropX[cropNumber] +
                    int((x + 0.5) * cropWidth[cropNumber] / GRID_WIDTH);
      int sampleY = cropY[cropNumber] +
                    int((y + 0.5) * cropHeight[cropNumber] / GRID_HEIGHT);
      sampleX = constrain(sampleX, 0, camera.width - 1);
      sampleY = constrain(sampleY, 0, camera.height - 1);
      color pixelColour = camera.pixels[sampleY * camera.width + sampleX];

      int pixelIndex = y * GRID_WIDTH + x;
      int inputIndex = inputOffset + pixelIndex * 3;
      previewColours[pixelIndex] = pixelColour;
      samples[inputIndex] = red(pixelColour) / 255.0;
      samples[inputIndex + 1] = green(pixelColour) / 255.0;
      samples[inputIndex + 2] = blue(pixelColour) / 255.0;
    }
  }
}

void drawCropRectangle(int cropNumber, color lineColour) {
  noFill();
  stroke(lineColour);
  strokeWeight(cropNumber == activeCrop ? 5 : 3);
  rect(CAMERA_X + cropX[cropNumber], CAMERA_Y + cropY[cropNumber],
       cropWidth[cropNumber], cropHeight[cropNumber]);
}

void drawSamplePreview(color[] previewColours, int previewX, int previewY) {
  int cellSize = 6;

  noStroke();
  for (int y = 0; y < GRID_HEIGHT; y++) {
    for (int x = 0; x < GRID_WIDTH; x++) {
      fill(previewColours[y * GRID_WIDTH + x]);
      rect(previewX + x * cellSize,
           previewY + y * cellSize,
           cellSize, cellSize);
    }
  }
}

void sendToWekinator() {
  OscMessage message = new OscMessage("/wek/inputs");

  for (int i = 0; i < samples.length; i++) {
    message.add(samples[i]);
  }

  oscP5.send(message, wekinator);
}

void mousePressed() {
  if (mouseX >= CAMERA_X && mouseX < CAMERA_X + CAMERA_WIDTH &&
      mouseY >= CAMERA_Y && mouseY < CAMERA_Y + CAMERA_HEIGHT) {
    cropStartX = mouseX - CAMERA_X;
    cropStartY = mouseY - CAMERA_Y;
    selectingCrop = true;
  }
}

void mouseDragged() {
  if (!selectingCrop) {
    return;
  }

  int endX = constrain(mouseX - CAMERA_X, 0, CAMERA_WIDTH - 1);
  int endY = constrain(mouseY - CAMERA_Y, 0, CAMERA_HEIGHT - 1);
  int dx = endX - cropStartX;
  int dy = endY - cropStartY;

  int desiredWidth = max(abs(dx), abs(dy) * 2);
  int horizontalLimit = dx >= 0 ? CAMERA_WIDTH - cropStartX : cropStartX;
  int verticalLimit = (dy >= 0 ? CAMERA_HEIGHT - cropStartY : cropStartY) * 2;
  int width = min(desiredWidth, min(horizontalLimit, verticalLimit));
  width = max(2, width - width % 2);
  int height = width / 2;

  cropX[activeCrop] = dx >= 0 ? cropStartX : cropStartX - width;
  cropY[activeCrop] = dy >= 0 ? cropStartY : cropStartY - height;
  cropWidth[activeCrop] = width;
  cropHeight[activeCrop] = height;
}

void mouseReleased() {
  if (!selectingCrop) {
    return;
  }

  selectingCrop = false;
  if (cropWidth[activeCrop] < 20 || cropHeight[activeCrop] < 20) {
    resetCrop(activeCrop);
  }
}

void keyPressed() {
  if (key == '1') {
    activeCrop = 0;
  } else if (key == '2') {
    activeCrop = 1;
  } else if (key == 'r' || key == 'R') {
    resetCrop(0);
    resetCrop(1);
  } else if (key == 'l' || key == 'L') {
    screenLightOn = !screenLightOn;
  }
}

void resetCrop(int cropNumber) {
  cropX[cropNumber] = cropNumber == 0 ? 10 : 330;
  cropY[cropNumber] = DEFAULT_CROP_Y;
  cropWidth[cropNumber] = DEFAULT_CROP_WIDTH;
  cropHeight[cropNumber] = DEFAULT_CROP_HEIGHT;
}
