#define ANALOG_IN_PIN  32                                                   // ESP32 pin GPIO32 (ADC1_4) connected to voltage sensor
#define R1 47                                                               // resistor values in voltage sensor (in kiloohms)
#define R2 10                                                               // resistor values in voltage sensor (in kiloohms)
#define ADC_RESOLUTION 4096                                                 // range ADC working in 12-bit mode
#define VOLTAGE_MAX 4.2                                                     // max battery voltage
#define VOLTAGE_MIN 3.3                                                     // min battery voltage
#define ADC_REFERENCE 1.1                                                   // reference voltage of ESP32
#define BATTERY_MIN_PERCENTAGE 15                                           // min battery percentage to warning

// calc voltage
float voltage(int adc)
{
    return (float)adc * (ADC_REFERENCE / ADC_RESOLUTION) * ((R1 + R2) / R2);
}

// calc percentage of battery
float calc_battery_percentage(float vadc)
{
    float battery_percentage = 100.0 * (vadc - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN);

    if (battery_percentage < 0.0)
        battery_percentage = 0.0;
    if (battery_percentage > 100.0)
        battery_percentage = 100.0;

    return battery_percentage;
}

void battery_control() {
    // int adc = analogRead(ANALOG_IN_PIN);
    int adc = 2477;

    bat = calc_battery_percentage(voltage(adc));
	Serial.printf("Battery: %.2f%\n", bat);
	// print a low battery warning
    if (bat <= 15.0)
        Serial.print("Warning! Battery is low.\n");
}