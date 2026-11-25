// teste integrado das placas 2 e 3 com painel web (bancada, sem computador)
//
// o ESP32 cria a rede Wi-Fi "Bebedouro-Gatos" (sem senha)
// no celular: conectar nessa rede e abrir http://192.168.4.1
// (se o celular reclamar que não tem internet, manter conectado mesmo assim)
//
// a página mostra o estado do sistema e tem botões para os comandos de TDS e pH
//
// o que é real e o que é simulado:
//   presença: sensor ultrassônico real
//   boias de nível: reais, mexidas à mão (fechar o contato = "com sinal")
//   TDS e pH: valores simulados pelos botões da página (sem sensor real)
//   bombas: a lógica liga e desliga os MOSFETs normalmente; sem bombas conectadas,
//           a página mostra qual bomba estaria ligada
//   placa 3: recebe o estado pela UART (a placa 3 precisa do firmware do MSP430)
//
// regras implementadas (sinal presente = a condição ocorre, em todas as boias):
//   circulação (bomba3): gato presente por 1 s liga; ausente por 2 s desliga;
//                        só no estado NORMAL e com bebedouro acima do mínimo
//   reposição (bomba1):  NORMAL com bebedouro no mínimo, ou REABASTECENDO sem o bebedouro cheio;
//                        para quando o bebedouro enche ou o r1 chega ao mínimo
//   qualidade:           mede TDS e pH (alternados, 1 a cada 2 s) só com bebedouro acima do mínimo,
//                        sem bloqueio pós-troca, gato ausente e bombas desligadas há 10 s
//   troca pedida:        TDS > 120 ppm ou pH < 6,0 ou pH > 8,5 por 15 s seguidos, no estado NORMAL,
//                        vai para AGUARDANDO
//   descarte (bomba2):   AGUARDANDO ou DESCARTANDO, gato ausente por 6 s, bebedouro acima do mínimo,
//                        r2 não cheio; vira DESCARTANDO; para quando r2 enche ou bebedouro chega ao mínimo
//   bebedouro no mínimo durante AGUARDANDO ou DESCARTANDO: vira REABASTECENDO
//   bebedouro cheio durante REABASTECENDO: vira NORMAL, bloqueia a medição por 60 s
//                        e apaga TDS e pH (sem leitura)
//   falha:               bomba ligada além do tempo máximo desliga tudo (só reiniciando o ESP)
//
// diferenças de propósito em relação à lógica final:
//   sem sensores reais de TDS e pH
//   circulação com 1 s de presença e 2 s de ausência (lógica final: 5 s e 5 s)
//   leitura de presença confirmada por 3 leituras seguidas, boias com filtro de 50 ms
//   LED de troca da falha fica aceso fixo no envio: a piscada é feita pela placa 3 (campo falha)
//
// IMPORTANTE: gravar o código pelo USB com a placa 1 DESLIGADA do ESP,
// e só depois ligar o ESP pela placa 1 (nunca dois 5 V ao mesmo tempo no ESP)
//
// no Arduino IDE, a pasta do sketch precisa se chamar teste_integrado_web (igual ao arquivo .ino)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>

// ===============================================================
// pinos (conforme o esquemático da placa 2)
// ===============================================================

// sensor ultrassônico de presença
const int PINO_ULTRASSONICO_GATILHO = 32;
const int PINO_ULTRASSONICO_ECO = 33;

// gates dos MOSFETs das bombas
const int PINO_MOSFET_BOMBA_REPOSICAO = 25;
const int PINO_MOSFET_BOMBA_DESCARTE = 26;
const int PINO_MOSFET_BOMBA_CIRCULACAO = 27;

// comunicação serial (UART) com a placa 3
const int PINO_UART_TRANSMISSAO = 17;
const int PINO_UART_RECEPCAO = 16;

// se o TX do ESP estiver chegando no TX da placa 3 (cabo cruzado errado),
// trocar para true: o ESP passa a transmitir pelo GPIO16 e receber pelo GPIO17
const bool TROCAR_TX_E_RX_DA_UART = false;

// ===============================================================
// parâmetros ajustáveis
// ===============================================================

// rede Wi-Fi criada pelo ESP32
const char* NOME_DA_REDE_WIFI = "Bebedouro-Gatos";

// nível lógico que liga o MOSFET da bomba (IRL540N, canal N: nível alto liga)
const int NIVEL_LOGICO_BOMBA_LIGADA = HIGH;

// qualidade da água
const float LIMITE_TDS_EM_PPM = 120.0;
const float PH_MINIMO_ACEITAVEL = 6.0;
const float PH_MAXIMO_ACEITAVEL = 8.5;

// valores simulados (botões a, b, c, d, e, f)
const float TDS_PADRAO_EM_PPM = 50.0;
const float TDS_ALTO_EM_PPM = 300.0;
const float PH_PADRAO = 7.0;
const float PH_ALTO = 9.0;

// presença do gato
const float DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS = 30.0;
const float DISTANCIA_SEM_ECO_EM_CENTIMETROS = 400.0;
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 25000;
const unsigned long INTERVALO_ENTRE_LEITURAS_DE_PRESENCA_MS = 100;
const int LEITURAS_SEGUIDAS_PARA_CONFIRMAR_PRESENCA = 3;

