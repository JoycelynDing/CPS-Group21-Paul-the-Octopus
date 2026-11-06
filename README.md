# Robot-Arm-Project

**CPS Group 21 — Paul the Octopus**

## 2. System Architecture

The interaction begins at the flag game board. After the player places two flag cards, the octopus shows a short thinking response through its LED eyes and tentacles, then points towards the predicted winner.

```mermaid
flowchart LR
    subgraph Frontstage[Player interaction]
        A[Place two flags] --> B[Octopus thinks] --> C[Tentacle points to the result]
    end

    subgraph Backstage[System process]
        D[Computer camera] --> E[Processing image input]
        E --> F[Wekinator machine learning]
        F --> G[Processing result filter]
        G -->|Serial| H[Arduino]
        H --> I[LED eyes and six motors]
    end

    A -. triggers .-> D
    I -. expresses .-> B
    I -. performs .-> C
```

Behind the interaction, the camera captures the two flags. Processing converts both image regions into pixel data and sends them to Wekinator. The machine-learning model continuously predicts one of four match results and returns the associated class probabilities. Processing then filters the frame-by-frame predictions into one stable result for each round.

The confirmed class and the two highest probabilities are sent to Arduino through serial communication. The class determines the final pointing direction. The difference between the two probabilities represents the model's confidence and controls the thinking behaviour shown before the result.

Arduino converts the decision into LED expressions and tentacle movements. The two tentacles use six motors in total. Each tentacle has one motor for base rotation, one for the main body, and one for the tip.

### Component navigation

