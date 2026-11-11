/*
  TESTE DA BOMBA COM MOSFET E PARTIDA SUAVE (PWM)

  Este sketch serve apenas para testar uma bomba de 5 V acionada por um
  MOSFET lógico, com partida suave por PWM.

  A bomba liga com partida suave enquanto o botão estiver pressionado
  e desliga assim que ele for solto.

  Comandos pelo Serial Monitor (115200 baud, com nova linha no final):
    potencia N    define a potência final em porcentagem (0 a 100)
    minimo N      define a potência inicial da rampa em porcentagem (0 a 100)
    rampa N       define a duração da rampa em milissegundos (0 a 20000)
    status        mostra os valores atuais
    ajuda         mostra esta lista

  ATENÇÃO: a bomba nunca deve funcionar sem água.
*/

// =====================================================
// 1. CONFIGURAÇÕES
// =====================================================

// Pino do ESP32 ligado ao gate do MOSFET (através de um resistor)
const int PINO_BOMBA = 25;

// Pino do botão: um lado do botão no pino e o outro lado no GND
// O resistor de pull-up interno do ESP32 é usado, então não precisa
// de resistor externo
const int PINO_BOTAO = 32;

// Tempo que a leitura do botão precisa ficar estável para ser aceita
// (filtro contra o ruído de contato, chamado de debounce)
const unsigned long TEMPO_FILTRO_BOTAO_MILISSEGUNDOS = 30;

// Frequência de 20 kHz fica acima do que o ouvido humano percebe,
// evitando o zumbido da bomba em frequências mais baixas
const uint32_t FREQUENCIA_PWM_HZ = 20000;

// Resolução de 10 bits: o duty vai de 0 a 1023
const uint8_t RESOLUCAO_PWM_BITS = 10;
const uint32_t DUTY_MAXIMO = (1 << RESOLUCAO_PWM_BITS) - 1;

// Desligamento automático de segurança, caso a bomba fique ligada
// por tempo demais (por exemplo, sem água)
const unsigned long TEMPO_MAXIMO_LIGADA_MILISSEGUNDOS = 60000;

// Intervalo entre as mensagens de progresso enquanto a bomba está ligada
const unsigned long INTERVALO_PROGRESSO_MILISSEGUNDOS = 500;

// O núcleo 3.x do ESP32 mudou a API do PWM (LEDC)
// Esta verificação faz o mesmo código funcionar nas duas versões
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define API_LEDC_NOVA 1
#else
  #define API_LEDC_NOVA 0
  const uint8_t CANAL_PWM = 0;
#endif

// =====================================================
// 2. VARIÁVEIS AJUSTÁVEIS PELO SERIAL MONITOR
// =====================================================

// Potência final da bomba, depois de terminada a rampa
int potenciaFinalPercentual = 100;

// Potência no início da rampa
// Abaixo de certo valor a bomba não consegue girar, então a rampa
// começa de um valor mínimo (ajustar conforme a bomba)
int potenciaInicialPercentual = 30;

// Duração da partida suave
unsigned long duracaoRampaMilissegundos = 2000;

// =====================================================
// 3. ESTADO DO TESTE
// =====================================================

bool bombaLigada = false;
uint32_t dutyAtual = 0;
unsigned long instanteInicioRampa = 0;
unsigned long instanteUltimoProgresso = 0;

// Botão: leitura aceita depois do filtro e leitura bruta mais recente
bool botaoPressionado = false;
bool ultimaLeituraBruta = false;
unsigned long instanteUltimaMudancaLeitura = 0;

// Depois do desligamento automático de segurança, a bomba só pode
// ligar de novo quando o botão for solto e pressionado outra vez
bool bloqueadoPorTempoMaximo = false;

// =====================================================
// 4. FUNÇÕES DO PWM
// =====================================================

/*
  Converte uma porcentagem (0 a 100) para o valor de duty do PWM.
  Duty é a parte de cada ciclo em que o pino fica em nível alto.
*/
uint32_t converterPercentualParaDuty(int percentual) {
  return (uint32_t)percentual * DUTY_MAXIMO / 100;
}

/*
  Escreve o duty no pino da bomba e guarda o valor aplicado.
*/
void escreverDutyNaBomba(uint32_t duty) {
#if API_LEDC_NOVA
  ledcWrite(
    PINO_BOMBA,
    duty
  );
#else
  ledcWrite(
    CANAL_PWM,
    duty
  );
#endif

  dutyAtual = duty;
}

