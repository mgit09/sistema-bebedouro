# Bebedouro Automatizado para Gatos

Bebedouro automatizado para gatos baseado em ESP32. O sistema monitora a qualidade da água (TDS, pH e turbidez), os níveis dos reservatórios e a presença do gato, e realiza a troca de água automaticamente com três bombas. Um painel web hospedado no próprio ESP32 mostra o estado do sistema em tempo real.

Projeto desenvolvido em grupo na disciplina de Laboratório de Sistemas I. O escopo é apenas a parte da água (sem alimentação).

**Status:** em desenvolvimento (fase de testes e integração na placa de circuito impresso).

## Como o sistema funciona

- Três reservatórios de água: **água limpa**, **bebedouro** (onde o gato bebe) e **água suja**.
- Três bombas: reposição (água limpa para o bebedouro), descarte (bebedouro para água suja) e circulação (dentro do bebedouro).
- A troca de água é controlada por uma máquina de estados: `NORMAL`, `AGUARDANDO`, `DESCARTANDO` e `REABASTECENDO`.
- O ESP32 cria a própria rede Wi-Fi e serve uma página com as leituras, os níveis, as bombas e o estado da troca.

A lógica completa (condições, decisões e casos extremos) está em [`docs/`](docs/).

## Hardware

| Item | Função |
|---|---|
| ESP32 DevKitC (ESP32-WROOM) | Controlador |
| Sensor TDS (TDS Meter V1.0) | Sólidos dissolvidos na água |
| Sensor de pH (PH-4502C) | Acidez da água |
| Sensor de turbidez (ST100) | Sujeira na água |
| Sensor ultrassônico HC-SR04 | Presença do gato |
| 4 sensores de nível (mini boia) | Níveis mínimo e máximo dos reservatórios |
| 3 bombas JT100 (3 a 5 V) | Circulação, reposição e descarte |
| Fonte 12 V 3 A + 2 módulos LM2596 | Alimentação (reguladores de 5 V e 3,3 V) |

A lista completa de componentes está em [`docs/componentes.txt`](docs/componentes.txt) e o resumo da pinagem do ESP32 em [`docs/pinagem_esp32.txt`](docs/pinagem_esp32.txt).

Cuidado: as saídas dos sensores de pH e turbidez podem passar de 3,3 V, e os GPIOs do ESP32 **não toleram 5 V**. Adaptar a tensão antes de conectar.

## Estrutura do repositório

```
bebedouro-gatos/
├── README.md
├── docs/                       documentação do projeto
│   ├── componentes.txt
│   ├── pinagem_esp32.txt
│   ├── cenarios_bebedouro.txt
│   ├── logica_esp.ods
│   └── modelagem/              diagramas iniciais (Visual Paradigm)
├── hardware/
│   └── placas/                 esquemáticos e projeto das placas
└── firmware/
    ├── placa_pcb/              testes feitos na placa de circuito impresso
    │   └── teste_sensores_por_comando/
    ├── protoboard/             testes individuais feitos na protoboard
    │   ├── teste_sensores_de_nivel/
    │   ├── teste_tds/
    │   ├── teste_presenca/
    │   └── teste_leds/
    └── painel_web/
        └── teste_painel_web/   painel web com dados simulados
```

## Como rodar os códigos

Os códigos (`.ino`) foram feitos e testados na **Arduino IDE**, não no VS Code.

1. Instalar a [Arduino IDE](https://www.arduino.cc/en/software).
2. Abrir o **Gerenciador de Placas** e instalar o pacote **esp32** (da Espressif Systems).
3. Selecionar a placa **ESP32 Dev Module** e a porta serial correta.
4. Abrir o arquivo `.ino` dentro da pasta do sketch (a pasta e o arquivo têm o mesmo nome; arquivos `.h` ficam na mesma pasta).
5. Fazer o upload e abrir o **Monitor Serial** em **115200 baud**.

### Testes por comando na placa

O sketch em `firmware/placa_pcb/teste_sensores_por_comando/` recebe comandos pelo Monitor Serial para testar um sensor de cada vez na placa principal.

### Painel web

1. Abrir `firmware/painel_web/teste_painel_web/teste_painel_web.ino` e fazer o upload.
2. Conectar o celular na rede Wi-Fi criada pelo ESP32 (nome `Bebedouro-Gatos`).
3. Abrir `http://192.168.4.1` no navegador.

O teste usa dados simulados. Para usar com dados reais, incluir `painel_web.h` no programa principal e preencher a estrutura `dadosDoPainel` a cada volta do `loop()`.

**Atenção:** com o Wi-Fi ligado, o ESP32 não lê entradas analógicas nos pinos ADC2 (GPIO 0, 2, 4, 12 a 15 e 25 a 27). Os sensores analógicos devem usar pinos ADC1 (GPIO 32 a 36 e 39).

## Autores

- Mateus
- Victor Gabriel
- Arthur Mesquita
- Diogo Muzzi
- Lucas Mendes

## Licença

A definir.
