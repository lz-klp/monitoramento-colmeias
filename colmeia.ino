#include <Arduino.h>
#include <DHT.h>
#include "HX711.h"
#include <WiFi.h>
#include <HTTPClient.h>


// ============================================================
// CONFIGURAÇÃO GERAL
// ============================================================

constexpr char DEVICE_ID[] = "ESP32-001";
constexpr char HIVE_ID[] = "COLMEIA-001";

constexpr unsigned long INTERVALO_LEITURA = 5000;


// ============================================================
// WI-FI
// ============================================================

// Substitua pelos dados da sua rede Wi-Fi.
constexpr char WIFI_SSID[] = "NOME_DA_REDE";
constexpr char WIFI_PASSWORD[] = "SENHA_DA_REDE";

// Tempo máximo de espera na tentativa inicial de conexão.
constexpr unsigned long WIFI_TIMEOUT = 15000;


// ============================================================
// SERVIDOR FASTAPI
// ============================================================

constexpr char SERVER_URL[] =
    "http://192.168.0.109:8000/measurements";

constexpr unsigned long HTTP_TIMEOUT = 5000;


// ============================================================
// PINOS
// ============================================================

constexpr uint8_t PIN_DHT_INTERNO = 4;
constexpr uint8_t PIN_DHT_EXTERNO = 5;

constexpr uint8_t PIN_HX711_DT = 18;
constexpr uint8_t PIN_HX711_SCK = 19;


// ============================================================
// DHT22
// ============================================================

#define DHT_TYPE DHT22

DHT dhtInterno(PIN_DHT_INTERNO, DHT_TYPE);
DHT dhtExterno(PIN_DHT_EXTERNO, DHT_TYPE);


// ============================================================
// HX711
// ============================================================

HX711 balanca;

// Valor provisório.
// Será substituído após a calibração da célula de carga.
float fatorCalibracao = 1.0;


// ============================================================
// CONTROLE DE TEMPO
// ============================================================

unsigned long ultimaLeitura = 0;


// ============================================================
// ESTRUTURA DOS DADOS
// ============================================================

struct DadosColmeia {
    unsigned long timestamp;

    float temperaturaInterna;
    float umidadeInterna;

    float temperaturaExterna;
    float umidadeExterna;

    float pesoKg;

    bool dhtInternoValido;
    bool dhtExternoValido;
    bool pesoValido;
};


// ============================================================
// PROTÓTIPOS DAS FUNÇÕES
// ============================================================

void iniciarSensores();

void conectarWiFi();

void manterWiFi();

DadosColmeia lerSensores();

bool leituraDhtValida(
    float temperatura,
    float umidade
);

void exibirDados(
    const DadosColmeia &dados
);

void exibirCsv(
    const DadosColmeia &dados
);

String gerarJson(
    const DadosColmeia &dados
);

void exibirJson(
    const DadosColmeia &dados
);

void enviarDados(
    const DadosColmeia &dados
);


// ============================================================
// SETUP
// ============================================================

void setup() {
    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("==========================================");
    Serial.println(" MONITORAMENTO INTELIGENTE DE COLMEIAS");
    Serial.println(" VERSAO 3 - WI-FI + JSON + HTTP");
    Serial.println("==========================================");
    Serial.println();

    iniciarSensores();

    Serial.println();

    conectarWiFi();

    Serial.println();
    Serial.println("Sistema iniciado.");
    Serial.println();
}


// ============================================================
// LOOP
// ============================================================

void loop() {
    manterWiFi();

    const unsigned long tempoAtual = millis();

    if (tempoAtual - ultimaLeitura >= INTERVALO_LEITURA) {
        ultimaLeitura = tempoAtual;

        const DadosColmeia dados = lerSensores();

        exibirDados(dados);
        exibirCsv(dados);
        exibirJson(dados);
        enviarDados(dados);
    }
}


// ============================================================
// INICIALIZAÇÃO DOS SENSORES
// ============================================================

void iniciarSensores() {
    Serial.println("Inicializando sensores...");

    // DHT22
    dhtInterno.begin();
    dhtExterno.begin();

    Serial.println("DHT22 inicializados.");

    // HX711
    balanca.begin(PIN_HX711_DT, PIN_HX711_SCK);

    if (balanca.is_ready()) {
        Serial.println("HX711 detectado.");

        balanca.set_scale(fatorCalibracao);

        Serial.println("Realizando tara...");
        balanca.tare();

        Serial.println("Tara concluida.");
    } else {
        Serial.println("ERRO: HX711 nao encontrado.");
    }
}


