#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// ===== CONFIGURAÇÕES GERAIS =====
#define RELAY_PIN 12
#define BOOT_BUTTON 0
#define DNS_PORT 53

const char *AP_SSID = "CafeZinho-Setup";
const char *AP_PASSWORD = "cafezinho123";
const char *MDNS_NAME = "cafezinho";

// ===== OBJETOS GLOBAIS =====
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);

// ===== ESTRUTURAS DE DADOS =====
struct SystemState
{
  // Status básico
  bool coffeeOn = false;
  bool isAPMode = true;
  bool wifiConnected = false;

  // Configurações WiFi
  String wifiSSID = "";
  String wifiPassword = "";

  // Configurações de automação
  bool autoMode = false;
  String scheduleTime = "07:00";
  String scheduleDays = "monday,tuesday,wednesday,thursday,friday";
  bool autoOffEnabled = false;
  String autoOffTime = "18:00";

  // Tempo atual
  String currentTime = "--:--";
  String currentDate = "--/--/----";

  // Timestamps para controlar mudanças por seção
  unsigned long scheduleConfigTimestamp = 0; // Para horários e dias
  unsigned long advancedConfigTimestamp = 0; // Para modo auto e auto-off
} systemState;

struct HistoryEntry
{
  String action; // "on" ou "off"
  String type;   // "manual", "automatic", "scheduled"
  String time;   // "HH:MM"
  String date;   // "DD/MM/AAAA"
  unsigned long timestamp;
};

// ===== VARIÁVEIS GLOBAIS =====
HistoryEntry history[10];
int historyCount = 0;
unsigned long lastScheduleCheck = 0;
unsigned long lastWifiCheck = 0; // Para verificar WiFi periodicamente
unsigned long bootButtonPressTime = 0;
bool bootButtonPressed = false;

// ===== FUNÇÕES AUXILIARES =====

void debugPrint(String message);
void errorPrint(String message);
void checkWiFiConnection(); // Declaração da função de monitoramento WiFi

void debugPrint(String message)
{
  Serial.println("[DEBUG] " + message);
}

void errorPrint(String message)
{
  Serial.println("[ERROR] " + message);
}

// ===== GERENCIAMENTO DE ARQUIVOS =====

bool initFileSystem()
{
  if (!LittleFS.begin(true))
  {
    errorPrint("Falha ao montar LittleFS");
    return false;
  }

  debugPrint("LittleFS montado com sucesso");

  // Listar arquivos para debug
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  debugPrint("Arquivos disponíveis:");
  while (file)
  {
    debugPrint("- " + String(file.name()) + " (" + String(file.size()) + " bytes)");
    file = root.openNextFile();
  }

  return true;
}

// ===== GERENCIAMENTO DE CONFIGURAÇÕES =====

void incrementScheduleTimestamp()
{
  systemState.scheduleConfigTimestamp++;
  debugPrint("Schedule timestamp incrementado: " + String(systemState.scheduleConfigTimestamp));
}

void incrementAdvancedTimestamp()
{
  systemState.advancedConfigTimestamp++;
  debugPrint("Advanced timestamp incrementado: " + String(systemState.advancedConfigTimestamp));
}

void loadConfiguration()
{
  preferences.begin("cafezinho", false);

  systemState.wifiSSID = preferences.getString("ssid", "");
  systemState.wifiPassword = preferences.getString("password", "");
  systemState.autoMode = preferences.getBool("autoMode", false);
  systemState.scheduleTime = preferences.getString("scheduleTime", "07:00");
  systemState.scheduleDays = preferences.getString("scheduleDays", "monday,tuesday,wednesday,thursday,friday");
  systemState.autoOffEnabled = preferences.getBool("autoOffEnabled", false);
  systemState.autoOffTime = preferences.getString("autoOffTime", "18:00");

  // Carregar timestamps salvos
  systemState.scheduleConfigTimestamp = preferences.getULong("scheduleTimestamp", 1);
  systemState.advancedConfigTimestamp = preferences.getULong("advancedTimestamp", 1);

  preferences.end();

  debugPrint("Configurações carregadas:");
  debugPrint("WiFi SSID: " + systemState.wifiSSID);
  debugPrint("Modo Auto: " + String(systemState.autoMode));
  debugPrint("Horário: " + systemState.scheduleTime);
  debugPrint("Dias: " + systemState.scheduleDays);
  debugPrint("Schedule Timestamp: " + String(systemState.scheduleConfigTimestamp));
  debugPrint("Advanced Timestamp: " + String(systemState.advancedConfigTimestamp));
}

