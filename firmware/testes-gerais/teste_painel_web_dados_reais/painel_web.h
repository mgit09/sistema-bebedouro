// painel web do bebedouro
// mostra leituras dos sensores, níveis dos reservatórios, bombas, estado da troca de água
// e uma simulação na tela dos LEDs e do buzzer da placa 3
// o ESP32 cria a própria rede Wi-Fi, então não depende da rede do local da apresentação

#ifndef PAINEL_WEB_H
#define PAINEL_WEB_H

#include <WiFi.h>
#include <WebServer.h>

// ---------------------------------------------------------------
// configurações
// ---------------------------------------------------------------

// nome e senha da rede Wi-Fi criada pelo ESP32 (senha com no mínimo 8 caracteres)
const char* NOME_DA_REDE_DO_PAINEL = "Bebedouro-Gatos";
const char* SENHA_DA_REDE_DO_PAINEL = "bebedouro123";

// porta padrão do protocolo HTTP
const uint16_t PORTA_DO_SERVIDOR_WEB = 80;

// quantidade de LEDs da placa 3
// ordem: posição 0 = vermelho, posições 1 a 5 = verdes, posições 6 a 10 = amarelos
const int QUANTIDADE_DE_LEDS = 11;

// ---------------------------------------------------------------
// dados exibidos no painel
// o programa principal preenche esta estrutura e o painel só lê
// ---------------------------------------------------------------

struct DadosDoPainel {
  // leituras dos sensores
  float tdsEmPpm = 0;
  bool tdsValido = false;
  float ph = 0;
  bool phValido = false;
  float distanciaDoGatoEmCentimetros = 0;
  bool gatoPresente = false;

  // situação da medição de qualidade da água
  bool bloqueioPosTrocaAtivo = false;          // aguardando a água estabilizar após uma troca
  bool leituraDeQualidadePermitida = false;    // água parada e sensores submersos

  // níveis (verdadeiro quando a condição descrita está ocorrendo)
  bool reservatorioLimpoNoMinimo = false;      // precisa repor água limpa
  bool reservatorioSujoNoMaximo = false;       // precisa esvaziar água suja
  bool bebedouroNoMinimo = false;
  bool bebedouroNoMaximo = false;

  // bombas
  bool bombaDeReposicaoLigada = false;         // reservatório limpo para bebedouro
  bool bombaDeDescarteLigada = false;          // bebedouro para reservatório sujo
  bool bombaDeCirculacaoLigada = false;

  // estado da lógica de troca de água
  // valores esperados: "NORMAL", "AGUARDANDO", "DESCARTANDO", "REABASTECENDO"
  String estadoDaTroca = "NORMAL";
  bool falhaDetectada = false;

  // LEDs da placa 3 (mesma ordem de QUANTIDADE_DE_LEDS)
  bool ledsAcesos[QUANTIDADE_DE_LEDS] = {false};

  // último sinal do buzzer: 0 = nenhum, 1 = beep longo, 2 = dois beeps, 3 = três beeps
  int ultimoSinalDoBuzzer = 0;
  unsigned long contadorDeSinaisDoBuzzer = 0;
};

// instância única, acessada pelo programa principal
DadosDoPainel dadosDoPainel;

// servidor HTTP
WebServer servidorWeb(PORTA_DO_SERVIDOR_WEB);

// ---------------------------------------------------------------
// página HTML (fica guardada na memória de programa)
// a página pede os dados em /dados a cada 2 segundos e atualiza a tela sem recarregar
// ---------------------------------------------------------------

