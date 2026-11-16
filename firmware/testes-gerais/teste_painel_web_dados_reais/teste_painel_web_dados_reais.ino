// sistema do bebedouro automático para gatos
// lê os sensores reais, controla as bombas, decide a troca de água
// envia o estado para a placa 3 (LEDs, buzzer e display) pela UART e para o painel web
// o arquivo painel_web.h precisa estar na mesma pasta deste sketch

#include <Arduino.h>
#include "painel_web.h"

// ===============================================================
// pinos (conforme o esquemático da placa 2)
// ===============================================================

// sensores analógicos (ADC1, funcionam com o Wi-Fi ligado)
const int PINO_SENSOR_PH = 36;
const int PINO_SENSOR_TDS = 34;

// sensor ultrassônico de presença
const int PINO_ULTRASSONICO_GATILHO = 32;
const int PINO_ULTRASSONICO_ECO = 33;

// gates dos MOSFETs das bombas
const int PINO_MOSFET_BOMBA_REPOSICAO = 25;
const int PINO_MOSFET_BOMBA_DESCARTE = 26;
const int PINO_MOSFET_BOMBA_CIRCULACAO = 27;

// boias de nível (códigos da planilha: snivel1, snivel2, snivel3min, snivel3max)
// CONFIRMAR qual GPIO é cada boia: o esquemático usa NIVEL_R1_BAIXO (19), NIVEL_R1_ALTO (18),
// NIVEL_R2 (23) e NIVEL_R3 (21), nomes que não batem com os códigos da planilha
// palpite atual: o par BAIXO/ALTO é o bebedouro (único com mínimo e máximo),
// NIVEL_R2 é o r2 (água suja) e sobra o GPIO 21 para o r1 (água limpa)
// o monitor serial imprime o nível lógico bruto dos 4 pinos para conferir mexendo em cada boia
const int PINO_SNIVEL3MIN_BEBEDOURO_MINIMO = 19;
const int PINO_SNIVEL3MAX_BEBEDOURO_MAXIMO = 18;
const int PINO_SNIVEL2_RESERVATORIO_SUJO_MAXIMO = 23;
const int PINO_SNIVEL1_RESERVATORIO_LIMPO_MINIMO = 21;

// comunicação serial (UART) com a placa 3
const int PINO_UART_TRANSMISSAO = 17;
const int PINO_UART_RECEPCAO = 16;

// ===============================================================
// parâmetros ajustáveis
// ===============================================================

// nível lógico lido quando a boia envia sinal (contato fechado)
// as boias de mínimo ficam viradas para cima e fecham quando a água chega ao mínimo
// as boias de máximo ficam viradas para baixo e fecham quando a água chega ao máximo
// então, em todas, "com sinal" = condição ocorrendo (mínimo atingido ou máximo atingido)
// se no teste o sinal aparecer invertido, trocar para LOW
const int NIVEL_LOGICO_COM_SINAL = HIGH;

// nível lógico que liga o MOSFET da bomba
const int NIVEL_LOGICO_BOMBA_LIGADA = HIGH;

// qualidade da água
const float LIMITE_TDS_EM_PPM = 120.0;
const float PH_MINIMO_ACEITAVEL = 6.0;
const float PH_MAXIMO_ACEITAVEL = 8.5;

// calibração do pH com 2 pontos (pH 7,20 a 2,622 V e pH 6,20 a 2,688 V)
const float INCLINACAO_DA_CALIBRACAO_DO_PH = -15.04;
const float DESLOCAMENTO_DA_CALIBRACAO_DO_PH = 46.63;

// temperatura usada na compensação do TDS (não há sensor de temperatura)
const float TEMPERATURA_DA_AGUA_EM_CELSIUS = 25.0;

// presença do gato
const float DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS = 30.0;
const float DISTANCIA_SEM_ECO_EM_CENTIMETROS = 400.0;
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 25000;
const unsigned long INTERVALO_ENTRE_LEITURAS_DE_PRESENCA_MS = 100;

// tempos da lógica
const unsigned long TEMPO_DE_PRESENCA_PARA_CIRCULAR_MS = 5000;
const unsigned long TEMPO_DE_AUSENCIA_PARA_PARAR_CIRCULACAO_MS = 5000;
const unsigned long TEMPO_DE_AUSENCIA_PARA_DESCARTAR_MS = 6000;
const unsigned long TEMPO_BOMBAS_DESLIGADAS_PARA_MEDIR_MS = 10000;
const unsigned long TEMPO_DE_CONFIRMACAO_DE_QUALIDADE_RUIM_MS = 15000;
const unsigned long TEMPO_DE_ESTABILIZACAO_APOS_TROCA_MS = 60000;
const unsigned long INTERVALO_ENTRE_MEDICOES_DE_QUALIDADE_MS = 2000;

// tempo máximo contínuo de cada bomba ligada (C9, ajustar nos testes)
const unsigned long TEMPO_MAXIMO_BOMBA_REPOSICAO_MS = 120000;
const unsigned long TEMPO_MAXIMO_BOMBA_DESCARTE_MS = 120000;
const unsigned long TEMPO_MAXIMO_BOMBA_CIRCULACAO_MS = 300000;