void saveConfiguration(bool scheduleChanged = false, bool advancedChanged = false)
{
  preferences.begin("cafezinho", false);

  preferences.putString("ssid", systemState.wifiSSID);
  preferences.putString("password", systemState.wifiPassword);
  preferences.putBool("autoMode", systemState.autoMode);
  preferences.putString("scheduleTime", systemState.scheduleTime);
  preferences.putString("scheduleDays", systemState.scheduleDays);
  preferences.putBool("autoOffEnabled", systemState.autoOffEnabled);
  preferences.putString("autoOffTime", systemState.autoOffTime);

  // Incrementar timestamps conforme necessário
  if (scheduleChanged)
  {
    incrementScheduleTimestamp();
    preferences.putULong("scheduleTimestamp", systemState.scheduleConfigTimestamp);
  }

  if (advancedChanged)
  {
    incrementAdvancedTimestamp();
    preferences.putULong("advancedTimestamp", systemState.advancedConfigTimestamp);
  }

  preferences.end();

  debugPrint("Configurações salvas - Schedule: " + String(systemState.scheduleConfigTimestamp) +
             ", Advanced: " + String(systemState.advancedConfigTimestamp));
}

void resetToFactory()
{
  debugPrint("=== EXECUTANDO RESET DE FÁBRICA ===");

  preferences.begin("cafezinho", false);
  preferences.clear();
  preferences.end();

  // Desligar cafeteira manualmente
  systemState.coffeeOn = false;
  digitalWrite(RELAY_PIN, LOW);

  // Resetar estado
  systemState.coffeeOn = false;
  systemState.autoMode = false;
  systemState.scheduleTime = "07:00";
  systemState.scheduleDays = "monday,tuesday,wednesday,thursday,friday";
  systemState.autoOffEnabled = false;
  systemState.autoOffTime = "18:00";
  systemState.wifiSSID = "";
  systemState.wifiPassword = "";
  systemState.wifiConnected = false;

  // Incrementar ambos timestamps para indicar mudança
  incrementScheduleTimestamp();
  incrementAdvancedTimestamp();

  // Limpar histórico
  historyCount = 0;

  debugPrint("Reset concluído! Reiniciando...");
  delay(2000);
  ESP.restart();
}

// ===== MONITORAMENTO DE WIFI =====

void checkWiFiConnection()
{
  unsigned long now = millis();

  // Se está em modo AP e tem SSID configurado, tentar reconectar a cada 60 segundos
  if (systemState.isAPMode && systemState.wifiSSID.length() > 0 && (now - lastWifiCheck > 60000))
  {
    lastWifiCheck = now;

    debugPrint("Tentando sair do modo AP e conectar ao WiFi: " + systemState.wifiSSID);

    if (connectToWiFi())
    {
      debugPrint("Sucesso! Saindo do modo AP");
      // Não precisa fazer mais nada, connectToWiFi() já configura tudo
    }
    else
    {
      debugPrint("Falha na reconexão, mantendo modo AP");
      // Volta ao modo AP se não conseguir conectar
      if (!systemState.isAPMode)
      {
        initAPMode();
      }
    }
  }

  // Se não está em modo AP, verificar se ainda está conectado
  else if (!systemState.isAPMode && (now - lastWifiCheck > 30000))
  {
    lastWifiCheck = now;

    if (WiFi.status() != WL_CONNECTED)
    {
      debugPrint("Conexão WiFi perdida! Voltando ao modo AP");
      systemState.wifiConnected = false;
      initAPMode();
    }
  }
}

void updateCurrentTime()
{
  if (!systemState.wifiConnected)
  {
    systemState.currentTime = "--:--";
    systemState.currentDate = "--/--/----";
    return;
  }

  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);

  char timeStr[6];
  char dateStr[11];

  sprintf(timeStr, "%02d:%02d", ptm->tm_hour, ptm->tm_min);
  sprintf(dateStr, "%02d/%02d/%04d", ptm->tm_mday, ptm->tm_mon + 1, ptm->tm_year + 1900);

  systemState.currentTime = String(timeStr);
  systemState.currentDate = String(dateStr);
}

