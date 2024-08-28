#include "thingProperties.h"

// Necessary libraries for Arduino Display are added here
#include "Arduino_H7_Video.h"
#include "lvgl.h"
#include "Arduino_GigaDisplayTouch.h"

// custom font is stored in the same directory and used
LV_FONT_DECLARE(font_18);  

// Initialize display and touch
Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch TouchDetector;

// Labels for displaying flow rate, pH value, ultrasonic readings, and cloud status
lv_obj_t* flowRateLabel;
lv_obj_t* phValueLabel;
lv_obj_t* adulterationLabel; // Adulteration label within pH box
lv_obj_t* MessageLabel; // Cloud status label
lv_obj_t* filledVolumeLabel; // Filled volume label

// Objects included for buttons and Indicator
lv_obj_t* resetButtonInsideFlowRate; // Reset button inside flowRateBox
lv_obj_t* phValueBox; // Reference to the pH value box for background color change
lv_obj_t* ultrasonicBox; // Box for ultrasonic readings

// Variables for Flow rate calculations
#define FLOW_SENSOR_PIN D13               
#define PULSES_PER_LITER 450              // Number of pulses per liter (adjusted as required)
volatile int pulseCount = 0;              
float totalVolume_L = 0;                 

unsigned long previousMillis = 0;         
const long flowRateInterval = 1000;       

// pH sensor variables
#define PH_SENSOR_PIN A2                  // Analog pin was connected to the pH sensor
#define NUM_SAMPLES 10                    // Number of samples for averaging
#define PH_CALIBRATION_OFFSET -2.33       // Adjusted calibration offset for lemon juice
#define PH_CALIBRATION_SLOPE 0.017        // Calibration slope (default is 3.5 for 5V, but adjusted here)

unsigned long pHPreviousMillis = 0;       
const long pHInterval = 1000;             
int pHReadings[NUM_SAMPLES];              
int readingIndex = 0;                     
float filteredpHValue = 7.00;             

// Ultrasonic sensor variables
#define ULTRASONIC_SIG_PIN D6          // D6 GPIO pin connected to the ultrasonic SIG

// Software debounce for pulse counting
volatile unsigned long lastInterruptTime = 0;
#define DEBOUNCE_DELAY 50  

// Defined tank dimensions
#define TANK_HEIGHT_CM 50       // Total height of the tank in centimeters (adjusted as required)
#define TANK_VOLUME_ML 2000     // Total volume of the tank in milliliters (2 liters claculated using a waterbottle)

unsigned long ultrasonicPreviousMillis = 0; // Last update time for ultrasonic reading
const long ultrasonicUpdateInterval = 3000; // Update interval (3 seconds)