// ============================================================
// CONEXÃO WI-FI
// ============================================================

void conectarWiFi() {
    Serial.print("Conectando ao Wi-Fi: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long inicioTentativa = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - inicioTentativa < WIFI_TIMEOUT
    ) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Wi-Fi conectado.");

        Serial.print("Endereco IP: ");
        Serial.println(WiFi.localIP());

        Serial.print("Intensidade do sinal: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.println("ERRO: nao foi possivel conectar ao Wi-Fi.");
        Serial.println("O sistema continuara lendo os sensores.");
    }
}


// ============================================================
// MANUTENÇÃO DA CONEXÃO WI-FI
// ============================================================

void manterWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    static unsigned long ultimaTentativa = 0;
    constexpr unsigned long INTERVALO_RECONEXAO = 10000;

    const unsigned long tempoAtual = millis();

    if (tempoAtual - ultimaTentativa < INTERVALO_RECONEXAO) {
        return;
    }

    ultimaTentativa = tempoAtual;

    Serial.println("Wi-Fi desconectado. Tentando reconectar...");

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}


// ============================================================
// LEITURA DOS SENSORES
// ============================================================

DadosColmeia lerSensores() {
    DadosColmeia dados;

    dados.timestamp = millis();

    // --------------------------------------------------------
    // DHT22 interno
    // --------------------------------------------------------

    dados.temperaturaInterna = dhtInterno.readTemperature();
    dados.umidadeInterna = dhtInterno.readHumidity();

    dados.dhtInternoValido = leituraDhtValida(
        dados.temperaturaInterna,
        dados.umidadeInterna
    );

    // --------------------------------------------------------
    // DHT22 externo
    // --------------------------------------------------------

    dados.temperaturaExterna = dhtExterno.readTemperature();
    dados.umidadeExterna = dhtExterno.readHumidity();

    dados.dhtExternoValido = leituraDhtValida(
        dados.temperaturaExterna,
        dados.umidadeExterna
    );

    // --------------------------------------------------------
    // HX711
    // --------------------------------------------------------

    if (balanca.is_ready()) {
        dados.pesoKg = balanca.get_units(10);
        dados.pesoValido = true;
    } else {
        dados.pesoKg = NAN;
        dados.pesoValido = false;
    }

    return dados;
}


// ============================================================
// VALIDAÇÃO DOS DHT22
// ============================================================

bool leituraDhtValida(
    const float temperatura,
    const float umidade
) {
    return !isnan(temperatura) && !isnan(umidade);
}


// ============================================================
// SAÍDA LEGÍVEL
// ============================================================

void exibirDados(
    const DadosColmeia &dados
) {
    Serial.println("------------------------------------------");

    Serial.print("Dispositivo: ");
    Serial.println(DEVICE_ID);

    Serial.print("Colmeia: ");
    Serial.println(HIVE_ID);

    Serial.print("Tempo: ");
    Serial.print(dados.timestamp / 1000);
    Serial.println(" s");

    Serial.print("Wi-Fi: ");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("conectado");
    } else {
        Serial.println("desconectado");
    }

    Serial.println();

    // --------------------------------------------------------
    // Interior
    // --------------------------------------------------------

    Serial.println("INTERIOR DA COLMEIA");

    if (dados.dhtInternoValido) {
        Serial.print("Temperatura: ");
        Serial.print(dados.temperaturaInterna, 2);
        Serial.println(" C");

        Serial.print("Umidade: ");
        Serial.print(dados.umidadeInterna, 2);
        Serial.println(" %");
    } else {
        Serial.println("Erro na leitura do DHT22 interno.");
    }

    Serial.println();

    // --------------------------------------------------------
    // Exterior
    // --------------------------------------------------------

    Serial.println("AMBIENTE EXTERNO");

    if (dados.dhtExternoValido) {
        Serial.print("Temperatura: ");
        Serial.print(dados.temperaturaExterna, 2);
        Serial.println(" C");

        Serial.print("Umidade: ");
        Serial.print(dados.umidadeExterna, 2);
        Serial.println(" %");
    } else {
        Serial.println("Erro na leitura do DHT22 externo.");
    }

    Serial.println();

    // --------------------------------------------------------
    // Peso
    // --------------------------------------------------------

    Serial.println("SISTEMA DE PESAGEM");

    if (dados.pesoValido) {
        Serial.print("Peso: ");
        Serial.print(dados.pesoKg, 3);
        Serial.println(" kg");
    } else {
        Serial.println("Erro na leitura do HX711.");
    }

    Serial.println("------------------------------------------");
}


