// teste da placa do bebedouro, controlado por comandos no monitor serial
// placa: ESP32 DevKitC | monitor serial: 115200 baud

#include "driver/gpio.h"

// pinos conforme o esquemático da placa
const int PINO_SENSOR_PH = 36;
const int PINO_SENSOR_TDS = 34;
const int PINO_SENSOR_TURBIDEZ = 35;
const int PINO_ULTRASSONICO_TRIGGER = 32;
const int PINO_ULTRASSONICO_ECHO = 33;
const int PINO_UART_TX = 17;
const int PINO_UART_RX = 16;

// bombas 1, 2 e 3 nos pinos 25, 26 e 27
const int QUANTIDADE_DE_BOMBAS = 3;
const int PINOS_DAS_BOMBAS[QUANTIDADE_DE_BOMBAS] = {
  25,
  26,
  27
};

// tempo máximo que uma bomba fica ligada por comando
const unsigned long TEMPO_MAXIMO_DA_BOMBA_LIGADA_EM_MILISSEGUNDOS = 5000;

/**
 * Dados de uma boia de nível: nome, pino e o significado de cada estado do contato.
 */
struct BoiaDeNivel {
  const char* nome;
  int pino;
  const char* significadoDoContatoFechado;
  const char* significadoDoContatoAberto;
};

// significados seguem a convenção da lista de componentes:
// boia de mínimo: sinal (contato fechado) = nível no mínimo ou abaixo
// boia de máximo: ausência de sinal (contato aberto) = nível no máximo
// ajustar se a montagem física das boias estiver invertida
const int QUANTIDADE_DE_BOIAS = 4;
const BoiaDeNivel BOIAS_DE_NIVEL[QUANTIDADE_DE_BOIAS] = {
  {"NIVEL_R1_ALTO", 18, "abaixo do maximo", "NIVEL MAXIMO ATINGIDO"},
  {"NIVEL_R1_BAIXO", 19, "NIVEL MINIMO (ou abaixo)", "acima do minimo"},
  {"NIVEL_R2", 23, "NIVEL MINIMO (ou abaixo)", "acima do minimo"},
  {"NIVEL_R3", 21, "abaixo do maximo", "NIVEL MAXIMO ATINGIDO"}
};

// votação de leituras para estabilizar o contato das boias
const int QUANTIDADE_DE_LEITURAS_POR_BOIA = 5;
const int INTERVALO_ENTRE_LEITURAS_DA_BOIA_EM_MILISSEGUNDOS = 5;

// leitura analógica: média de várias amostras
const int QUANTIDADE_DE_AMOSTRAS_ANALOGICAS = 30;
const int INTERVALO_ENTRE_AMOSTRAS_ANALOGICAS_EM_MILISSEGUNDOS = 10;

// divisor de tensão da turbidez e do pH: 10 kΩ em série e 18 kΩ para o GND
const float FATOR_DO_DIVISOR_DE_TENSAO = (10000.0 + 18000.0) / 18000.0;

// ultrassônico
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 30000;
const float VELOCIDADE_DO_SOM_EM_CENTIMETROS_POR_MICROSSEGUNDO = 0.0343;
const int QUANTIDADE_DE_MEDICOES_PARA_MEDIA = 5;
const int INTERVALO_ENTRE_MEDICOES_EM_MILISSEGUNDOS = 60;
const float DISTANCIA_DE_PRESENCA_EM_CENTIMETROS = 30.0;

// tds: sem sensor de temperatura, assume-se água a 25 graus
const float TEMPERATURA_DA_AGUA_EM_CELSIUS = 25.0;
const float LIMITE_DE_TDS_AGUA_RUIM_EM_PPM = 120.0;

// ph: valores típicos do módulo, trocar após calibrar com solução tampão
const float TENSAO_NO_PH_7_EM_VOLTS = 2.5;
const float VARIACAO_DE_TENSAO_POR_UNIDADE_DE_PH_EM_VOLTS = 0.18;