/*
  Configura o PWM do ESP32 (periférico LEDC) no pino da bomba.
*/
void configurarPwm() {
#if API_LEDC_NOVA
  ledcAttach(
    PINO_BOMBA,
    FREQUENCIA_PWM_HZ,
    RESOLUCAO_PWM_BITS
  );
#else
  ledcSetup(
    CANAL_PWM,
    FREQUENCIA_PWM_HZ,
    RESOLUCAO_PWM_BITS
  );

  ledcAttachPin(
    PINO_BOMBA,
    CANAL_PWM
  );
#endif

  // A bomba começa desligada
  escreverDutyNaBomba(0);
}

// =====================================================
// 5. CONTROLE DA BOMBA
// =====================================================

/*
  Liga a bomba e reinicia a contagem da rampa de partida suave.
*/
void ligarBomba() {
  bombaLigada = true;
  instanteInicioRampa = millis();
  instanteUltimoProgresso = millis();

  Serial.println("Bomba ligada: partida suave iniciada");
}

/*
  Desliga a bomba imediatamente.
*/
void desligarBomba() {
  bombaLigada = false;
  escreverDutyNaBomba(0);

  Serial.println("Bomba desligada");
}

/*
  Atualiza o duty da bomba conforme o tempo decorrido da rampa.
  Também desliga a bomba se o tempo máximo de segurança for excedido.
*/
void atualizarPartidaSuave() {
  if (!bombaLigada) {
    return;
  }

  unsigned long tempoDecorrido = millis() - instanteInicioRampa;

  // Desligamento automático de segurança
  if (tempoDecorrido >= TEMPO_MAXIMO_LIGADA_MILISSEGUNDOS) {
    desligarBomba();
    bloqueadoPorTempoMaximo = true;

    Serial.println("Desligada automaticamente: tempo máximo atingido");
    Serial.println("Solte o botão para liberar um novo acionamento");
    return;
  }

  // A potência inicial nunca pode passar da final
  int potenciaInicialEfetiva = potenciaInicialPercentual;

  if (potenciaInicialEfetiva > potenciaFinalPercentual) {
    potenciaInicialEfetiva = potenciaFinalPercentual;
  }

  uint32_t dutyInicial = converterPercentualParaDuty(potenciaInicialEfetiva);
  uint32_t dutyFinal = converterPercentualParaDuty(potenciaFinalPercentual);
  uint32_t dutyCalculado;

  if (tempoDecorrido >= duracaoRampaMilissegundos) {
    // Rampa concluída: mantém a potência final
    dutyCalculado = dutyFinal;
  }
  else {
    // Durante a rampa: sobe de forma linear do duty inicial ao final
    dutyCalculado = dutyInicial +
      (dutyFinal - dutyInicial) * tempoDecorrido / duracaoRampaMilissegundos;
  }

  if (dutyCalculado != dutyAtual) {
    escreverDutyNaBomba(dutyCalculado);
  }
}

/*
  Lê o botão com filtro de ruído e liga a bomba enquanto ele estiver
  pressionado. Ao soltar, a bomba desliga.
*/
void atualizarBotao() {
  // Com pull-up, o botão pressionado leva o pino ao nível baixo
  bool leituraBruta = (digitalRead(PINO_BOTAO) == LOW);
  unsigned long agora = millis();

  // Qualquer mudança reinicia a contagem do filtro
  if (leituraBruta != ultimaLeituraBruta) {
    ultimaLeituraBruta = leituraBruta;
    instanteUltimaMudancaLeitura = agora;
  }

  // Só aceita a leitura depois de ficar estável pelo tempo do filtro
  bool leituraEstavel =
    (agora - instanteUltimaMudancaLeitura >= TEMPO_FILTRO_BOTAO_MILISSEGUNDOS);

  if (!leituraEstavel || leituraBruta == botaoPressionado) {
    return;
  }

  botaoPressionado = leituraBruta;

  if (botaoPressionado) {
    // Botão pressionado: liga, a menos que esteja travada por segurança
    if (!bloqueadoPorTempoMaximo) {
      ligarBomba();
    }
  }
  else {
    // Botão solto: libera a trava e desliga a bomba
    bloqueadoPorTempoMaximo = false;

    if (bombaLigada) {
      desligarBomba();
    }
  }
}

// =====================================================
// 6. MENSAGENS NO SERIAL MONITOR
// =====================================================

