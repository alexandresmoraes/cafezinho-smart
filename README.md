# CaféZinho Smart

Sistema IoT para automação de cafeteira com ESP32, controle via web e programação de horários.

## Hardware Necessário

- ESP32 (qualquer modelo)
- Módulo relé 5V
- Fonte 5V para o relé
- Cafeteira elétrica
- Jumpers e protoboard

## Pinagem

```
ESP32 GPIO 12 → IN do Relé
ESP32 GND    → GND do Relé  
ESP32 VIN    → VCC do Relé (5V)
ESP32 GPIO 0 → Botão BOOT (reset de fábrica)
```

## Instalação

### 1. Arduino IDE

```bash
# Instale as bibliotecas:
- WiFi (nativa ESP32)
- WebServer (nativa ESP32) 
- DNSServer (nativa ESP32)
- ESPmDNS (nativa ESP32)
- ArduinoJson (via Library Manager)
- Preferences (nativa ESP32)
- LittleFS (nativa ESP32)
- NTPClient (via Library Manager)
```

### 2. Upload do Firmware

1. Abra o arquivo `cafezinho.ino` no Arduino IDE
2. Selecione a placa ESP32 
3. Selecione a porta COM correta
4. Compile e faça upload

### 3. Upload da Interface Web

1. Vá em `Tools → ESP32 Sketch Data Upload`
2. Coloque o arquivo `index.html` na pasta `data/` do sketch
3. Faça upload dos arquivos SPIFFS/LittleFS

### 4. Configuração Inicial

1. Após upload, o ESP32 criará uma rede WiFi: `CafeZinho-Setup`
2. Senha: `cafezinho123`
3. Conecte-se e acesse: `http://cafezinho.local` ou `http://ip-serial-monitor`
4. Configure sua rede WiFi nas configurações

## Uso

### Acesso Local
- URL: `http://cafezinho.local` (mDNS)
- IP direto: verificar no monitor serial

### Funcionalidades

**Controle Manual:**
- Liga/desliga cafeteira via interface web
- Status em tempo real

**Modo Automático:**
- Program horários por dia da semana
- Desligamento automático opcional
- Sincronização via NTP

**Configurações:**
- WiFi com proteção contra alterações acidentais
- Timestamps separados por seção
- Histórico de ações

### Reset de Fábrica
Mantenha pressionado o botão BOOT por 10 segundos.

## API Endpoints

```http
GET  /api/status     # Status do sistema
GET  /api/history    # Histórico de ações  
POST /api/coffee     # Controlar cafeteira
POST /api/config     # Salvar configurações
```

## Troubleshooting

**Não conecta no WiFi:**
- Verifique credenciais
- Router pode demorar após queda de energia (sistema aguarda 60s)
- Reconexão automática a cada 60s em modo AP

**Modo AP inesperado:**
- Sistema só volta ao AP se realmente perder conexão
- Campos WiFi vazios mantêm configuração atual

**Horários não funcionam:**
- Verifique conexão WiFi (NTP necessário)
- Ative o modo automático
- Configure dias da semana

## Licença

MIT License - use como quiser.