// turbidez: acima disso a leitura fica perto do limite do ADC
const float TENSAO_DE_ALERTA_NO_PINO_EM_VOLTS = 3.0;

// uart com o msp430 (combinar a velocidade com o outro lado)
const unsigned long VELOCIDADE_DA_UART_EM_BAUD = 9600;

// intervalo entre as impressões de cada modo
const unsigned long INTERVALO_ENTRE_IMPRESSOES_EM_MILISSEGUNDOS = 1000;

// estado do programa
char modoAtual = 'x';
unsigned long instanteDaUltimaImpressao = 0;
int numeroDaBombaLigada = 0;
unsigned long instanteDeLigamentoDaBomba = 0;
unsigned long contadorDeMensagensDaUart = 0;

/**
 * Desliga as três bombas.
 */
void desligarTodasAsBombas() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    digitalWrite(
      PINOS_DAS_BOMBAS[indice],
      LOW
    );
  }

  numeroDaBombaLigada = 0;
}

/**
 * Liga uma única bomba (1 a 3), desligando antes qualquer outra.
 */
void ligarBomba(
  int numeroDaBomba
) {
  // intertravamento: só uma bomba por vez
  desligarTodasAsBombas();
  delay(100);

  digitalWrite(
    PINOS_DAS_BOMBAS[numeroDaBomba - 1],
    HIGH
  );

  numeroDaBombaLigada = numeroDaBomba;
  instanteDeLigamentoDaBomba = millis();

  Serial.print("Bomba ");
  Serial.print(numeroDaBomba);
  Serial.println(" LIGADA (desliga sozinha ou com o comando x)");
}

/**
 * Desliga a bomba ligada quando o tempo máximo termina.
 */
void verificarTempoLimiteDaBomba() {
  if (numeroDaBombaLigada == 0) {
    return;
  }

  unsigned long tempoLigadaEmMilissegundos = millis() - instanteDeLigamentoDaBomba;

  if (tempoLigadaEmMilissegundos >= TEMPO_MAXIMO_DA_BOMBA_LIGADA_EM_MILISSEGUNDOS) {
    desligarTodasAsBombas();
    Serial.println("Bomba desligada por tempo limite");
  }
}

/**
 * Lê uma boia várias vezes e devolve o nível lógico mais comum.
 */
int lerNivelLogicoEstavel(
  int pinoDaBoia
) {
  int quantidadeDeLeiturasEmNivelBaixo = 0;

  for (int leitura = 0; leitura < QUANTIDADE_DE_LEITURAS_POR_BOIA; leitura++) {
    // LOW significa contato fechado (ligado ao GND)
    if (digitalRead(pinoDaBoia) == LOW) {
      quantidadeDeLeiturasEmNivelBaixo++;
    }
    delay(INTERVALO_ENTRE_LEITURAS_DA_BOIA_EM_MILISSEGUNDOS);
  }

  if (quantidadeDeLeiturasEmNivelBaixo > QUANTIDADE_DE_LEITURAS_POR_BOIA / 2) {
    return LOW;
  }

  return HIGH;
}

/**
 * Imprime o estado do contato e o significado de cada boia.
 */
void imprimirEstadoDasBoias() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    const BoiaDeNivel& boia = BOIAS_DE_NIVEL[indice];
    bool contatoFechado = (lerNivelLogicoEstavel(boia.pino) == LOW);

    Serial.print(boia.nome);
    Serial.print(" (GPIO ");
    Serial.print(boia.pino);
    Serial.print("): ");
    Serial.print(contatoFechado ? "FECHADO -> " : "ABERTO -> ");
    Serial.println(
      contatoFechado
        ? boia.significadoDoContatoFechado
        : boia.significadoDoContatoAberto
    );
  }

  Serial.println("---");
}

/**
 * Lê a tensão no pino analógico várias vezes e devolve a média em volts.
 */