String getCurrentDayOfWeek()
{
  if (!systemState.wifiConnected)
    return "";

  time_t epochTime = timeClient.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);

  String days[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
  return days[ptm->tm_wday];
}

// ===== GERENCIAMENTO DE HISTÓRICO =====

void addHistoryEntry(String action, String type)
{
  updateCurrentTime();

  // Mover entradas existentes
  for (int i = 9; i > 0; i--)
  {
    history[i] = history[i - 1];
  }

  // Adicionar nova entrada
  history[0].action = action;
  history[0].type = type;
  history[0].time = systemState.currentTime;
  history[0].date = systemState.currentDate;
  history[0].timestamp = millis();

  if (historyCount < 10)
    historyCount++;

  debugPrint("Histórico: " + action + " (" + type + ") às " + systemState.currentTime);
}

// ===== CONTROLE DA CAFETEIRA =====

void setCoffeeState(bool turnOn, String type = "manual")
{
  if (systemState.coffeeOn == turnOn)
    return;

  systemState.coffeeOn = turnOn;
  digitalWrite(RELAY_PIN, turnOn ? HIGH : LOW);

  addHistoryEntry(turnOn ? "on" : "off", type);

  debugPrint("Cafeteira " + String(turnOn ? "LIGADA" : "DESLIGADA") + " (" + type + ")");
}

// ===== VERIFICAÇÃO DE HORÁRIOS =====

bool isScheduleActive()
{
  return systemState.autoMode &&
         systemState.scheduleTime.length() > 0 &&
         systemState.scheduleDays.length() > 0;
}

void checkScheduledTimes()
{
  if (!systemState.wifiConnected || !isScheduleActive())
    return;

  unsigned long now = millis();
  if (now - lastScheduleCheck < 60000)
    return; // Verificar a cada minuto
  lastScheduleCheck = now;

  updateCurrentTime();

  // Verificar horário de ligar
  if (systemState.currentTime == systemState.scheduleTime && !systemState.coffeeOn)
  {
    String today = getCurrentDayOfWeek();

    if (systemState.scheduleDays.indexOf(today) >= 0)
    {
      debugPrint("Horário programado atingido! Ligando cafeteira...");
      setCoffeeState(true, "scheduled");
    }
  }

  // Verificar desligamento automático
  if (systemState.autoOffEnabled &&
      systemState.currentTime == systemState.autoOffTime &&
      systemState.coffeeOn)
  {
    debugPrint("Desligamento automático ativado!");
    setCoffeeState(false, "automatic");
  }
}

// ===== GERENCIAMENTO DE WIFI =====

bool setupMDNS()
{
  MDNS.end(); // Parar se estava rodando

  if (MDNS.begin(MDNS_NAME))
  {
    MDNS.addService("http", "tcp", 80);
    debugPrint("mDNS iniciado: http://" + String(MDNS_NAME) + ".local");
    return true;
  }
  else
  {
    errorPrint("Falha ao iniciar mDNS");
    return false;
  }
}

void initAPMode()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  IPAddress IP = WiFi.softAPIP();
  systemState.isAPMode = true;
  systemState.wifiConnected = false;

  setupMDNS();

  debugPrint("=== MODO AP ATIVO ===");
  debugPrint("SSID: " + String(AP_SSID));
  debugPrint("Password: " + String(AP_PASSWORD));
  debugPrint("IP: " + IP.toString());
}