// tempos da lógica
const unsigned long TEMPO_DE_PRESENCA_PARA_CIRCULAR_MS = 1000;          // lógica final: 5000
const unsigned long TEMPO_DE_AUSENCIA_PARA_PARAR_CIRCULACAO_MS = 2000;  // lógica final: 5000
const unsigned long TEMPO_DE_AUSENCIA_PARA_DESCARTAR_MS = 6000;
const unsigned long TEMPO_BOMBAS_DESLIGADAS_PARA_MEDIR_MS = 10000;
const unsigned long TEMPO_DE_CONFIRMACAO_DE_QUALIDADE_RUIM_MS = 15000;
const unsigned long TEMPO_DE_ESTABILIZACAO_APOS_TROCA_MS = 60000;
const unsigned long INTERVALO_ENTRE_MEDICOES_DE_QUALIDADE_MS = 2000;

// filtro das boias: o sinal precisa ficar estável por esse tempo para valer
const unsigned long TEMPO_DE_ESTABILIDADE_DA_BOIA_MS = 50;

// tempo máximo contínuo de cada bomba ligada
const unsigned long TEMPO_MAXIMO_BOMBA_REPOSICAO_MS = 120000;
const unsigned long TEMPO_MAXIMO_BOMBA_DESCARTE_MS = 120000;
const unsigned long TEMPO_MAXIMO_BOMBA_CIRCULACAO_MS = 300000;

// buzzer: tempo entre repetições do mesmo alerta (0 = tocar uma única vez)
const unsigned long INTERVALO_DE_REPETICAO_DO_ALERTA_MS = 60000;
const unsigned long INTERVALO_MINIMO_ENTRE_SINAIS_MS = 3000;

// placa 3
const unsigned long VELOCIDADE_DA_UART_PLACA_3 = 9600;
const unsigned long INTERVALO_DE_ENVIO_PARA_PLACA_3_MS = 500;

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

// boias de nível
// CONFIRMAR qual GPIO é cada boia: o esquemático chama os pinos de NIVEL_R1_BAIXO (19), NIVEL_R1_ALTO (18),
// NIVEL_R2 (23) e NIVEL_R3 (21). Suposição atual: 19 e 18 são o bebedouro (mínimo e máximo),
// 23 é o r2 (água suja) e 21 é o r1 (água limpa). O botão "trocar GPIO r1/r2" da página troca 21 e 23.
struct Boia {
  const char* nome;
  int pino;
  bool comSinal;                       // estado já filtrado
  bool leituraBruta;                   // última leitura, ainda sem filtro
  unsigned long instanteDaMudancaMs;   // quando a leitura bruta mudou pela última vez
};

Boia boiaDoBebedouroNoMinimo = {"bebedouro mínimo", 19, false, false, 0};
Boia boiaDoBebedouroNoMaximo = {"bebedouro máximo", 18, false, false, 0};
Boia boiaDoReservatorioSujo = {"r2 sujo máximo", 23, false, false, 0};
Boia boiaDoReservatorioLimpo = {"r1 limpo mínimo", 21, false, false, 0};

Boia* todasAsBoias[4] = {
  &boiaDoBebedouroNoMinimo,
  &boiaDoBebedouroNoMaximo,
  &boiaDoReservatorioSujo,
  &boiaDoReservatorioLimpo
};

// nível lógico lido quando a boia envia sinal (contato fechado)
// padrão: nível alto, com resistor de pull-down interno (fechar o contato liga o pino em 3,3 V)
int nivelLogicoComSinal = HIGH;

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
int leiturasSeguidasDiferentes = 0;
unsigned long instanteDaUltimaMudancaDePresencaMs = 0;
unsigned long instanteDaUltimaLeituraDePresencaMs = 0;

// valores simulados de TDS e pH
struct ValorSimulado {
  const char* nome;
  float valorPadrao;
  float valorAtual;
  bool voltaAoPadraoNoFimDaTroca;
};

ValorSimulado tdsSimulado = {"TDS (ppm)", TDS_PADRAO_EM_PPM, TDS_PADRAO_EM_PPM, false};
ValorSimulado phSimulado = {"pH", PH_PADRAO, PH_PADRAO, false};

// qualidade da água (o que o sistema "mediu")
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

// LEDs da placa 3 (a placa 3 acende cada par e o LED de troca a partir desses 11 valores)
// posição 0 = LED de troca, posições 1 a 5 = verdes, posições 6 a 10 = alertas
// pares: 1 = água limpa, 2 = água suja, 3 = TDS, 4 = pH, 5 = bebedouro
const int QUANTIDADE_DE_LEDS = 11;
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

// envio para a placa 3
String ultimaLinhaEnviada = "";
String linhaRecebidaDaPlaca3 = "";
unsigned long instanteDoUltimoEnvioParaPlaca3Ms = 0;

// servidor web
WebServer servidorWeb(80);

// registro dos últimos eventos (aparece na página)
const int TAMANHO_DO_REGISTRO = 30;
String registroDeEventos[TAMANHO_DO_REGISTRO];
int proximaPosicaoDoRegistro = 0;
int totalDeEventosGuardados = 0;

// ===============================================================
// página web
// ===============================================================

