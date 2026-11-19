// teste da bomba 1 pelo sensor de presença (ultrassônico HC-SR04)
//
// como usar:
//   1. abrir o Monitor Serial (Ferramentas > Monitor Serial) em 115200 baud
//   2. colocar a mão (ou o gato) a menos de 30 cm do sensor: a bomba 1 liga
//   3. tirar a mão: a bomba 1 desliga
//
// proteção: se a presença durar mais que o tempo máximo, a bomba desliga sozinha
// e só liga de novo depois que a presença sumir e voltar

#include <Arduino.h>
#include <esp_system.h>

// ===============================================================
// configurações
// ===============================================================

// pino do gate do MOSFET da bomba 1 (conforme o esquemático da placa 2)
const int PINO_MOSFET_BOMBA_1 = 25;

// pinos do sensor ultrassônico
const int PINO_ULTRASSONICO_GATILHO = 32;
const int PINO_ULTRASSONICO_ECO = 33;

// nível lógico que liga o MOSFET da bomba (IRL540N, canal N: nível alto liga)
const int NIVEL_LOGICO_BOMBA_LIGADA = HIGH;

// presença
const float DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS = 30.0;
const float DISTANCIA_SEM_ECO_EM_CENTIMETROS = 400.0;
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 25000;
const unsigned long INTERVALO_ENTRE_LEITURAS_MS = 100;

// quantas leituras seguidas iguais são necessárias para mudar a presença
// (evita a bomba ficar ligando e desligando por uma leitura ruim)
const int LEITURAS_SEGUIDAS_PARA_CONFIRMAR = 3;

// proteção: desliga a bomba sozinha depois desse tempo (0 = sem limite)
const unsigned long TEMPO_MAXIMO_LIGADA_EM_SEGUNDOS = 60;

// intervalo entre as mensagens de acompanhamento no monitor serial
const unsigned long INTERVALO_DA_DEPURACAO_MS = 1000;

// ===============================================================
// variáveis globais
// ===============================================================

bool bombaLigada = false;
unsigned long instanteDeLigamentoEmMs = 0;

float distanciaEmCentimetros = DISTANCIA_SEM_ECO_EM_CENTIMETROS;
bool presencaConfirmada = false;
int leiturasSeguidasDiferentes = 0;
unsigned long instanteDaUltimaLeituraMs = 0;
unsigned long instanteDaUltimaDepuracaoMs = 0;

// verdadeiro depois que a proteção de tempo desligou a bomba
// só volta a falso quando a presença some
bool bloqueadaPelaProtecao = false;

// ===============================================================
// funções
// ===============================================================

/*
  Escreve no monitor serial o tempo desde que o ESP32 ligou
  Se esse tempo voltar a zero ao ligar a bomba, o ESP32 reiniciou
*/
void imprimirTempoLigado() {
  Serial.print(" [tempo ligado: ");
  Serial.print(millis() / 1000.0, 1);
  Serial.println(" s]");
}

/*
  Liga a bomba 1
*/
void ligarBomba() {
  bombaLigada = true;
  instanteDeLigamentoEmMs = millis();
  digitalWrite(PINO_MOSFET_BOMBA_1, NIVEL_LOGICO_BOMBA_LIGADA);

  Serial.print("Bomba 1 LIGADA");
  imprimirTempoLigado();
}

/*
  Desliga a bomba 1
*/
void desligarBomba() {
  bombaLigada = false;
  digitalWrite(PINO_MOSFET_BOMBA_1, !NIVEL_LOGICO_BOMBA_LIGADA);

  Serial.print("Bomba 1 desligada");
  imprimirTempoLigado();
}

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

    Serial.print(presencaConfirmada ? "Presença detectada" : "Presença sumiu");
    Serial.print(" (");
    Serial.print(distanciaEmCentimetros, 1);
    Serial.println(" cm)");
  }
}

/*
  Liga a bomba com presença e desliga sem presença
  Depois que a proteção de tempo desliga a bomba, ela só liga de novo
  quando a presença sumir e voltar
*/
void controlarBomba() {
  if (!presencaConfirmada) {
    bloqueadaPelaProtecao = false;
  }

  if (presencaConfirmada && !bombaLigada && !bloqueadaPelaProtecao) {
    ligarBomba();
  }

  if (!presencaConfirmada && bombaLigada) {
    desligarBomba();
  }
}

/*
  Desliga a bomba se passou do tempo máximo (proteção contra esquecimento)
*/
void verificarTempoMaximo() {
  if (TEMPO_MAXIMO_LIGADA_EM_SEGUNDOS == 0 || !bombaLigada) {
    return;
  }

  bool passouDoLimite = (millis() - instanteDeLigamentoEmMs) >= TEMPO_MAXIMO_LIGADA_EM_SEGUNDOS * 1000UL;
  if (passouDoLimite) {
    Serial.print("Tempo máximo atingido: ");
    desligarBomba();
    bloqueadaPelaProtecao = true;
  }
}

/*
  Traduz o motivo do último reinício do ESP32 para texto
  Argumentos:
    motivo: código devolvido por esp_reset_reason()
*/
const char* descreverMotivoDoReinicio(esp_reset_reason_t motivo) {
  switch (motivo) {
    case ESP_RST_POWERON:
      return "ligado na energia";
    case ESP_RST_EXT:
      return "botão EN (reset externo)";
    case ESP_RST_SW:
      return "reinício por software";
    case ESP_RST_PANIC:
      return "erro no programa (panic)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog (programa travado)";
    case ESP_RST_BROWNOUT:
      return "QUEDA DE TENSÃO (brownout)";
    default:
      return "outro motivo";
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
  Serial.print(" | bomba1=");
  Serial.println(bombaLigada);
}

// ===============================================================
// programa principal
// ===============================================================

/*
  Configura os pinos (bomba desligada) e mostra o motivo do último reinício
*/
void setup() {
  Serial.begin(115200);
  delay(500);

  // bomba começa desligada antes de virar saída
  digitalWrite(PINO_MOSFET_BOMBA_1, !NIVEL_LOGICO_BOMBA_LIGADA);
  pinMode(PINO_MOSFET_BOMBA_1, OUTPUT);

  // ultrassônico
  pinMode(PINO_ULTRASSONICO_GATILHO, OUTPUT);
  pinMode(PINO_ULTRASSONICO_ECO, INPUT);

  // motivo do último reinício (mostra brownout, se tiver acontecido)
  Serial.print("Motivo do último reinício: ");
  Serial.println(descreverMotivoDoReinicio(esp_reset_reason()));

  Serial.println("=== Teste da bomba 1 pelo sensor de presença ===");
  Serial.println("Presença (menos de 30 cm) liga a bomba 1, sem presença desliga");
}

/*
  Lê a presença, controla a bomba e aplica a proteção de tempo máximo
*/
void loop() {
  atualizarPresenca();
  controlarBomba();
  verificarTempoMaximo();
  imprimirDepuracao();
}
