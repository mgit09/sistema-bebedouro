#include <Arduino.h>

const int PH_PIN = 36; // GPIO 36 (VP / SENSOR_VP)

// ============================================================================
// PARÂMETROS CALIBRADOS COM 2 PONTOS (pH 7.20 @ 2.622V e pH 6.20 @ 2.688V)
// ============================================================================
const float SLOPE = -15.04;
float calibration_value = 46.63;

// Configurações de amostragem para estabilização
const int NUM_SAMPLES = 20;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // No ESP32, o GPIO 36 é exclusivamente de entrada analógica
  pinMode(PH_PIN, INPUT);

  // Configura resolução de 12 bits (0 a 4095) e atenuação para leitura até ~3.1V - 3.3V
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.println("======================================");
  Serial.println("  Monitor de pH - ESP32 (GPIO 36/VP)  ");
  Serial.println("======================================");
}

// Função para coletar amostras, ordenar e tirar a média descartando extremos
float getFilteredVoltage() {
  float samples[NUM_SAMPLES];

  for (int i = 0; i < NUM_SAMPLES; i++) {
    // analogReadMilliVolts() faz compensação interna das curvas de fábrica do ESP32
    samples[i] = analogReadMilliVolts(PH_PIN) / 1000.0;
    delay(20);
  }

  // Ordenação (Bubble sort) para eliminar picos de ruído
  for (int i = 0; i < NUM_SAMPLES - 1; i++) {
    for (int j = i + 1; j < NUM_SAMPLES; j++) {
      if (samples[i] > samples[j]) {
        float temp = samples[i];
        samples[i] = samples[j];
        samples[j] = temp;
      }
    }
  }

  // Descarta os 4 menores e os 4 maiores valores, faz a média dos 12 centrais
  float sum = 0;
  for (int i = 4; i < NUM_SAMPLES - 4; i++) {
    sum += samples[i];
  }

  return sum / (NUM_SAMPLES - 8);
}

void loop() {
  float voltage = getFilteredVoltage();
  
  // Cálculo do pH
  float ph = (SLOPE * voltage) + calibration_value;

  // Saída formatada para o Serial Monitor
  Serial.print("Tensao lida: ");
  Serial.print(voltage, 3);
  Serial.print(" V  |  pH Estimado: ");
  Serial.println(ph, 2);

  delay(1000);
}