float lerTensaoMediaNoPinoEmVolts(
  int pinoAnalogico
) {
  float somaDasTensoesEmMilivolts = 0.0;

  for (int amostra = 0; amostra < QUANTIDADE_DE_AMOSTRAS_ANALOGICAS; amostra++) {
    // analogReadMilliVolts usa a calibração de fábrica do ADC
    somaDasTensoesEmMilivolts += analogReadMilliVolts(pinoAnalogico);
    delay(INTERVALO_ENTRE_AMOSTRAS_ANALOGICAS_EM_MILISSEGUNDOS);
  }

  return somaDasTensoesEmMilivolts / QUANTIDADE_DE_AMOSTRAS_ANALOGICAS / 1000.0;
}

/**
 * Faz uma única medição do ultrassônico e devolve a distância em cm (-1 se não houve eco).
 */
float medirDistanciaUmaVezEmCentimetros() {
  digitalWrite(
    PINO_ULTRASSONICO_TRIGGER,
    LOW
  );
  delayMicroseconds(2);

  // pulso de 10 microssegundos dispara o som
  digitalWrite(
    PINO_ULTRASSONICO_TRIGGER,
    HIGH
  );
  delayMicroseconds(10);
  digitalWrite(
    PINO_ULTRASSONICO_TRIGGER,
    LOW
  );

  unsigned long duracaoDoEcoEmMicrossegundos = pulseIn(
    PINO_ULTRASSONICO_ECHO,
    HIGH,
    TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS
  );

  // zero significa que acabou o tempo sem eco
  if (duracaoDoEcoEmMicrossegundos == 0) {
    return -1.0;
  }

  // o som vai e volta, então divide-se por 2
  return duracaoDoEcoEmMicrossegundos
    * VELOCIDADE_DO_SOM_EM_CENTIMETROS_POR_MICROSSEGUNDO
    / 2.0;
}

/**
 * Faz várias medições e devolve a média das válidas (-1 se todas falharam).
 */
