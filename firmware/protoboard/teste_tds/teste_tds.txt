// teste do sensor de tds do bebedouro
// placa: ESP32 DevKitC | monitor serial: 115200

// pino analógico do sensor (adc1)
const int PINO_SENSOR_TDS = 34;

// quantidade de leituras usadas na média
const int QUANTIDADE_DE_LEITURAS_PARA_MEDIA = 30;
const int INTERVALO_ENTRE_LEITURAS_EM_MILISSEGUNDOS = 10;

// sem sensor de temperatura, assume-se água a 25 graus
const float TEMPERATURA_DA_AGUA_EM_CELSIUS = 25.0;

// limite inicial do grupo para considerar a água ruim (ainda a calibrar)
const float LIMITE_DE_TDS_AGUA_RUIM_EM_PPM = 120.0;

// intervalo entre as impressões no monitor serial
const int INTERVALO_DE_IMPRESSAO_EM_MILISSEGUNDOS = 1000;

/**
 * Lê a tensão do sensor várias vezes e devolve a média em volts.
 */
float lerTensaoMediaEmVolts() {
  float somaDasTensoesEmMilivolts = 0.0;

  for (int leitura = 0; leitura < QUANTIDADE_DE_LEITURAS_PARA_MEDIA; leitura++) {
    // analogReadMilliVolts já aplica a calibração de fábrica do adc
    somaDasTensoesEmMilivolts += analogReadMilliVolts(PINO_SENSOR_TDS);
    delay(INTERVALO_ENTRE_LEITURAS_EM_MILISSEGUNDOS);
  }

  float mediaEmMilivolts = somaDasTensoesEmMilivolts / QUANTIDADE_DE_LEITURAS_PARA_MEDIA;

  return mediaEmMilivolts / 1000.0;
}

/**
 * Converte a tensão em ppm usando a curva do fabricante, com compensação de temperatura.
 */
float converterTensaoParaPpm(
  float tensaoEmVolts
) {
  // a condutividade da água muda cerca de 2% por grau acima de 25
  float coeficienteDeCompensacao = 1.0 + 0.02 * (TEMPERATURA_DA_AGUA_EM_CELSIUS - 25.0);
  float tensaoCompensadaEmVolts = tensaoEmVolts / coeficienteDeCompensacao;

  // polinômio de terceiro grau fornecido pelo fabricante do módulo
  float valorEmPpm = (
    133.42 * tensaoCompensadaEmVolts * tensaoCompensadaEmVolts * tensaoCompensadaEmVolts
    - 255.86 * tensaoCompensadaEmVolts * tensaoCompensadaEmVolts
    + 857.39 * tensaoCompensadaEmVolts
  ) * 0.5;

  return valorEmPpm;
}

/**
 * Configura a serial e o pino analógico.
 */
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(
    PINO_SENSOR_TDS,
    INPUT
  );

  // atenuação de 11 dB permite ler até cerca de 3,1V
  analogSetPinAttenuation(
    PINO_SENSOR_TDS,
    ADC_11db
  );

  Serial.println("=== Teste do sensor de TDS ===");
  Serial.println("Mergulhe a sonda em agua e espere estabilizar");
}

/**
 * Lê o sensor, converte para ppm e informa se a água passou do limite.
 */
void loop() {
  float tensaoEmVolts = lerTensaoMediaEmVolts();
  float valorEmPpm = converterTensaoParaPpm(tensaoEmVolts);

  Serial.print("Tensao: ");
  Serial.print(
    tensaoEmVolts,
    3
  );
  Serial.print(" V | TDS: ");
  Serial.print(
    valorEmPpm,
    0
  );
  Serial.print(" ppm | ");

  if (valorEmPpm > LIMITE_DE_TDS_AGUA_RUIM_EM_PPM) {
    Serial.println("AGUA RUIM (acima do limite)");
  } else {
    Serial.println("agua dentro do limite");
  }

  delay(INTERVALO_DE_IMPRESSAO_EM_MILISSEGUNDOS);
}
