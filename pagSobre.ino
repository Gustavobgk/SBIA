#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>        
#include <WiFiManager.h>      
#include <LittleFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HX711.h>
#include <ESP32Servo.h>

// ===================== CONFIGURAÇÕES DO AP =====================
const char* wifiManagerSSID  = "Bafômetro";
const char* wifiManagerPASS  = "12345678";

// ===================== PINOS E OBJETOS =====================
#define SDA_PIN 23
#define SCL_PIN 5
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define PINO_MQ3 34
const int LIMITE_ALCOOL_RAW = 3000;

#define HX711_DOUT 33   
#define HX711_SCK  32   
const float FATOR_CALIBRACAO = 10000.0; // <-- COLOQUE SEU FATOR AQUI
const float LIMITE_PESO_KG   = 0.5;   

#define PINO_SERVO 15   
const int ANGULO_TRAVADO = 0;
const int ANGULO_LIBERADO = 90;

WebServer server(80);
HX711 balanca;
Servo travaServo; 

// ===================== MEMÓRIA COMPARTILHADA (MUTEX) =====================
SemaphoreHandle_t mutexValores;

int    globalRaw    = 0;
float  globalPpm    = 0.0;
String globalStatus = "CARREGANDO";
float  globalPeso   = 0.0;

// ===================== HANDLES DAS TASKS =====================
TaskHandle_t TaskCore0;
TaskHandle_t TaskCore1;

// ===================== FUNÇÕES AUXILIARES =====================
String getUptimeString() {
  unsigned long sec = millis() / 1000;
  return String(sec) + "s";
}

// ===================== ROTAS WEB =====================
void handleRoot() {
  String html = "<!DOCTYPE html>\n<html lang='pt-BR'>\n<head>\n<meta charset='UTF-8'>\n<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n<title>Painel do Bafometro</title>\n";
  html += "<link rel='stylesheet' href='https://unpkg.com/@sakun/system.css'>\n";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>\n";
  html += "<style>body{padding:20px; background-color:#e0e0e0;} canvas{background:#fff; width:100%!important; max-height:400px; border:1px solid #000; margin-top:10px;}</style>\n</head>\n<body>\n";

  html += "<div class='window'>\n<div class='title-bar'><h1 class='title'>SISTEMA CIBERFISICO - INTERTRAVAMENTO</h1></div>\n<div class='window-pane'>\n";
  html += "<h2>Dados ao Vivo</h2>\n<p>Motor: <strong id='statusVal' style='font-size:1.2em;'>CARREGANDO...</strong></p>\n";
  html += "<p>Valor Bruto: <span id='rawVal'>0</span> / 4095</p>\n<p>PPM Estimado: <span id='ppmVal'>0</span></p>\n";
  html += "<hr>\n<h2>Balanca (HX711)</h2>\n<p>Peso no Banco: <strong id='pesoVal'>0.00</strong> kg</p>\n";
  html += "<hr>\n<h2>Performance do Hardware</h2>\n<p>Tempo Ativo: <span id='uptimeVal'>0</span> s</p>\n";
  html += "<p>Memoria Livre: <span id='ramVal'>0</span> KB</p>\n</div>\n</div>\n";

  html += "<div class='window'>\n<div class='title-bar'><h1 class='title'>TELEMETRIA (Historico LittleFS)</h1></div>\n<div class='window-pane'>\n";
  html += "<canvas id='myChart'></canvas>\n<br>\n<button class='btn' onclick=\"window.location.href='/log'\">Baixar Arquivo CSV</button>\n</div>\n</div>\n";

  html += "<script>\nlet myChart;\nfetch('/log').then(response => response.text()).then(csvText => {\n";
  html += "const lines = csvText.split('\\n'); const labelsData = []; const ppmData = [];\n";
  html += "for(let i = 1; i < lines.length; i++) { const columns = lines[i].split(',');\n";
  html += "if(columns.length >= 5) { labelsData.push(columns[0]); ppmData.push(parseFloat(columns[2])); } }\n";
  html += "const ctx = document.getElementById('myChart').getContext('2d');\n";
  html += "myChart = new Chart(ctx, { type: 'line', data: { labels: labelsData, datasets: [{ label: 'Nivel (PPM)', data: ppmData, borderColor: '#000000', backgroundColor: 'rgba(0,0,0,0.1)', borderWidth: 2, pointRadius: 1, tension: 0.1 }] }, options: { animation: false } });\n";
  html += "iniciarTelemetriaAoVivo(); }).catch(err => console.error('Erro:', err));\n";

  html += "function iniciarTelemetriaAoVivo() { setInterval(() => { fetch('/data').then(r => r.json()).then(data => {\n";
  html += "const elStatus = document.getElementById('statusVal'); elStatus.innerText = data.status;\n";
  html += "elStatus.style.color = (data.status === 'BLOQUEADO') ? '#d9534f' : '#5cb85c';\n";
  html += "document.getElementById('rawVal').innerText = data.raw; document.getElementById('ppmVal').innerText = data.ppm;\n";
  html += "document.getElementById('pesoVal').innerText = data.peso; document.getElementById('uptimeVal').innerText = data.uptime;\n";
  html += "document.getElementById('ramVal').innerText = data.ram;\n";
  html += "myChart.data.labels.push(data.hora); myChart.data.datasets[0].data.push(data.ppm); myChart.update(); }); }, 1000); }\n";
  html += "</script>\n</body>\n</html>";

  html += "<div class='window'>\n<div class='title-bar'><h1 class='title'>SOBRE</h1></div>\n<div class='window-pane'>\n";
  html += "<button class='btn' onclick=\"window.location.href='/sobre'\">Sobre o Projeto</button>\n";
  html += "</div>\n</div>\n";

  server.send(200, "text/html", html);
}