const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bebedouro: teste</title>
<style>
  body { font-family: sans-serif; margin: 0; padding: 12px; background: #f4f6f8; color: #111; }
  h1 { font-size: 18px; margin: 0 0 8px 0; }
  .card { background: #fff; border-radius: 10px; padding: 12px; margin-bottom: 10px; box-shadow: 0 1px 3px rgba(0,0,0,0.15); }
  .card h2 { font-size: 14px; margin: 0 0 8px 0; color: #555; text-transform: uppercase; }
  .estado { font-size: 26px; font-weight: bold; }
  .e0 { color: #1b8a3a; } .e1 { color: #b36b00; } .e2 { color: #c62828; } .e3 { color: #1565c0; }
  .ef { color: #c62828; }
  .chip { display: inline-block; padding: 6px 10px; margin: 3px; border-radius: 14px; font-size: 14px; }
  .on { background: #1b8a3a; color: #fff; }
  .off { background: #e3e6ea; color: #333; }
  .leds { display: flex; flex-wrap: wrap; gap: 14px; }
  .par { display: flex; flex-direction: column; align-items: center; font-size: 12px; }
  .led { width: 28px; height: 28px; border-radius: 50%; border: 2px solid #777; background: #ddd; }
  .led.g { background: #2ecc40; } .led.r { background: #e53935; } .led.y { background: #ffc400; }
  .pisca { animation: pisca 1s steps(2) infinite; }
  @keyframes pisca { 50% { opacity: 0.2; } }
  button { font-size: 15px; padding: 10px 12px; margin: 3px; border: 0; border-radius: 8px; background: #1565c0; color: #fff; }
  button.sec { background: #546e7a; }
  button.perigo { background: #c62828; }
  input { font-size: 15px; padding: 8px; width: 80px; }
  .linha { font-size: 13px; margin: 2px 0; word-break: break-all; }
  #registro { font-family: monospace; font-size: 12px; max-height: 220px; overflow-y: auto; }
  small { color: #555; }
</style>
</head>
<body>
<h1>Bebedouro: teste das placas 2 e 3 <small id="conn"></small></h1>

<div class="card">
  <h2>Estado da troca</h2>
  <div id="estado" class="estado">...</div>
  <div class="linha" id="passo"></div>
  <small>ligado há <span id="tempo">0</span></small>
</div>

<div class="card">
  <h2>Bombas (se estivessem conectadas)</h2>
  <div id="bombas"></div>
</div>

<div class="card">
  <h2>Boias e presença</h2>
  <div id="boias"></div>
  <div id="presenca"></div>
</div>

<div class="card">
  <h2>Qualidade da água</h2>
  <div class="linha" id="tds"></div>
  <div class="linha" id="ph"></div>
  <div class="linha" id="medicao"></div>
  <p><b>TDS</b><br>
    <button onclick="cmd('a')">50 fixo</button>
    <button onclick="cmd('b')">300 fixo</button>
    <button onclick="cmd('c')">300 até a troca</button><br>
    <input id="tdsv" type="number" value="150" step="1">
    <button class="sec" onclick="cmd('t', document.getElementById('tdsv').value)">fixar valor</button>
  </p>
  <p><b>pH</b><br>
    <button onclick="cmd('d')">7,0 fixo</button>
    <button onclick="cmd('e')">9,0 fixo</button>
    <button onclick="cmd('f')">9,0 até a troca</button><br>
    <input id="phv" type="number" value="5.5" step="0.1">
    <button class="sec" onclick="cmd('p', document.getElementById('phv').value)">fixar valor</button>
  </p>
</div>

<div class="card">
  <h2>O que o ESP manda para a placa 3</h2>
  <div class="leds" id="leds"></div>
  <div class="linha" id="buzzer"></div>
  <div class="linha" id="uart"></div>
</div>

<div class="card">
  <h2>Ferramentas</h2>
  <button class="perigo" onclick="cmd('x')">simular falha</button>
  <button class="sec" onclick="cmd('i')">inverter lógica das boias</button>
  <button class="sec" onclick="cmd('s')">trocar GPIO r1/r2</button>
  <div class="linha" id="logica"></div>
</div>

<div class="card">
  <h2>Eventos (mais novo primeiro)</h2>
  <div id="registro"></div>
</div>

<script>
const $ = id => document.getElementById(id);

function chip(ligado, texto) {
  return '<span class="chip ' + (ligado ? 'on' : 'off') + '">' + texto + '</span>';
}

function cmd(comando, valor) {
  let url = '/cmd?c=' + comando;
  if (valor !== undefined) { url += '&v=' + encodeURIComponent(valor); }
  fetch(url).then(atualizar);
}

function descreverSimulado(nome, medido, simulado, temporario) {
  return nome + ' medido: <b>' + (medido === null ? 'sem leitura' : medido) + '</b> | simulado: ' + simulado +
         (temporario ? ' (volta ao padrão quando a troca terminar)' : ' (fixo)');
}

function desenhar(d) {
  $('estado').textContent = d.estado + (d.falha ? ' + FALHA' : '');
  $('estado').className = 'estado e' + d.estadoNum + (d.falha ? ' ef' : '');
  $('passo').textContent = d.passo;
  $('tempo').textContent = d.t + ' s';

  const nomesDasBombas = ['bomba1 reposição', 'bomba2 descarte', 'bomba3 circulação'];
  $('bombas').innerHTML = d.bombas.map((b, i) => chip(b, nomesDasBombas[i] + ': ' + (b ? 'LIGADA' : 'desligada'))).join('');

  $('boias').innerHTML =
    chip(d.bebMin, 'bebedouro mínimo (GPIO' + d.pinos[0] + '): ' + (d.bebMin ? 'COM sinal' : 'sem sinal')) +
    chip(d.bebMax, 'bebedouro máximo (GPIO' + d.pinos[1] + '): ' + (d.bebMax ? 'COM sinal' : 'sem sinal')) +
    chip(d.r2Max, 'r2 sujo máximo (GPIO' + d.pinos[2] + '): ' + (d.r2Max ? 'COM sinal' : 'sem sinal')) +
    chip(d.r1Min, 'r1 limpo mínimo (GPIO' + d.pinos[3] + '): ' + (d.r1Min ? 'COM sinal' : 'sem sinal'));
  $('presenca').innerHTML = chip(d.gato, 'presença: ' + (d.gato ? 'SIM' : 'não')) + ' <small>distância ' + d.dist + ' cm</small>';

  $('tds').innerHTML = descreverSimulado('TDS', d.tdsMedido, d.tdsSim, d.tdsTemp);
  $('ph').innerHTML = descreverSimulado('pH', d.phMedido, d.phSim, d.phTemp);
  $('medicao').innerHTML = 'medição: <b>' + d.medicao + '</b>';

  const nomesDosPares = ['Água limpa', 'Água suja', 'TDS', 'pH', 'Bebedouro'];
  let leds = '';
  for (let i = 1; i <= 5; i++) {
    const cor = d.leds[i] ? 'g' : (d.leds[i + 5] ? 'r' : '');
    leds += '<div class="par"><span class="led ' + cor + '"></span>' + nomesDosPares[i - 1] + '</div>';
  }
  leds += '<div class="par"><span class="led ' + (d.leds[0] ? 'y' : '') + (d.falha ? ' pisca' : '') + '"></span>Troca</div>';
  $('leds').innerHTML = leds;
  $('buzzer').textContent = 'último sinal do buzzer: padrão ' + d.buzzerPadrao + ', contador ' + d.buzzerContador;
  $('uart').textContent = 'UART enviada: ' + d.uart;
  $('logica').textContent = 'boia com sinal = nível ' + (d.sinalAlto ? 'ALTO (pull-down)' : 'BAIXO (pull-up)');

  $('registro').innerHTML = d.log.slice().reverse().map(l => '<div>' + l + '</div>').join('');
}

async function atualizar() {
  try {
    const resposta = await fetch('/estado');
    desenhar(await resposta.json());
    $('conn').textContent = '(conectado)';
  } catch (erro) {
    $('conn').textContent = '(sem conexão)';
  }
}

atualizar();
setInterval(atualizar, 1000);
</script>
</body>
</html>
)rawliteral";

// ===============================================================
// utilidades
// ===============================================================

/*
  Guarda um evento no registro da página e também o escreve na serial (se houver computador)
  Argumentos:
    texto: descrição do evento
*/
void registrarEvento(const String& texto) {
  String linha = "[" + String(millis() / 1000.0, 1) + " s] " + texto;
  Serial.println(linha);

  registroDeEventos[proximaPosicaoDoRegistro] = linha;
  proximaPosicaoDoRegistro = (proximaPosicaoDoRegistro + 1) % TAMANHO_DO_REGISTRO;
  if (totalDeEventosGuardados < TAMANHO_DO_REGISTRO) {
    totalDeEventosGuardados++;
  }
}

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

// ===============================================================
// leitura dos sensores
// ===============================================================

/*
  Configura o resistor interno das boias conforme a lógica escolhida
  Sinal em nível alto usa pull-down; sinal em nível baixo usa pull-up
*/
void configurarPinosDasBoias() {
  int modoDoPino = (nivelLogicoComSinal == HIGH) ? INPUT_PULLDOWN : INPUT_PULLUP;
  for (int indice = 0; indice < 4; indice++) {
    pinMode(todasAsBoias[indice]->pino, modoDoPino);
  }
}

/*
  Lê uma boia com filtro de estabilidade e registra quando ela muda
  Argumentos:
    boia: boia a ser atualizada
*/
void atualizarBoia(Boia& boia) {
  unsigned long agoraMs = millis();
  bool leituraAgora = (digitalRead(boia.pino) == nivelLogicoComSinal);

  if (leituraAgora != boia.leituraBruta) {
    boia.leituraBruta = leituraAgora;
    boia.instanteDaMudancaMs = agoraMs;
  }

  bool estavelPorTempoSuficiente = (agoraMs - boia.instanteDaMudancaMs) >= TEMPO_DE_ESTABILIDADE_DA_BOIA_MS;
  if (boia.comSinal != boia.leituraBruta && estavelPorTempoSuficiente) {
    boia.comSinal = boia.leituraBruta;
    registrarEvento(
      String("Boia ") + boia.nome + " (GPIO" + boia.pino + "): " + (boia.comSinal ? "COM sinal" : "sem sinal")
    );
  }
}

/*
  Lê as quatro boias e atualiza as variáveis de nível
  Em todas as boias, sinal presente = a condição descrita está ocorrendo
*/
void lerNiveis() {
  for (int indice = 0; indice < 4; indice++) {
    atualizarBoia(*todasAsBoias[indice]);
  }

  reservatorioLimpoNoMinimo = boiaDoReservatorioLimpo.comSinal;
  reservatorioSujoNoMaximo = boiaDoReservatorioSujo.comSinal;
  bebedouroNoMinimo = boiaDoBebedouroNoMinimo.comSinal;
  bebedouroNoMaximo = boiaDoBebedouroNoMaximo.comSinal;
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
  A presença só muda depois de algumas leituras seguidas indicando a mudança
*/
void atualizarPresencaDoGato() {
  unsigned long agoraMs = millis();

  if (agoraMs - instanteDaUltimaLeituraDePresencaMs < INTERVALO_ENTRE_LEITURAS_DE_PRESENCA_MS) {
    return;
  }
  instanteDaUltimaLeituraDePresencaMs = agoraMs;

  distanciaDoGatoEmCentimetros = medirDistanciaEmCentimetros();
  bool presencaAgora = distanciaDoGatoEmCentimetros < DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS;

  if (presencaAgora == gatoPresente) {
    leiturasSeguidasDiferentes = 0;
    return;
  }

  leiturasSeguidasDiferentes++;
  if (leiturasSeguidasDiferentes >= LEITURAS_SEGUIDAS_PARA_CONFIRMAR_PRESENCA) {
    gatoPresente = presencaAgora;
    instanteDaUltimaMudancaDePresencaMs = agoraMs;
    leiturasSeguidasDiferentes = 0;

    registrarEvento(
      String(gatoPresente ? "Presença detectada (" : "Presença sumiu (") + String(distanciaDoGatoEmCentimetros, 1) + " cm)"
    );
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
  registrarEvento(String("Ligou ") + bomba.nome);
}

/*
  Desliga uma bomba
  Argumentos:
    bomba: bomba a ser desligada
*/
void desligarBomba(Bomba& bomba) {
  bomba.ligada = false;
  digitalWrite(bomba.pinoDoMosfet, !NIVEL_LOGICO_BOMBA_LIGADA);
  registrarEvento(String("Desligou ") + bomba.nome);
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
      registrarEvento(String("FALHA: tempo máximo excedido em ") + bomba->nome);
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
// qualidade da água (valores simulados) e troca
// ===============================================================

/*
  Define um valor simulado e registra o evento
  Argumentos:
    valor: valor simulado a ser alterado (TDS ou pH)
    novoValor: novo valor
    voltaAoPadraoNoFimDaTroca: verdadeiro para voltar ao padrão quando a troca terminar
*/
void definirValorSimulado(ValorSimulado& valor, float novoValor, bool voltaAoPadraoNoFimDaTroca) {
  valor.valorAtual = novoValor;
  valor.voltaAoPadraoNoFimDaTroca = voltaAoPadraoNoFimDaTroca;

  registrarEvento(
    String("Simulado: ") + valor.nome + " = " + String(novoValor, 2)
    + (voltaAoPadraoNoFimDaTroca ? " (volta ao padrão quando a troca terminar)" : " (fixo)")
  );
}

/*
  Volta ao padrão os valores simulados marcados como temporários
  Chamada quando a troca de água termina
*/
void restaurarValoresSimuladosTemporarios() {
  ValorSimulado* valores[2] = {&tdsSimulado, &phSimulado};

  for (int indice = 0; indice < 2; indice++) {
    if (valores[indice]->voltaAoPadraoNoFimDaTroca) {
      definirValorSimulado(*valores[indice], valores[indice]->valorPadrao, false);
    }
  }
}

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
  Explica em texto por que a medição não é permitida agora
*/
const char* motivoDaMedicaoNaoPermitida() {
  if (bebedouroNoMinimo) {
    return "bloqueada: bebedouro no mínimo (sensores fora da água)";
  }
  if (bloqueioPosTrocaAtivo) {
    return "bloqueada: bloqueio pós-troca (água estabilizando)";
  }
  if (gatoPresente) {
    return "bloqueada: gato presente";
  }
  if (algumaBombaEstaLigada()) {
    return "bloqueada: bomba ligada";
  }
  return "bloqueada: esperando bombas desligadas por 10 s";
}

/*
  "Mede" TDS e pH de forma alternada (um por intervalo), copiando o valor simulado
  Sem medição permitida, mantém o último valor, exceto com o bebedouro no mínimo
*/
void medirQualidadeDaAgua() {
  // termina o bloqueio pós-troca quando o tempo de estabilização passa
  if (bloqueioPosTrocaAtivo && (millis() - instanteDoFimDaTrocaMs >= TEMPO_DE_ESTABILIZACAO_APOS_TROCA_MS)) {
    bloqueioPosTrocaAtivo = false;
    registrarEvento("Fim do bloqueio pós-troca");
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
  instanteDaUltimaMedicaoMs = millis();

  // alterna entre TDS e pH
  if (proximaMedicaoEhTds) {
    tdsEmPpm = tdsSimulado.valorAtual;
    tdsValido = true;
  } else {
    ph = phSimulado.valorAtual;
    phValido = true;
  }
  proximaMedicaoEhTds = !proximaMedicaoEhTds;
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
    if (!aguaTemQualidadeRuim()) {
      // qualidade voltou ao normal antes do tempo: a contagem zera
      qualidadeRuimEmAnalise = false;
    } else if (leituraDeQualidadePermitida) {
      if (!qualidadeRuimEmAnalise) {
        qualidadeRuimEmAnalise = true;
        instanteDoInicioDaQualidadeRuimMs = millis();
        registrarEvento("Qualidade ruim detectada: confirmando por 15 s");
      } else if (millis() - instanteDoInicioDaQualidadeRuimMs >= TEMPO_DE_CONFIRMACAO_DE_QUALIDADE_RUIM_MS) {
        estadoDaTroca = ESTADO_AGUARDANDO;
        qualidadeRuimEmAnalise = false;
        registrarEvento("Qualidade ruim confirmada: estado AGUARDANDO");
      }
    }
  }

  // C7: descarte terminado (bebedouro no mínimo) passa para o reabastecimento
  if (((estadoDaTroca == ESTADO_DESCARTANDO) || (estadoDaTroca == ESTADO_AGUARDANDO)) && bebedouroNoMinimo) {
    estadoDaTroca = ESTADO_REABASTECENDO;
    registrarEvento("Descarte terminado: estado REABASTECENDO");
  }

  // C8: bebedouro cheio de novo, volta ao normal e bloqueia a medição por um tempo
  if ((estadoDaTroca == ESTADO_REABASTECENDO) && bebedouroNoMaximo) {
    estadoDaTroca = ESTADO_NORMAL;
    bloqueioPosTrocaAtivo = true;
    instanteDoFimDaTrocaMs = millis();
    tdsValido = false;
    phValido = false;
    qualidadeRuimEmAnalise = false;
    registrarEvento("Reabastecimento terminado: estado NORMAL (bloqueio de 60 s)");
    restaurarValoresSimuladosTemporarios();
  }
}

// ===============================================================
// LEDs, buzzer e comunicação com a placa 3
// ===============================================================

/*
  Define os dois LEDs de um par (verde e alerta)
  Argumentos:
    numeroDoPar: número do par, de 1 a 5
    verdeAceso: estado do LED verde
    alertaAceso: estado do LED de alerta
*/
void definirParDeLeds(
  int numeroDoPar,
  bool verdeAceso,
  bool alertaAceso
) {
  ledsAcesos[INDICE_DO_PRIMEIRO_LED_VERDE + numeroDoPar - 1] = verdeAceso;
  ledsAcesos[INDICE_DO_PRIMEIRO_LED_AMARELO + numeroDoPar - 1] = alertaAceso;
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

  // par 5: bebedouro (alerta no mínimo ou enquanto reabastece)
  bool bebedouroComAtencao = bebedouroNoMinimo || (estadoDaTroca == ESTADO_REABASTECENDO);
  definirParDeLeds(5, !bebedouroComAtencao, bebedouroComAtencao);

  // LED de troca: aceso na troca pendente ou em andamento e na falha
  // (na falha a placa 3 pisca esse LED, usando o campo falha da mensagem)
  ledsAcesos[INDICE_DO_LED_VERMELHO] = (estadoDaTroca != ESTADO_NORMAL) || falhaDetectada;
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

      registrarEvento(
        String("Sinal do buzzer: padrão ") + alerta.padrao + " (contador " + contadorDeSinaisDoBuzzer + ")"
      );
      return;
    }
  }
}

/*
  Envia o estado para a placa 3 pela UART, uma linha de texto por envio
  Formato: $B,<11 LEDs em 0/1>,<padrão do buzzer>,<contador do buzzer>,<TDS>,<pH x100>,<estado>,<falha>
  LEDs na ordem: LED de troca, verdes 1 a 5, alertas 1 a 5
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
  ultimaLinhaEnviada = linha;
}

/*
  Registra tudo que a placa 3 enviar de volta (útil para testar o cabo da UART)
*/
void mostrarMensagensDaPlaca3() {
  while (Serial2.available() > 0) {
    char caractere = Serial2.read();
    if (caractere == '\n') {
      if (linhaRecebidaDaPlaca3.length() > 0) {
        registrarEvento(String("Placa 3 enviou: ") + linhaRecebidaDaPlaca3);
        linhaRecebidaDaPlaca3 = "";
      }
    } else if (caractere != '\r' && linhaRecebidaDaPlaca3.length() < 60) {
      linhaRecebidaDaPlaca3 += caractere;
    }
  }
}

// ===============================================================
// comandos (página web e monitor serial)
// ===============================================================

/*
  Executa um comando de uma letra, vindo da página web ou do monitor serial
  Comandos t e p usam o número recebido em argumento (ex.: t com 150, p com 5.5)
  Argumentos:
    tecla: letra do comando
    argumento: número em texto, usado só por t e p
*/
void executarComando(char tecla, const String& argumento) {
  switch (tecla) {
    case 'a':
      definirValorSimulado(tdsSimulado, TDS_PADRAO_EM_PPM, false);
      break;
    case 'b':
      definirValorSimulado(tdsSimulado, TDS_ALTO_EM_PPM, false);
      break;
    case 'c':
      definirValorSimulado(tdsSimulado, TDS_ALTO_EM_PPM, true);
      break;
    case 'd':
      definirValorSimulado(phSimulado, PH_PADRAO, false);
      break;
    case 'e':
      definirValorSimulado(phSimulado, PH_ALTO, false);
      break;
    case 'f':
      definirValorSimulado(phSimulado, PH_ALTO, true);
      break;
    case 't':
    case 'p': {
      String numeroDigitado = argumento;
      numeroDigitado.trim();
      if (numeroDigitado.length() == 0) {
        registrarEvento("Faltou o número do comando");
        break;
      }
      float novoValor = numeroDigitado.toFloat();
      if (tecla == 't') {
        definirValorSimulado(tdsSimulado, constrain(novoValor, 0.0f, 1000.0f), false);
      } else {
        definirValorSimulado(phSimulado, constrain(novoValor, 0.0f, 14.0f), false);
      }
      break;
    }
    case 'x':
      falhaDetectada = true;
      registrarEvento("FALHA simulada: bombas desligadas, só sai reiniciando o ESP");
      break;
    case 'i':
      nivelLogicoComSinal = (nivelLogicoComSinal == HIGH) ? LOW : HIGH;
      configurarPinosDasBoias();
      registrarEvento(
        String("Lógica das boias: sinal em nível ") + (nivelLogicoComSinal == HIGH ? "ALTO (pull-down)" : "BAIXO (pull-up)")
      );
      break;
    case 's': {
      int pinoAuxiliar = boiaDoReservatorioLimpo.pino;
      boiaDoReservatorioLimpo.pino = boiaDoReservatorioSujo.pino;
      boiaDoReservatorioSujo.pino = pinoAuxiliar;
      registrarEvento(
        String("GPIO trocados: r1 limpo = GPIO") + boiaDoReservatorioLimpo.pino
        + ", r2 sujo = GPIO" + boiaDoReservatorioSujo.pino
      );
      break;
    }
    default:
      break;
  }
}

/*
  Lê comandos digitados no monitor serial (opcional, só com computador ligado)
*/
void tratarComandosDoMonitorSerial() {
  while (Serial.available() > 0) {
    char tecla = Serial.read();
    String argumento = "";
    if (tecla == 't' || tecla == 'p') {
      argumento = Serial.readStringUntil('\n');
    }
    executarComando(tecla, argumento);
  }
}

// ===============================================================
// painel web
// ===============================================================

/*
  Explica em texto o que o sistema espera que aconteça agora (ajuda a lembrar de mexer nas boias)
*/
const char* descreverProximoPasso() {
  if (falhaDetectada) {
    return "FALHA ativa: só sai reiniciando o ESP (desligar e ligar)";
  }

  switch (estadoDaTroca) {
    case ESTADO_AGUARDANDO:
      if (bebedouroNoMinimo) {
        return "AGUARDANDO: bebedouro no mínimo, passa direto para REABASTECENDO";
      }
      if (reservatorioSujoNoMaximo) {
        return "AGUARDANDO: r2 cheio, o descarte não começa (ABRA a boia do r2)";
      }
      if (gatoPresente) {
        return "AGUARDANDO: tire a mão do sensor (descarte precisa de 6 s de ausência)";
      }
      return "AGUARDANDO: o descarte deve começar em instantes";

    case ESTADO_DESCARTANDO:
      if (bombaDeDescarte.ligada) {
        return "DESCARTANDO: bomba2 ligada. Para terminar, FECHE a boia de mínimo do bebedouro";
      }
      if (reservatorioSujoNoMaximo) {
        return "DESCARTANDO: pausado, r2 cheio. ABRA a boia do r2 para retomar";
      }
      return "DESCARTANDO: esperando gato ausente por 6 s e outras bombas desligadas";

    case ESTADO_REABASTECENDO:
      if (bombaDeReposicao.ligada) {
        return "REABASTECENDO: bomba1 ligada. Para terminar, ABRA a boia de mínimo e FECHE a de máximo do bebedouro";
      }
      if (reservatorioLimpoNoMinimo) {
        return "REABASTECENDO: parado, r1 sem água (ABRA a boia do r1)";
      }
      return "REABASTECENDO: a bomba1 deve ligar em instantes";

    default:
      if (bloqueioPosTrocaAtivo) {
        return "NORMAL: bloqueio pós-troca, sem medir até acabar";
      }
      if (qualidadeRuimEmAnalise) {
        return "NORMAL: qualidade ruim em análise (precisa se manter por 15 s)";
      }
      return "NORMAL: nada pendente";
  }
}

/*
  Converte verdadeiro/falso para texto JSON
  Argumentos:
    valor: valor a ser convertido
*/
String paraJson(bool valor) {
  return valor ? "true" : "false";
}

/*
  Monta o JSON com todo o estado do sistema, lido pela página a cada segundo
*/
String montarJson() {
  String json = "{";
  json += "\"t\":" + String(millis() / 1000.0, 0);
  json += ",\"estado\":\"" + String(nomeDoEstado(estadoDaTroca)) + "\"";
  json += ",\"estadoNum\":" + String((int)estadoDaTroca);
  json += ",\"falha\":" + paraJson(falhaDetectada);
  json += ",\"passo\":\"" + String(descreverProximoPasso()) + "\"";

  json += ",\"bombas\":[" + paraJson(bombaDeReposicao.ligada) + "," + paraJson(bombaDeDescarte.ligada)
          + "," + paraJson(bombaDeCirculacao.ligada) + "]";

  json += ",\"bebMin\":" + paraJson(bebedouroNoMinimo);
  json += ",\"bebMax\":" + paraJson(bebedouroNoMaximo);
  json += ",\"r2Max\":" + paraJson(reservatorioSujoNoMaximo);
  json += ",\"r1Min\":" + paraJson(reservatorioLimpoNoMinimo);
  json += ",\"pinos\":[" + String(boiaDoBebedouroNoMinimo.pino) + "," + String(boiaDoBebedouroNoMaximo.pino)
          + "," + String(boiaDoReservatorioSujo.pino) + "," + String(boiaDoReservatorioLimpo.pino) + "]";
  json += ",\"sinalAlto\":" + paraJson(nivelLogicoComSinal == HIGH);

  json += ",\"gato\":" + paraJson(gatoPresente);
  json += ",\"dist\":" + String(distanciaDoGatoEmCentimetros, 1);

  json += ",\"tdsMedido\":" + (tdsValido ? String(tdsEmPpm, 0) : String("null"));
  json += ",\"tdsSim\":" + String(tdsSimulado.valorAtual, 0);
  json += ",\"tdsTemp\":" + paraJson(tdsSimulado.voltaAoPadraoNoFimDaTroca);
  json += ",\"phMedido\":" + (phValido ? String(ph, 2) : String("null"));
  json += ",\"phSim\":" + String(phSimulado.valorAtual, 2);
  json += ",\"phTemp\":" + paraJson(phSimulado.voltaAoPadraoNoFimDaTroca);
  json += ",\"medicao\":\"" + String(leituraDeQualidadePermitida ? "permitida" : motivoDaMedicaoNaoPermitida()) + "\"";

  json += ",\"leds\":[";
  for (int indice = 0; indice < QUANTIDADE_DE_LEDS; indice++) {
    json += ledsAcesos[indice] ? "1" : "0";
    if (indice < QUANTIDADE_DE_LEDS - 1) {
      json += ",";
    }
  }
  json += "]";
  json += ",\"buzzerPadrao\":" + String(ultimoPadraoDoBuzzer);
  json += ",\"buzzerContador\":" + String(contadorDeSinaisDoBuzzer);
  json += ",\"uart\":\"" + ultimaLinhaEnviada + "\"";

  json += ",\"log\":[";
  for (int indice = 0; indice < totalDeEventosGuardados; indice++) {
    int posicao = (proximaPosicaoDoRegistro - totalDeEventosGuardados + indice + TAMANHO_DO_REGISTRO) % TAMANHO_DO_REGISTRO;
    if (indice > 0) {
      json += ",";
    }
    json += "\"" + registroDeEventos[posicao] + "\"";
  }
  json += "]}";

  return json;
}

/*
  Entrega a página principal
*/
void atenderPaginaPrincipal() {
  servidorWeb.send_P(200, "text/html; charset=utf-8", PAGINA_HTML);
}

/*
  Entrega o estado do sistema em JSON
*/
void atenderEstado() {
  servidorWeb.sendHeader("Cache-Control", "no-store");
  servidorWeb.send(200, "application/json; charset=utf-8", montarJson());
}

/*
  Recebe um comando da página (endereço /cmd?c=<letra>&v=<número>)
*/
void atenderComando() {
  String comando = servidorWeb.arg("c");
  String valor = servidorWeb.arg("v");

  if (comando.length() > 0) {
    executarComando(comando[0], valor);
  }
  servidorWeb.send(200, "text/plain", "ok");
}

/*
  Cria a rede Wi-Fi e liga o servidor web
*/
void iniciarPainelWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(NOME_DA_REDE_WIFI);

  servidorWeb.on("/", HTTP_GET, atenderPaginaPrincipal);
  servidorWeb.on("/estado", HTTP_GET, atenderEstado);
  servidorWeb.on("/cmd", HTTP_GET, atenderComando);
  servidorWeb.begin();
}

// ===============================================================
// programa principal
// ===============================================================

/*
  Configura pinos, UART da placa 3 e painel web
*/
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(200);
  delay(300);

  // bombas começam desligadas antes de virarem saída
  Bomba* bombas[3] = {&bombaDeReposicao, &bombaDeDescarte, &bombaDeCirculacao};
  for (int indice = 0; indice < 3; indice++) {
    digitalWrite(bombas[indice]->pinoDoMosfet, !NIVEL_LOGICO_BOMBA_LIGADA);
    pinMode(bombas[indice]->pinoDoMosfet, OUTPUT);
  }

  // boias
  configurarPinosDasBoias();

  // ultrassônico
  pinMode(PINO_ULTRASSONICO_GATILHO, OUTPUT);
  pinMode(PINO_ULTRASSONICO_ECO, INPUT);

  // UART com a placa 3
  int pinoDeRecepcao = TROCAR_TX_E_RX_DA_UART ? PINO_UART_TRANSMISSAO : PINO_UART_RECEPCAO;
  int pinoDeTransmissao = TROCAR_TX_E_RX_DA_UART ? PINO_UART_RECEPCAO : PINO_UART_TRANSMISSAO;
  Serial2.begin(VELOCIDADE_DA_UART_PLACA_3, SERIAL_8N1, pinoDeRecepcao, pinoDeTransmissao);

  iniciarPainelWeb();

  registrarEvento(String("Motivo do último reinício: ") + descreverMotivoDoReinicio(esp_reset_reason()));
  registrarEvento(String("Wi-Fi: rede ") + NOME_DA_REDE_WIFI + ", endereço http://" + WiFi.softAPIP().toString());
  registrarEvento(
    String("UART da placa 3: TX=GPIO") + pinoDeTransmissao + " RX=GPIO" + pinoDeRecepcao
  );
}

/*
  Repete a leitura dos sensores, a lógica de troca, o controle das bombas e as saídas
*/
void loop() {
  // painel web, comandos e retorno da placa 3
  servidorWeb.handleClient();
  tratarComandosDoMonitorSerial();
  mostrarMensagensDaPlaca3();

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
}