// teste de bancada do bebedouro
// objetivo: verificar sensores e bombas SEM a lógica automática
//
// ao ligar (ou apertar o botão EN) o ESP32 faz uma rodada de teste das bombas, ainda SEM Wi-Fi:
//   espera 10 s, bomba1 por 5 s, espera 5 s, bomba2 por 5 s, espera 5 s, bomba3 por 5 s
// depois sobe a rede Wi-Fi "Bebedouro-Teste" com uma página que mostra as leituras
// e tem botões para ligar cada bomba por 5 s (uma de cada vez)
//
// acompanhar também o monitor serial (115200), que mostra o motivo do último reinício

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>

// ===============================================================
// pinos (conforme o esquemático da placa 2)
// ===============================================================

// gates dos MOSFETs das bombas
const int QUANTIDADE_DE_BOMBAS = 3;
const int PINOS_DAS_BOMBAS[QUANTIDADE_DE_BOMBAS] = {25, 26, 27};
const char* NOMES_DAS_BOMBAS[QUANTIDADE_DE_BOMBAS] = {"Bomba 1 (GPIO25)", "Bomba 2 (GPIO26)", "Bomba 3 (GPIO27)"};

// boias de nível (nomes do esquemático, sem assumir qual é qual)
const int QUANTIDADE_DE_BOIAS = 4;
const int PINOS_DAS_BOIAS[QUANTIDADE_DE_BOIAS] = {19, 18, 23, 21};

// sensores
const int PINO_SENSOR_PH = 36;
const int PINO_SENSOR_TDS = 34;
const int PINO_ULTRASSONICO_GATILHO = 32;
const int PINO_ULTRASSONICO_ECO = 33;

// ===============================================================
// parâmetros
// ===============================================================

// nível lógico que liga o MOSFET da bomba (IRL540N, canal N: nível alto liga)
const int NIVEL_LOGICO_BOMBA_LIGADA = HIGH;

// tempos do teste
const int SEGUNDOS_DE_ESPERA_INICIAL = 10;
const int SEGUNDOS_DE_ESPERA_ENTRE_BOMBAS = 5;
const int SEGUNDOS_DE_BOMBA_LIGADA = 5;

// calibração do pH com 2 pontos (pH 7,20 a 2,622 V e pH 6,20 a 2,688 V)
const float INCLINACAO_DA_CALIBRACAO_DO_PH = -15.04;
const float DESLOCAMENTO_DA_CALIBRACAO_DO_PH = 46.63;

// temperatura usada na compensação do TDS
const float TEMPERATURA_DA_AGUA_EM_CELSIUS = 25.0;

// presença
const float DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS = 30.0;
const float DISTANCIA_SEM_ECO_EM_CENTIMETROS = 400.0;
const unsigned long TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS = 25000;
const unsigned long INTERVALO_ENTRE_LEITURAS_DE_PRESENCA_MS = 200;

// amostragem dos sensores analógicos
const int QUANTIDADE_DE_AMOSTRAS = 20;
const int AMOSTRAS_DESCARTADAS_EM_CADA_EXTREMO = 4;
const unsigned long PAUSA_ENTRE_AMOSTRAS_MS = 5;
const unsigned long INTERVALO_ENTRE_MEDICOES_ANALOGICAS_MS = 1000;

// rede Wi-Fi do teste
const char* NOME_DA_REDE_DO_TESTE = "Bebedouro-Teste";
const char* SENHA_DA_REDE_DO_TESTE = "bebedouro123";

// ===============================================================
// variáveis globais
// ===============================================================

WebServer servidorWeb(80);

// bombas
bool bombaLigada[QUANTIDADE_DE_BOMBAS] = {false, false, false};
unsigned long instanteDeDesligamentoMs[QUANTIDADE_DE_BOMBAS] = {0, 0, 0};

// boias: false = pull-down interno (sinal = nível alto), true = pull-up interno (sinal = nível baixo)
bool boiasComPullUp = false;

// leituras
float tensaoDoTdsEmVolts = 0;
float tdsEmPpm = 0;
float tensaoDoPhEmVolts = 0;
float ph = 0;
float distanciaEmCentimetros = DISTANCIA_SEM_ECO_EM_CENTIMETROS;
bool gatoPresente = false;
bool proximaMedicaoEhTds = true;
unsigned long instanteDaUltimaMedicaoAnalogicaMs = 0;
unsigned long instanteDaUltimaLeituraDePresencaMs = 0;

