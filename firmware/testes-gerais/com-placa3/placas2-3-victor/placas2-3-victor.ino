/*
 * TESTE DE INTEGRACAO - PLACA 2 (ESP32) <-> PLACA 3 (MSP430)
 * Arduino IDE, placa "ESP32 Dev Module". Monitor Serial a 115200.
 *
 * Digite no Monitor Serial (e tecle Enter):
 *   1 = tudo normal              2 = pH critico (5,0)
 *   3 = TDS critico (800 ppm)    4 = turbidez critica (8,0 NTU)
 *   5 = nivel LIMPO baixo        6 = nivel SUJO alto
 *   7 = troca PENDENTE (pisca)   8 = troca EM ANDAMENTO (fixo)
 *   9 = turbidez SEM SENSOR ("-")
 *   v = manda uma visita ($V)    r = zera visitas ($R)
 *   x = manda quadro com checksum ERRADO (deve ser ignorado)
 *   p = PAUSA/retoma o envio (pausado > 10 s = MSP mostra "Erro")
 */

#define PINO_RX_MSP 16   // GPIO16 <- TX do MSP430 (P1.2)
#define PINO_TX_MSP 17   // GPIO17 -> RX do MSP430 (P1.1)

HardwareSerial MSP(2);   // UART2 do ESP32 (a UART0 e o USB)

// Cenario atual
int  ph10 = 72, tds = 180, turb10 = 25;
bool turbOk = true;
int  nivelLimpoBaixo = 0, nivelSujoAlto = 0, troca = 0;
bool pausado = false;

// ---------- envio ----------
uint8_t checksum(const String &s) {
  uint8_t cs = 0;
  for (size_t i = 0; i < s.length(); i++) cs ^= (uint8_t)s[i];
  return cs;
}

void enviaQuadro(const String &corpo, bool estragar = false) {
  uint8_t cs = checksum(corpo);
  if (estragar) cs ^= 0xFF;                 // checksum errado de proposito
  char cauda[8];
  snprintf(cauda, sizeof(cauda), "*%02X\r\n", cs);
  String q = "$" + corpo + cauda;
  MSP.print(q);
  Serial.print(">> ESP enviou: ");
  Serial.print(q);                          // q ja termina com \r\n
}

String montaD() {
  String c = "D,";
  c += String(ph10) + ",";
  c += String(tds) + ",";
  c += (turbOk ? String(turb10) : String("-")) + ",";
  c += String(nivelLimpoBaixo) + ",";
  c += String(nivelSujoAlto) + ",";
  c += String(troca);
  return c;
}

void cenarioNormal() {
  ph10 = 72; tds = 180; turb10 = 25; turbOk = true;
  nivelLimpoBaixo = 0; nivelSujoAlto = 0; troca = 0;
}

// ---------- recepcao ----------
String linha;

void trataLinhaDoMSP(const String &l) {
  // Esperado: $CORPO*HH
  int ast = l.indexOf('*');
  if (!l.startsWith("$") || ast < 0 || ast + 3 > (int)l.length()) {
    Serial.println("<< MSP (formato estranho): " + l);
    return;
  }
  String corpo = l.substring(1, ast);
  uint8_t recebido = (uint8_t)strtol(l.substring(ast + 1, ast + 3).c_str(), NULL, 16);
  bool ok = (recebido == checksum(corpo));

  Serial.print("<< MSP enviou: " + l);
  Serial.println(ok ? "   [checksum OK]" : "   [CHECKSUM ERRADO]");

  if (ok && corpo.startsWith("S,")) {
    // S,alarme,silenciado,visitas,com
    int a, s, v, c;
    if (sscanf(corpo.c_str(), "S,%d,%d,%d,%d", &a, &s, &v, &c) == 4) {
      const char *com[] = {"aguardando", "OK", "PERDIDA"};
      Serial.printf("   -> alarme=%s  silenciado=%s  visitas=%d  comunicacao=%s\n",
                    a ? "SIM" : "nao", s ? "SIM" : "nao", v,
                    (c >= 0 && c <= 2) ? com[c] : "?");
    }
  } else if (ok && corpo == "K") {
    Serial.println("   -> usuario apertou SILENCIAR na Placa 3");
  } else if (ok && corpo == "Z") {
    Serial.println("   -> usuario zerou as visitas na Placa 3");
  }
}

// ---------- comandos do Monitor Serial ----------
void trataComando(char c) {
  switch (c) {
    case '1': cenarioNormal();                     Serial.println("## Cenario: tudo normal"); break;
    case '2': cenarioNormal(); ph10 = 50;          Serial.println("## Cenario: pH 5,0 (critico)"); break;
    case '3': cenarioNormal(); tds = 800;          Serial.println("## Cenario: TDS 800 ppm (critico)"); break;
    case '4': cenarioNormal(); turb10 = 80;        Serial.println("## Cenario: turbidez 8,0 NTU (critica)"); break;
    case '5': cenarioNormal(); nivelLimpoBaixo = 1; Serial.println("## Cenario: nivel LIMPO baixo"); break;
    case '6': cenarioNormal(); nivelSujoAlto = 1;  Serial.println("## Cenario: nivel SUJO alto"); break;
    case '7': cenarioNormal(); troca = 1;          Serial.println("## Cenario: troca PENDENTE"); break;
    case '8': cenarioNormal(); troca = 2;          Serial.println("## Cenario: troca EM ANDAMENTO"); break;
    case '9': cenarioNormal(); turbOk = false;     Serial.println("## Cenario: turbidez SEM SENSOR"); break;
    case 'v': case 'V': enviaQuadro("V"); break;
    case 'r': case 'R': enviaQuadro("R"); break;
    case 'x': case 'X': Serial.println("## Enviando quadro com checksum ERRADO:");
                        enviaQuadro(montaD(), true); break;
    case 'p': case 'P': pausado = !pausado;
                        Serial.println(pausado ? "## Envio PAUSADO (MSP deve mostrar Erro em 10 s)"
                                               : "## Envio RETOMADO"); break;
    default: break;   // ignora Enter e outros caracteres
  }
}

void setup() {
  Serial.begin(115200);
  MSP.begin(9600, SERIAL_8N1, PINO_RX_MSP, PINO_TX_MSP);
  delay(500);
  Serial.println();
  Serial.println("=== Teste Placa 2 <-> Placa 3 ===");
  Serial.println("Comandos: 1-9 cenarios | v visita | r zera | x checksum errado | p pausa");
}

unsigned long ultimoEnvio = 0;

void loop() {
  // 1) Envia o $D a cada 1 s
  if (!pausado && millis() - ultimoEnvio >= 1000) {
    ultimoEnvio = millis();
    enviaQuadro(montaD());
  }

  // 2) Le o que chega do MSP430
  while (MSP.available()) {
    char ch = MSP.read();
    if (ch == '\n') {
      if (linha.length()) trataLinhaDoMSP(linha);
      linha = "";
    } else if (ch != '\r') {
      if (linha.length() < 80) linha += ch;
    }
  }

  // 3) Le os comandos digitados no Monitor Serial
  while (Serial.available()) trataComando((char)Serial.read());
}