// teste do painel web com dados simulados
// não usa sensores nem bombas: serve só para ver a página funcionando no celular
// o arquivo painel_web.h precisa estar na mesma pasta deste sketch

#include "painel_web.h"

// intervalo entre as mudanças dos dados simulados
const unsigned long INTERVALO_DA_SIMULACAO_MS = 3000;

unsigned long instanteDaUltimaSimulacaoMs = 0;
int passoDaSimulacao = 0;

/*
  Preenche o painel com valores de exemplo que mudam a cada passo
*/
void simularDados() {
  passoDaSimulacao = (passoDaSimulacao + 1) % 4;

  // valores que variam um pouco a cada passo
  dadosDoPainel.tdsEmPpm = 80 + passoDaSimulacao * 20;
  dadosDoPainel.turbidez = 100 + passoDaSimulacao * 30;
  dadosDoPainel.ph = 7.0 + passoDaSimulacao * 0.1;
  dadosDoPainel.distanciaDoGatoEmCentimetros = 40 - passoDaSimulacao * 10;
  dadosDoPainel.gatoPresente = (passoDaSimulacao >= 2);

  // percorre os estados da troca de água
  const char* nomesDosEstados[] = {"NORMAL", "AGUARDANDO", "DESCARTANDO", "REABASTECENDO"};
  dadosDoPainel.estadoDaTroca = nomesDosEstados[passoDaSimulacao];

  // níveis e bombas coerentes com o estado simulado
  dadosDoPainel.reservatorioLimpoNoMinimo = (passoDaSimulacao == 3);
  dadosDoPainel.reservatorioSujoNoMaximo = (passoDaSimulacao == 2);
  dadosDoPainel.bebedouroNoMinimo = (passoDaSimulacao == 3);
  dadosDoPainel.bebedouroNoMaximo = (passoDaSimulacao == 0);
  dadosDoPainel.bombaDeCirculacaoLigada = (passoDaSimulacao == 0);
  dadosDoPainel.bombaDeDescarteLigada = (passoDaSimulacao == 2);
  dadosDoPainel.bombaDeReposicaoLigada = (passoDaSimulacao == 3);
  dadosDoPainel.falhaDetectada = false;
}

/*
  Inicia a serial e o painel web
*/
void setup() {
  Serial.begin(115200);
  iniciarPainelWeb();
}

/*
  Atende o painel e atualiza os dados simulados periodicamente
*/
void loop() {
  atenderPainelWeb();

  // troca os dados simulados sem bloquear o servidor
  if (millis() - instanteDaUltimaSimulacaoMs >= INTERVALO_DA_SIMULACAO_MS) {
    instanteDaUltimaSimulacaoMs = millis();
    simularDados();
  }
}
