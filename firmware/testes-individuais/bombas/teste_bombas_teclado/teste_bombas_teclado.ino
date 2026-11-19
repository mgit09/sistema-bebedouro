// teste das bombas pelo teclado (Monitor Serial da Arduino IDE)
//
// como usar:
//   1. abrir o Monitor Serial (Ferramentas > Monitor Serial) em 115200 baud
//   2. digitar uma tecla e apertar Enter (ou o botão Enviar)
//
// teclas:
//   1  liga a bomba 1      q  desliga a bomba 1
//   2  liga a bomba 2      w  desliga a bomba 2
//   3  liga a bomba 3      e  desliga a bomba 3
//   0  desliga todas as bombas
//   ?  mostra esta ajuda e o estado das bombas

#include <Arduino.h>
#include <esp_system.h>

// ===============================================================
// configurações
// ===============================================================

// pinos dos gates dos MOSFETs (conforme o esquemático da placa 2)
const int QUANTIDADE_DE_BOMBAS = 3;
const int PINOS_DAS_BOMBAS[QUANTIDADE_DE_BOMBAS] = {25, 26, 27};

// nível lógico que liga o MOSFET da bomba (IRL540N, canal N: nível alto liga)
const int NIVEL_LOGICO_BOMBA_LIGADA = HIGH;

// proteção: desliga a bomba sozinha depois desse tempo (0 = sem limite)
const unsigned long TEMPO_MAXIMO_LIGADA_EM_SEGUNDOS = 60;

// ===============================================================
// variáveis globais
// ===============================================================

bool bombaLigada[QUANTIDADE_DE_BOMBAS] = {false, false, false};
unsigned long instanteDeLigamentoEmMs[QUANTIDADE_DE_BOMBAS] = {0, 0, 0};

// ===============================================================
// funções
// ===============================================================

/*
  Escreve no monitor serial o tempo desde que o ESP32 ligou
  Se esse tempo voltar a zero ao ligar uma bomba, o ESP32 reiniciou
*/
void imprimirTempoLigado() {
  Serial.print(" [tempo ligado: ");
  Serial.print(millis() / 1000.0, 1);
  Serial.println(" s]");
}

/*
  Liga uma bomba
  Argumentos:
    indiceDaBomba: 0, 1 ou 2 (bomba 1, 2 ou 3)
*/
void ligarBomba(int indiceDaBomba) {
  bombaLigada[indiceDaBomba] = true;
  instanteDeLigamentoEmMs[indiceDaBomba] = millis();
  digitalWrite(PINOS_DAS_BOMBAS[indiceDaBomba], NIVEL_LOGICO_BOMBA_LIGADA);

  Serial.print("Bomba ");
  Serial.print(indiceDaBomba + 1);
  Serial.print(" LIGADA");
  imprimirTempoLigado();
}

/*
  Desliga uma bomba
  Argumentos:
    indiceDaBomba: 0, 1 ou 2 (bomba 1, 2 ou 3)
*/
void desligarBomba(int indiceDaBomba) {
  bombaLigada[indiceDaBomba] = false;
  digitalWrite(PINOS_DAS_BOMBAS[indiceDaBomba], !NIVEL_LOGICO_BOMBA_LIGADA);

  Serial.print("Bomba ");
  Serial.print(indiceDaBomba + 1);
  Serial.print(" desligada");
  imprimirTempoLigado();
}

/*
  Desliga todas as bombas
*/
void desligarTodasAsBombas() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    if (bombaLigada[indice]) {
      desligarBomba(indice);
    }
  }
  Serial.println("Todas as bombas desligadas");
}

/*
  Mostra as teclas e o estado atual de cada bomba
*/
void imprimirAjuda() {
  Serial.println();
  Serial.println("=== Teste das bombas pelo teclado ===");
  Serial.println("1 liga bomba 1   |  q desliga bomba 1");
  Serial.println("2 liga bomba 2   |  w desliga bomba 2");
  Serial.println("3 liga bomba 3   |  e desliga bomba 3");
  Serial.println("0 desliga todas  |  ? mostra esta ajuda");
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    Serial.print("Bomba ");
    Serial.print(indice + 1);
    Serial.print(" (GPIO");
    Serial.print(PINOS_DAS_BOMBAS[indice]);
    Serial.print("): ");
    Serial.println(bombaLigada[indice] ? "LIGADA" : "desligada");
  }
  Serial.println();
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
  Executa a ação da tecla recebida pelo monitor serial
  Argumentos:
    tecla: caractere digitado
*/
void tratarTecla(char tecla) {
  switch (tecla) {
    case '1':
      ligarBomba(0);
      break;
    case '2':
      ligarBomba(1);
      break;
    case '3':
      ligarBomba(2);
      break;
    case 'q':
    case 'Q':
      desligarBomba(0);
      break;
    case 'w':
    case 'W':
      desligarBomba(1);
      break;
    case 'e':
    case 'E':
      desligarBomba(2);
      break;
    case '0':
      desligarTodasAsBombas();
      break;
    case '?':
      imprimirAjuda();
      break;
    default:
      // ignora Enter, espaço e outras teclas
      break;
  }
}

/*
  Desliga as bombas que passaram do tempo máximo (proteção contra esquecimento)
*/
void verificarTempoMaximo() {
  if (TEMPO_MAXIMO_LIGADA_EM_SEGUNDOS == 0) {
    return;
  }

  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    bool passouDoLimite = (millis() - instanteDeLigamentoEmMs[indice]) >= TEMPO_MAXIMO_LIGADA_EM_SEGUNDOS * 1000UL;
    if (bombaLigada[indice] && passouDoLimite) {
      Serial.print("Tempo máximo atingido: ");
      desligarBomba(indice);
    }
  }
}

// ===============================================================
// programa principal
// ===============================================================

/*
  Configura os pinos (bombas desligadas) e mostra a ajuda
*/
void setup() {
  Serial.begin(115200);
  delay(500);

  // bombas começam desligadas antes de virarem saída
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    digitalWrite(PINOS_DAS_BOMBAS[indice], !NIVEL_LOGICO_BOMBA_LIGADA);
    pinMode(PINOS_DAS_BOMBAS[indice], OUTPUT);
  }

  // motivo do último reinício (mostra brownout, se tiver acontecido)
  Serial.print("Motivo do último reinício: ");
  Serial.println(descreverMotivoDoReinicio(esp_reset_reason()));

  imprimirAjuda();
}

/*
  Lê as teclas do monitor serial e aplica a proteção de tempo máximo
*/
void loop() {
  while (Serial.available() > 0) {
    char tecla = Serial.read();
    tratarTecla(tecla);
  }

  verificarTempoMaximo();
}