float medirDistanciaMediaEmCentimetros() {
  float somaDasDistancias = 0.0;
  int quantidadeDeMedicoesValidas = 0;

  for (int medicao = 0; medicao < QUANTIDADE_DE_MEDICOES_PARA_MEDIA; medicao++) {
    float distanciaEmCentimetros = medirDistanciaUmaVezEmCentimetros();

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
 * Imprime a distância medida e se há presença detectada.
 */
void imprimirUltrassonico() {
  float distanciaMediaEmCentimetros = medirDistanciaMediaEmCentimetros();

  if (distanciaMediaEmCentimetros < 0) {
    Serial.println("Ultrassonico: sem eco (nada a frente ou fora do alcance)");
    return;
  }

  Serial.print("Ultrassonico: ");
  Serial.print(
    distanciaMediaEmCentimetros,
    1
  );
  Serial.print(" cm | ");
  Serial.println(
    distanciaMediaEmCentimetros < DISTANCIA_DE_PRESENCA_EM_CENTIMETROS
      ? "PRESENCA DETECTADA"
      : "ninguem por perto"
  );
}

/**
 * Converte a tensão do sensor de tds em ppm, com compensação de temperatura.
 */
float converterTensaoTdsParaPpm(
  float tensaoEmVolts
) {
  // a condutividade da água muda cerca de 2% por grau acima de 25
  float coeficienteDeCompensacao = 1.0 + 0.02 * (TEMPERATURA_DA_AGUA_EM_CELSIUS - 25.0);
  float tensaoCompensadaEmVolts = tensaoEmVolts / coeficienteDeCompensacao;

  // polinômio de terceiro grau fornecido pelo fabricante do módulo
  return (
    133.42 * tensaoCompensadaEmVolts * tensaoCompensadaEmVolts * tensaoCompensadaEmVolts
    - 255.86 * tensaoCompensadaEmVolts * tensaoCompensadaEmVolts
    + 857.39 * tensaoCompensadaEmVolts
  ) * 0.5;
}

/**
 * Imprime a tensão e o valor estimado de tds.
 */
void imprimirTds() {
  float tensaoEmVolts = lerTensaoMediaNoPinoEmVolts(PINO_SENSOR_TDS);
  float valorEmPpm = converterTensaoTdsParaPpm(tensaoEmVolts);

  Serial.print("TDS: ");
  Serial.print(
    tensaoEmVolts,
    3
  );
  Serial.print(" V | ");
  Serial.print(
    valorEmPpm,
    0
  );
  Serial.print(" ppm | ");
  Serial.println(
    valorEmPpm > LIMITE_DE_TDS_AGUA_RUIM_EM_PPM
      ? "AGUA RUIM (acima do limite)"
      : "dentro do limite"
  );
}

/**
 * Imprime a tensão da turbidez no pino e a estimada na saída do sensor (sem converter para NTU).
 */
void imprimirTurbidez() {
  float tensaoNoPinoEmVolts = lerTensaoMediaNoPinoEmVolts(PINO_SENSOR_TURBIDEZ);

  // desfaz o divisor para obter a tensão original do sensor
  float tensaoNaSaidaDoSensorEmVolts = tensaoNoPinoEmVolts * FATOR_DO_DIVISOR_DE_TENSAO;

  Serial.print("Turbidez: pino ");
  Serial.print(
    tensaoNoPinoEmVolts,
    3
  );
  Serial.print(" V | saida do sensor ~");
  Serial.print(
    tensaoNaSaidaDoSensorEmVolts,
    3
  );
  Serial.print(" V");

  if (tensaoNoPinoEmVolts > TENSAO_DE_ALERTA_NO_PINO_EM_VOLTS) {
    Serial.print(" | ATENCAO: perto do limite do ADC");
  }

  Serial.println();
}

/**
 * Imprime a tensão do pH e uma estimativa provisória (calibrar com solução tampão).
 */
void imprimirPh() {
  float tensaoNoPinoEmVolts = lerTensaoMediaNoPinoEmVolts(PINO_SENSOR_PH);
  float tensaoNaSaidaDoModuloEmVolts = tensaoNoPinoEmVolts * FATOR_DO_DIVISOR_DE_TENSAO;

  // quanto maior a tensão, mais ácida a água
  float phEstimado = 7.0
    + (TENSAO_NO_PH_7_EM_VOLTS - tensaoNaSaidaDoModuloEmVolts)
    / VARIACAO_DE_TENSAO_POR_UNIDADE_DE_PH_EM_VOLTS;

  Serial.print("pH: pino ");
  Serial.print(
    tensaoNoPinoEmVolts,
    3
  );
  Serial.print(" V | saida do modulo ~");
  Serial.print(
    tensaoNaSaidaDoModuloEmVolts,
    3
  );
  Serial.print(" V | pH provisorio ~");
  Serial.println(
    phEstimado,
    1
  );
}

/**
 * Envia uma mensagem de teste pela UART.
 */
void enviarMensagemDeTesteDaUart() {
  contadorDeMensagensDaUart++;

  Serial2.print("TESTE_UART ");
  Serial2.println(contadorDeMensagensDaUart);

  Serial.print("Enviado pela UART: TESTE_UART ");
  Serial.println(contadorDeMensagensDaUart);
}

/**
 * Mostra no monitor serial tudo o que chegou pela UART.
 */
void mostrarBytesRecebidosPelaUart() {
  while (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}

/**
 * Mostra a lista de comandos.
 */
void imprimirAjuda() {
  Serial.println("=== Comandos ===");
  Serial.println("b = boias de nivel");
  Serial.println("u = ultrassonico");
  Serial.println("t = tds");
  Serial.println("r = turbidez");
  Serial.println("p = ph");
  Serial.println("m = uart (loopback ou msp430)");
  Serial.println("1, 2, 3 = liga a bomba 1, 2 ou 3 (uma por vez, 5 s no maximo)");
  Serial.println("x = desliga as bombas e para o modo atual");
  Serial.println("h = esta ajuda");
}

/**
 * Executa um comando recebido pelo monitor serial.
 */
void processarComando(
  char comando
) {
  switch (comando) {
    case 'h':
      imprimirAjuda();
      break;

    case 'b':
    case 'u':
    case 't':
    case 'r':
    case 'p':
    case 'm':
      modoAtual = comando;
      Serial.print("Modo: ");
      Serial.println(comando);
      break;

    case '1':
    case '2':
    case '3':
      ligarBomba(comando - '0');
      break;

    case 'x':
      desligarTodasAsBombas();
      modoAtual = 'x';
      Serial.println("Bombas desligadas e modo parado");
      break;

    default:
      Serial.println("Comando desconhecido, envie h para ver a ajuda");
      break;
  }
}

/**
 * Lê os comandos digitados no monitor serial, ignorando quebras de linha.
 */
void lerComandosDoMonitorSerial() {
  while (Serial.available()) {
    char caractere = Serial.read();

    if (caractere == '\n' || caractere == '\r' || caractere == ' ') {
      continue;
    }

    processarComando(caractere);
  }
}

/**
 * Configura os pinos, a serial do monitor e a UART do MSP430.
 */
void setup() {
  // bombas primeiro: garante saídas em nível baixo logo na partida
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    pinMode(
      PINOS_DAS_BOMBAS[indice],
      OUTPUT
    );
  }
  desligarTodasAsBombas();

  Serial.begin(115200);
  delay(500);

  // as boias não têm pull-up externo, então usa-se o pull-up interno
  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    pinMode(
      BOIAS_DE_NIVEL[indice].pino,
      INPUT_PULLUP
    );
  }

  pinMode(
    PINO_ULTRASSONICO_TRIGGER,
    OUTPUT
  );
  digitalWrite(
    PINO_ULTRASSONICO_TRIGGER,
    LOW
  );
  pinMode(
    PINO_ULTRASSONICO_ECHO,
    INPUT
  );

  // atenuação de 11 dB permite ler até cerca de 3,1V
  analogSetAttenuation(ADC_11db);
  pinMode(
    PINO_SENSOR_PH,
    INPUT
  );
  pinMode(
    PINO_SENSOR_TDS,
    INPUT
  );
  pinMode(
    PINO_SENSOR_TURBIDEZ,
    INPUT
  );

  Serial2.begin(
    VELOCIDADE_DA_UART_EM_BAUD,
    SERIAL_8N1,
    PINO_UART_RX,
    PINO_UART_TX
  );

  // pull-up no RX: linha parada em nível alto evita lixo com o msp430 desligado
  gpio_set_pull_mode(
    static_cast<gpio_num_t>(PINO_UART_RX),
    GPIO_PULLUP_ONLY
  );

  Serial.println("=== Teste da placa do bebedouro ===");
  imprimirAjuda();
}

/**
 * Trata os comandos, o tempo das bombas e imprime o modo atual a cada intervalo.
 */
void loop() {
  lerComandosDoMonitorSerial();
  verificarTempoLimiteDaBomba();

  // a UART é lida o tempo todo para não perder bytes
  if (modoAtual == 'm') {
    mostrarBytesRecebidosPelaUart();
  }

  unsigned long agoraEmMilissegundos = millis();

  if (agoraEmMilissegundos - instanteDaUltimaImpressao < INTERVALO_ENTRE_IMPRESSOES_EM_MILISSEGUNDOS) {
    return;
  }

  instanteDaUltimaImpressao = agoraEmMilissegundos;

  switch (modoAtual) {
    case 'b':
      imprimirEstadoDasBoias();
      break;
    case 'u':
      imprimirUltrassonico();
      break;
    case 't':
      imprimirTds();
      break;
    case 'r':
      imprimirTurbidez();
      break;
    case 'p':
      imprimirPh();
      break;
    case 'm':
      enviarMensagemDeTesteDaUart();
      break;
    default:
      break;
  }
}
