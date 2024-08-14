#define SENSOR_PIN D6 // Define the pin for the sensor

// Define tank dimensions
#define TANK_HEIGHT_CM 50       // Total height of the tank in centimeters (adjust as needed)
#define TANK_VOLUME_ML 2000     // Total volume of the tank in milliliters (2 liters)

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR_PIN, INPUT); // Set the pin for the sensor as INPUT initially
}

void loop() {
  long duration;
  float distance_cm;
  float fuelLevel_mL;
  float filledVolume_mL;

  // Trigger the sensor
  pinMode(SENSOR_PIN, OUTPUT);
  digitalWrite(SENSOR_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(SENSOR_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SENSOR_PIN, LOW);

  // Switch back to INPUT to read the echo
  pinMode(SENSOR_PIN, INPUT);
  duration = pulseIn(SENSOR_PIN, HIGH);

  // Calculate distance in cm
  if (duration == 0) {
    Serial.println("No echo detected");
    distance_cm = -1; // Use -1 to indicate no valid reading
  } else {
    distance_cm = duration * 0.0344 / 2; // Convert duration to distance in cm
  }



  // Calculate fuel level in milliliters
  if (distance_cm >= 0 && distance_cm <= TANK_HEIGHT_CM) {
    // Calculate the remaining height in the tank
    float remainingHeight_cm = TANK_HEIGHT_CM - distance_cm;
    
    // Calculate the remaining fuel level in milliliters
    fuelLevel_mL = (remainingHeight_cm / TANK_HEIGHT_CM) * TANK_VOLUME_ML;

    // Calculate the filled volume based on remaining fuel
    filledVolume_mL = TANK_VOLUME_ML - fuelLevel_mL;

    Serial.print("Filled Volume: ");
    Serial.print(filledVolume_mL);
    Serial.println(" mL");
  } else {
    Serial.println("Distance out of range");
    filledVolume_mL = -1; // Invalid reading
  }

  delay(1000); // Delay between readings
}
