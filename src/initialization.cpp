#include "ebike_controller.h"

// =============================================================================
// INITIALIZATION
// =============================================================================

void ebike_setup() {
  // Initialize assist profiles from configuration
  initializeAssistProfiles();
  
  // Pin configurations
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);
  
  // Battery status LED pin
  pinMode(BATTERY_LED_PIN, OUTPUT);
  digitalWrite(BATTERY_LED_PIN, LOW);
  
  // PAS sensor pins as input with pull-up
  pinMode(PAS_PIN_A, INPUT_PULLUP);
  pinMode(PAS_PIN_B, INPUT_PULLUP);
  
  // Enable hardware interrupts for PAS sensors
  attachInterrupt(digitalPinToInterrupt(PAS_PIN_A), pas_interrupt_handler, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PAS_PIN_B), pas_interrupt_handler, CHANGE);
  
  // Set initial values
  last_loop_time = millis();
  last_pedal_activity = millis();
  
  Serial.println("=== E-Bike Controller v2.0 ===");
  Serial.println("Torque+PAS+Speed combination");
  Serial.print("Active profiles: ");
  Serial.println(NUM_ACTIVE_PROFILES);
  Serial.println("System ready!");
}