bool connectToWiFi()
{
  if (systemState.wifiSSID.length() == 0)
  {
    debugPrint("SSID não configurado");
    return false;
  }

  debugPrint("Conectando ao WiFi: " + systemState.wifiSSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(systemState.wifiSSID.c_str(), systemState.wifiPassword.c_str());

  // Aguardar até 60 segundos para conectar (útil após quedas de energia)
  int attempts = 0;
  int maxAttempts = 120; // 120 * 500ms = 60 segundos

  debugPrint("Aguardando conexão WiFi (até 60s)...");

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts)
  {
    delay(500);
    Serial.print(".");
    attempts++;

    // A cada 10 segundos, mostrar progresso
    if (attempts % 20 == 0)
    {
      debugPrint("Tentando conectar... " + String(attempts / 2) + "s");
    }
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    systemState.isAPMode = false;
    systemState.wifiConnected = true;

    // Garantir que o SSID seja preservado
    if (systemState.wifiSSID.length() == 0)
    {
      systemState.wifiSSID = WiFi.SSID();
    }

    delay(1000); // Aguardar estabilizar
    setupMDNS();

    timeClient.begin();
    timeClient.update();

    debugPrint("=== WIFI CONECTADO ===");
    debugPrint("IP: " + WiFi.localIP().toString());
    debugPrint("SSID: " + WiFi.SSID());
    debugPrint("Tempo para conectar: " + String(attempts / 2) + " segundos");

    return true;
  }
  else
  {
    errorPrint("Falha na conexão WiFi após 60 segundos");
    debugPrint("Status WiFi: " + String(WiFi.status()));
    return false;
  }
}

// ===== CONTROLE DO BOTÃO BOOT =====

void handleFactoryReset()
{
  if (digitalRead(BOOT_BUTTON) == LOW)
  {
    if (!bootButtonPressed)
    {
      bootButtonPressed = true;
      bootButtonPressTime = millis();
      debugPrint("Botão BOOT pressionado - mantenha por 10s para reset");
    }
    else
    {
      unsigned long pressedTime = millis() - bootButtonPressTime;
      if (pressedTime >= 10000)
      { // 10 segundos
        resetToFactory();
      }
    }
  }
  else
  {
    if (bootButtonPressed)
    {
      unsigned long pressedTime = millis() - bootButtonPressTime;
      if (pressedTime < 10000)
      {
        debugPrint("Reset cancelado");
      }
      bootButtonPressed = false;
    }
  }
}

// ===== ROTAS DA API =====

void handleRoot()
{
  if (LittleFS.exists("/index.html"))
  {
    File file = LittleFS.open("/index.html", "r");
    if (file)
    {
      server.streamFile(file, "text/html");
      file.close();
      return;
    }
  }

  // Fallback HTML
  server.send(200, "text/html",
              "<html><head><title>CaféZinho Smart</title></head><body>"
              "<h1>CaféZinho Smart</h1>"
              "<p>Sistema funcionando!</p>"
              "<p>Carregue o arquivo index.html no LittleFS</p>"
              "</body></html>");
}