// motivo do último reinício, guardado como texto
String motivoDoUltimoReinicio = "";

// ===============================================================
// página HTML
// ===============================================================

const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Teste de bancada</title>
<style>
  body { font-family: sans-serif; background: #f2f4f7; margin: 0; padding: 14px; color: #222; }
  h1 { font-size: 1.25rem; margin: 0 0 10px 0; }
  .cartao { background: #fff; border-radius: 12px; padding: 12px; margin-bottom: 12px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
  .cartao h2 { font-size: 1rem; margin: 0 0 8px 0; color: #444; }
  .linha { display: flex; justify-content: space-between; align-items: center; padding: 7px 0;
           border-bottom: 1px solid #eee; gap: 8px; }
  .linha:last-child { border-bottom: none; }
  .etiqueta { padding: 3px 10px; border-radius: 12px; color: #fff; font-size: 0.9rem; white-space: nowrap; }
  .verde { background: #2e9e4f; }
  .vermelho { background: #d63a3a; }
  .cinza { background: #888; }
  .amarelo { background: #e0a800; }
  button { font-size: 0.95rem; padding: 8px 12px; border-radius: 8px; border: 1px solid #bbb; background: #fafafa; }
  .nota { font-size: 0.78rem; color: #777; margin-top: 6px; }
  .grande { font-size: 1.05rem; font-weight: bold; }
</style>
</head>
<body>
  <h1>Teste de bancada do bebedouro</h1>

  <div class="cartao">
    <h2>Bombas</h2>
    <div class="linha"><span>Bomba 1 (GPIO25)</span><span id="bomba0" class="etiqueta cinza">...</span><button onclick="enviar('/bomba?numero=1')">Ligar 5 s</button></div>
    <div class="linha"><span>Bomba 2 (GPIO26)</span><span id="bomba1" class="etiqueta cinza">...</span><button onclick="enviar('/bomba?numero=2')">Ligar 5 s</button></div>
    <div class="linha"><span>Bomba 3 (GPIO27)</span><span id="bomba2" class="etiqueta cinza">...</span><button onclick="enviar('/bomba?numero=3')">Ligar 5 s</button></div>
    <div style="margin-top:8px"><button onclick="enviar('/desligar')">Desligar todas</button></div>
    <div class="nota">Uma bomba por vez. Cada botão ignora qualquer lógica e liga o pino por 5 s</div>
  </div>

  <div class="cartao">
    <h2>Sensores de nível (boias)</h2>
    <div class="linha"><span>NIVEL_R1_BAIXO (GPIO19)</span><span id="boia0" class="etiqueta cinza">...</span></div>
    <div class="linha"><span>NIVEL_R1_ALTO (GPIO18)</span><span id="boia1" class="etiqueta cinza">...</span></div>
    <div class="linha"><span>NIVEL_R2 (GPIO23)</span><span id="boia2" class="etiqueta cinza">...</span></div>
    <div class="linha"><span>NIVEL_R3 (GPIO21)</span><span id="boia3" class="etiqueta cinza">...</span></div>
    <div class="linha"><span id="modoDasBoias">...</span><button onclick="enviar('/trocarmodo')">Trocar modo</button></div>
    <div class="nota">FECHADA = contato fechado (sinal). Se aparecer o contrário do real, toque em "Trocar modo"</div>
  </div>

  <div class="cartao">
    <h2>Qualidade da água</h2>
    <div class="linha"><span>TDS</span><span class="grande" id="tds">...</span></div>
    <div class="linha"><span>Tensão do TDS</span><span id="tdsTensao">...</span></div>
    <div class="linha"><span>pH</span><span class="grande" id="ph">...</span></div>
    <div class="linha"><span>Tensão do pH</span><span id="phTensao">...</span></div>
  </div>

  <div class="cartao">
    <h2>Presença (HC-SR04)</h2>
    <div class="linha"><span>Estado</span><span id="presenca" class="etiqueta cinza">...</span></div>
    <div class="linha"><span>Distância</span><span id="distancia">...</span></div>
  </div>

  <div class="cartao">
    <h2>Diagnóstico do ESP32</h2>
    <div class="linha"><span>Tempo ligado</span><span id="tempoLigado">...</span></div>
    <div class="linha"><span>Motivo do último reinício</span><span id="motivoDoReinicio">...</span></div>
    <div class="nota">Se o "tempo ligado" voltar a zero ao ligar uma bomba, o ESP32 reiniciou (problema de alimentação)</div>
    <div class="nota">Conexão: <span id="conexao">...</span></div>
  </div>

<script>
  // define o texto e a cor de uma etiqueta
  function definirEtiqueta(
    identificador,
    texto,
    cor
  ) {
    const elemento = document.getElementById(identificador);
    elemento.textContent = texto;
    elemento.className = 'etiqueta ' + cor;
  }

  // define o texto simples de um campo
  function definirTexto(
    identificador,
    texto
  ) {
    document.getElementById(identificador).textContent = texto;
  }

  // envia um comando ao ESP32 e atualiza a tela
  async function enviar(endereco) {
    try {
      await fetch(endereco);
    } catch (erro) {
      definirTexto('conexao', 'comando não enviado');
    }
    atualizar();
  }

  // pede os dados ao ESP32 e atualiza a tela
  async function atualizar() {
    try {
      const resposta = await fetch('/dados');
      const dados = await resposta.json();

      for (let indice = 0; indice < 3; indice++) {
        definirEtiqueta('bomba' + indice, dados.bombas[indice] ? 'LIGADA' : 'desligada', dados.bombas[indice] ? 'verde' : 'cinza');
      }
      for (let indice = 0; indice < 4; indice++) {
        const texto = (dados.boiasFechadas[indice] ? 'FECHADA' : 'aberta') + ' (bruto ' + dados.boiasBrutas[indice] + ')';
        definirEtiqueta('boia' + indice, texto, dados.boiasFechadas[indice] ? 'verde' : 'cinza');
      }
      definirTexto('modoDasBoias', dados.modoDasBoias);

      definirTexto('tds', dados.tdsEmPpm + ' ppm');
      definirTexto('tdsTensao', dados.tensaoDoTds + ' V');
      definirTexto('ph', dados.ph);
      definirTexto('phTensao', dados.tensaoDoPh + ' V');

      definirEtiqueta('presenca', dados.gatoPresente ? 'PRESENTE' : 'ausente', dados.gatoPresente ? 'amarelo' : 'cinza');
      definirTexto('distancia', dados.distancia + ' cm');

      definirTexto('tempoLigado', dados.tempoLigadoEmSegundos + ' s');
      definirTexto('motivoDoReinicio', dados.motivoDoUltimoReinicio);
      definirTexto('conexao', 'conectado');
    } catch (erro) {
      definirTexto('conexao', 'sem resposta do ESP32');
    }
  }

  setInterval(atualizar, 1000);
  atualizar();
</script>
</body>
</html>
)rawliteral";

// ===============================================================
// bombas
// ===============================================================

/*
  Liga ou desliga uma bomba pelo pino do MOSFET
  Argumentos:
    indiceDaBomba: 0, 1 ou 2
    ligar: verdadeiro para ligar
*/
void definirBomba(
  int indiceDaBomba,
  bool ligar
) {
  bombaLigada[indiceDaBomba] = ligar;
  digitalWrite(
    PINOS_DAS_BOMBAS[indiceDaBomba],
    ligar ? NIVEL_LOGICO_BOMBA_LIGADA : !NIVEL_LOGICO_BOMBA_LIGADA
  );
  Serial.print(NOMES_DAS_BOMBAS[indiceDaBomba]);
  Serial.println(ligar ? " LIGADA" : " desligada");
}

/*
  Desliga todas as bombas
*/
void desligarTodasAsBombas() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    if (bombaLigada[indice]) {
      definirBomba(indice, false);
    }
  }
}

/*
  Liga uma bomba por alguns segundos, desligando antes as outras (uma por vez)
  O desligamento acontece em atualizarBombasTemporizadas()
  Argumentos:
    indiceDaBomba: 0, 1 ou 2
    duracaoEmSegundos: tempo ligada
*/
void ligarBombaPorTempo(
  int indiceDaBomba,
  int duracaoEmSegundos
) {
  desligarTodasAsBombas();
  definirBomba(indiceDaBomba, true);
  instanteDeDesligamentoMs[indiceDaBomba] = millis() + duracaoEmSegundos * 1000UL;
}

/*
  Desliga as bombas cujo tempo terminou
*/
void atualizarBombasTemporizadas() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    if (bombaLigada[indice] && millis() >= instanteDeDesligamentoMs[indice]) {
      definirBomba(indice, false);
    }
  }
}

/*
  Espera alguns segundos, mostrando a contagem no monitor serial
  Argumentos:
    texto: o que está sendo esperado
    segundos: quantidade de segundos
*/
void esperarComContagem(
  const char* texto,
  int segundos
) {
  for (int restante = segundos; restante > 0; restante--) {
    Serial.print(texto);
    Serial.print(": ");
    Serial.print(restante);
    Serial.println(" s");
    delay(1000);
  }
}

/*
  Rodada de teste das bombas, feita uma única vez no início e sem Wi-Fi ligado
  Se as bombas funcionarem aqui mas não pela página, o problema pode ser o Wi-Fi puxando corrente
*/
void executarSequenciaDeTesteDasBombas() {
  Serial.println("=== Sequência de teste das bombas (sem Wi-Fi) ===");
  esperarComContagem("Início da sequência em", SEGUNDOS_DE_ESPERA_INICIAL);

  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    definirBomba(indice, true);
    esperarComContagem("Ligada, restam", SEGUNDOS_DE_BOMBA_LIGADA);
    definirBomba(indice, false);

    if (indice < QUANTIDADE_DE_BOMBAS - 1) {
      esperarComContagem("Pausa", SEGUNDOS_DE_ESPERA_ENTRE_BOMBAS);
    }
  }
  Serial.println("=== Fim da sequência ===");
}

// ===============================================================
// sensores
// ===============================================================

/*
  Configura os pinos das boias conforme o modo atual (pull-down ou pull-up interno)
*/
void configurarPinosDasBoias() {
  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    pinMode(PINOS_DAS_BOIAS[indice], boiasComPullUp ? INPUT_PULLUP : INPUT_PULLDOWN);
  }
}

/*
  Informa se a boia está fechada (enviando sinal) no modo atual
  Argumentos:
    indiceDaBoia: 0 a 3, na ordem de PINOS_DAS_BOIAS
*/
bool boiaEstaFechada(int indiceDaBoia) {
  int nivelDeSinal = boiasComPullUp ? LOW : HIGH;
  return digitalRead(PINOS_DAS_BOIAS[indiceDaBoia]) == nivelDeSinal;
}

/*
  Lê um pino analógico várias vezes e devolve a tensão filtrada em volts
  Ordena as amostras, descarta as extremas e faz a média das centrais
  Argumentos:
    pino: pino analógico (ADC1)
*/
float lerTensaoFiltradaEmVolts(int pino) {
  float amostras[QUANTIDADE_DE_AMOSTRAS];

  // coleta as amostras
  for (int indice = 0; indice < QUANTIDADE_DE_AMOSTRAS; indice++) {
    amostras[indice] = analogReadMilliVolts(pino) / 1000.0;
    delay(PAUSA_ENTRE_AMOSTRAS_MS);
  }

  // ordena em ordem crescente
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
  Converte a tensão do sensor TDS em ppm (fórmula padrão do módulo TDS Meter V1.0)
  Argumentos:
    tensaoEmVolts: tensão filtrada lida no sensor
*/
float calcularTdsPelaTensao(float tensaoEmVolts) {
  float coeficienteDeCompensacao = 1.0 + 0.02 * (TEMPERATURA_DA_AGUA_EM_CELSIUS - 25.0);
  float tensaoCompensada = tensaoEmVolts / coeficienteDeCompensacao;
  float tensaoAoQuadrado = tensaoCompensada * tensaoCompensada;
  float tensaoAoCubo = tensaoAoQuadrado * tensaoCompensada;
  return (133.42 * tensaoAoCubo - 255.86 * tensaoAoQuadrado + 857.39 * tensaoCompensada) * 0.5;
}

/*
  Converte a tensão do sensor de pH em pH, usando a calibração de 2 pontos
  Argumentos:
    tensaoEmVolts: tensão filtrada lida no sensor
*/
float calcularPhPelaTensao(float tensaoEmVolts) {
  return INCLINACAO_DA_CALIBRACAO_DO_PH * tensaoEmVolts + DESLOCAMENTO_DA_CALIBRACAO_DO_PH;
}

/*
  Mede TDS e pH de forma alternada, um por intervalo
*/
void atualizarMedicoesAnalogicas() {
  if (millis() - instanteDaUltimaMedicaoAnalogicaMs < INTERVALO_ENTRE_MEDICOES_ANALOGICAS_MS) {
    return;
  }

  if (proximaMedicaoEhTds) {
    tensaoDoTdsEmVolts = lerTensaoFiltradaEmVolts(PINO_SENSOR_TDS);
    tdsEmPpm = calcularTdsPelaTensao(tensaoDoTdsEmVolts);
  } else {
    tensaoDoPhEmVolts = lerTensaoFiltradaEmVolts(PINO_SENSOR_PH);
    ph = calcularPhPelaTensao(tensaoDoPhEmVolts);
  }
  proximaMedicaoEhTds = !proximaMedicaoEhTds;
  instanteDaUltimaMedicaoAnalogicaMs = millis();
}

/*
  Mede a distância do ultrassônico em centímetros
  Retorna DISTANCIA_SEM_ECO_EM_CENTIMETROS quando nenhum eco volta
*/
float medirDistanciaEmCentimetros() {
  digitalWrite(PINO_ULTRASSONICO_GATILHO, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_ULTRASSONICO_GATILHO, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_ULTRASSONICO_GATILHO, LOW);

  unsigned long duracaoDoEcoEmMicrossegundos = pulseIn(
    PINO_ULTRASSONICO_ECO,
    HIGH,
    TEMPO_LIMITE_DO_ECO_EM_MICROSSEGUNDOS
  );
  if (duracaoDoEcoEmMicrossegundos == 0) {
    return DISTANCIA_SEM_ECO_EM_CENTIMETROS;
  }
  return duracaoDoEcoEmMicrossegundos * 0.0343 / 2.0;
}

/*
  Atualiza a distância e a presença a cada intervalo fixo
*/
void atualizarPresenca() {
  if (millis() - instanteDaUltimaLeituraDePresencaMs < INTERVALO_ENTRE_LEITURAS_DE_PRESENCA_MS) {
    return;
  }
  instanteDaUltimaLeituraDePresencaMs = millis();
  distanciaEmCentimetros = medirDistanciaEmCentimetros();
  gatoPresente = distanciaEmCentimetros < DISTANCIA_MAXIMA_DE_PRESENCA_EM_CENTIMETROS;
}

// ===============================================================
// diagnóstico do ESP32
// ===============================================================

/*
  Traduz o motivo do último reinício do ESP32 para texto
  Argumentos:
    motivo: código devolvido por esp_reset_reason()
*/
String descreverMotivoDoReinicio(esp_reset_reason_t motivo) {
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
// servidor web
// ===============================================================

/*
  Converte um booleano para o texto "true" ou "false" do JSON
  Argumentos:
    valor: booleano a converter
*/
String booleanoParaJson(bool valor) {
  return valor ? "true" : "false";
}

/*
  Monta o JSON com todos os dados do teste
*/
String montarJsonDosDados() {
  String json = "{";

  // bombas
  json += "\"bombas\":[";
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    json += (indice > 0 ? "," : "") + booleanoParaJson(bombaLigada[indice]);
  }
  json += "],";

  // boias (nível bruto e interpretação)
  json += "\"boiasBrutas\":[";
  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    json += (indice > 0 ? "," : "") + String(digitalRead(PINOS_DAS_BOIAS[indice]));
  }
  json += "],\"boiasFechadas\":[";
  for (int indice = 0; indice < QUANTIDADE_DE_BOIAS; indice++) {
    json += (indice > 0 ? "," : "") + booleanoParaJson(boiaEstaFechada(indice));
  }
  json += "],";
  json += String("\"modoDasBoias\":\"") + (boiasComPullUp ? "Modo: pull-up interno (sinal = nível baixo)" : "Modo: pull-down interno (sinal = nível alto)") + "\",";

  // qualidade da água
  json += "\"tdsEmPpm\":" + String(tdsEmPpm, 0) + ",";
  json += "\"tensaoDoTds\":" + String(tensaoDoTdsEmVolts, 3) + ",";
  json += "\"ph\":" + String(ph, 2) + ",";
  json += "\"tensaoDoPh\":" + String(tensaoDoPhEmVolts, 3) + ",";

  // presença
  json += "\"gatoPresente\":" + booleanoParaJson(gatoPresente) + ",";
  json += "\"distancia\":" + String(distanciaEmCentimetros, 1) + ",";

  // diagnóstico
  json += "\"tempoLigadoEmSegundos\":" + String(millis() / 1000) + ",";
  json += "\"motivoDoUltimoReinicio\":\"" + motivoDoUltimoReinicio + "\"";

  json += "}";
  return json;
}

/*
  Envia a página HTML (rota "/")
*/
void enviarPaginaPrincipal() {
  servidorWeb.send_P(
    200,
    "text/html; charset=utf-8",
    PAGINA_HTML
  );
}

/*
  Envia os dados em JSON (rota "/dados")
*/
void enviarDadosEmJson() {
  servidorWeb.send(
    200,
    "application/json",
    montarJsonDosDados()
  );
}

/*
  Responde "ok" depois de um comando
*/
void responderOk() {
  servidorWeb.send(
    200,
    "text/plain; charset=utf-8",
    "ok"
  );
}

/*
  Rota "/bomba?numero=N": liga a bomba N (1 a 3) por alguns segundos
*/
void receberComandoDeBomba() {
  int numeroDaBomba = servidorWeb.arg("numero").toInt();
  if (numeroDaBomba >= 1 && numeroDaBomba <= QUANTIDADE_DE_BOMBAS) {
    ligarBombaPorTempo(numeroDaBomba - 1, SEGUNDOS_DE_BOMBA_LIGADA);
  }
  responderOk();
}

/*
  Rota "/desligar": desliga todas as bombas
*/
void receberComandoParaDesligar() {
  desligarTodasAsBombas();
  responderOk();
}

/*
  Rota "/trocarmodo": alterna o modo das boias entre pull-down e pull-up interno
*/
void receberComandoParaTrocarModoDasBoias() {
  boiasComPullUp = !boiasComPullUp;
  configurarPinosDasBoias();
  responderOk();
}

/*
  Cria a rede Wi-Fi do ESP32 e inicia o servidor web
*/
void iniciarPaginaDeTeste() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(
    NOME_DA_REDE_DO_TESTE,
    SENHA_DA_REDE_DO_TESTE
  );

  servidorWeb.on("/", enviarPaginaPrincipal);
  servidorWeb.on("/dados", enviarDadosEmJson);
  servidorWeb.on("/bomba", receberComandoDeBomba);
  servidorWeb.on("/desligar", receberComandoParaDesligar);
  servidorWeb.on("/trocarmodo", receberComandoParaTrocarModoDasBoias);
  servidorWeb.begin();

  Serial.print("Página de teste em http://");
  Serial.println(WiFi.softAPIP());
}

// ===============================================================
// programa principal
// ===============================================================

/*
  Configura os pinos, roda a sequência de teste das bombas e sobe a página
*/
void setup() {
  Serial.begin(115200);
  delay(500);

  // motivo do último reinício (mostra brownout, se tiver acontecido)
  motivoDoUltimoReinicio = descreverMotivoDoReinicio(esp_reset_reason());
  Serial.print("Motivo do último reinício: ");
  Serial.println(motivoDoUltimoReinicio);

  // bombas começam desligadas antes de virarem saída
  for (int indice = 0; indice < QUANTIDADE_DE_BOMBAS; indice++) {
    digitalWrite(PINOS_DAS_BOMBAS[indice], !NIVEL_LOGICO_BOMBA_LIGADA);
    pinMode(PINOS_DAS_BOMBAS[indice], OUTPUT);
  }

  configurarPinosDasBoias();

  pinMode(PINO_ULTRASSONICO_GATILHO, OUTPUT);
  pinMode(PINO_ULTRASSONICO_ECO, INPUT);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // uma rodada de teste das bombas, ainda sem Wi-Fi
  executarSequenciaDeTesteDasBombas();

  iniciarPaginaDeTeste();
}

/*
  Atende a página, desliga bombas temporizadas e atualiza os sensores
*/
void loop() {
  servidorWeb.handleClient();
  atualizarBombasTemporizadas();
  atualizarPresenca();
  atualizarMedicoesAnalogicas();
}