// amostragem dos sensores analógicos
const int QUANTIDADE_DE_AMOSTRAS = 20;
const int AMOSTRAS_DESCARTADAS_EM_CADA_EXTREMO = 4;
const unsigned long PAUSA_ENTRE_AMOSTRAS_MS = 10;

// buzzer: tempo entre repetições do mesmo alerta (0 = tocar uma única vez)
const unsigned long INTERVALO_DE_REPETICAO_DO_ALERTA_MS = 60000;
const unsigned long INTERVALO_MINIMO_ENTRE_SINAIS_MS = 3000;

// placa 3
const unsigned long VELOCIDADE_DA_UART_PLACA_3 = 9600;
const unsigned long INTERVALO_DE_ENVIO_PARA_PLACA_3_MS = 500;
const unsigned long PERIODO_DO_PISCA_MS = 500;

// mensagens de acompanhamento no monitor serial
const unsigned long INTERVALO_DA_DEPURACAO_MS = 2000;

// ===============================================================
// tipos e variáveis globais
// ===============================================================

enum EstadoDaTroca {
  ESTADO_NORMAL = 0,
  ESTADO_AGUARDANDO = 1,
  ESTADO_DESCARTANDO = 2,
  ESTADO_REABASTECENDO = 3
};

struct Bomba {
  const char* nome;
  int pinoDoMosfet;
  unsigned long tempoMaximoLigadaMs;
  bool ligada;
  unsigned long instanteDeLigamentoMs;
};

Bomba bombaDeReposicao = {"bomba1 (reposição)", PINO_MOSFET_BOMBA_REPOSICAO, TEMPO_MAXIMO_BOMBA_REPOSICAO_MS, false, 0};
Bomba bombaDeDescarte = {"bomba2 (descarte)", PINO_MOSFET_BOMBA_DESCARTE, TEMPO_MAXIMO_BOMBA_DESCARTE_MS, false, 0};
Bomba bombaDeCirculacao = {"bomba3 (circulação)", PINO_MOSFET_BOMBA_CIRCULACAO, TEMPO_MAXIMO_BOMBA_CIRCULACAO_MS, false, 0};

// estado geral
EstadoDaTroca estadoDaTroca = ESTADO_NORMAL;
bool falhaDetectada = false;

// níveis (verdadeiro quando a condição descrita está ocorrendo)
bool reservatorioLimpoNoMinimo = false;
bool reservatorioSujoNoMaximo = false;
bool bebedouroNoMinimo = false;
bool bebedouroNoMaximo = false;

// presença do gato
float distanciaDoGatoEmCentimetros = DISTANCIA_SEM_ECO_EM_CENTIMETROS;
bool gatoPresente = false;
unsigned long instanteDaUltimaMudancaDePresencaMs = 0;
unsigned long instanteDaUltimaLeituraDePresencaMs = 0;

// qualidade da água
float tdsEmPpm = 0;
bool tdsValido = false;
float ph = 0;
bool phValido = false;
bool proximaMedicaoEhTds = true;
unsigned long instanteDaUltimaMedicaoMs = 0;
unsigned long instanteDoInicioDaQualidadeRuimMs = 0;
bool qualidadeRuimEmAnalise = false;
bool bloqueioPosTrocaAtivo = false;
unsigned long instanteDoFimDaTrocaMs = 0;
unsigned long instanteDaUltimaBombaLigadaMs = 0;
bool leituraDeQualidadePermitida = false;

// LEDs da placa 3
// posição 0 = vermelho, posições 1 a 5 = verdes, posições 6 a 10 = amarelos
const int INDICE_DO_LED_VERMELHO = 0;
const int INDICE_DO_PRIMEIRO_LED_VERDE = 1;
const int INDICE_DO_PRIMEIRO_LED_AMARELO = 6;
bool ledsAcesos[QUANTIDADE_DE_LEDS] = {false};

// buzzer
// padrões: 1 = um beep longo (falha), 2 = dois beeps curtos (r1 baixo), 3 = três beeps curtos (r2 cheio)
struct AlertaDoBuzzer {
  int padrao;
  bool jaSinalizado;
  unsigned long instanteDoUltimoSinalMs;
};

const int QUANTIDADE_DE_ALERTAS = 3;
const int ALERTA_DE_FALHA = 0;
const int ALERTA_DE_RESERVATORIO_SUJO_CHEIO = 1;
const int ALERTA_DE_RESERVATORIO_LIMPO_BAIXO = 2;

// a ordem do vetor é a prioridade (falha primeiro)
AlertaDoBuzzer alertasDoBuzzer[QUANTIDADE_DE_ALERTAS] = {
  {1, false, 0},
  {3, false, 0},
  {2, false, 0}
};

int ultimoPadraoDoBuzzer = 0;
unsigned long contadorDeSinaisDoBuzzer = 0;
unsigned long instanteDoUltimoSinalEmitidoMs = 0;