| Stage | Role | Detailed section |
| --- | --- | --- |
| Computer camera | Captures the left and right flag regions | [Camera Input and Image Preprocessing](#software-camera-input) |
| Processing input sketch | Crops, downsamples, and sends 7,500 RGB values | [Camera Input and Image Preprocessing](#software-camera-input) |
| Wekinator | Classifies the flag pair into one of four results | [Machine Learning](#machine-learning) |
| Processing bridge | Receives predictions, controls each round, and sends one stable command | [Round-State Filtering](#software-round-filtering) |
| OSC and serial links | Transfer data between Processing, Wekinator, and Arduino | [Communication Interfaces](#software-communication) |
| Arduino | Converts the confirmed result and confidence values into behaviour | [Arduino Command Handling](#software-arduino) |
| LEDs and motors | Express thinking and move the two tentacles | [Hardware Design](#hardware-design) |

## 5. Software Design

The software is divided into camera capture, machine learning, result filtering, communication, and robot control. Each component can be tested separately.

```text
Camera
  ↓
Processing: crop and downsample two flag images
  ↓  OSC /wek/inputs, port 6448
Wekinator: four-class KNN model
  ↓  OSC /wek/outputs and /Match-Result, port 12000
Processing: round-state filtering and serial bridge
  ↓  USB serial, 9600 baud
Arduino: input validation and robot control
```

<a id="software-components"></a>

### 5.1 Software Components

| Component | File | Responsibility |
| --- | --- | --- |
| Camera input | `Camera_to_Wekinator_40x30/Camera_to_Wekinator_40x30.pde` | Captures regions A and B and converts them into fixed-size RGB inputs |
| Machine learning | `Wekinator FlagMatch AB.app` and `FlagMatch_AB_4Class/` | Stores the training data and KNN model and predicts the result for each flag pair |
| Arduino bridge | `processing_osc_to_arduino/processing_osc_to_arduino.pde` | Receives classes and probabilities, manages each round, and sends one confirmed result through serial |
| Robot control | `../Archive_Obsolete/RobotArm/RobotArm.ino` | Validates serial data and controls the Base, X, and Y servos |

The bridge without round-state filtering is stored in `processing_osc_to_arduino_backup/`. It can be used to test direct communication between Wekinator and Arduino.

<a id="software-camera-input"></a>

### 5.2 Camera Input and Image Preprocessing

The camera view contains two horizontal capture regions:

- Region A contains the left flag.
- Region B contains the right flag.
- Both regions use a 2:1 aspect ratio and are not stretched.
- Each region is downsampled to 50 × 25 pixels.
- Each pixel contributes red, green, and blue values.

Each flag produces:

```text
50 × 25 × 3 = 3,750 inputs
```

The two regions are combined into one OSC message:

```text
3,750 + 3,750 = 7,500 inputs
```

Values 1–3,750 belong to region A, and values 3,751–7,500 belong to region B. Wekinator distinguishes the images by their fixed positions in the input array.

<a id="software-communication"></a>

### 5.3 Communication Interfaces

The camera sketch sends image data to Wekinator:

```text
Address: /wek/inputs
Port: 6448
Values: 7,500 floats
```

Wekinator returns the predicted class and class probabilities:

```text
/wek/outputs    Predicted class
/Match-Result   Class probabilities
Port: 12000
```

The Processing bridge sends one CSV line to Arduino:

```text
<class>,<top_probability>,<second_probability>
```

For example:

```text
2,0.7426,0.1831
```

This represents Class 2, with a top probability of 0.7426 and a second-place probability of 0.1831. Processing and Arduino both use a baud rate of 9600.

<a id="software-round-filtering"></a>

### 5.4 Round-State Filtering

Wekinator evaluates every camera frame. Small changes in lighting, position, or camera noise can make the predicted class switch between frames. Sending every prediction directly to Arduino could therefore trigger repeated or conflicting movements.

The Processing bridge uses the following round-state sequence:

```text
Class 1 remains stable for 500 ms
  ↓
The next round is armed
  ↓
Class 2–4 is first detected
  ↓
Wait 2 seconds for the player to place both flags
  ↓
Record predictions for 2 seconds
  ↓
Select the most frequently detected class
  ↓
Send one confirmed result
  ↓
Lock until Class 1 is detected again
```

During the two-second observation period, Processing counts the occurrences of Classes 2–4. The most frequently detected class becomes the final result. The bridge also averages the two highest probabilities from samples associated with that class.

After sending the result, the bridge ignores further predictions until Class 1 remains stable again. Removing both flags therefore ends the current round and enables the next one.

<a id="software-arduino"></a>

### 5.5 Arduino Command Handling

Arduino validates each serial message before starting a movement. The confirmed class selects the final pointing action, while the difference between the two probability values controls the confidence-dependent thinking behaviour.

The current Arduino program also waits three seconds before moving. When this delay is used together with the two-second placement period and two-second observation period in Processing, the robot responds approximately seven seconds after the first non-empty prediction.

<a id="machine-learning"></a>

## 6. Machine Learning

<a id="ml-task"></a>

### 6.1 Classification Task

The project uses a four-class classifier:

| Class | Label |
| --- | --- |
| 1 | Nothing / empty board |
| 2 | A beats B |
| 3 | B beats A |
| 4 | Draw |

The model maps a pair of flag images directly to a match result. It does not identify each country separately or query an external football database.

For matches with a winner, both left-right arrangements are recorded. If the winning flag is in region A, the sample is labelled Class 2. After swapping the flags, it is labelled Class 3. A draw remains Class 4 in both arrangements.

<a id="ml-training-data"></a>

### 6.2 Training Data

When `Start Recording` is active, every captured frame becomes an independent training example. Each recording session contains about 30 frames, with a possible difference of one or two frames because recording is stopped manually.

Using a similar number of frames for each matchup prevents one recorded matchup from contributing much more data than another. It does not make the four output classes equal because the dataset contains different numbers of wins and draws.

The current dataset contains 14,070 examples:

| Class | Examples |
| --- | ---: |
| Class 1 | 30 |
| Class 2 | 5,887 |
| Class 3 | 5,877 |
| Class 4 | 2,276 |
| **Total** | **14,070** |

Recording a static scene produces many nearly identical examples. This increases the dataset size while providing limited additional variation. A more diverse dataset should include changes in:

- flag position;
- camera distance;
- lighting and screen illumination;
- background and shadows.

Both flags should remain fully inside their capture regions.

<a id="ml-model"></a>

### 6.3 KNN Model

Wekinator uses a K-Nearest Neighbours classifier:

```text
K = 101
```

The model finds the 101 closest training examples and predicts a class from their labels.

A single recording produces about 30 highly similar frames. With a small K, one recording can dominate the local neighbourhood and produce probabilities close to 0% or 100%. Setting K above 30 is a practical way to reduce this effect, although it is not a mathematical requirement.

K = 101 is roughly equivalent to more than three 30-frame recording batches. It can produce more cautious results when the nearest samples come from several recordings. A larger K cannot correct duplicated data, class imbalance, or missing lighting and position variations.

<a id="ml-probabilities"></a>

### 6.4 Interpreting Class Probabilities

Wekinator's class probabilities are based on the labels of nearby KNN examples. They are not calibrated probabilities and should not be interpreted as the real-world chance of a team winning.

For an identical input, KNN should return the same result. If the probability changes while the scene appears still, the pixel values are usually changing because of camera noise, automatic exposure, shadows, or small movements. The model does not accumulate confidence over time.

The total number of examples in a class also does not guarantee that the class will win a prediction. KNN mainly depends on the examples closest to the current input.

Frame-level predictions are stabilised by Processing rather than by the model itself. See [Round-State Filtering](#software-round-filtering) for the complete timing and decision logic.

<a id="ml-limitations"></a>

### 6.5 Limitations and Future Improvements

The model uses low-resolution RGB pixels, so lighting, reflections, shadows, background changes, and flag position can affect its output. It learns pixel similarity rather than flag identity, country information, or football rules.

The dataset is imbalanced, and evaluation currently relies on live observation. There is no separate test set or confusion matrix. The displayed probabilities are useful for interaction and motion control, but they are not calibrated confidence scores or real match predictions.

Possible improvements include:

- balancing the four classes;
- reducing duplicate frames;
- recording more lighting, distance, and position variations;
- reserving some flag pairs for testing;
- measuring errors with a confusion matrix;
- adding colour normalisation or more stable visual features.