void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(9600);
  delay(1500); 

  
  initProperties();

  // Connect to Arduino IoT Cloud
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  
  // Debugging information
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();

  // Initialize display and touch detector
  Display.begin();
  TouchDetector.begin();

  // Flow sensor setup
  pinMode(FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulses, RISING); // Attach interrupt for pulse counting

  // Initialize pH readings array
  for (int i = 0; i < NUM_SAMPLES; i++) {
    pHReadings[i] = 0;
  }

  // Ultrasonic sensor setup
  pinMode(ULTRASONIC_SIG_PIN, OUTPUT);

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

  // Bottom left (Ultrasonic Reading)
  ultrasonicBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(ultrasonicBox, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_style_bg_color(ultrasonicBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(ultrasonicBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(ultrasonicBox, lv_color_hex(0x000000), LV_PART_MAIN);

  filledVolumeLabel = lv_label_create(ultrasonicBox);
  lv_obj_set_style_text_font(filledVolumeLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_label_set_text(filledVolumeLabel, "Filled Volume: 0.00 mL");
  lv_obj_align(filledVolumeLabel, LV_ALIGN_CENTER, 0, 0);

  // Bottom right (Message box like project topic)
  lv_obj_t* MessageBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(MessageBox, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_style_bg_color(MessageBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(MessageBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(MessageBox, lv_color_hex(0x000000), LV_PART_MAIN);

  MessageLabel = lv_label_create(MessageBox);
  lv_obj_set_style_text_font(MessageLabel, &font_18, LV_PART_MAIN);  // Set custom font
  lv_label_set_text(MessageLabel, "Real-Time Fuel Theft Prevention\nand Analysis System Utilizing\nIoT Technologies");
  lv_obj_align(MessageLabel, LV_ALIGN_CENTER, 0, 0); // Centered within the box
}

void loop() {
  ArduinoCloud.update();  // Update cloud variables
  lv_task_handler();      // Handle LVGL tasks
  delay(5);              // Add a small delay to avoid high CPU usage

  unsigned long currentMillis = millis();

  // Update flow rate
  if (currentMillis - previousMillis >= flowRateInterval) {
    previousMillis = currentMillis;
    updateFlowRate();
  }

  // Update pH value
  if (currentMillis - pHPreviousMillis >= pHInterval) {
    pHPreviousMillis = currentMillis;
    updatePHValue();
  }

  // Update filled volume every 3 seconds to display stable readings
  if (currentMillis - ultrasonicPreviousMillis >= ultrasonicUpdateInterval) {
    ultrasonicPreviousMillis = currentMillis;
    updateUltrasonicReading();
  }
}

void countPulses() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime >= DEBOUNCE_DELAY) {
    pulseCount++;
    lastInterruptTime = interruptTime;
  }
}

void updateFlowRate() {
  noInterrupts();  
  int pulses = pulseCount;
  pulseCount = 0;
  interrupts();  

  totalVolume_L += (pulses / (float)PULSES_PER_LITER);
  lv_label_set_text(flowRateLabel, ("Flow Rate: " + String(totalVolume_L, 2) + " L").c_str());

  // Update cloud variable
  tank_Capacity = totalVolume_L;
  ArduinoCloud.update(); // Ensure the cloud variable is updated
}

void updatePHValue() {
  int sensorValue = analogRead(PH_SENSOR_PIN);
  pHReadings[readingIndex] = sensorValue;
  readingIndex = (readingIndex + 1) % NUM_SAMPLES;

  int total = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    total += pHReadings[i];
  }
  float averageSensorValue = total / (float)NUM_SAMPLES;
  filteredpHValue = (averageSensorValue * PH_CALIBRATION_SLOPE) + PH_CALIBRATION_OFFSET;

  lv_label_set_text(phValueLabel, ("pH Value: " + String(filteredpHValue, 2)).c_str());

  if (filteredpHValue < 6.2 || filteredpHValue > 8.0) {
    lv_label_set_text(adulterationLabel, "Adulterated: Yes");
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 500) {  
      lastBlink = millis();
      static bool blinkFlag = false;
      blinkFlag = !blinkFlag;
      lv_color_t color = blinkFlag ? lv_color_hex(0xFF0000) : lv_color_hex(0xFFFFFF);
      lv_obj_set_style_bg_color(phValueBox, color, LV_PART_MAIN);
    }
  } else {
    lv_label_set_text(adulterationLabel, "Adulterated: No");
    lv_obj_set_style_bg_color(phValueBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  }
}

void updateUltrasonicReading() {
  // Trigger pulse in which the signal pin is used for both trigger and echo
  pinMode(ULTRASONIC_SIG_PIN, OUTPUT);
  digitalWrite(ULTRASONIC_SIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_SIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_SIG_PIN, LOW);

  pinMode(ULTRASONIC_SIG_PIN, INPUT);
  long duration = pulseIn(ULTRASONIC_SIG_PIN, HIGH);
  float distance_cm = (duration / 2.0) * 0.0344;

  float filledVolume_mL = -1; // Default to -1 to indicate invalid reading

  if (distance_cm >= 0 && distance_cm <= TANK_HEIGHT_CM) {
    // Calculates the remaining height in the tank
    float remainingHeight_cm = TANK_HEIGHT_CM - distance_cm;

    // Calculates the remaining fuel level in milliliters
    float fuelLevel_mL = (remainingHeight_cm / TANK_HEIGHT_CM) * TANK_VOLUME_ML;

    // Calculates the filled volume based on remaining fuel
    filledVolume_mL = TANK_VOLUME_ML - fuelLevel_mL;
  }

  if (filledVolume_mL >= 0) {
    lv_label_set_text(filledVolumeLabel, ("Filled Volume: " + String(filledVolume_mL, 2) + " mL").c_str());
    tank_Capacity = filledVolume_mL; // Update cloud variable
  } else {
    lv_label_set_text(filledVolumeLabel, "Filled Volume: Error");
  }
}

void reset_button_event_handler(lv_event_t* e) {
  // Reset total volume
  totalVolume_L = 0;
  lv_label_set_text(flowRateLabel, ("Flow Rate: " + String(totalVolume_L, 2) + " L").c_str());
  
  // Update cloud variable
  tank_Capacity = totalVolume_L;
  ArduinoCloud.update(); // Ensure the cloud variable is updated
}

// Cloud variable change handler
void onTankCapacityChange() {
  
  Serial.println("Cloud variable tank_Capacity changed: " + String(tank_Capacity));
}

