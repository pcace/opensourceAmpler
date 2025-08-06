#include "ebike_controller.h"

// =============================================================================
// SPEED-DEPENDENT ASSIST INTERPOLATION
// =============================================================================

void calculate_speed_dependent_assist() {
  // Fallback: If no valid VESC data, use first value (0 km/h)
  if (!vesc_data_valid) {
    dynamic_assist_factor = ASSIST_PROFILES[current_mode][0];
    return;
  }
  
  // Find the two support points for interpolation
  int lower_index = 0;
  int upper_index = NUM_SPEED_POINTS - 1;
  
  // Find correct interval
  for (int i = 0; i < NUM_SPEED_POINTS - 1; i++) {
    if (current_speed_kmh >= SPEED_POINTS_KMH[i] && 
        current_speed_kmh <= SPEED_POINTS_KMH[i + 1]) {
      lower_index = i;
      upper_index = i + 1;
      break;
    }
  }
  
  // Speed outside defined range?
  if (current_speed_kmh <= SPEED_POINTS_KMH[0]) {
    // Below minimum speed → first value
    dynamic_assist_factor = ASSIST_PROFILES[current_mode][0];
    return;
  }
  
  if (current_speed_kmh >= SPEED_POINTS_KMH[NUM_SPEED_POINTS - 1]) {
    // Above maximum speed → last value  
    dynamic_assist_factor = ASSIST_PROFILES[current_mode][NUM_SPEED_POINTS - 1];
    return;
  }
  
  // Linear interpolation between the two support points
  float speed_low = SPEED_POINTS_KMH[lower_index];
  float speed_high = SPEED_POINTS_KMH[upper_index];
  float assist_low = ASSIST_PROFILES[current_mode][lower_index];
  float assist_high = ASSIST_PROFILES[current_mode][upper_index];
  
  // Interpolation factor (0.0 = lower point, 1.0 = upper point)
  float interpolation_factor = (current_speed_kmh - speed_low) / (speed_high - speed_low);
  
  // Linear interpolation: y = y1 + t × (y2 - y1)
  dynamic_assist_factor = assist_low + interpolation_factor * (assist_high - assist_low);
  
  // Safety limit
  dynamic_assist_factor = constrain(dynamic_assist_factor, 0.0, 4.0);
}

// =============================================================================
// POWER CALCULATION
// =============================================================================

void calculate_assist_power() {
  // 1. CALCULATE HUMAN POWER
  // P_human = M_crank × ω_crank × 2π  [Watt = Nm × rad/s]
  human_power_watts = filtered_torque * current_cadence_rps * 2.0 * PI;
  
  // Plausibility check (humans can achieve max ~500W short-term)
  if (human_power_watts > 500.0) {
    human_power_watts = 500.0;
  }
  
  // 2. CALCULATE SPEED-DEPENDENT ASSIST FACTOR
  calculate_speed_dependent_assist();
  
  // 3. CALCULATE ASSIST POWER (NOW speed-dependent!)
  assist_power_watts = dynamic_assist_factor * human_power_watts;
  
  // 4. LIMIT POWER
  if (assist_power_watts > MAX_MOTOR_POWER) {
    assist_power_watts = MAX_MOTOR_POWER;
  }
  
  // 5. CALCULATE TARGET MOTOR CURRENT
  // P = U × I  →  I = P / U
  if (assist_power_watts > 0 && VOLTAGE_BATTERY > 0) {
    target_current_amps = assist_power_watts / VOLTAGE_BATTERY;
  } else {
    target_current_amps = 0.0;
  }
  
  // 6. LIMIT CURRENT
  target_current_amps = constrain(target_current_amps, 0.0, MAX_MOTOR_CURRENT);
  
  // 7. MINIMUM CURRENT FOR MOTOR ACTIVATION
  if (target_current_amps > 0 && target_current_amps < MIN_MOTOR_CURRENT) {
    target_current_amps = MIN_MOTOR_CURRENT;
  }
  
  // DEBUG: Log power calculation periodically
  static unsigned long last_power_debug = 0;
  unsigned long now = millis();
  if (now - last_power_debug > 2000) { // Every 2 seconds
    Serial.printf("POWER CALC - Torque:%.1fNm Cadence:%.1fRPM Human:%.0fW Factor:%.2f Assist:%.0fW Current:%.2fA\n", 
                  filtered_torque, current_cadence_rpm, human_power_watts, 
                  dynamic_assist_factor, assist_power_watts, target_current_amps);
    last_power_debug = now;
  }
}