const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Bebedouro dos Gatos</title>
<style>
  body { font-family: sans-serif; background: #f2f4f7; margin: 0; padding: 16px; color: #222; }
  h1 { font-size: 1.3rem; margin: 0 0 4px 0; }
  .subtitulo { font-size: 0.85rem; color: #666; margin-bottom: 16px; }
  .cartao { background: #fff; border-radius: 12px; padding: 14px; margin-bottom: 12px;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
  .cartao h2 { font-size: 1rem; margin: 0 0 10px 0; color: #444; }
  .linha { display: flex; justify-content: space-between; align-items: center; padding: 6px 0;
           border-bottom: 1px solid #eee; }
  .linha:last-child { border-bottom: none; }
  .indicador { padding: 3px 10px; border-radius: 12px; font-size: 0.85rem; color: #fff; }
  .verde { background: #2e9e4f; }
  .vermelho { background: #d63a3a; }
  .amarelo { background: #e0a800; }
  .cinza { background: #888; }
  #bannerFalha { display: none; background: #d63a3a; color: #fff; padding: 12px;
                 border-radius: 12px; margin-bottom: 12px; font-weight: bold; }

  /* simulação dos LEDs da placa 3 */
  .grade-de-pares { display: grid; grid-template-columns: repeat(5, 1fr); text-align: center; gap: 4px; }
  .rotulo-do-par { font-size: 0.72rem; color: #555; margin-bottom: 4px; }
  .led { display: inline-block; width: 24px; height: 24px; border-radius: 50%;
         margin: 3px 0; opacity: 0.15; }
  .led.aceso { opacity: 1; box-shadow: 0 0 8px rgba(0,0,0,0.35); }
  .cor-verde { background: #2e9e4f; }
  .cor-amarela { background: #e0a800; }
  .cor-vermelha { background: #d63a3a; }
  .nota { font-size: 0.78rem; color: #777; margin-top: 8px; }

  /* simulação do display de 7 segmentos e do botão da placa 3 */
  .display-simulado { background: #111; color: #ff3b30; font-family: monospace; font-size: 2.2rem;
                      letter-spacing: 4px; text-align: center; padding: 10px; border-radius: 8px;
                      margin: 6px 0; min-height: 2.6rem; }
  .legenda-do-display { text-align: center; font-size: 0.78rem; color: #666; margin-bottom: 8px; }
  button { font-size: 0.95rem; padding: 8px 14px; border-radius: 8px; border: 1px solid #bbb;
           background: #fafafa; }
</style>
</head>
<body>
  <h1>Bebedouro dos Gatos</h1>
  <div class="subtitulo">Conexão: <span id="conexao">...</span></div>

  <div id="bannerFalha">FALHA DETECTADA: bombas desligadas, reinicie o ESP32</div>

  <div class="cartao">
    <h2>Gato</h2>
    <div class="linha"><span>Presença</span><span id="gato" class="indicador cinza">...</span></div>
    <div class="linha"><span>Distância</span><span id="distancia">...</span></div>
  </div>

  <div class="cartao">
    <h2>Qualidade da água</h2>
    <div class="linha"><span>TDS</span><span id="tds">...</span></div>
    <div class="linha"><span>pH</span><span id="ph">...</span></div>
    <div class="linha"><span>Medição</span><span id="situacaoDaMedicao" class="indicador cinza">...</span></div>
  </div>

  <div class="cartao">
    <h2>Níveis</h2>
    <div class="linha"><span>Reservatório de água limpa</span><span id="reservatorioLimpo" class="indicador cinza">...</span></div>
    <div class="linha"><span>Reservatório de água suja</span><span id="reservatorioSujo" class="indicador cinza">...</span></div>
    <div class="linha"><span>Bebedouro</span><span id="bebedouro" class="indicador cinza">...</span></div>
  </div>

  <div class="cartao">
    <h2>Troca de água</h2>
    <div class="linha"><span>Estado</span><span id="estadoDaTroca" class="indicador cinza">...</span></div>
    <div class="linha"><span>Bomba de reposição</span><span id="bombaDeReposicao" class="indicador cinza">...</span></div>
    <div class="linha"><span>Bomba de descarte</span><span id="bombaDeDescarte" class="indicador cinza">...</span></div>
    <div class="linha"><span>Bomba de circulação</span><span id="bombaDeCirculacao" class="indicador cinza">...</span></div>
  </div>

  <div class="cartao">
    <h2>Placa 3 (simulação na tela)</h2>
    <div class="grade-de-pares">
      <div><div class="rotulo-do-par">Água limpa</div>
        <span id="ledVerde1" class="led cor-verde"></span><br><span id="ledAmarelo1" class="led cor-amarela"></span></div>
      <div><div class="rotulo-do-par">Água suja</div>
        <span id="ledVerde2" class="led cor-verde"></span><br><span id="ledAmarelo2" class="led cor-amarela"></span></div>
      <div><div class="rotulo-do-par">TDS</div>
        <span id="ledVerde3" class="led cor-verde"></span><br><span id="ledAmarelo3" class="led cor-amarela"></span></div>
      <div><div class="rotulo-do-par">pH</div>
        <span id="ledVerde4" class="led cor-verde"></span><br><span id="ledAmarelo4" class="led cor-amarela"></span></div>
      <div><div class="rotulo-do-par">Bebedouro</div>
        <span id="ledVerde5" class="led cor-verde"></span><br><span id="ledAmarelo5" class="led cor-amarela"></span></div>
    </div>
    <div class="linha">
      <span>LED vermelho (troca pendente/em andamento, pisca na falha)</span>
      <span id="ledVermelho" class="led cor-vermelha"></span>
    </div>
    <div class="linha"><span>Último sinal do buzzer</span><span id="sinalDoBuzzer">nenhum</span></div>
    <div class="linha">
      <label><input type="checkbox" id="somDoBuzzerLigado"> Tocar o som do buzzer neste aparelho</label>
    </div>

    <div id="display" class="display-simulado">----</div>
    <div id="legendaDoDisplay" class="legenda-do-display">TDS (ppm)</div>
    <div style="text-align:center">
      <button id="botaoDoDisplay" onclick="alternarInformacaoDoDisplay()">Botão: alternar informação</button>
    </div>
    <div class="nota">O display e o botão simulam os da placa 3 (na placa real, o botão fica no MSP430)</div>
  </div>

<script>
  // define o texto e a cor de um indicador da página
  function definirIndicador(
    identificador,
    texto,
    cor
  ) {
    const elemento = document.getElementById(identificador);
    elemento.textContent = texto;
    elemento.className = 'indicador ' + cor;
  }

  // define o texto simples de um campo da página
  function definirTexto(
    identificador,
    texto
  ) {
    document.getElementById(identificador).textContent = texto;
  }

  // acende ou apaga um LED simulado
  function definirLed(
    identificador,
    aceso
  ) {
    document.getElementById(identificador).classList.toggle('aceso', aceso);
  }

  // informação mostrada no display: 0 = TDS, 1 = pH, 2 = estado da troca
  let informacaoDoDisplay = 0;
  let ultimosDadosRecebidos = null;

  // letra mostrada no display para cada estado da troca ("F" na falha)
  const LETRAS_DO_ESTADO = {
    'NORMAL': 'n',
    'AGUARDANDO': 'A',
    'DESCARTANDO': 'd',
    'REABASTECENDO': 'r'
  };

  // simula o botão da placa 3, que alterna a informação do display
  function alternarInformacaoDoDisplay() {
    informacaoDoDisplay = (informacaoDoDisplay + 1) % 3;
    if (ultimosDadosRecebidos !== null) {
      atualizarDisplay(ultimosDadosRecebidos);
    }
  }

  // atualiza o texto do display simulado conforme a informação escolhida
  function atualizarDisplay(dados) {
    let texto = '';
    let legenda = '';
    if (informacaoDoDisplay === 0) {
      texto = dados.tdsValido ? Math.round(dados.tdsEmPpm) : '----';
      legenda = 'TDS (ppm)';
    } else if (informacaoDoDisplay === 1) {
      texto = dados.phValido ? dados.ph : '----';
      legenda = 'pH';
    } else {
      texto = dados.falhaDetectada ? 'F' : LETRAS_DO_ESTADO[dados.estadoDaTroca];
      legenda = 'Estado (n = normal, A = aguardando, d = descartando, r = reabastecendo, F = falha)';
    }
    definirTexto('display', texto);
    definirTexto('legendaDoDisplay', legenda);
  }

  // som do buzzer no navegador (só depois de o usuário marcar a caixa, por regra dos navegadores)
  let contextoDeAudio = null;
  let ultimoContadorDoBuzzer = null;

  // toca um beep de uma duração, começando em um instante (em segundos)
  function tocarBeep(
    inicioEmSegundos,
    duracaoEmSegundos
  ) {
    const oscilador = contextoDeAudio.createOscillator();
    oscilador.frequency.value = 2500;
    oscilador.connect(contextoDeAudio.destination);
    oscilador.start(contextoDeAudio.currentTime + inicioEmSegundos);
    oscilador.stop(contextoDeAudio.currentTime + inicioEmSegundos + duracaoEmSegundos);
  }

  // toca o padrão do sinal: 1 = beep longo, 2 = dois beeps curtos, 3 = três beeps curtos
  function tocarPadraoDoBuzzer(padrao) {
    if (!document.getElementById('somDoBuzzerLigado').checked) {
      return;
    }
    if (contextoDeAudio === null) {
      contextoDeAudio = new (window.AudioContext || window.webkitAudioContext)();
    }
    if (padrao === 1) {
      tocarBeep(0, 1.0);
    } else {
      for (let numeroDoBeep = 0; numeroDoBeep < padrao; numeroDoBeep++) {
        tocarBeep(numeroDoBeep * 0.35, 0.15);
      }
    }
  }

  // texto de cada padrão de sinal do buzzer
  const TEXTOS_DO_BUZZER = {
    0: 'nenhum',
    1: '1 beep longo (falha)',
    2: '2 beeps curtos (repor água limpa)',
    3: '3 beeps curtos (esvaziar água suja)'
  };

  // pede os dados ao ESP32 e atualiza todos os campos da página
  async function atualizarPainel() {
    try {
      const resposta = await fetch('/dados');
      const dados = await resposta.json();

      // gato
      definirIndicador('gato', dados.gatoPresente ? 'Presente' : 'Ausente', dados.gatoPresente ? 'amarelo' : 'cinza');
      definirTexto('distancia', dados.distanciaDoGatoEmCentimetros + ' cm');

      // qualidade da água
      definirTexto('tds', dados.tdsValido ? dados.tdsEmPpm + ' ppm' : 'sem leitura');
      definirTexto('ph', dados.phValido ? dados.ph : 'sem leitura');
      if (dados.bloqueioPosTrocaAtivo) {
        definirIndicador('situacaoDaMedicao', 'Aguardando estabilizar', 'amarelo');
      } else if (dados.leituraDeQualidadePermitida) {
        definirIndicador('situacaoDaMedicao', 'Medindo', 'verde');
      } else {
        definirIndicador('situacaoDaMedicao', 'Aguardando água parada', 'cinza');
      }

      // níveis
      definirIndicador('reservatorioLimpo', dados.reservatorioLimpoNoMinimo ? 'Repor água' : 'OK', dados.reservatorioLimpoNoMinimo ? 'vermelho' : 'verde');
      definirIndicador('reservatorioSujo', dados.reservatorioSujoNoMaximo ? 'Esvaziar' : 'OK', dados.reservatorioSujoNoMaximo ? 'vermelho' : 'verde');
      if (dados.bebedouroNoMaximo) {
        definirIndicador('bebedouro', 'Cheio', 'verde');
      } else if (dados.bebedouroNoMinimo) {
        definirIndicador('bebedouro', 'No mínimo', 'vermelho');
      } else {
        definirIndicador('bebedouro', 'Intermediário', 'amarelo');
      }

      // troca de água
      definirIndicador('estadoDaTroca', dados.estadoDaTroca, dados.estadoDaTroca === 'NORMAL' ? 'verde' : 'amarelo');
      definirIndicador('bombaDeReposicao', dados.bombaDeReposicaoLigada ? 'Ligada' : 'Desligada', dados.bombaDeReposicaoLigada ? 'verde' : 'cinza');
      definirIndicador('bombaDeDescarte', dados.bombaDeDescarteLigada ? 'Ligada' : 'Desligada', dados.bombaDeDescarteLigada ? 'verde' : 'cinza');
      definirIndicador('bombaDeCirculacao', dados.bombaDeCirculacaoLigada ? 'Ligada' : 'Desligada', dados.bombaDeCirculacaoLigada ? 'verde' : 'cinza');

      // LEDs da placa 3 (posição 0 = vermelho, 1 a 5 = verdes, 6 a 10 = amarelos)
      definirLed('ledVermelho', dados.ledsAcesos[0] === 1);
      for (let numeroDoPar = 1; numeroDoPar <= 5; numeroDoPar++) {
        definirLed('ledVerde' + numeroDoPar, dados.ledsAcesos[numeroDoPar] === 1);
        definirLed('ledAmarelo' + numeroDoPar, dados.ledsAcesos[numeroDoPar + 5] === 1);
      }

      // buzzer
      let textoDoBuzzer = TEXTOS_DO_BUZZER[dados.ultimoSinalDoBuzzer];
      if (dados.contadorDeSinaisDoBuzzer > 0) {
        textoDoBuzzer += ' (total de sinais: ' + dados.contadorDeSinaisDoBuzzer + ')';
      }
      definirTexto('sinalDoBuzzer', textoDoBuzzer);

      // toca o som quando chega um sinal novo (a primeira leitura só marca o ponto de partida)
      if (ultimoContadorDoBuzzer !== null && dados.contadorDeSinaisDoBuzzer > ultimoContadorDoBuzzer) {
        tocarPadraoDoBuzzer(dados.ultimoSinalDoBuzzer);
      }
      ultimoContadorDoBuzzer = dados.contadorDeSinaisDoBuzzer;

      // display
      ultimosDadosRecebidos = dados;
      atualizarDisplay(dados);

      // falha
      document.getElementById('bannerFalha').style.display = dados.falhaDetectada ? 'block' : 'none';
      definirTexto('conexao', 'conectado');
    } catch (erro) {
      definirTexto('conexao', 'sem resposta do ESP32');
    }
  }

  // atualiza a cada 2 segundos
  setInterval(atualizarPainel, 2000);
  atualizarPainel();
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------
// funções do servidor
// ---------------------------------------------------------------

/*
  Converte um booleano para o texto "true" ou "false" do formato JSON
*/
String booleanoParaJson(bool valor) {
  return valor ? "true" : "false";
}

/*
  Monta o texto JSON com todos os dados atuais do painel
  Retorna: texto JSON pronto para ser enviado ao navegador
*/
String montarJsonDosDados() {
  String json = "{";

  // sensores
  json += "\"tdsEmPpm\":" + String(dadosDoPainel.tdsEmPpm, 0) + ",";
  json += "\"tdsValido\":" + booleanoParaJson(dadosDoPainel.tdsValido) + ",";
  json += "\"ph\":" + String(dadosDoPainel.ph, 2) + ",";
  json += "\"phValido\":" + booleanoParaJson(dadosDoPainel.phValido) + ",";
  json += "\"distanciaDoGatoEmCentimetros\":" + String(dadosDoPainel.distanciaDoGatoEmCentimetros, 1) + ",";
  json += "\"gatoPresente\":" + booleanoParaJson(dadosDoPainel.gatoPresente) + ",";

  // situação da medição
  json += "\"bloqueioPosTrocaAtivo\":" + booleanoParaJson(dadosDoPainel.bloqueioPosTrocaAtivo) + ",";
  json += "\"leituraDeQualidadePermitida\":" + booleanoParaJson(dadosDoPainel.leituraDeQualidadePermitida) + ",";

  // níveis
  json += "\"reservatorioLimpoNoMinimo\":" + booleanoParaJson(dadosDoPainel.reservatorioLimpoNoMinimo) + ",";
  json += "\"reservatorioSujoNoMaximo\":" + booleanoParaJson(dadosDoPainel.reservatorioSujoNoMaximo) + ",";
  json += "\"bebedouroNoMinimo\":" + booleanoParaJson(dadosDoPainel.bebedouroNoMinimo) + ",";
  json += "\"bebedouroNoMaximo\":" + booleanoParaJson(dadosDoPainel.bebedouroNoMaximo) + ",";

  // bombas
  json += "\"bombaDeReposicaoLigada\":" + booleanoParaJson(dadosDoPainel.bombaDeReposicaoLigada) + ",";
  json += "\"bombaDeDescarteLigada\":" + booleanoParaJson(dadosDoPainel.bombaDeDescarteLigada) + ",";
  json += "\"bombaDeCirculacaoLigada\":" + booleanoParaJson(dadosDoPainel.bombaDeCirculacaoLigada) + ",";

  // LEDs da placa 3 (lista de 0 e 1)
  json += "\"ledsAcesos\":[";
  for (int indiceDoLed = 0; indiceDoLed < QUANTIDADE_DE_LEDS; indiceDoLed++) {
    if (indiceDoLed > 0) {
      json += ",";
    }
    json += dadosDoPainel.ledsAcesos[indiceDoLed] ? "1" : "0";
  }
  json += "],";

  // buzzer
  json += "\"ultimoSinalDoBuzzer\":" + String(dadosDoPainel.ultimoSinalDoBuzzer) + ",";
  json += "\"contadorDeSinaisDoBuzzer\":" + String(dadosDoPainel.contadorDeSinaisDoBuzzer) + ",";

  // estado da troca e falha
  json += "\"estadoDaTroca\":\"" + dadosDoPainel.estadoDaTroca + "\",";
  json += "\"falhaDetectada\":" + booleanoParaJson(dadosDoPainel.falhaDetectada);

  json += "}";
  return json;
}

/*
  Envia a página HTML do painel ao navegador (rota "/")
*/
void enviarPaginaPrincipal() {
  servidorWeb.send_P(
    200,
    "text/html; charset=utf-8",
    PAGINA_HTML
  );
}

/*
  Envia os dados atuais em formato JSON ao navegador (rota "/dados")
*/
void enviarDadosEmJson() {
  servidorWeb.send(
    200,
    "application/json",
    montarJsonDosDados()
  );
}

/*
  Responde com erro 404 para qualquer rota desconhecida
*/
void enviarPaginaNaoEncontrada() {
  servidorWeb.send(
    404,
    "text/plain; charset=utf-8",
    "Página não encontrada"
  );
}

/*
  Cria a rede Wi-Fi do ESP32 e inicia o servidor web
  Chamar uma vez no setup()
*/
void iniciarPainelWeb() {
  // cria a rede Wi-Fi própria do ESP32
  WiFi.mode(WIFI_AP);
  WiFi.softAP(
    NOME_DA_REDE_DO_PAINEL,
    SENHA_DA_REDE_DO_PAINEL
  );

  // registra as rotas do servidor
  servidorWeb.on("/", enviarPaginaPrincipal);
  servidorWeb.on("/dados", enviarDadosEmJson);
  servidorWeb.onNotFound(enviarPaginaNaoEncontrada);

  // inicia o servidor
  servidorWeb.begin();

  // mostra no monitor serial o endereço para abrir no celular
  Serial.print("Painel web disponível em http://");
  Serial.println(WiFi.softAPIP());
}

/*
  Atende as requisições dos navegadores
  Chamar a cada volta do loop(), sem usar delay() longo no programa
*/
void atenderPainelWeb() {
  servidorWeb.handleClient();
}

#endif