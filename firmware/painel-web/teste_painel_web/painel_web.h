// painel web do bebedouro
// mostra leituras dos sensores, níveis dos reservatórios, bombas e estado da troca de água
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

// ---------------------------------------------------------------
// dados exibidos no painel
// o programa principal preenche esta estrutura e o painel só lê
// ---------------------------------------------------------------

struct DadosDoPainel {
  // leituras dos sensores
  float tdsEmPpm = 0;
  float turbidez = 0;
  float ph = 0;
  float distanciaDoGatoEmCentimetros = 0;
  bool gatoPresente = false;

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
    <div class="linha"><span>Turbidez</span><span id="turbidez">...</span></div>
    <div class="linha"><span>pH</span><span id="ph">...</span></div>
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

  // pede os dados ao ESP32 e atualiza todos os campos da página
  async function atualizarPainel() {
    try {
      const resposta = await fetch('/dados');
      const dados = await resposta.json();

      // gato
      definirIndicador('gato', dados.gatoPresente ? 'Presente' : 'Ausente', dados.gatoPresente ? 'amarelo' : 'cinza');
      definirTexto('distancia', dados.distanciaDoGatoEmCentimetros + ' cm');

      // qualidade da água
      definirTexto('tds', dados.tdsEmPpm + ' ppm');
      definirTexto('turbidez', dados.turbidez);
      definirTexto('ph', dados.ph);

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
  json += "\"turbidez\":" + String(dadosDoPainel.turbidez, 0) + ",";
  json += "\"ph\":" + String(dadosDoPainel.ph, 2) + ",";
  json += "\"distanciaDoGatoEmCentimetros\":" + String(dadosDoPainel.distanciaDoGatoEmCentimetros, 1) + ",";
  json += "\"gatoPresente\":" + booleanoParaJson(dadosDoPainel.gatoPresente) + ",";

  // níveis
  json += "\"reservatorioLimpoNoMinimo\":" + booleanoParaJson(dadosDoPainel.reservatorioLimpoNoMinimo) + ",";
  json += "\"reservatorioSujoNoMaximo\":" + booleanoParaJson(dadosDoPainel.reservatorioSujoNoMaximo) + ",";
  json += "\"bebedouroNoMinimo\":" + booleanoParaJson(dadosDoPainel.bebedouroNoMinimo) + ",";
  json += "\"bebedouroNoMaximo\":" + booleanoParaJson(dadosDoPainel.bebedouroNoMaximo) + ",";

  // bombas
  json += "\"bombaDeReposicaoLigada\":" + booleanoParaJson(dadosDoPainel.bombaDeReposicaoLigada) + ",";
  json += "\"bombaDeDescarteLigada\":" + booleanoParaJson(dadosDoPainel.bombaDeDescarteLigada) + ",";
  json += "\"bombaDeCirculacaoLigada\":" + booleanoParaJson(dadosDoPainel.bombaDeCirculacaoLigada) + ",";

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
