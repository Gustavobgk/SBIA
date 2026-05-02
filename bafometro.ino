#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h> 

// credenciais de rede
const char* ssid = "Gtr";
const char* password = "12345678";

WebServer server(80);

// rotas I2C e LCD
#define SDA_PIN 23
#define SCL_PIN 5
LiquidCrystal_I2C lcd(0x27, 16, 2); 

#define PINO_MQ3 34
const int LIMITE_ALCOOL_RAW = 3000; 

// NTP config UTC-3
const long  gmtOffset_sec = -10800; 
const int   daylightOffset_sec = 0; 

// Timers
unsigned long previousLcdMillis = 0;
const long lcdInterval = 500; 
unsigned long previousLogMillis = 0;
const long logInterval = 10000; 

int currentRaw = 0;
float currentPpm = 0.0;
String currentStatus = "LIBERADO";

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) {
    return "Aguardando_NTP"; 
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void handleRoot() {
  String html = "<!DOCTYPE html>\n";
  html += "<html lang='pt-BR'>\n";
  html += "<head>\n";
  html += "<meta charset='UTF-8'>\n";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n";
  html += "<title>Painel do Bafometro</title>\n";
  html += "<link rel='stylesheet' href='https://unpkg.com/@sakun/system.css'>\n";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>\n";
  html += "<style>\n";
  html += "body { padding: 20px; background-color: #e0e0e0; }\n"; 
  html += ".window { margin-bottom: 20px; }\n";
  html += "canvas { background-color: #ffffff; width: 100% !important; max-height: 400px; border: 1px solid #000; margin-top: 10px; }\n";
  html += "a { color: #000; font-weight: bold; }\n";
  html += "</style>\n";
  html += "</head>\n";
  html += "<body>\n";
  
  html += "<div class='window'>\n";
  html += "<div class='title'>SISTEMA DE INTERTRAVAMENTO </div>\n";
  html += "<div class='window-pane'>\n";
  html += "<h2>Dados ao Vivo</h2>\n";
  html += "<p>Motor: <strong id='statusVal' style='font-size: 1.2em;'>CARREGANDO...</strong></p>\n";
  html += "<p>Valor Bruto: <span id='rawVal'>0</span> / 4095</p>\n";
  html += "<p>PPM Estimado: <span id='ppmVal'>0</span></p>\n";
  html += "<hr>\n";
  html += "<h2>Desempenho do ESP32</h2>\n";
  html += "<p>Tempo Ativo: <span id='uptimeVal'>0</span> s</p>\n";
  html += "<p>Memoria Livre: <span id='ramVal'>0</span> KB</p>\n";
  html += "</div>\n";
  html += "</div>\n";

  html += "<div class='window'>\n";
  html += "<div class='title'>TELEMETRIA (Historico LittleFS)</div>\n";
  html += "<div class='window-pane'>\n";
  html += "<canvas id='myChart'></canvas>\n";
  html += "<br>\n";
  html += "<button class='btn' onclick=\"window.location.href='/log'\">Baixar Arquivo CSV</button>\n";
  html += "</div>\n";
  html += "</div>\n";

  html += "<script>\n";
  html += "let myChart;\n";
  html += "fetch('/log')\n";
  html += ".then(response => response.text())\n";
  html += ".then(csvText => {\n";
  html += "const lines = csvText.split('\\n');\n"; 
  html += "const labelsData = [];\n";
  html += "const ppmData = [];\n";
  html += "for(let i = 1; i < lines.length; i++) {\n";
  html += "const columns = lines[i].split(',');\n";
  html += "if(columns.length >= 4) {\n";
  html += "labelsData.push(columns[0]);\n";
  html += "ppmData.push(parseFloat(columns[2]));\n";
  html += "}\n";
  html += "}\n";
  html += "const ctx = document.getElementById('myChart').getContext('2d');\n";
  html += "myChart = new Chart(ctx, {\n";
  html += "type: 'line',\n";
  html += "data: {\n";
  html += "labels: labelsData,\n";
  html += "datasets: [{\n";
  html += "label: 'Nivel (PPM)',\n";
  html += "data: ppmData,\n";
  html += "borderColor: '#000000',\n"; // Linha preta para combinar com Mac OS
  html += "backgroundColor: 'rgba(0, 0, 0, 0.1)',\n";
  html += "borderWidth: 2,\n";
  html += "pointRadius: 1,\n";
  html += "tension: 0.1\n";
  html += "}]\n";
  html += "},\n";
  html += "options: { scales: { y: { beginAtZero: true } }, animation: false }\n";
  html += "});\n";
  html += "iniciarTelemetriaAoVivo();\n";
  html += "})\n";
  html += ".catch(err => console.error('Erro no historico: ', err));\n";

  html += "function iniciarTelemetriaAoVivo() {\n";
  html += "setInterval(() => {\n";
  html += "fetch('/data')\n";
  html += ".then(r => r.json())\n";
  html += ".then(data => {\n";
  html += "const elStatus = document.getElementById('statusVal');\n";
  html += "elStatus.innerText = data.status;\n";
  // Feedback visual clássico sem quebrar o layout limpo
  html += "elStatus.style.color = (data.status === 'BLOQUEADO') ? '#d9534f' : '#5cb85c';\n";
  html += "document.getElementById('rawVal').innerText = data.raw;\n";
  html += "document.getElementById('ppmVal').innerText = data.ppm;\n";
  html += "document.getElementById('uptimeVal').innerText = data.uptime;\n";
  html += "document.getElementById('ramVal').innerText = data.ram;\n";
  html += "myChart.data.labels.push(data.hora);\n";
  html += "myChart.data.datasets[0].data.push(data.ppm);\n";
  html += "myChart.update();\n";
  html += "});\n";
  html += "}, 1000);\n";
  html += "}\n";
  html += "</script>\n";
  html += "</body>\n";
  html += "</html>\n";

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"ram\":" + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"raw\":" + String(currentRaw) + ",";
  json += "\"ppm\":\"" + String(currentPpm, 1) + "\",";
  json += "\"status\":\"" + currentStatus + "\",";
  json += "\"hora\":\"" + getTimeString() + "\"";
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleLog() {
  File file = LittleFS.open("/log.csv", "r");
  if (!file) {
    server.send(500, "text/plain", "Falha ao abrir log.");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=log.csv");
  server.streamFile(file, "text/csv");
  file.close();
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_MQ3, INPUT);

  if (!LittleFS.begin(true)) {
    Serial.println("Falha no LittleFS");
    return;
  }
  
  File file = LittleFS.open("/log.csv", "a");
  if (file.size() == 0) {
    file.println("Data_Hora,Bruto,PPM,Status");
  }
  file.close();

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0);
  lcd.print("Buscando WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sincronizando");
  lcd.setCursor(0, 1);
  lcd.print("Relogio NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");
  delay(3000); 
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Acesse o IP:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(4000); 
  lcd.clear();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/log", handleLog);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  if (currentMillis - previousLcdMillis >= lcdInterval) {
    previousLcdMillis = currentMillis;

    currentRaw = analogRead(PINO_MQ3);
    currentPpm = (currentRaw / 4095.0) * 1000.0;
    currentStatus = (currentRaw >= LIMITE_ALCOOL_RAW) ? "BLOQUEADO" : "LIBERADO ";

    lcd.setCursor(0, 0);
    lcd.print("Alcool: ");
    lcd.print(currentRaw);
    lcd.print("    "); 

    lcd.setCursor(0, 1);
    lcd.print(currentStatus);
    lcd.print("       "); 
  }

  if (currentMillis - previousLogMillis >= logInterval) {
    previousLogMillis = currentMillis;
    
    File file = LittleFS.open("/log.csv", "a");
    if (file) {
      file.print(getTimeString()); 
      file.print(",");
      file.print(currentRaw);
      file.print(",");
      file.print(currentPpm, 1);
      file.print(",");
      file.println(currentStatus);
      file.close();
      Serial.println("Log salvo no disco: " + getTimeString());
    }
  }
}