// ============================================================
// SAÍDA CSV
// ============================================================

void exibirCsv(
    const DadosColmeia &dados
) {
    Serial.print("CSV;");

    Serial.print(DEVICE_ID);
    Serial.print(";");

    Serial.print(HIVE_ID);
    Serial.print(";");

    Serial.print(dados.timestamp);
    Serial.print(";");

    // DHT interno
    if (dados.dhtInternoValido) {
        Serial.print(dados.temperaturaInterna, 2);
    } else {
        Serial.print("NA");
    }

    Serial.print(";");

    if (dados.dhtInternoValido) {
        Serial.print(dados.umidadeInterna, 2);
    } else {
        Serial.print("NA");
    }

    Serial.print(";");

    // DHT externo
    if (dados.dhtExternoValido) {
        Serial.print(dados.temperaturaExterna, 2);
    } else {
        Serial.print("NA");
    }

    Serial.print(";");

    if (dados.dhtExternoValido) {
        Serial.print(dados.umidadeExterna, 2);
    } else {
        Serial.print("NA");
    }

    Serial.print(";");

    // Peso
    if (dados.pesoValido) {
        Serial.print(dados.pesoKg, 3);
    } else {
        Serial.print("NA");
    }

    Serial.println();
}


// ============================================================
// GERAÇÃO DO JSON
// ============================================================

String gerarJson(
    const DadosColmeia &dados
) {
    String json;

    // Reserva memória para reduzir realocações da String.
    json.reserve(320);

    json += "{";

    json += "\"device_id\":\"";
    json += DEVICE_ID;
    json += "\",";

    json += "\"hive_id\":\"";
    json += HIVE_ID;
    json += "\",";

    json += "\"timestamp_ms\":";
    json += String(dados.timestamp);
    json += ",";

    json += "\"wifi_connected\":";
    json += (
        WiFi.status() == WL_CONNECTED
        ? "true"
        : "false"
    );
    json += ",";

    json += "\"temperature_internal\":";
    if (dados.dhtInternoValido) {
        json += String(dados.temperaturaInterna, 2);
    } else {
        json += "null";
    }
    json += ",";

    json += "\"humidity_internal\":";
    if (dados.dhtInternoValido) {
        json += String(dados.umidadeInterna, 2);
    } else {
        json += "null";
    }
    json += ",";

    json += "\"temperature_external\":";
    if (dados.dhtExternoValido) {
        json += String(dados.temperaturaExterna, 2);
    } else {
        json += "null";
    }
    json += ",";

    json += "\"humidity_external\":";
    if (dados.dhtExternoValido) {
        json += String(dados.umidadeExterna, 2);
    } else {
        json += "null";
    }
    json += ",";

    json += "\"weight_kg\":";
    if (dados.pesoValido) {
        json += String(dados.pesoKg, 3);
    } else {
        json += "null";
    }

    json += "}";

    return json;
}


// ============================================================
// SAÍDA JSON
// ============================================================

void exibirJson(
    const DadosColmeia &dados
) {
    const String json = gerarJson(dados);

    Serial.print("JSON:");
    Serial.println(json);
}



// ============================================================
// ENVIO DOS DADOS PARA O FASTAPI
// ============================================================

void enviarDados(
    const DadosColmeia &dados
) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("HTTP: envio cancelado - Wi-Fi desconectado.");
        return;
    }

    const String json = gerarJson(dados);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT);

    if (!http.begin(SERVER_URL)) {
        Serial.println("HTTP: erro ao iniciar conexao com o servidor.");
        return;
    }

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    Serial.print("HTTP: enviando dados para ");
    Serial.println(SERVER_URL);

    const int codigoResposta = http.POST(json);

    if (codigoResposta > 0) {
        Serial.print("HTTP: codigo de resposta = ");
        Serial.println(codigoResposta);

        const String resposta = http.getString();

        if (resposta.length() > 0) {
            Serial.print("HTTP: resposta do servidor = ");
            Serial.println(resposta);
        }

        if (codigoResposta == 201) {
            Serial.println("HTTP: medicao salva com sucesso.");
        } else {
            Serial.println("HTTP: servidor respondeu, mas nao com 201.");
        }
    } else {
        Serial.print("HTTP: falha no envio. Erro = ");
        Serial.println(http.errorToString(codigoResposta));
    }

    http.end();
}
