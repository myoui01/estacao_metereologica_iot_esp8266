#include <ESP8266WiFi.h>
#include <DHT.h>

// ======= CONFIGURAÇÃO Wi-Fi =======
const char* ssid = "ACONTECE TWIBI";
const char* password = "QaLBH8uyiu";

WiFiServer server(80);

// ======= SENSOR DHT22 =======
#define DHT_SENSOR_PIN  D7
#define DHT_SENSOR_TYPE DHT22
DHT dht_sensor(DHT_SENSOR_PIN, DHT_SENSOR_TYPE);

// ======= SENSOR DE CHUVA =======
#define RAIN_DO_PIN  D5   // Pino digital do sensor de chuva
#define RAIN_POWER_PIN D6 // Pino de alimentação do sensor de chuva

// ======= SENSOR DE GÁS MQ2 =======
#define MQ2_AO_PIN A0    // Pino analógico do MQ2

// ======= LEDS =======
#define LED_RED_PIN D4   // GPIO2
#define LED_GREEN_PIN D3 // GPIO0
#define LED_BLUE_PIN D1 // GPIO5

// Variáveis para modo manual/automático
bool modoManual = false;
String corManual = "verde"; // verde padrão

// ======= Variáveis do sensor MQ2 para média móvel =======
const int mq2Samples = 10;          // número de amostras para média móvel
int mq2Readings[mq2Samples];        // buffer das leituras
int mq2Index = 0;
long mq2Total = 0;

String header; // usado para processar requisição HTTP

void setup() {
  Serial.begin(115200);

  // Inicializa sensores
  dht_sensor.begin();
  pinMode(RAIN_DO_PIN, INPUT);
  pinMode(RAIN_POWER_PIN, OUTPUT);
  digitalWrite(RAIN_POWER_PIN, LOW); // Começa desligado

  // Inicializa LEDs (saída)
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN, OUTPUT);
  desligarLEDs();

  // Inicializa Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi conectado! IP: ");
  Serial.println(WiFi.localIP());

  server.begin();

  Serial.println("Aquecendo o sensor MQ2...");
  delay(20000); // Tempo para aquecer o MQ2

  // Inicializa o buffer com leituras iniciais para evitar lixo
  for (int i = 0; i < mq2Samples; i++) {
    mq2Readings[i] = analogRead(MQ2_AO_PIN);
    delay(50);
  }
  // Soma inicial do buffer
  mq2Total = 0;
  for (int i = 0; i < mq2Samples; i++) {
    mq2Total += mq2Readings[i];
  }
}

void loop() {
  // ======== LEITURA DOS SENSORES ========
  // DHT22 - Temperatura e Umidade
  float humi = dht_sensor.readHumidity();
  float tempC = dht_sensor.readTemperature();

  if (isnan(humi) || isnan(tempC)) {
    Serial.println("Falha na leitura do DHT!");
  }

  // MQ2 - Sensor de Gás (média móvel)
  mq2Total -= mq2Readings[mq2Index];
  int mq2NewReading = analogRead(MQ2_AO_PIN);
  mq2Readings[mq2Index] = mq2NewReading;
  mq2Total += mq2NewReading;
  mq2Index = (mq2Index + 1) % mq2Samples;
  int mq2Average = mq2Total / mq2Samples;

  Serial.print("MQ2 - Valor médio do gás: ");
  Serial.println(mq2Average);

  // Sensor de Chuva
  digitalWrite(RAIN_POWER_PIN, HIGH); // Liga sensor de chuva
  delay(10);
  int rainState = digitalRead(RAIN_DO_PIN);
  digitalWrite(RAIN_POWER_PIN, LOW);  // Desliga para poupar

  String rainStatus = (rainState == HIGH) ? "Chuva Detectada ☔" : "Sem Chuva ☀️";
  Serial.println(rainStatus);

  // ======== Controle dos LEDs ========
  if (!modoManual) {
    // Modo automático: LEDs indicam status
    controleLEDsAutomatico(tempC, mq2Average, rainState);
  } else {
    // Modo manual: LEDs acendem conforme cor escolhida na web
    controleLEDsManual(corManual);
  }

  // ======== SERVIDOR WEB ========
  WiFiClient client = server.available();
  if (client) {
    Serial.println("Novo cliente conectado.");
    String currentLine = "";
    unsigned long currentTime = millis();
    unsigned long previousTime = currentTime;
    const long timeoutTime = 2000;

    while (client.connected() && (currentTime - previousTime <= timeoutTime)) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Requisição completa, processar e enviar resposta
            responderCliente(client, tempC, humi, rainStatus, mq2Average, modoManual, corManual);
            break;
          } else {
            // Verificar comandos GET para modo manual
            if (header.indexOf("GET /manual/vermelho") >= 0) {
              modoManual = true;
              corManual = "vermelho";
            } else if (header.indexOf("GET /manual/verde") >= 0) {
              modoManual = true;
              corManual = "verde";
            } else if (header.indexOf("GET /manual/azul") >= 0) {
              modoManual = true;
              corManual = "azul";
            } else if (header.indexOf("GET /automatico") >= 0) {
              modoManual = false; // volta para automático
            }
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    // Limpa o header para próxima conexão
    header = "";
    client.stop();
    Serial.println("Cliente desconectado.");
  }

  delay(2000);
}

// Função para desligar todos os LEDs
void desligarLEDs() {
  digitalWrite(LED_RED_PIN, LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN, LOW);
}