void handleGetStatus()
{
  updateCurrentTime();

  DynamicJsonDocument doc(1024);

  // Status básico
  doc["coffeeOn"] = systemState.coffeeOn;
  doc["autoMode"] = systemState.autoMode;
  doc["scheduleActive"] = isScheduleActive();
  doc["wifiConnected"] = systemState.wifiConnected;
  doc["currentTime"] = systemState.currentTime;
  doc["currentDate"] = systemState.currentDate;

  // Timestamps separados para cada seção
  doc["scheduleConfigTimestamp"] = systemState.scheduleConfigTimestamp;
  doc["advancedConfigTimestamp"] = systemState.advancedConfigTimestamp;

  // Configurações
  doc["scheduleTime"] = systemState.scheduleTime;
  doc["scheduleDays"] = systemState.scheduleDays;
  doc["autoOffEnabled"] = systemState.autoOffEnabled;
  doc["autoOffTime"] = systemState.autoOffTime;

  // Configurações WiFi (apenas quando conectado)
  if (systemState.wifiConnected && !systemState.isAPMode)
  {
    doc["wifiSSID"] = systemState.wifiSSID;
  }

  // WiFi SSID e informações de sinal
  if (systemState.isAPMode)
  {
    doc["wifiSSID"] = AP_SSID;
    doc["wifiSignal"] = 100; // AP sempre 100%
  }
  else if (systemState.wifiConnected)
  {
    doc["wifiSSID"] = systemState.wifiSSID.length() > 0 ? systemState.wifiSSID : WiFi.SSID();

    // Corrigir cálculo do sinal WiFi (RSSI típico: -30 a -90 dBm)
    int rssi = WiFi.RSSI();
    int signalPercent;
    if (rssi >= -50)
    {
      signalPercent = 100;
    }
    else if (rssi <= -90)
    {
      signalPercent = 0;
    }
    else
    {
      signalPercent = map(rssi, -90, -50, 0, 100);
    }
    doc["wifiSignal"] = signalPercent;
  }
  else
  {
    doc["wifiSSID"] = "";
    doc["wifiSignal"] = 0;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetHistory()
{
  DynamicJsonDocument doc(2048);
  JsonArray historyArray = doc.createNestedArray("history");

  for (int i = 0; i < historyCount; i++)
  {
    JsonObject entry = historyArray.createNestedObject();
    entry["id"] = i + 1;
    entry["action"] = history[i].action;
    entry["type"] = history[i].type;
    entry["time"] = history[i].time;
    entry["date"] = history[i].date;
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSetCoffee()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "application/json", "{\"error\": \"Dados não fornecidos\"}");
    return;
  }

  String json = server.arg("plain");
  DynamicJsonDocument doc(256);
  deserializeJson(doc, json);

  if (doc.containsKey("state"))
  {
    bool turnOn = doc["state"];
    setCoffeeState(turnOn, "manual");
    server.send(200, "application/json", "{\"success\": true}");
  }
  else
  {
    server.send(400, "application/json", "{\"error\": \"Parâmetro 'state' obrigatório\"}");
  }
}

void handleSaveConfig()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "application/json", "{\"error\": \"Dados não fornecidos\"}");
    return;
  }

  String json = server.arg("plain");
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, json);

  debugPrint("=== SALVANDO CONFIGURAÇÕES ===");
  debugPrint("JSON recebido: " + json);

  bool wifiChanged = false;
  bool scheduleChanged = false;
  bool advancedChanged = false;
  String newSSID = "";
  String newPassword = "";

  // Verificar mudanças em configurações de horário
  if (doc.containsKey("scheduleTime") && doc["scheduleTime"].as<String>() != systemState.scheduleTime)
  {
    scheduleChanged = true;
    systemState.scheduleTime = doc["scheduleTime"].as<String>();
    debugPrint("ScheduleTime alterado: " + systemState.scheduleTime);
  }

  if (doc.containsKey("scheduleDays") && doc["scheduleDays"].as<String>() != systemState.scheduleDays)
  {
    scheduleChanged = true;
    systemState.scheduleDays = doc["scheduleDays"].as<String>();
    debugPrint("ScheduleDays alterado: " + systemState.scheduleDays);
  }

  // Verificar mudanças em configurações avançadas
  if (doc.containsKey("autoMode") && doc["autoMode"].as<bool>() != systemState.autoMode)
  {
    advancedChanged = true;
    systemState.autoMode = doc["autoMode"];
    debugPrint("AutoMode alterado: " + String(systemState.autoMode));
  }

  if (doc.containsKey("autoOffEnabled") && doc["autoOffEnabled"].as<bool>() != systemState.autoOffEnabled)
  {
    advancedChanged = true;
    systemState.autoOffEnabled = doc["autoOffEnabled"];
    debugPrint("AutoOffEnabled alterado: " + String(systemState.autoOffEnabled));
  }

  if (doc.containsKey("autoOffTime") && doc["autoOffTime"].as<String>() != systemState.autoOffTime)
  {
    advancedChanged = true;
    systemState.autoOffTime = doc["autoOffTime"].as<String>();
    debugPrint("AutoOffTime alterado: " + systemState.autoOffTime);
  }

  // Verificar se WiFi foi explicitamente alterado
  if (doc.containsKey("wifiSSID") && doc.containsKey("wifiPassword"))
  {
    newSSID = doc["wifiSSID"].as<String>();
    newSSID.trim();
    newPassword = doc["wifiPassword"].as<String>();

    // Só alterar WiFi se SSID não estiver vazio OU se for uma limpeza intencional
    if (newSSID.length() > 0)
    {
      if (newSSID != systemState.wifiSSID || newPassword != systemState.wifiPassword)
      {
        wifiChanged = true;
        systemState.wifiSSID = newSSID;
        systemState.wifiPassword = newPassword;
        debugPrint("WiFi alterado para: " + newSSID);
      }
    }
    else
    {
      // Se SSID vazio, só alterar se senha também estiver vazia (reset intencional)
      if (newPassword.length() == 0 &&
          (systemState.wifiSSID.length() > 0 || systemState.wifiPassword.length() > 0))
      {
        wifiChanged = true;
        systemState.wifiSSID = "";
        systemState.wifiPassword = "";
        debugPrint("WiFi limpo intencionalmente");
      }
      else
      {
        debugPrint("WiFi não alterado - mantendo configuração atual: " + systemState.wifiSSID);
      }
    }
  }

  // Salvar configurações com timestamps específicos
  saveConfiguration(scheduleChanged, advancedChanged);

  // Preparar resposta
  DynamicJsonDocument responseDoc(512);
  responseDoc["success"] = true;
  responseDoc["scheduleConfigTimestamp"] = systemState.scheduleConfigTimestamp;
  responseDoc["advancedConfigTimestamp"] = systemState.advancedConfigTimestamp;

  if (wifiChanged)
  {
    responseDoc["message"] = "Reconectando WiFi...";
    responseDoc["wifiChanged"] = true;

    String response;
    serializeJson(responseDoc, response);
    server.send(200, "application/json", response);

    delay(1000);

    if (systemState.wifiSSID.length() > 0)
    {
      debugPrint("Tentando reconectar WiFi...");
      if (!connectToWiFi())
      {
        debugPrint("Falha na reconexão - mantendo modo atual");
        if (systemState.isAPMode)
        {
          // Se já estava em AP, continua
          debugPrint("Mantendo modo AP");
        }
        else
        {
          // Se estava conectado e falhou, volta ao AP
          debugPrint("Voltando ao modo AP");
          initAPMode();
        }
      }
    }
    else
    {
      debugPrint("SSID vazio - voltando ao modo AP");
      initAPMode();
    }
  }
  else
  {
    responseDoc["message"] = "Configurações salvas com sucesso!";
    responseDoc["wifiChanged"] = false;

    String response;
    serializeJson(responseDoc, response);
    server.send(200, "application/json", response);
  }

  debugPrint("=== FIM SALVAMENTO ===");
}

