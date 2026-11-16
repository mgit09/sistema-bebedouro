// teste dos leds de status do bebedouro
// placa: ESP32 DevKitC | monitor serial: 115200

// pinos dos leds
const int PINO_LED_VERMELHO_REPOR_RESERVATORIO_LIMPO = 18;
const int PINO_LED_VERMELHO_ESVAZIAR_RESERVATORIO_SUJO = 19;
const int PINO_LED_AMARELO_TROCA_EM_ANDAMENTO = 21;

// tempo que cada led fica aceso nos testes
const int TEMPO_ACESO_EM_MILISSEGUNDOS = 1000;

/**
 * Liga ou desliga um led.
 */
void definirEstadoDoLed(
  int pinoDoLed,
  bool ligado
) {
  // HIGH fornece 3,3V no pino e acende o led
  // LOW leva o pino a 0V e apaga o led
  digitalWrite(
    pinoDoLed,
    ligado ? HIGH : LOW
  );
}

/**
 * Apaga os três leds de uma vez.
 */
void apagarTodosOsLeds() {
  definirEstadoDoLed(
    PINO_LED_VERMELHO_REPOR_RESERVATORIO_LIMPO,
    false
  );
  definirEstadoDoLed(
    PINO_LED_VERMELHO_ESVAZIAR_RESERVATORIO_SUJO,
    false
  );
  definirEstadoDoLed(
    PINO_LED_AMARELO_TROCA_EM_ANDAMENTO,
    false
  );
}

/**
 * Acende um led por um tempo e depois apaga, avisando no monitor serial.
 */
void testarUmLed(
  int pinoDoLed,
  const char* nomeDoLed
) {
  Serial.print("Acendendo: ");
  Serial.println(nomeDoLed);

  definirEstadoDoLed(
    pinoDoLed,
    true
  );
  delay(TEMPO_ACESO_EM_MILISSEGUNDOS);

  definirEstadoDoLed(
    pinoDoLed,
    false
  );
  delay(300);
}

/**
 * Pisca todos os leds juntos, simulando o estado de falha.
 */
void piscarTodosOsLeds(
  int quantidadeDePiscadas,
  int duracaoEmMilissegundos
) {
  for (int contador = 0; contador < quantidadeDePiscadas; contador++) {
    // liga os três
    definirEstadoDoLed(
      PINO_LED_VERMELHO_REPOR_RESERVATORIO_LIMPO,
      true
    );
    definirEstadoDoLed(
      PINO_LED_VERMELHO_ESVAZIAR_RESERVATORIO_SUJO,
      true
    );
    definirEstadoDoLed(
      PINO_LED_AMARELO_TROCA_EM_ANDAMENTO,
      true
    );
    delay(duracaoEmMilissegundos);

    // desliga os três
    apagarTodosOsLeds();
    delay(duracaoEmMilissegundos);
  }
}

/**
 * Configura a serial e os pinos dos leds como saída.
 */
void setup() {
  Serial.begin(115200);
  delay(500);

  // pinMode define se o pino lê (INPUT) ou envia sinal (OUTPUT)
  pinMode(
    PINO_LED_VERMELHO_REPOR_RESERVATORIO_LIMPO,
    OUTPUT
  );
  pinMode(
    PINO_LED_VERMELHO_ESVAZIAR_RESERVATORIO_SUJO,
    OUTPUT
  );
  pinMode(
    PINO_LED_AMARELO_TROCA_EM_ANDAMENTO,
    OUTPUT
  );

  // garante que todos comecem apagados
  apagarTodosOsLeds();

  Serial.println("=== Teste dos leds de status ===");
}

/**
 * Repete o ciclo de teste: um led por vez e depois todos piscando.
 */
void loop() {
  testarUmLed(
    PINO_LED_VERMELHO_REPOR_RESERVATORIO_LIMPO,
    "led1 vermelho (repor reservatorio de agua limpa)"
  );
  testarUmLed(
    PINO_LED_VERMELHO_ESVAZIAR_RESERVATORIO_SUJO,
    "led2 vermelho (esvaziar reservatorio de agua suja)"
  );
  testarUmLed(
    PINO_LED_AMARELO_TROCA_EM_ANDAMENTO,
    "led3 amarelo (troca em andamento)"
  );

  Serial.println("Piscando todos (simula falha)");
  piscarTodosOsLeds(
    5,
    250
  );

  Serial.println("--- fim do ciclo ---");
  delay(1500);
}