// controle de envio e depuração
unsigned long instanteDoUltimoEnvioParaPlaca3Ms = 0;
unsigned long instanteDaUltimaDepuracaoMs = 0;

// ===============================================================
// utilidades
// ===============================================================

/*
  Retorna o nome do estado da troca de água em texto
  Argumentos:
    estado: estado a ser convertido
*/
const char* nomeDoEstado(EstadoDaTroca estado) {
  switch (estado) {
    case ESTADO_AGUARDANDO:
      return "AGUARDANDO";
    case ESTADO_DESCARTANDO:
      return "DESCARTANDO";
    case ESTADO_REABASTECENDO:
      return "REABASTECENDO";
    default:
      return "NORMAL";
  }
}

/*
  Informa se a boia está enviando sinal (contato fechado)
  Argumentos:
    pino: pino ligado à boia
*/
bool boiaEstaComSinal(int pino) {
  return digitalRead(pino) == NIVEL_LOGICO_COM_SINAL;
}

// ===============================================================
// leitura dos sensores
// ===============================================================

/*
  Lê as quatro boias e atualiza as variáveis de nível
  Em todas as boias, sinal presente = a condição descrita está ocorrendo
  (água no mínimo ou abaixo nas boias de mínimo, água no máximo ou acima nas de máximo)
*/
void lerNiveis() {
  reservatorioLimpoNoMinimo = boiaEstaComSinal(PINO_SNIVEL1_RESERVATORIO_LIMPO_MINIMO);
  reservatorioSujoNoMaximo = boiaEstaComSinal(PINO_SNIVEL2_RESERVATORIO_SUJO_MAXIMO);
  bebedouroNoMinimo = boiaEstaComSinal(PINO_SNIVEL3MIN_BEBEDOURO_MINIMO);
  bebedouroNoMaximo = boiaEstaComSinal(PINO_SNIVEL3MAX_BEBEDOURO_MAXIMO);
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
  Atualiza a presença do gato a cada intervalo fixo e guarda quando ela mudou
*/
void atualizarPresencaDoGato() {
  unsigned long agoraMs = millis();

  if (agoraMs - instanteDaUltimaLeituraDePresencaMs < INTERVALO_ENTRE_LEITURAS_DE_PRESENCA_MS) {
    return;
  }
  instanteDaUltimaLeituraDePresencaMs = agoraMs;

  distanciaDoGatoEmCentimetros = medirDistanciaEmCentimetros();
  bool presencaAgora = distanciaDoGatoEmCentimetros < DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS;

  // guarda o instante em que a presença mudou, para medir "por pelo menos X segundos"
  if (presencaAgora != gatoPresente) {
    gatoPresente = presencaAgora;
    instanteDaUltimaMudancaDePresencaMs = agoraMs;
  }
}

/*
  Informa se o gato está presente continuamente há pelo menos o tempo informado
  Argumentos:
    tempoMinimoMs: tempo mínimo de presença contínua
*/
bool presencaPorPeloMenos(unsigned long tempoMinimoMs) {
  return gatoPresente && (millis() - instanteDaUltimaMudancaDePresencaMs >= tempoMinimoMs);
}

/*
  Informa se o gato está ausente continuamente há pelo menos o tempo informado
  Argumentos:
    tempoMinimoMs: tempo mínimo de ausência contínua
*/
bool ausenciaPorPeloMenos(unsigned long tempoMinimoMs) {
  return !gatoPresente && (millis() - instanteDaUltimaMudancaDePresencaMs >= tempoMinimoMs);
}

/*
  Lê um pino analógico várias vezes e devolve a tensão em volts
  Ordena as amostras, descarta as extremas e faz a média das centrais
  Argumentos:
    pino: pino analógico (ADC1) a ser lido
*/
float lerTensaoFiltradaEmVolts(int pino) {
  float amostras[QUANTIDADE_DE_AMOSTRAS];

  // coleta as amostras (analogReadMilliVolts já compensa a curva de fábrica do ESP32)
  for (int indice = 0; indice < QUANTIDADE_DE_AMOSTRAS; indice++) {
    amostras[indice] = analogReadMilliVolts(pino) / 1000.0;
    delay(PAUSA_ENTRE_AMOSTRAS_MS);
  }

  // ordena em ordem crescente para eliminar picos de ruído
  for (int primeiro = 0; primeiro < QUANTIDADE_DE_AMOSTRAS - 1; primeiro++) {
    for (int segundo = primeiro + 1; segundo < QUANTIDADE_DE_AMOSTRAS; segundo++) {
      if (amostras[primeiro] > amostras[segundo]) {
        float auxiliar = amostras[primeiro];
        amostras[primeiro] = amostras[segundo];
        amostras[segundo] = auxiliar;
      }
    }
  }

  // média das amostras centrais
  float soma = 0;
  int quantidadeUsada = QUANTIDADE_DE_AMOSTRAS - 2 * AMOSTRAS_DESCARTADAS_EM_CADA_EXTREMO;
  for (int indice = AMOSTRAS_DESCARTADAS_EM_CADA_EXTREMO; indice < QUANTIDADE_DE_AMOSTRAS - AMOSTRAS_DESCARTADAS_EM_CADA_EXTREMO; indice++) {
    soma += amostras[indice];
  }

  return soma / quantidadeUsada;
}

/*
  Converte a tensão do sensor de pH em valor de pH usando a calibração de 2 pontos
  Argumentos:
    tensaoEmVolts: tensão filtrada lida no sensor
*/
float calcularPhPelaTensao(float tensaoEmVolts) {
  return INCLINACAO_DA_CALIBRACAO_DO_PH * tensaoEmVolts + DESLOCAMENTO_DA_CALIBRACAO_DO_PH;
}

/*
  Converte a tensão do sensor TDS em ppm com a fórmula padrão do módulo TDS Meter V1.0
  Argumentos:
    tensaoEmVolts: tensão filtrada lida no sensor
*/
float calcularTdsPelaTensao(float tensaoEmVolts) {
  // compensação pela temperatura da água
  float coeficienteDeCompensacao = 1.0 + 0.02 * (TEMPERATURA_DA_AGUA_EM_CELSIUS - 25.0);
  float tensaoCompensada = tensaoEmVolts / coeficienteDeCompensacao;

  // polinômio de condutividade para ppm
  float tensaoAoQuadrado = tensaoCompensada * tensaoCompensada;
  float tensaoAoCubo = tensaoAoQuadrado * tensaoCompensada;
  return (133.42 * tensaoAoCubo - 255.86 * tensaoAoQuadrado + 857.39 * tensaoCompensada) * 0.5;
}

// ===============================================================
// bombas
// ===============================================================

/*
  Informa se alguma das bombas está ligada
*/
bool algumaBombaEstaLigada() {
  return bombaDeReposicao.ligada || bombaDeDescarte.ligada || bombaDeCirculacao.ligada;
}

/*
  Informa se todas as bombas estão desligadas há pelo menos o tempo informado
  Argumentos:
    tempoMinimoMs: tempo mínimo com todas as bombas desligadas
*/
bool bombasDesligadasPorPeloMenos(unsigned long tempoMinimoMs) {
  return !algumaBombaEstaLigada() && (millis() - instanteDaUltimaBombaLigadaMs >= tempoMinimoMs);
}

/*
  Liga uma bomba e guarda o instante do ligamento
  Argumentos:
    bomba: bomba a ser ligada
*/
void ligarBomba(Bomba& bomba) {
  bomba.ligada = true;
  bomba.instanteDeLigamentoMs = millis();
  digitalWrite(bomba.pinoDoMosfet, NIVEL_LOGICO_BOMBA_LIGADA);
  Serial.print("Ligou ");
  Serial.println(bomba.nome);
}

/*
  Desliga uma bomba
  Argumentos:
    bomba: bomba a ser desligada
*/
void desligarBomba(Bomba& bomba) {
  bomba.ligada = false;
  digitalWrite(bomba.pinoDoMosfet, !NIVEL_LOGICO_BOMBA_LIGADA);
  Serial.print("Desligou ");
  Serial.println(bomba.nome);
}

/*
  Aplica as condições C4, C5 e C6 para ligar e desligar as bombas
  Primeiro avaliam-se todas as paradas, depois as partidas (descarte, reposição, circulação)
*/
void controlarBombas() {
  // paradas
  // C4: reposição para quando o bebedouro enche ou a água limpa acaba
  if (bombaDeReposicao.ligada && (bebedouroNoMaximo || reservatorioLimpoNoMinimo)) {
    desligarBomba(bombaDeReposicao);
  }
  // C5: circulação para quando o gato sai ou a água cai ao mínimo
  if (bombaDeCirculacao.ligada && (ausenciaPorPeloMenos(TEMPO_DE_AUSENCIA_PARA_PARAR_CIRCULACAO_MS) || bebedouroNoMinimo)) {
    desligarBomba(bombaDeCirculacao);
  }
  // C6: descarte para quando r2 enche ou o bebedouro chega ao mínimo
  if (bombaDeDescarte.ligada && (reservatorioSujoNoMaximo || bebedouroNoMinimo)) {
    desligarBomba(bombaDeDescarte);
  }

  // com falha nenhuma bomba pode ligar
  if (falhaDetectada) {
    return;
  }

  // C6: mandar água suja do bebedouro para r2
  bool estadoPermiteDescarte = (estadoDaTroca == ESTADO_AGUARDANDO) || (estadoDaTroca == ESTADO_DESCARTANDO);
  if (!bombaDeDescarte.ligada
      && estadoPermiteDescarte
      && !bebedouroNoMinimo
      && !reservatorioSujoNoMaximo
      && ausenciaPorPeloMenos(TEMPO_DE_AUSENCIA_PARA_DESCARTAR_MS)
      && !bombaDeReposicao.ligada
      && !bombaDeCirculacao.ligada) {
    ligarBomba(bombaDeDescarte);
    estadoDaTroca = ESTADO_DESCARTANDO;
  }

  // C4: mandar água limpa de r1 para o bebedouro
  bool precisaRepor = ((estadoDaTroca == ESTADO_NORMAL) && bebedouroNoMinimo)
                      || ((estadoDaTroca == ESTADO_REABASTECENDO) && !bebedouroNoMaximo);
  if (!bombaDeReposicao.ligada
      && precisaRepor
      && !reservatorioLimpoNoMinimo
      && !bombaDeDescarte.ligada
      && !bombaDeCirculacao.ligada) {
    ligarBomba(bombaDeReposicao);
  }

  // C5: circular a água enquanto o gato está presente
  // a condição "<= snivel3max" é sempre verdadeira com boia, então não é testada
  if (!bombaDeCirculacao.ligada
      && estadoDaTroca == ESTADO_NORMAL
      && !bebedouroNoMinimo
      && presencaPorPeloMenos(TEMPO_DE_PRESENCA_PARA_CIRCULAR_MS)
      && !bombaDeReposicao.ligada
      && !bombaDeDescarte.ligada) {
    ligarBomba(bombaDeCirculacao);
  }

  // registra o último instante em que alguma bomba estava ligada
  if (algumaBombaEstaLigada()) {
    instanteDaUltimaBombaLigadaMs = millis();
  }
}

/*
  Aplica a condição C9: desliga tudo e ativa a falha se alguma bomba passar do tempo máximo
  A falha só sai reiniciando o ESP32
*/
void protegerContraBombaLigadaPorTempoDemais() {
  Bomba* bombas[3] = {&bombaDeReposicao, &bombaDeDescarte, &bombaDeCirculacao};

  for (int indice = 0; indice < 3; indice++) {
    Bomba* bomba = bombas[indice];
    if (bomba->ligada && (millis() - bomba->instanteDeLigamentoMs > bomba->tempoMaximoLigadaMs)) {
      Serial.print("FALHA: tempo máximo excedido em ");
      Serial.println(bomba->nome);
      falhaDetectada = true;
    }
  }

  if (falhaDetectada) {
    for (int indice = 0; indice < 3; indice++) {
      if (bombas[indice]->ligada) {
        desligarBomba(*bombas[indice]);
      }
    }
  }
}

// ===============================================================
// qualidade da água e troca
// ===============================================================

/*
  Informa se a medição de qualidade é permitida agora
  Exige sensores submersos, bombas desligadas há um tempo, gato ausente e sem bloqueio pós-troca
*/
bool medicaoDeQualidadeEstaPermitida() {
  return !bebedouroNoMinimo
         && !bloqueioPosTrocaAtivo
         && !gatoPresente
         && bombasDesligadasPorPeloMenos(TEMPO_BOMBAS_DESLIGADAS_PARA_MEDIR_MS);
}

/*
  Mede TDS e pH de forma alternada (um por intervalo) para um sensor não interferir no outro
  Sem medição permitida, mantém o último valor, exceto com o bebedouro no mínimo
*/
void medirQualidadeDaAgua() {
  // termina o bloqueio pós-troca quando o tempo de estabilização passa
  if (bloqueioPosTrocaAtivo && (millis() - instanteDoFimDaTrocaMs >= TEMPO_DE_ESTABILIZACAO_APOS_TROCA_MS)) {
    bloqueioPosTrocaAtivo = false;
  }

  leituraDeQualidadePermitida = medicaoDeQualidadeEstaPermitida();

  // sensores fora da água: as leituras antigas deixam de valer
  if (bebedouroNoMinimo) {
    tdsValido = false;
    phValido = false;
    return;
  }

  if (!leituraDeQualidadePermitida) {
    return;
  }

  if (millis() - instanteDaUltimaMedicaoMs < INTERVALO_ENTRE_MEDICOES_DE_QUALIDADE_MS) {
    return;
  }

  // alterna entre TDS e pH
  if (proximaMedicaoEhTds) {
    tdsEmPpm = calcularTdsPelaTensao(lerTensaoFiltradaEmVolts(PINO_SENSOR_TDS));
    tdsValido = true;
  } else {
    ph = calcularPhPelaTensao(lerTensaoFiltradaEmVolts(PINO_SENSOR_PH));
    phValido = true;
  }
  proximaMedicaoEhTds = !proximaMedicaoEhTds;

  // o instante é guardado depois da leitura, que leva algumas centenas de ms
  instanteDaUltimaMedicaoMs = millis();
}

/*
  Informa se TDS ou pH, nas últimas medições válidas, indicam água ruim
*/
bool aguaTemQualidadeRuim() {
  bool tdsRuim = tdsValido && (tdsEmPpm > LIMITE_TDS_EM_PPM);
  bool phRuim = phValido && ((ph < PH_MINIMO_ACEITAVEL) || (ph > PH_MAXIMO_ACEITAVEL));
  return tdsRuim || phRuim;
}

/*
  Aplica as condições C3, C7 e C8, que mudam o estado da troca de água
*/
void atualizarEstadoDaTroca() {
  // C3: registrar qualidade ruim (só no estado normal e com medição permitida)
  if (estadoDaTroca == ESTADO_NORMAL) {
    if (leituraDeQualidadePermitida && aguaTemQualidadeRuim()) {
      // a qualidade ruim precisa se manter por um tempo para evitar disparo por pico
      if (!qualidadeRuimEmAnalise) {
        qualidadeRuimEmAnalise = true;
        instanteDoInicioDaQualidadeRuimMs = millis();
      } else if (millis() - instanteDoInicioDaQualidadeRuimMs >= TEMPO_DE_CONFIRMACAO_DE_QUALIDADE_RUIM_MS) {
        estadoDaTroca = ESTADO_AGUARDANDO;
        qualidadeRuimEmAnalise = false;
        Serial.println("Qualidade ruim confirmada: estado AGUARDANDO");
      }
    } else if (!aguaTemQualidadeRuim()) {
      qualidadeRuimEmAnalise = false;
    }
  }

  // C7: descarte terminado (bebedouro no mínimo) passa para o reabastecimento
  if (((estadoDaTroca == ESTADO_DESCARTANDO) || (estadoDaTroca == ESTADO_AGUARDANDO)) && bebedouroNoMinimo) {
    estadoDaTroca = ESTADO_REABASTECENDO;
    Serial.println("Descarte terminado: estado REABASTECENDO");
  }

  // C8: bebedouro cheio de novo, volta ao normal e bloqueia a medição por um tempo
  if ((estadoDaTroca == ESTADO_REABASTECENDO) && bebedouroNoMaximo) {
    estadoDaTroca = ESTADO_NORMAL;
    bloqueioPosTrocaAtivo = true;
    instanteDoFimDaTrocaMs = millis();
    tdsValido = false;
    phValido = false;
    qualidadeRuimEmAnalise = false;
    Serial.println("Reabastecimento terminado: estado NORMAL");
  }
}

// ===============================================================
// LEDs, buzzer e comunicação com a placa 3
// ===============================================================

/*
  Define os dois LEDs de um par (verde e amarelo)
  Argumentos:
    numeroDoPar: número do par, de 1 a 5
    verdeAceso: estado do LED verde
    amareloAceso: estado do LED amarelo
*/
void definirParDeLeds(
  int numeroDoPar,
  bool verdeAceso,
  bool amareloAceso
) {
  ledsAcesos[INDICE_DO_PRIMEIRO_LED_VERDE + numeroDoPar - 1] = verdeAceso;
  ledsAcesos[INDICE_DO_PRIMEIRO_LED_AMARELO + numeroDoPar - 1] = amareloAceso;
}

/*
  Calcula o estado dos 11 LEDs
  Pares: 1 = água limpa, 2 = água suja, 3 = TDS, 4 = pH, 5 = bebedouro
*/
void atualizarLeds() {
  // par 1: água limpa (C1)
  definirParDeLeds(1, !reservatorioLimpoNoMinimo, reservatorioLimpoNoMinimo);

  // par 2: água suja (C2)
  definirParDeLeds(2, !reservatorioSujoNoMaximo, reservatorioSujoNoMaximo);

  // par 3: TDS (apagado sem leitura)
  bool tdsRuim = tdsValido && (tdsEmPpm > LIMITE_TDS_EM_PPM);
  definirParDeLeds(3, tdsValido && !tdsRuim, tdsRuim);

  // par 4: pH (apagado sem leitura)
  bool phRuim = phValido && ((ph < PH_MINIMO_ACEITAVEL) || (ph > PH_MAXIMO_ACEITAVEL));
  definirParDeLeds(4, phValido && !phRuim, phRuim);

  // par 5: bebedouro (amarelo no mínimo ou enquanto reabastece)
  bool bebedouroComAtencao = bebedouroNoMinimo || (estadoDaTroca == ESTADO_REABASTECENDO);
  definirParDeLeds(5, !bebedouroComAtencao, bebedouroComAtencao);

  // vermelho: aceso na troca pendente ou em andamento, piscando na falha
  if (falhaDetectada) {
    ledsAcesos[INDICE_DO_LED_VERMELHO] = ((millis() / PERIODO_DO_PISCA_MS) % 2) == 0;
  } else {
    ledsAcesos[INDICE_DO_LED_VERMELHO] = (estadoDaTroca != ESTADO_NORMAL);
  }
}

/*
  Informa se o alerta do buzzer de índice informado está ativo agora
  Argumentos:
    indiceDoAlerta: índice do alerta (falha, r2 cheio ou r1 baixo)
*/
bool alertaEstaAtivo(int indiceDoAlerta) {
  switch (indiceDoAlerta) {
    case ALERTA_DE_FALHA:
      return falhaDetectada;
    case ALERTA_DE_RESERVATORIO_SUJO_CHEIO:
      return reservatorioSujoNoMaximo;
    default:
      return reservatorioLimpoNoMinimo;
  }
}

/*
  Decide se algum alerta deve soar agora e registra o sinal
  Cada alerta soa quando começa e, se configurado, se repete enquanto continuar
  A placa 3 toca o sinal quando o contador muda
*/
void atualizarAlertasDoBuzzer() {
  unsigned long agoraMs = millis();

  // alertas que acabaram podem soar de novo na próxima ocorrência
  for (int indice = 0; indice < QUANTIDADE_DE_ALERTAS; indice++) {
    if (!alertaEstaAtivo(indice)) {
      alertasDoBuzzer[indice].jaSinalizado = false;
    }
  }

  // evita sinais colados uns nos outros
  if (agoraMs - instanteDoUltimoSinalEmitidoMs < INTERVALO_MINIMO_ENTRE_SINAIS_MS) {
    return;
  }

  // emite no máximo um sinal por chamada, respeitando a prioridade do vetor
  for (int indice = 0; indice < QUANTIDADE_DE_ALERTAS; indice++) {
    if (!alertaEstaAtivo(indice)) {
      continue;
    }

    AlertaDoBuzzer& alerta = alertasDoBuzzer[indice];
    bool primeiroSinal = !alerta.jaSinalizado;
    bool repeticaoVencida = (INTERVALO_DE_REPETICAO_DO_ALERTA_MS > 0)
                            && (agoraMs - alerta.instanteDoUltimoSinalMs >= INTERVALO_DE_REPETICAO_DO_ALERTA_MS);

    if (primeiroSinal || repeticaoVencida) {
      alerta.jaSinalizado = true;
      alerta.instanteDoUltimoSinalMs = agoraMs;
      ultimoPadraoDoBuzzer = alerta.padrao;
      contadorDeSinaisDoBuzzer++;
      instanteDoUltimoSinalEmitidoMs = agoraMs;
      return;
    }
  }
}

/*
  Envia o estado para a placa 3 pela UART, uma linha de texto por envio
  Formato: $B,<11 LEDs em 0/1>,<padrão do buzzer>,<contador do buzzer>,<TDS>,<pH x100>,<estado>,<falha>
  LEDs na ordem: vermelho, verdes 1 a 5, amarelos 1 a 5
  TDS e pH valem -1 quando não há leitura válida
  Estado: 0 = NORMAL, 1 = AGUARDANDO, 2 = DESCARTANDO, 3 = REABASTECENDO
*/
void enviarEstadoParaPlaca3() {
  if (millis() - instanteDoUltimoEnvioParaPlaca3Ms < INTERVALO_DE_ENVIO_PARA_PLACA_3_MS) {
    return;
  }
  instanteDoUltimoEnvioParaPlaca3Ms = millis();

  String linha = "$B,";
  for (int indice = 0; indice < QUANTIDADE_DE_LEDS; indice++) {
    linha += ledsAcesos[indice] ? '1' : '0';
  }
  linha += ",";
  linha += String(ultimoPadraoDoBuzzer);
  linha += ",";
  linha += String(contadorDeSinaisDoBuzzer);
  linha += ",";
  linha += tdsValido ? String((int)round(tdsEmPpm)) : String(-1);
  linha += ",";
  linha += phValido ? String((int)round(ph * 100.0)) : String(-1);
  linha += ",";
  linha += String((int)estadoDaTroca);
  linha += ",";
  linha += falhaDetectada ? "1" : "0";

  Serial2.println(linha);
}

// ===============================================================
// painel web e depuração
// ===============================================================

/*
  Copia o estado atual do sistema para a estrutura lida pelo painel web
*/
void atualizarPainel() {
  dadosDoPainel.tdsEmPpm = tdsEmPpm;
  dadosDoPainel.tdsValido = tdsValido;
  dadosDoPainel.ph = ph;
  dadosDoPainel.phValido = phValido;
  dadosDoPainel.distanciaDoGatoEmCentimetros = distanciaDoGatoEmCentimetros;
  dadosDoPainel.gatoPresente = gatoPresente;

  dadosDoPainel.bloqueioPosTrocaAtivo = bloqueioPosTrocaAtivo;
  dadosDoPainel.leituraDeQualidadePermitida = leituraDeQualidadePermitida;

  dadosDoPainel.reservatorioLimpoNoMinimo = reservatorioLimpoNoMinimo;
  dadosDoPainel.reservatorioSujoNoMaximo = reservatorioSujoNoMaximo;
  dadosDoPainel.bebedouroNoMinimo = bebedouroNoMinimo;
  dadosDoPainel.bebedouroNoMaximo = bebedouroNoMaximo;

  dadosDoPainel.bombaDeReposicaoLigada = bombaDeReposicao.ligada;
  dadosDoPainel.bombaDeDescarteLigada = bombaDeDescarte.ligada;
  dadosDoPainel.bombaDeCirculacaoLigada = bombaDeCirculacao.ligada;

  dadosDoPainel.estadoDaTroca = nomeDoEstado(estadoDaTroca);
  dadosDoPainel.falhaDetectada = falhaDetectada;

  for (int indice = 0; indice < QUANTIDADE_DE_LEDS; indice++) {
    dadosDoPainel.ledsAcesos[indice] = ledsAcesos[indice];
  }

  dadosDoPainel.ultimoSinalDoBuzzer = ultimoPadraoDoBuzzer;
  dadosDoPainel.contadorDeSinaisDoBuzzer = contadorDeSinaisDoBuzzer;
}

/*
  Imprime no monitor serial um resumo do sistema para acompanhar os testes
*/
void imprimirDepuracaoNaSerial() {
  if (millis() - instanteDaUltimaDepuracaoMs < INTERVALO_DA_DEPURACAO_MS) {
    return;
  }
  instanteDaUltimaDepuracaoMs = millis();

  // nível lógico bruto dos pinos das boias, para identificar qual GPIO é cada boia
  Serial.print("GPIO19=");
  Serial.print(digitalRead(PINO_SNIVEL3MIN_BEBEDOURO_MINIMO));
  Serial.print(" GPIO18=");
  Serial.print(digitalRead(PINO_SNIVEL3MAX_BEBEDOURO_MAXIMO));
  Serial.print(" GPIO23=");
  Serial.print(digitalRead(PINO_SNIVEL2_RESERVATORIO_SUJO_MAXIMO));
  Serial.print(" GPIO21=");
  Serial.print(digitalRead(PINO_SNIVEL1_RESERVATORIO_LIMPO_MINIMO));
  Serial.print(" || estado=");
  Serial.print(nomeDoEstado(estadoDaTroca));
  Serial.print(" | r1Min=");
  Serial.print(reservatorioLimpoNoMinimo);
  Serial.print(" r2Max=");
  Serial.print(reservatorioSujoNoMaximo);
  Serial.print(" bebMin=");
  Serial.print(bebedouroNoMinimo);
  Serial.print(" bebMax=");
  Serial.print(bebedouroNoMaximo);
  Serial.print(" | gato=");
  Serial.print(gatoPresente);
  Serial.print(" (");
  Serial.print(distanciaDoGatoEmCentimetros, 1);
  Serial.print(" cm) | TDS=");
  Serial.print(tdsValido ? String(tdsEmPpm, 0) : String("--"));
  Serial.print(" pH=");
  Serial.print(phValido ? String(ph, 2) : String("--"));
  Serial.print(" | bombas=");
  Serial.print(bombaDeReposicao.ligada);
  Serial.print(bombaDeDescarte.ligada);
  Serial.print(bombaDeCirculacao.ligada);
  Serial.print(" | falha=");
  Serial.println(falhaDetectada);
}

// ===============================================================
// programa principal
// ===============================================================

/*
  Configura pinos, ADC, UART da placa 3 e painel web
*/
void setup() {
  Serial.begin(115200);

  // bombas começam desligadas antes de virarem saída
  int pinosDasBombas[3] = {
    PINO_MOSFET_BOMBA_REPOSICAO,
    PINO_MOSFET_BOMBA_DESCARTE,
    PINO_MOSFET_BOMBA_CIRCULACAO
  };
  for (int indice = 0; indice < 3; indice++) {
    digitalWrite(pinosDasBombas[indice], !NIVEL_LOGICO_BOMBA_LIGADA);
    pinMode(pinosDasBombas[indice], OUTPUT);
  }

  // boias (assume resistor de pull-down interno, sinal = nível alto)
  pinMode(PINO_SNIVEL3MIN_BEBEDOURO_MINIMO, INPUT_PULLDOWN);
  pinMode(PINO_SNIVEL3MAX_BEBEDOURO_MAXIMO, INPUT_PULLDOWN);
  pinMode(PINO_SNIVEL1_RESERVATORIO_LIMPO_MINIMO, INPUT_PULLDOWN);
  pinMode(PINO_SNIVEL2_RESERVATORIO_SUJO_MAXIMO, INPUT_PULLDOWN);

  // ultrassônico
  pinMode(PINO_ULTRASSONICO_GATILHO, OUTPUT);
  pinMode(PINO_ULTRASSONICO_ECO, INPUT);

  // ADC de 12 bits com atenuação para ler até cerca de 3,1 V
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // UART com a placa 3
  Serial2.begin(
    VELOCIDADE_DA_UART_PLACA_3,
    SERIAL_8N1,
    PINO_UART_RECEPCAO,
    PINO_UART_TRANSMISSAO
  );

  iniciarPainelWeb();
}

/*
  Repete a leitura dos sensores, a lógica de troca, o controle das bombas e as saídas
*/
void loop() {
  atenderPainelWeb();

  // leituras
  lerNiveis();
  atualizarPresencaDoGato();
  medirQualidadeDaAgua();

  // lógica
  atualizarEstadoDaTroca();
  controlarBombas();
  protegerContraBombaLigadaPorTempoDemais();

  // saídas
  atualizarLeds();
  atualizarAlertasDoBuzzer();
  enviarEstadoParaPlaca3();
  atualizarPainel();
  imprimirDepuracaoNaSerial();
}