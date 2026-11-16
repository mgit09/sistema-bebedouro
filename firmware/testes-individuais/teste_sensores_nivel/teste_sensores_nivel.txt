// teste das boias de nível do bebedouro
// placa: ESP32 DevKitC | monitor serial: 115200

// quantidade de boias testadas
const int QUANTIDADE_DE_BOIAS = 4;

// pinos de cada boia, na mesma ordem dos nomes
const int PINOS_DAS_BOIAS[QUANTIDADE_DE_BOIAS] = {
  13,
  14,
  16,
  17
};

// nomes usados na impressão do monitor serial
const char* NOMES_DAS_BOIAS[QUANTIDADE_DE_BOIAS] = {
  "snivel1",
  "snivel2",
  "snivel3min",
  "snivel3max"
};

// ajustar conforme o modelo da boia
// true: contato fechado (LOW) significa água presente na altura da boia
// false: contato fechado (LOW) significa água ausente na altura da boia
const bool CONTATO_FECHADO_INDICA_AGUA_PRESENTE = true;

// leituras seguidas para eliminar ruído do contato
const int QUANTIDADE_DE_LEITURAS_POR_BOIA = 5;
const int INTERVALO_ENTRE_LEITURAS_EM_MILISSEGUNDOS = 5;

// intervalo entre as impressões no monitor serial
const int INTERVALO_DE_IMPRESSAO_EM_MILISSEGUNDOS = 500;

/**
 * Lê uma boia várias vezes e devolve o nível lógico mais comum (votação).
 */
int lerNivelLogicoEstavel(
  int pinoDaBoia
) {
  int quantidadeDeLeiturasEmNivelBaixo = 0;

  for (int leitura = 0; leitura < QUANTIDADE_DE_LEITURAS_POR_BOIA; leitura++) {
    // conta quantas vezes o pino leu LOW (contato fechado)
    if (digitalRead(pinoDaBoia) == LOW) {
      quantidadeDeLeiturasEmNivelBaixo++;
    }
    delay(INTERVALO_ENTRE_LEITURAS_EM_MILISSEGUNDOS);
  }

  // se a maioria das leituras foi LOW, o resultado é LOW
  if (quantidadeDeLeiturasEmNivelBaixo > QUANTIDADE_DE_LEITURAS_POR_BOIA / 2) {
    return LOW;
  }

  return HIGH;
}

/**
 * Indica se há água na altura da boia, conforme a configuração do modelo.
 */
bool existeAguaNaAlturaDaBoia(
  int nivelLogicoLido
) {
  bool contatoFechado = (nivelLogicoLido == LOW);

  // se o modelo for invertido, a lógica também inverte
  return (contatoFechado == CONTATO_FECHADO_INDICA_AGUA_PRESENTE);
}

/**
 * Configura a serial e os pinos das boias como entrada com pull-up interno.
 */
void setup() {
  Serial.begin(115200);
  delay(500);

  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    // INPUT_PULLUP liga um resistor interno entre o pino e 3,3V
    pinMode(
      PINOS_DAS_BOIAS[indice],
      INPUT_PULLUP
    );
  }

  Serial.println("=== Teste das boias de nivel ===");
  Serial.println("Mova cada boia para cima e para baixo e observe");
}

/**
 * Lê todas as boias e imprime uma linha com o estado de cada uma.
 */
void loop() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    int nivelLogico = lerNivelLogicoEstavel(PINOS_DAS_BOIAS[indice]);
    bool existeAgua = existeAguaNaAlturaDaBoia(nivelLogico);

    Serial.print(NOMES_DAS_BOIAS[indice]);
    Serial.print(": ");
    Serial.print(nivelLogico == LOW ? "FECHADO(LOW) " : "ABERTO(HIGH) ");
    Serial.print(existeAgua ? "-> AGUA" : "-> SEM AGUA");

    // separador entre as boias, exceto na última
    if (indice < QUANTIDADE_DE_BOIAS - 1) {
      Serial.print(" | ");
    }
  }

  Serial.println();
  delay(INTERVALO_DE_IMPRESSAO_EM_MILISSEGUNDOS);
}
