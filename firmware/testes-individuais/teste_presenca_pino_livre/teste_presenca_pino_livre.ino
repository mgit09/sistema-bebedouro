// teste do sensor de presença com multímetro
//
// com presença (menos de 30 cm), o ESP32 coloca 3,3 V no pino de teste (GPIO22)
// sem presença, o pino fica em 0 V
//
// como usar:
//   1. ligar a ponta vermelha do multímetro no GPIO22 e a preta no GND
//   2. abrir o Monitor Serial em 115200 baud (opcional, mostra a distância)
//   3. colocar a mão na frente do sensor: o multímetro deve ir de 0 V para 3,3 V
//   4. tirar a mão: o multímetro volta para 0 V

#include <Arduino.h>

// ===============================================================
// configurações
// ===============================================================

// pino livre usado só para o teste (não está ligado a nada no esquemático)
const int PINO_DE_TESTE = 22;

// pinos do sensor ultrassônico (conforme o esquemático da placa 2)
const int PINO_ULTRASSONICO_GATILHO = 32;
const int PINO_ULTRASSONICO_ECO = 33;

// presença
const float DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS = 30.0;
const float DISTANCIA_SEM_ECO_EM_CENTIMETROS = 400.0;
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 25000;
const unsigned long INTERVALO_ENTRE_LEITURAS_MS = 100;

// quantas leituras seguidas diferentes são necessárias para mudar a presença
const int LEITURAS_SEGUIDAS_PARA_CONFIRMAR = 3;

// intervalo entre as mensagens no monitor serial
const unsigned long INTERVALO_DA_DEPURACAO_MS = 1000;

// ===============================================================
// variáveis globais
// ===============================================================

float distanciaEmCentimetros = DISTANCIA_SEM_ECO_EM_CENTIMETROS;
bool presencaConfirmada = false;
int leiturasSeguidasDiferentes = 0;
unsigned long instanteDaUltimaLeituraMs = 0;
unsigned long instanteDaUltimaDepuracaoMs = 0;

// ===============================================================
// funções
// ===============================================================

/*
  Mede a distância do ultrassônico em centímetros
  Retorna DISTANCIA_SEM_ECO_EM_CENTIMETROS quando nenhum eco volta
*/
float medirDistanciaEmCentimetros() {
  // pulso de 10 microssegundos no gatilho
  digitalWrite(PINO_ULTRASSONICO_GATILHO, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_ULTRASSONICO_GATILHO, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_ULTRASSONICO_GATILHO, LOW);

  // duração do eco (zero quando estoura o tempo limite)
  unsigned long duracaoDoEcoEmMicrossegundos = pulseIn(
    PINO_ULTRASSONICO_ECO,
    HIGH,
    TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS
  );

  if (duracaoDoEcoEmMicrossegundos == 0) {
    return DISTANCIA_SEM_ECO_EM_CENTIMETROS;
  }

  // velocidade do som: 0,0343 cm por microssegundo, ida e volta
  return duracaoDoEcoEmMicrossegundos * 0.0343 / 2.0;
}

/*
  Lê o sensor a cada intervalo fixo e atualiza a presença confirmada
  A presença só muda depois de várias leituras seguidas indicando a mudança
*/
void atualizarPresenca() {
  unsigned long agoraMs = millis();

  if (agoraMs - instanteDaUltimaLeituraMs < INTERVALO_ENTRE_LEITURAS_MS) {
    return;
  }
  instanteDaUltimaLeituraMs = agoraMs;

  distanciaEmCentimetros = medirDistanciaEmCentimetros();
  bool presencaAgora = distanciaEmCentimetros < DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS;

  if (presencaAgora == presencaConfirmada) {
    leiturasSeguidasDiferentes = 0;
    return;
  }

  leiturasSeguidasDiferentes++;
  if (leiturasSeguidasDiferentes >= LEITURAS_SEGUIDAS_PARA_CONFIRMAR) {
    presencaConfirmada = presencaAgora;
    leiturasSeguidasDiferentes = 0;
  }
}

/*
  Imprime no monitor serial um resumo para acompanhar o teste
*/
void imprimirDepuracao() {
  if (millis() - instanteDaUltimaDepuracaoMs < INTERVALO_DA_DEPURACAO_MS) {
    return;
  }
  instanteDaUltimaDepuracaoMs = millis();

  Serial.print("distancia=");
  Serial.print(distanciaEmCentimetros, 1);
  Serial.print(" cm | presenca=");
  Serial.print(presencaConfirmada);
  Serial.print(" | GPIO22=");
  Serial.println(digitalRead(PINO_DE_TESTE));
}

// ===============================================================
// programa principal
// ===============================================================

/*
  Configura o pino de teste (começa em 0 V) e o ultrassônico
*/
void setup() {
  Serial.begin(115200);
  delay(500);

  digitalWrite(PINO_DE_TESTE, LOW);
  pinMode(PINO_DE_TESTE, OUTPUT);

  pinMode(PINO_ULTRASSONICO_GATILHO, OUTPUT);
  pinMode(PINO_ULTRASSONICO_ECO, INPUT);

  Serial.println("=== Teste de presença com multímetro ===");
  Serial.println("Presença (menos de 30 cm) coloca 3,3 V no GPIO22");
}

/*
  Lê a presença e copia o resultado para o pino de teste
*/
void loop() {
  atualizarPresenca();
  digitalWrite(PINO_DE_TESTE, presencaConfirmada ? HIGH : LOW);
  imprimirDepuracao();
}
