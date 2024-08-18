#include "Arduino_H7_Video.h"
#include "lvgl.h"
#include "Arduino_GigaDisplayTouch.h"

// Include the custom font source file
LV_FONT_DECLARE(font_18);  // Ensure this matches the name in font_18.c

// Initialize display and touch
Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch TouchDetector;

// Labels for displaying flow rate, pH value, ultrasonic readings, and cloud status
lv_obj_t* flowRateLabel;
lv_obj_t* phValueLabel;
lv_obj_t* adulterationLabel; // Adulteration label within pH box
lv_obj_t* cloudStatusLabel; // Cloud status label
lv_obj_t* ultrasonicReadingLabel; // Ultrasonic readings label

// Button and indicator objects
lv_obj_t* resetButtonInsideFlowRate; // Reset button inside flowRateBox
lv_obj_t* phValueBox; // Reference to the pH value box for background color change
lv_obj_t* ultrasonicBox; // Box for ultrasonic readings

// Flow rate calculation variables
#define FLOW_SENSOR_PIN D13               // GPIO pin connected to the sensor signal
#define PULSES_PER_LITER 450              // Number of pulses per liter (adjust based on your sensor)
volatile int pulseCount = 0;              // Variable to store pulse count
float totalVolume_L = 0;                  // Total volume in liters

unsigned long previousMillis = 0;         // Stores the last time the flow rate was updated
const long flowRateInterval = 1000;       // Interval at which to update (milliseconds)

// pH sensor variables
#define PH_SENSOR_PIN A2                  // Analog pin connected to the pH sensor
#define NUM_SAMPLES 10                    // Number of samples for averaging
#define PH_CALIBRATION_OFFSET -2.33       // Adjusted calibration offset for lemon juice
#define PH_CALIBRATION_SLOPE 0.017        // Calibration slope (default is 3.5 for 5V, but adjusted here)

unsigned long pHPreviousMillis = 0;       // Stores the last time the pH value was updated
const long pHInterval = 1000;             // Interval at which to update pH value (milliseconds)
int pHReadings[NUM_SAMPLES];              // Array to store pH sensor readings
int readingIndex = 0;                     // Index for pHReadings array
float filteredpHValue = 7.00;             // Filtered pH value

// Ultrasonic sensor variables
#define ULTRASONIC_TRIGGER_PIN D9          // GPIO pin connected to the ultrasonic trigger
#define ULTRASONIC_ECHO_PIN D10            // GPIO pin connected to the ultrasonic echo
long duration;
float distance;                          // Calculated distance from the ultrasonic sensor

// Software debounce for pulse counting
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_DELAY 50  // Debounce time in milliseconds

