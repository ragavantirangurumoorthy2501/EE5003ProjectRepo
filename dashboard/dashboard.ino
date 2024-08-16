#include "Arduino_H7_Video.h"
#include "lvgl.h"
#include "Arduino_GigaDisplayTouch.h"

Arduino_H7_Video Display(800, 480, GigaDisplayShield);
Arduino_GigaDisplayTouch TouchDetector;

// Labels for displaying flow rate and pH value
lv_obj_t* flowRateLabel;
lv_obj_t* phValueLabel;

// Button and indicator objects
lv_obj_t* resetButton;
lv_obj_t* adulterationIndicator;

// Flow rate calculation variables
#define FLOW_SENSOR_PIN D13               // GPIO pin connected to the sensor signal
#define PULSES_PER_LITER 450             // Number of pulses per liter (adjust based on your sensor)
volatile int pulseCount = 0;  // Variable to store pulse count
float totalVolume_L = 0;     // Total volume in liters

unsigned long previousMillis = 0;  // Stores the last time the flow rate was updated
const long interval = 1000;        // Interval at which to update (milliseconds)

void setup() {
  Display.begin();
  TouchDetector.begin();

  // Flow sensor setup
  pinMode(FLOW_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulses, RISING); // Attach interrupt for pulse counting

  // Display & Grid Setup
  lv_obj_t* screen = lv_scr_act();
  
  // Create a container with grid 2x2
  static lv_coord_t col_dsc[] = {370, 370, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row_dsc[] = {215, 215, LV_GRID_TEMPLATE_LAST};
  lv_obj_t* cont = lv_obj_create(screen);
  lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
  lv_obj_set_size(cont, Display.width(), Display.height());
  
  // Set the background color of the container to aqua
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x00FFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_PART_MAIN); // Ensure the background color is applied fully
  
  lv_obj_center(cont);

  // Top left (Flow Rate)
  lv_obj_t* flowRateBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(flowRateBox, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_bg_color(flowRateBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(flowRateBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(flowRateBox, lv_color_hex(0x000000), LV_PART_MAIN);

  flowRateLabel = lv_label_create(flowRateBox);
  lv_obj_set_style_text_font(flowRateLabel, &lv_font_montserrat_14, LV_PART_MAIN);  // Set font size to 14
  lv_label_set_text(flowRateLabel, "Flow Rate: 0.00 L");
  lv_obj_align(flowRateLabel, LV_ALIGN_CENTER, 0, 0);

  // Top right (pH Value)
  lv_obj_t* phValueBox = lv_obj_create(cont);
  lv_obj_set_grid_cell(phValueBox, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_bg_color(phValueBox, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background
  lv_obj_set_style_border_width(phValueBox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(phValueBox, lv_color_hex(0x000000), LV_PART_MAIN);

  phValueLabel = lv_label_create(phValueBox);
  lv_obj_set_style_text_font(phValueLabel, &lv_font_montserrat_14, LV_PART_MAIN);  // Set font size to 14
  lv_label_set_text(phValueLabel, "pH Value: 7.00");
  lv_obj_align(phValueLabel, LV_ALIGN_CENTER, 0, 0);

  // Bottom left (Reset Button)
  resetButton = lv_btn_create(cont);
  lv_obj_set_grid_cell(resetButton, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_style_bg_color(resetButton, lv_color_hex(0x0000FF), LV_PART_MAIN);  // Blue background
  lv_obj_set_style_border_width(resetButton, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(resetButton, lv_color_hex(0x000000), LV_PART_MAIN);

  lv_obj_t* resetLabel = lv_label_create(resetButton);
  lv_label_set_text(resetLabel, "Reset");
  lv_obj_set_style_text_font(resetLabel, &lv_font_montserrat_14, LV_PART_MAIN);  // Set font size to 14
  lv_obj_align(resetLabel, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_event_cb(resetButton, reset_button_event_handler, LV_EVENT_CLICKED, NULL);

  // Bottom right (Adulteration Indicator)
  adulterationIndicator = lv_obj_create(cont);
  lv_obj_set_grid_cell(adulterationIndicator, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_style_bg_color(adulterationIndicator, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set default white background
  lv_obj_set_style_border_width(adulterationIndicator, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(adulterationIndicator, lv_color_hex(0x000000), LV_PART_MAIN);

  lv_obj_t* adulterationLabel = lv_label_create(adulterationIndicator);
  lv_label_set_text(adulterationLabel, "Adulterated: No");
  lv_obj_set_style_text_font(adulterationLabel, &lv_font_montserrat_14, LV_PART_MAIN);  // Set font size to 14
  lv_obj_align(adulterationLabel, LV_ALIGN_CENTER, 0, 0);
}

void loop() {
  lv_timer_handler(); // Handle LVGL tasks

  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Calculate total volume
    noInterrupts();  // Disable interrupts while calculating
    int pulses = pulseCount;
    pulseCount = 0;
    interrupts();  // Re-enable interrupts

    // Update total volume based on pulses
    totalVolume_L += (pulses / (float)PULSES_PER_LITER);

    // Create a String for flow rate text
    String flowRateText = String(totalVolume_L, 2) + " L";

    // Convert String to C-string (const char*) for lvgl
    lv_label_set_text(flowRateLabel, ("Flow Rate: " + flowRateText).c_str());
  }
}

// Event handler for reset button
void reset_button_event_handler(lv_event_t* e) {
  // Reset the flow rate and pH value
  lv_label_set_text(flowRateLabel, "Flow Rate: 0.00 L");
  lv_label_set_text(phValueLabel, "pH Value: 7.00");

  // Reset the adulteration indicator
  lv_obj_set_style_bg_color(adulterationIndicator, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_label_set_text(lv_obj_get_child(adulterationIndicator, 0), "Adulterated: No");

  // Reset total volume
  totalVolume_L = 0;
}

// Interrupt service routine for counting pulses
void countPulses() {
  pulseCount++;
}

