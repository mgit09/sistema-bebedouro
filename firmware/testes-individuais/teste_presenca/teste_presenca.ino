// teste do sensor ultrassônico HC-SR04 do bebedouro
// placa: ESP32 DevKitC | monitor serial: 115200

// pinos do sensor
const int PINO_TRIGGER_ULTRASSONICO = 22;
const int PINO_ECHO_ULTRASSONICO = 23;

// tempo máximo de espera pelo eco (cerca de 5 metros)
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 30000;

// velocidade do som em centímetros por microssegundo (aprox. 343 m/s)
const float VELOCIDADE_DO_SOM_EM_CENTIMETROS_POR_MICROSSEGUNDO = 0.0343;

// quantidade de medições usadas para tirar a média
const int QUANTIDADE_DE_MEDICOES_PARA_MEDIA = 5;

// pausa entre medições para o eco anterior sumir
const int INTERVALO_ENTRE_MEDICOES_EM_MILISSEGUNDOS = 60;

// abaixo dessa distância considera-se que há um gato presente
const float DISTANCIA_DE_PRESENCA_EM_CENTIMETROS = 30.0;

// intervalo entre as impressões no monitor serial
const int INTERVALO_DE_IMPRESSAO_EM_MILISSEGUNDOS = 300;

/**
 * Faz uma única medição e devolve a distância em centímetros (-1 se não houve eco).
 */
float medirDistanciaUmaVezEmCentimetros() {
  // garante o gatilho em nível baixo antes do pulso
  digitalWrite(
    PINO_TRIGGER_ULTRASSONICO,
    LOW
  );
  delayMicroseconds(2);

  // pulso de 10 microssegundos que dispara o som
  digitalWrite(
    PINO_TRIGGER_ULTRASSONICO,
    HIGH
  );
  delayMicroseconds(10);
  digitalWrite(
    PINO_TRIGGER_ULTRASSONICO,
    LOW
  );

  // mede por quanto tempo o pino ECHO fica em HIGH
  unsigned long duracaoDoEcoEmMicrossegundos = pulseIn(
    PINO_ECHO_ULTRASSONICO,
    HIGH,
    TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS
  );

  // zero significa que o tempo limite acabou sem eco
  if (duracaoDoEcoEmMicrossegundos == 0) {
    return -1.0;
  }

  // o som vai e volta, então divide-se por 2
  return duracaoDoEcoEmMicrossegundos
    * VELOCIDADE_DO_SOM_EM_CENTIMETROS_POR_MICROSSEGUNDO
    / 2.0;
}

/**
 * Faz várias medições, ignora as falhas e devolve a média (-1 se todas falharam).
 */
float medirDistanciaMediaEmCentimetros() {
  float somaDasDistancias = 0.0;
  int quantidadeDeMedicoesValidas = 0;

  for (int medicao = 0; medicao < QUANTIDADE_DE_MEDICOES_PARA_MEDIA; medicao++) {
    float distanciaEmCentimetros = medirDistanciaUmaVezEmCentimetros();

    // só entram na média as medições que tiveram eco
    if (distanciaEmCentimetros > 0) {
      somaDasDistancias += distanciaEmCentimetros;
      quantidadeDeMedicoesValidas++;
    }

    delay(INTERVALO_ENTRE_MEDICOES_EM_MILISSEGUNDOS);
  }

  if (quantidadeDeMedicoesValidas == 0) {
    return -1.0;
  }

  return somaDasDistancias / quantidadeDeMedicoesValidas;
}

/**
 * Configura a serial e os pinos do sensor.
 */
void setup() {
  Serial.begin(115200);
  delay(500);

  // trigger envia o pulso e echo recebe o tempo de retorno
  pinMode(
    PINO_TRIGGER_ULTRASSONICO,
    OUTPUT
  );
  pinMode(
    PINO_ECHO_ULTRASSONICO,
    INPUT
  );

  digitalWrite(
    PINO_TRIGGER_ULTRASSONICO,
    LOW
  );

  Serial.println("=== Teste do sensor ultrassonico HC-SR04 ===");
  Serial.println("Aproxime e afaste a mao do sensor");
}

/**
 * Mede a distância média e informa se há presença detectada.
 */
void loop() {
  float distanciaMediaEmCentimetros = medirDistanciaMediaEmCentimetros();

  if (distanciaMediaEmCentimetros < 0) {
    Serial.println("Sem eco (nada a frente ou fora do alcance)");
  } else {
    Serial.print("Distancia: ");
    Serial.print(
      distanciaMediaEmCentimetros,
      1
    );
    Serial.print(" cm | ");

    if (distanciaMediaEmCentimetros < DISTANCIA_DE_PRESENCA_EM_CENTIMETROS) {
      Serial.println("PRESENCA DETECTADA");
    } else {
      Serial.println("ninguem por perto");
    }
  }

  delay(INTERVALO_DE_IMPRESSAO_EM_MILISSEGUNDOS);
}