void setup() {
  // Initialize display and touch detector
  Display.begin();
  TouchDetector.begin();

  // Initialize serial communication for debugging
  Serial.begin(115200);

  // Flow sensor setup
  pinMode(FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulses, RISING); // Attach interrupt for pulse counting

  // Initialize pH readings array
  for (int i = 0; i < NUM_SAMPLES; i++) {
    pHReadings[i] = 0;
  }

  // Ultrasonic sensor setup
  pinMode(ULTRASONIC_TRIGGER_PIN, OUTPUT);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);

  // Initialize LVGL
  lv_init();

  // Create a screen and set it as active
  lv_obj_t* screen = lv_scr_act();

  // Create a container with grid 2x2
  static lv_coord_t col_dsc[] = {370, 370, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[] = {200, 200, 200, LV_GRID_TEMPLATE_LAST}; // Adjust row height
  lv_obj_t* cont = lv_obj_create(screen);
  lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
  lv_obj_set_size(cont, Display.width(), Display.height());

  // Set the background color of the container to aqua
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x00FFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_MAIN); // Ensure the background color is applied fully

  lv_obj_center(cont);

  // Top left (Flow Rate)
  lv_obj_t* flowRateBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(flowRateBox, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1); // Adjust to fit within grid
  lv_obj_set_style_bg_color(flowRateBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(flowRateBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(flowRateBox, lv_color_hex(0x000000), LV_PART_MAIN);

  flowRateLabel = lv_label_create(flowRateBox);
  lv_obj_set_style_text_font(flowRateLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_label_set_text(flowRateLabel, "Flow Rate: 0.00 L");
  lv_obj_align(flowRateLabel, LV_ALIGN_TOP_LEFT, 10, 10);

  // Create reset button inside flowRateBox and make it larger
  resetButtonInsideFlowRate = lv_btn_create(flowRateBox);
  lv_obj_set_size(resetButtonInsideFlowRate, 140, 50); // Increase size of the button
  lv_obj_align(resetButtonInsideFlowRate, LV_ALIGN_LEFT_MID, 10, 30); // Position to the left side, slightly lower

  lv_obj_t* resetButtonLabel = lv_label_create(resetButtonInsideFlowRate);
  lv_label_set_text(resetButtonLabel, "Reset");
  lv_obj_set_style_text_font(resetButtonLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_obj_align(resetButtonLabel, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(resetButtonInsideFlowRate, reset_button_event_handler, LV_EVENT_CLICKED, NULL);

  // Top right (pH Value and Adulteration Indicator)
  phValueBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(phValueBox, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_bg_color(phValueBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(phValueBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(phValueBox, lv_color_hex(0x000000), LV_PART_MAIN);

  phValueLabel = lv_label_create(phValueBox);
  lv_obj_set_style_text_font(phValueLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_label_set_text(phValueLabel, "pH Value: 7.00");
  lv_obj_align(phValueLabel, LV_ALIGN_TOP_LEFT, 10, 10); // Adjust alignment as needed

  adulterationLabel = lv_label_create(phValueBox); // Create label within pH box
  lv_obj_set_style_text_font(adulterationLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_label_set_text(adulterationLabel, "Adulterated: No");
  lv_obj_align(adulterationLabel, LV_ALIGN_BOTTOM_LEFT, 10, -10); // Adjust alignment as needed

  // Bottom left (Ultrasonic Readings)
  ultrasonicBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(ultrasonicBox, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1); // Adjust position and size
  lv_obj_set_style_bg_color(ultrasonicBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(ultrasonicBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(ultrasonicBox, lv_color_hex(0x000000), LV_PART_MAIN);

  ultrasonicReadingLabel = lv_label_create(ultrasonicBox);
  lv_obj_set_style_text_font(ultrasonicReadingLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_label_set_text(ultrasonicReadingLabel, "Distance: 0.00 cm");
  lv_obj_align(ultrasonicReadingLabel, LV_ALIGN_CENTER, 0, 0); // Centered within the box

  // Bottom right (Cloud Status)
  lv_obj_t* cloudStatusBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(cloudStatusBox, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_style_bg_color(cloudStatusBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(cloudStatusBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(cloudStatusBox, lv_color_hex(0x000000), LV_PART_MAIN);
  
  cloudStatusLabel = lv_label_create(cloudStatusBox);
  lv_label_set_text(cloudStatusLabel, "Cloud is not connected");
  lv_obj_set_style_text_font(cloudStatusLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_obj_align(cloudStatusLabel, LV_ALIGN_CENTER, 0, 0); // Centered within the box
}

void loop() {
  lv_timer_handler(); // Handle LVGL tasks

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= flowRateInterval) {
    previousMillis = currentMillis;

    // Calculate total volume
    noInterrupts();  // Disable interrupts while calculating
    int pulses = pulseCount;
    pulseCount = 0;
    interrupts();  // Re-enable interrupts

    // Debug: Print the number of pulses
    Serial.print("Pulses: ");
    Serial.println(pulses);

    // Update total volume based on pulses
    totalVolume_L += (pulses / (float)PULSES_PER_LITER);

    // Debug: Print the total volume
    Serial.print("Total Volume: ");
    Serial.println(totalVolume_L);

    // Create a String for flow rate text
    String flowRateText = String(totalVolume_L, 2) + " L";

    // Convert String to C-string (const char*) for lvgl
    lv_label_set_text(flowRateLabel, ("Flow Rate: " + flowRateText).c_str());
  }

  if (currentMillis - pHPreviousMillis >= pHInterval) {
    pHPreviousMillis = currentMillis;

    // Read the pH sensor value
    int sensorValue = analogRead(PH_SENSOR_PIN);

    // Store the reading and update the index
    pHReadings[readingIndex] = sensorValue;
    readingIndex = (readingIndex + 1) % NUM_SAMPLES;

    // Calculate the average of the pH readings
    int total = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      total += pHReadings[i];
    }
    float averageSensorValue = total / (float)NUM_SAMPLES;

    // Calculate the pH value
    filteredpHValue = (averageSensorValue * PH_CALIBRATION_SLOPE) + PH_CALIBRATION_OFFSET;

    // Update the pH label
    String pHValueText = String(filteredpHValue, 2);
    lv_label_set_text(phValueLabel, ("pH Value: " + pHValueText).c_str());

    // Handle adulteration and background color change
    if (filteredpHValue < 6.2 || filteredpHValue > 8.0) {
      lv_label_set_text(adulterationLabel, "Adulterated: Yes");
      static unsigned long lastBlink = 0;
      if (currentMillis - lastBlink >= 500) {  // Blink every 500ms
        lastBlink = currentMillis;
        static bool blinkFlag = false;
        blinkFlag = !blinkFlag;
        lv_color_t color = blinkFlag ? lv_color_hex(0xFF0000) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_bg_color(phValueBox, color, LV_PART_MAIN);
      }
    } else {
      lv_label_set_text(adulterationLabel, "Adulterated: No");
      lv_obj_set_style_bg_color(phValueBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Reset to White
    }
  }

  // Read ultrasonic sensor
  digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIGGER_PIN, LOW);

  duration = pulseIn(ULTRASONIC_ECHO_PIN, HIGH);
  distance = (duration / 2.0) * 0.0344; // Calculate distance in cm

  // Update the ultrasonic reading label
  String distanceText = String(distance, 2);
  lv_label_set_text(ultrasonicReadingLabel, ("Distance: " + distanceText + " cm").c_str());
}

// Event handler for reset button inside flowRateBox
void reset_button_event_handler(lv_event_t* e) {
  // Reset the flow rate and pH value
  lv_label_set_text(flowRateLabel, "Flow Rate: 0.00 L");
  lv_label_set_text(phValueLabel, "pH Value: 7.00");
  lv_label_set_text(adulterationLabel, "Adulterated: No"); // Reset adulteration indication
  totalVolume_L = 0;
  filteredpHValue = 7.00;
}

// Interrupt service routine to count pulses with software debounce
void countPulses() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > DEBOUNCE_DELAY) {
    pulseCount++;
    lastInterruptTime = interruptTime;
  }
}