void handleData() {
  xSemaphoreTake(mutexValores, portMAX_DELAY);
  String json = "{";
  json += "\"uptime\":"   + String(millis() / 1000)       + ",";
  json += "\"ram\":"      + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"raw\":"      + String(globalRaw)              + ",";
  json += "\"ppm\":\""    + String(globalPpm, 1)           + "\",";
  json += "\"status\":\"" + globalStatus                   + "\",";
  json += "\"peso\":\""   + String(globalPeso, 2)          + "\",";
  json += "\"hora\":\""   + getUptimeString()              + "\""; 
  json += "}";
  xSemaphoreGive(mutexValores);
  
  server.send(200, "application/json", json);
}

void handleLog() {
  File file = LittleFS.open("/log.csv", "r");
  if (!file) { server.send(500, "text/plain", "Falha ao abrir log."); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=log.csv");
  server.streamFile(file, "text/csv");
  file.close();
}

void handleResetWifi() {
  WiFiManager wm;
  wm.resetSettings();
  server.send(200, "text/html", "<p>Rede apagada! Reiniciando...</p>");
  delay(2000);
  ESP.restart();
}

void handleSobre() {
  String html = "<!DOCTYPE html>\n<html lang='pt-BR'>\n<head>\n";
  html += "<meta charset='UTF-8'>\n<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "<title>Sobre o Projeto</title>\n";
  html += "<link rel='stylesheet' href='https://unpkg.com/@sakun/system.css'>\n";
  html += "<style>body{padding:20px; background-color:#e0e0e0;} p{text-align: justify; line-height: 1.6; margin-bottom: 12px;}</style>\n</head>\n<body>\n";
  html += "<div class='window'>\n<div class='title-bar'><h1 class='title'>SOBRE O PROJETO</h1></div>\n<div class='window-pane'>\n";
  html += "<h2>Bafômetro com Intertravamento</h2>\n";
  html += "<p>Nosso projeto consiste em um sistema de Bloqueio de Ignição por Álcool, desenvolvido com o objetivo de aumentar a segurança no trânsito e evitar que motoristas alcoolizados utilizem veículos.</p>\n";
  html += "<p>Para isso, utilizamos um ESP32 como controlador principal, um display LCD para exibir informações do sistema, um servo motor que simula uma cancela de liberação ou bloqueio, um sensor de peso HX711 para identificar se há alguém sentado no banco do motorista e um sensor de álcool responsável por detectar sinais de embriaguez.</p>\n";
  html += "<p>O funcionamento ocorre da seguinte forma: ao identificar a presença de um motorista através do sensor de peso, o sensor de álcool realiza a verificação do nível de álcool. Caso o motorista não esteja alcoolizado, o servo motor libera a cancela, permitindo a ignição do veículo. Se for detectado álcool acima do permitido, o sistema bloqueia a liberação, impedindo o funcionamento do automóvel.</p>\n";
  html += "<p>Nosso principal objetivo é aplicar essa tecnologia em empresas de locação de veículos e transportadoras, ajudando a reduzir acidentes, aumentar a segurança e garantir maior controle sobre os motoristas antes da utilização dos veículos.</p>\n";
  html += "<p>Integrantes: Adriano Barbosa, Bernardo Fadel, Gustavo Boganika, Matheus Alberti, João Kraft</p>\n";
  html += "<br><button class='btn' onclick=\"window.location.href='/'\">Voltar</button>\n";
  html += "</div>\n</div>\n</body>\n</html>";
  html += "<button class='btn' onclick=\"window.location.href='/resetwifi'\">Desconectar Wi-Fi</button>\n";
  server.send(200, "text/html", html);
}

// ===================== NÚCLEO 0: WEB, LCD, SERVO E LOGS =====================
void codigoTaskCore0(void * pvParameters) {
  unsigned long lastLcdUpdate = 0;
  unsigned long lastLogUpdate = 0;
  bool telaBalanca = false;

  for(;;) {
    server.handleClient(); 

    // Pega as variáveis protegidas
    xSemaphoreTake(mutexValores, portMAX_DELAY);
    float pesoLocal = globalPeso;
    float ppmLocal = globalPpm;
    int rawLocal = globalRaw;
    String statusLocal = globalStatus;
    xSemaphoreGive(mutexValores);

    unsigned long currentMillis = millis();
    
    // 1. Atualiza LCD a cada 3s
    if (currentMillis - lastLcdUpdate >= 3000) {
      lastLcdUpdate = currentMillis;
      lcd.clear();
      if (telaBalanca) {
        lcd.setCursor(0, 0); lcd.print("Peso Banco:");
        lcd.setCursor(0, 1); lcd.print(pesoLocal, 2); lcd.print(" kg");
      } else {
        lcd.setCursor(0, 0); lcd.print("Alcool MQ-3:"); lcd.print(rawLocal);
        lcd.setCursor(0, 1); lcd.print(statusLocal);
      }
      telaBalanca = !telaBalanca; 
    }

    // 2. Lógica do Servo
    if (pesoLocal > LIMITE_PESO_KG && statusLocal == "LIBERADO ") {
      travaServo.write(ANGULO_LIBERADO);
    } else {
      travaServo.write(ANGULO_TRAVADO);
    }

    // 3. Grava Log no LittleFS a cada 10s
    if (currentMillis - lastLogUpdate >= 10000) {
      lastLogUpdate = currentMillis;
      File file = LittleFS.open("/log.csv", "a");
      if (file) {
        file.print(getUptimeString()); file.print(",");
        file.print(rawLocal); file.print(",");
        file.print(ppmLocal, 1); file.print(",");
        file.print(statusLocal); file.print(",");
        file.println(pesoLocal, 2);
        file.close();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // RTOS Yield (NÃO BLOQUEANTE)
  }
}

// ===================== NÚCLEO 1: SENSORES =====================
void codigoTaskCore1(void * pvParameters) {
  // Inicialização e Tara seguras e não-bloqueantes do HX711
  if (balanca.is_ready()) {
    balanca.set_scale(FATOR_CALIBRACAO);
    vTaskDelay(pdMS_TO_TICKS(300)); // Substitui o delay() proibido
    balanca.tare();
    Serial.println("Balanca zerada no Core 1!");
  }

  for(;;) {
    // 1. Lê a Balança
    float pesoLido = globalPeso; 
    if (balanca.is_ready()) {
      pesoLido = balanca.get_units(3);
      if (pesoLido < 0) pesoLido = 0.0;
    }

    // 2. Lê o Bafômetro
    int rawLido = analogRead(PINO_MQ3);
    float ppmLido = (rawLido / 4095.0) * 1000.0;
    String statusLido = (rawLido >= LIMITE_ALCOOL_RAW) ? "BLOQUEADO" : "LIBERADO ";

    // 3. Atualiza globais
    xSemaphoreTake(mutexValores, portMAX_DELAY);
    globalPeso   = pesoLido;
    globalRaw    = rawLido;
    globalPpm    = ppmLido;
    globalStatus = statusLido;
    xSemaphoreGive(mutexValores);

    vTaskDelay(pdMS_TO_TICKS(200)); // RTOS Yield (NÃO BLOQUEANTE)
  }
}

// ===================== SETUP PRINCIPAL =====================
void setup() {
  Serial.begin(115200);
  mutexValores = xSemaphoreCreateMutex();

  pinMode(PINO_MQ3, INPUT);
  travaServo.attach(PINO_SERVO); 
  travaServo.write(ANGULO_TRAVADO);

  // LittleFS Setup (Não bloqueante)
  if (LittleFS.begin(true)) {
    File file = LittleFS.open("/log.csv", "a");
    if (file.size() == 0) file.println("Uptime,Bruto,PPM,Status,Peso_kg");
    file.close();
  }

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  
  balanca.begin(HX711_DOUT, HX711_SCK);

  // ===================== CONEXÃO WI-FI COM WIFIMANAGER =====================


  lcd.setCursor(0, 0); lcd.print("Conectando...");
  lcd.setCursor(0, 1); lcd.print("WiFiManager");

  
  WiFiManager wm;
  // wm.resetSettings(); //  se quiser dar um reset descomente esta linha uma vez para apagar a rede salva e forçar o portal

  //  Tenta conectar na rede salva se falhar, abre o portal "bafometro" com senha "12345678"
  bool connected = wm.autoConnect(wifiManagerSSID, wifiManagerPASS);

  if (connected) {
    // Conectou com sucesso: exibe IP no LCD e no Serial
    Serial.println("WiFi conectado!");
    Serial.println("==============================");
    Serial.print("Acesse: http://");
    Serial.println(WiFi.localIP());
    Serial.println("==============================");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WiFi OK!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
  } else {
    // Não conseguiu conectar  reinicia o ESP32
    Serial.println("Falha ao conectar. Reiniciando...");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Falha WiFi!");
    lcd.setCursor(0, 1); lcd.print("Reiniciando...");
    delay(2000);
    ESP.restart();
  }
  

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/log", handleLog);
  server.on("/sobre", handleSobre);
  server.on("/resetwifi", handleResetWifi);
  server.begin();

  // Tarefas nos núcleos
  xTaskCreatePinnedToCore(codigoTaskCore0, "TaskCore0", 10000, NULL, 1, &TaskCore0, 0);
  xTaskCreatePinnedToCore(codigoTaskCore1, "TaskCore1", 10000, NULL, 1, &TaskCore1, 1);
}

void loop() {
  vTaskDelay(portMAX_DELAY); // Deixa o RTOS cuidar do resto
}