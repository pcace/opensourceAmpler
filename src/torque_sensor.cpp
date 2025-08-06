#include "ebike_controller.h"

// =============================================================================
// TORQUE SENSOR EVALUATION (Absolute torque from 2.48V center point)
// =============================================================================

void update_torque() {
  // DEBUG MODE: Use simulated values instead of sensor data
  if (debug_mode && debug_simulate_torque) {
    update_debug_simulation(); // Update debug simulation values
    
    crank_torque_nm = debug_torque_nm;
    filtered_torque = debug_torque_nm; // Direct assignment in debug mode
    
    // Simulate raw ADC value for debugging purposes
    // Scale torque back to ADC range for consistency (ESP32: 0-4095)
    if (debug_torque_nm > 0.0) {
      float torque_ratio = debug_torque_nm / TORQUE_MAX_NM;
      int max_deviation = max(TORQUE_STANDSTILL - TORQUE_MAX_BACKWARD, 
                             TORQUE_MAX_FORWARD - TORQUE_STANDSTILL);
      int simulated_deviation = (int)(torque_ratio * max_deviation);
      raw_torque_value = TORQUE_STANDSTILL + simulated_deviation;
    } else {
      raw_torque_value = TORQUE_STANDSTILL;
    }
    
    return; // Skip real sensor processing
  }
  
  // NORMAL MODE: Original sensor processing
  // Read ADC value (0-4095 for 12-bit ADC on ESP32)
  // ESP32 ADC measures 0-3.3V with 12-bit resolution
  // Torque sensor with 3kΩ pull-down resistor for voltage divider
  raw_torque_value = analogRead(TORQUE_SENSOR_PIN);
  
  // Calculate deviation from center point 
  // For ESP32 with 3.3V reference and 12V sensor via step-up converter + 3kΩ resistor:
  // Center should be around 2880 ADC (2.3V) as defined in TORQUE_STANDSTILL
  int deviation_from_center = raw_torque_value - TORQUE_STANDSTILL;
  
  // Use ABSOLUTE VALUE - force intensity regardless of pedal position
  int absolute_deviation = abs(deviation_from_center);
  
  // Check if deviation is above threshold
  if (absolute_deviation < TORQUE_THRESHOLD) {
    crank_torque_nm = 0.0;  // Below threshold = no meaningful torque
  } else {
    // Calculate maximum possible deviation from center
    // For ESP32: max(2880-0, 4095-2880) = max(2880, 1215) = 2880 ADC
    int max_deviation = max(TORQUE_STANDSTILL - TORQUE_MAX_BACKWARD, 
                           TORQUE_MAX_FORWARD - TORQUE_STANDSTILL);
    
    // Scale absolute deviation to 0-TORQUE_MAX_NM range
    crank_torque_nm = (float)absolute_deviation / max_deviation * TORQUE_MAX_NM;
  }
  
  // Clamp to reasonable range (only positive values)
  if (crank_torque_nm > TORQUE_MAX_NM) crank_torque_nm = TORQUE_MAX_NM;
  if (crank_torque_nm < 0.0) crank_torque_nm = 0.0;
  
  // Direct assignment without filtering
  filtered_torque = crank_torque_nm;
}