// Controle automático dos LEDs conforme sensores
void controleLEDsAutomatico(float temp, int gas, int chuva) {
  desligarLEDs();

  // Prioridade para alerta de gás ou alta temperatura
  if (gas >= 400 || temp >= 40) {
    digitalWrite(LED_RED_PIN, HIGH);
  } 
  // Se tudo normal
  else if (gas < 200 && temp < 40 && chuva == LOW) {
    digitalWrite(LED_GREEN_PIN, HIGH);
  }
  // Chuva detectada ou baixa umidade
  else if (chuva == HIGH) {
    digitalWrite(LED_BLUE_PIN, HIGH);
  }
}

// Controle manual dos LEDs pela cor escolhida na web
void controleLEDsManual(String cor) {
  desligarLEDs();
  if (cor == "vermelho") {
    digitalWrite(LED_RED_PIN, HIGH);
  } else if (cor == "verde") {
    digitalWrite(LED_GREEN_PIN, HIGH);
  } else if (cor == "azul") {
    digitalWrite(LED_BLUE_PIN, HIGH);
  }
}

// Responde o cliente HTTP com a interface web
void responderCliente(WiFiClient &client, float tempC, float humi, String rainStatus, int mq2Average, bool modoManual, String corManual) {
  // Status do gás com cores
  String mq2Status = "";
  String gasStatusClass = "status-good";

  if (mq2Average < 200) {
    mq2Status = "Ar limpo ✅";
  } else if (mq2Average < 400) {
    mq2Status = "Gás detectado ⚠️";
    gasStatusClass = "status-warning";
  } else {
    mq2Status = "Alerta de gás! 🚨";
    gasStatusClass = "status-danger";
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();

  // ====== Página HTML ======
  client.println("<!DOCTYPE html><html lang='pt-br'>");
  client.println("<head><meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<title>ESP8266 Monitoramento IoT</title>");
  client.println("<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>");
  client.println("<style>");
  client.println("body { margin:0; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background:#121212; color:#e0e0e0; }");
  client.println(".container { max-width: 900px; margin:auto; padding: 20px; }");
  client.println(".card { background:#1f1f1f; border-radius:12px; padding:25px 30px; margin-bottom:25px; box-shadow: 0 5px 15px rgba(0,0,0,0.6); }");
  client.println(".title { font-size:28px; margin-bottom:15px; font-weight:700; color:#81d4fa; }");
  client.println(".value { font-size:22px; margin-bottom:12px; }");
  client.println(".value span { font-weight:600; }");
  client.println(".status-good { color:#4caf50; font-weight:700; }");
  client.println(".status-warning { color:#ff9800; font-weight:700; }");
  client.println(".status-danger { color:#f44336; font-weight:700; }");
  client.println(".btn { padding: 10px 25px; margin: 10px 5px 0 0; border:none; border-radius: 5px; cursor:pointer; font-weight:700; font-size:16px; }");
  client.println(".btn-vermelho { background:#f44336; color:#fff; }");
  client.println(".btn-verde { background:#4caf50; color:#fff; }");
  client.println(".btn-azul { background:#2196f3; color:#fff; }");
  client.println(".btn-automatico { background:#000000; color:#fff; }");
  client.println("@media(max-width:600px) { .container { padding:10px; } .title { font-size:24px; } .value { font-size:18px; } }");
  client.println("</style></head>");

  client.println("<body><div class='container'>");
  client.println("<h1>🌐 Monitoramento IoT - ESP8266</h1>");

  // Dados em tempo real
  client.println("<div class='card'>");
  client.println("<div class='title'>📊 Dados em Tempo Real</div>");
  client.println("<div class='value'>🌡️ Temperatura: <span>" + String(tempC, 1) + "°C</span></div>");
  client.println("<div class='value'>💧 Umidade: <span>" + String(humi, 1) + "%</span></div>");
  client.println("<div class='value'>🌧️ Chuva: <span>" + rainStatus + "</span></div>");
  client.println("<div class='value'>🔥 Gás (MQ2): <span>" + String(mq2Average) + " — <span class='" + gasStatusClass + "'>" + mq2Status + "</span></span></div>");
  client.println("</div>");

    // Gráfico
  client.println("<div class='card'>");
  client.println("<div class='title'>Gráfico de Temperatura e Umidade</div>");
  client.println("<canvas id='myChart' style='max-width:100%; height:300px;'></canvas>");
  client.println("</div>");

  // Script do Gráfico
  client.println("<script>");
  client.println("const labels = ['Agora'];");
  client.println("const data = { labels: labels, datasets: [");
  client.println("{ label: 'Temperatura (°C)', backgroundColor: 'rgba(244, 67, 54, 0.7)', borderColor: 'rgba(244, 67, 54, 1)', data: [" + String(tempC, 1) + "], borderWidth: 1 },");
  client.println("{ label: 'Umidade (%)', backgroundColor: 'rgba(33, 150, 243, 0.7)', borderColor: 'rgba(33, 150, 243, 1)', data: [" + String(humi, 1) + "], borderWidth: 1 }");
  client.println("]};");
  client.println("const config = { type: 'bar', data: data, options: { responsive: true, scales: { y: { beginAtZero: true, max: 100 } } } };");
  client.println("const myChart = new Chart(document.getElementById('myChart'), config);");
  client.println("</script>");

  // Botões para modo manual
  client.println("<div class='card'>");
  client.println("<div class='title'>🎨 Controle Manual do LED RGB</div>");
  client.println("<p>Modo Atual: <b>" + (modoManual ? "Manual (Cor: " + corManual + ")" : "Automático") + "</b></p>");
  client.println("<a href='/manual/vermelho'><button class='btn btn-vermelho'>Vermelho</button></a>");
  client.println("<a href='/manual/verde'><button class='btn btn-verde'>Verde</button></a>");
  client.println("<a href='/manual/azul'><button class='btn btn-azul'>Azul</button></a>");
  client.println("</div>");

  client.println("</div></body></html>");
  client.println();
}