/*
  Mostra a lista de comandos disponíveis.
*/
void mostrarAjuda() {
  Serial.println();
  Serial.println("===== COMANDOS =====");
  Serial.println("Segure o botão para ligar a bomba (solte para desligar)");
  Serial.println("potencia N    : potência final em % (0 a 100)");
  Serial.println("minimo N      : potência inicial da rampa em % (0 a 100)");
  Serial.println("rampa N       : duração da rampa em ms (0 a 20000)");
  Serial.println("status        : mostra os valores atuais");
  Serial.println("ajuda         : mostra esta lista");
  Serial.println("====================");
  Serial.println();
}

/*
  Mostra os valores configurados e o estado atual da bomba.
*/
void imprimirStatus() {
  Serial.println();
  Serial.println("---------- STATUS ----------");

  Serial.print("Bomba: ");
  Serial.println(bombaLigada ? "LIGADA" : "DESLIGADA");

  Serial.print("Botão: ");
  Serial.println(botaoPressionado ? "PRESSIONADO" : "SOLTO");

  Serial.print("Duty atual: ");
  Serial.print(dutyAtual * 100 / DUTY_MAXIMO);
  Serial.print("% (");
  Serial.print(dutyAtual);
  Serial.print(" de ");
  Serial.print(DUTY_MAXIMO);
  Serial.println(")");

  Serial.print("Potência final: ");
  Serial.print(potenciaFinalPercentual);
  Serial.println("%");

  Serial.print("Potência inicial da rampa: ");
  Serial.print(potenciaInicialPercentual);
  Serial.println("%");

  Serial.print("Duração da rampa: ");
  Serial.print(duracaoRampaMilissegundos);
  Serial.println(" ms");

  Serial.println("----------------------------");
}

/*
  Mostra o duty atual a cada intervalo, enquanto a bomba está ligada.
*/
void imprimirProgresso() {
  if (!bombaLigada) {
    return;
  }

  unsigned long agora = millis();

  if (agora - instanteUltimoProgresso < INTERVALO_PROGRESSO_MILISSEGUNDOS) {
    return;
  }

  instanteUltimoProgresso = agora;

  Serial.print("Duty: ");
  Serial.print(dutyAtual * 100 / DUTY_MAXIMO);
  Serial.println("%");
}

// =====================================================
// 7. COMANDOS DO SERIAL MONITOR
// =====================================================

/*
  Interpreta um comando digitado no Serial Monitor e aplica o resultado.
*/
void processarComando(String comando) {
  comando.trim();
  comando.toLowerCase();

  if (comando == "status") {
    imprimirStatus();
    return;
  }

  if (comando == "ajuda") {
    mostrarAjuda();
    return;
  }

  // Comandos com valor: "nome valor"
  int posicaoEspaco = comando.indexOf(' ');

  if (posicaoEspaco < 0) {
    Serial.println("Comando inválido. Digite: ajuda");
    return;
  }

  String nome = comando.substring(0, posicaoEspaco);
  int valor = comando.substring(posicaoEspaco + 1).toInt();

  if (nome == "potencia" && valor >= 0 && valor <= 100) {
    potenciaFinalPercentual = valor;
  }
  else if (nome == "minimo" && valor >= 0 && valor <= 100) {
    potenciaInicialPercentual = valor;
  }
  else if (nome == "rampa" && valor >= 0 && valor <= 20000) {
    duracaoRampaMilissegundos = valor;
  }
  else {
    Serial.println("Comando ou valor inválido. Digite: ajuda");
    return;
  }

  Serial.print("Comando aplicado: ");
  Serial.println(comando);
}

// =====================================================
// 8. SETUP E LOOP
// =====================================================

void setup() {
  Serial.begin(115200);

  // Garante o pino em nível baixo antes de configurar o PWM,
  // para a bomba não receber nenhum pulso na inicialização
  pinMode(
    PINO_BOMBA,
    OUTPUT
  );

  digitalWrite(
    PINO_BOMBA,
    LOW
  );

  // Botão com resistor de pull-up interno
  pinMode(
    PINO_BOTAO,
    INPUT_PULLUP
  );

  configurarPwm();

  Serial.println();
  Serial.println("Teste da bomba iniciado.");
  Serial.println("Confirme que a bomba está dentro da água antes de ligar.");

  mostrarAjuda();
  imprimirStatus();
}

void loop() {
  // Recebe comandos do usuário
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    processarComando(comando);
  }

  // Lê o botão e liga ou desliga a bomba
  atualizarBotao();

  // Avança a rampa de partida suave sem travar o programa
  atualizarPartidaSuave();

  // Mostra o progresso enquanto a bomba está ligada
  imprimirProgresso();
}