// ===== CONFIGURAÇÃO DE ROTAS =====

void setupRoutes()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/history", HTTP_GET, handleGetHistory);
  server.on("/api/coffee", HTTP_POST, handleSetCoffee);
  server.on("/api/config", HTTP_POST, handleSaveConfig);

  server.serveStatic("/", LittleFS, "/");

  server.onNotFound([]()
                    {
    if (systemState.isAPMode) {
      handleRoot();
    } else {
      server.send(404, "text/plain", "Página não encontrada");
    } });
}

// ===== SETUP PRINCIPAL =====

void setup()
{
  Serial.begin(115200);
  delay(1000);

  debugPrint("=== CAFÉZINHO SMART v2.1 ===");

  // Configurar pinos
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);

  debugPrint("Pinos configurados:");
  debugPrint("- GPIO 12: Relé da cafeteira");
  debugPrint("- GPIO 0: Botão BOOT (reset)");

  // Inicializar sistemas
  if (!initFileSystem())
  {
    errorPrint("Sistema de arquivos falhou");
  }

  loadConfiguration();

  // Tentar conectar WiFi ou iniciar AP
  if (systemState.wifiSSID.length() > 0)
  {
    if (!connectToWiFi())
    {
      initAPMode();
    }
  }
  else
  {
    initAPMode();
  }

  setupRoutes();
  server.begin();
  timeClient.begin();

  debugPrint("=== SISTEMA PRONTO ===");
  debugPrint("Acesse: http://" + String(MDNS_NAME) + ".local");
  debugPrint("Ou IP: " + (systemState.isAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()));
  debugPrint("Reset de fábrica: Botão BOOT por 10s");
  debugPrint("Schedule timestamp: " + String(systemState.scheduleConfigTimestamp));
  debugPrint("Advanced timestamp: " + String(systemState.advancedConfigTimestamp));
}

// ===== LOOP PRINCIPAL =====

void loop()
{
  handleFactoryReset();

  // Sempre verificar WiFi, independente do modo
  checkWiFiConnection();

  if (systemState.isAPMode)
  {
    dnsServer.processNextRequest();
  }

  server.handleClient();

  if (systemState.wifiConnected)
  {
    timeClient.update();
  }

  checkScheduledTimes();

  delay(100);
}