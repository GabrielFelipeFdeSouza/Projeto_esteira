#include <WiFi.h>
#include <WebServer.h>

// Pinos
#define BUZZER_PIN 23
#define RELAY_PIN 33
#define LDR_PIN 32

// Estado geral
volatile int ldrCount = 0;
int metaLDR = 0;
bool esteiraLigada = false;
bool metaAtingida = false;
bool buzzerLigado = false;

int ldrBaseValue = 3000; // Valor médio de luz com esteira ligada
float ldrFactor = 0.7;   // 70% do valor base como threshold dinâmico
int ldrThreshold = 2000;
int ultimoEstado = HIGH;
unsigned long ultimoDebounce = 0;
unsigned long debounceDelay = 300;

// Wi-Fi Access Point
const char *ssid = "Esteira_AP";
const char *password = "12345678";

WebServer server(80);

// ==========================
// Leitura de LDR e contagem
// ==========================
void verificarLDR()
{
    int leitura = analogRead(LDR_PIN);

    if (leitura < ldrThreshold && ultimoEstado == HIGH && (millis() - ultimoDebounce > debounceDelay))
    {
        ldrCount++;
        ultimoEstado = LOW;
        ultimoDebounce = millis();

        if (metaLDR > 0 && ldrCount >= metaLDR && !metaAtingida)
        {
            esteiraLigada = false;
            digitalWrite(RELAY_PIN, LOW);
            metaAtingida = true;
            buzzerLigado = true;
        }
    }
    else if (leitura >= ldrThreshold)
    {
        ultimoEstado = HIGH;
    }
}

// ==========================
// Página principal com AJAX
// ==========================
String paginaHTML()
{
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <title>Controle Esteira</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background-color: #f1f1f1;
      margin: 0; padding: 20px;
    }
    .container {
      background: white; border-radius: 12px;
      max-width: 500px; margin: auto;
      box-shadow: 0 0 10px rgba(0,0,0,0.1); padding: 20px;
    }
    button, input[type=number] {
      padding: 14px 24px; margin: 10px;
      font-size: 16px; border-radius: 8px;
      border: none; cursor: pointer;
    }
    .on { background-color: #4CAF50; color: white; }
    .off { background-color: #f44336; color: white; }
    .info { font-size: 18px; margin: 10px 0; }
  </style>
</head>
<body>
  <div class="container">
    <h2>Controle da Esteira</h2>

    <div class="info"><strong>Contagem LDR:</strong> <span id="ldrCount">0</span></div>
    <div class="info"><strong>Meta:</strong> <span id="metaLDR">0</span></div>
    <div class="info"><strong>Status Esteira:</strong> <span id="statusEsteira">Desligada</span></div>
    <div class="info"><strong>Status Buzzer:</strong> <span id="statusBuzzer">Desligado</span></div>

    <form action="/setMeta" method="POST">
      <input type="number" name="meta" placeholder="Digite a meta" min="1" required>
      <button type="submit" class="on">Definir Meta</button>
    </form>

    <p><a href='/toggleEsteira'><button class="on">Ligar/Desligar Esteira</button></a></p>
    <p><a href='/buzzerToggle'><button class="off">Ligar/Desligar Buzzer</button></a></p>
    <p><a href='/reset'><button>Resetar Contagem</button></a></p>
  </div>

  <script>
    setInterval(() => {
      fetch('/status')
        .then(res => res.json())
        .then(data => {
          document.getElementById("ldrCount").textContent = data.ldrCount;
          document.getElementById("metaLDR").textContent = data.metaLDR;
          document.getElementById("statusEsteira").textContent = data.esteira ? "Ligada" : "Desligada";
          document.getElementById("statusBuzzer").textContent = data.buzzer ? "Ligado" : "Desligado";
        });
    }, 1000);
  </script>
</body>
</html>
)rawliteral";
    return html;
}

// ==========================
// JSON para status em tempo real
// ==========================
void handleStatus()
{
    String json = "{";
    json += "\"ldrCount\":" + String(ldrCount) + ",";
    json += "\"metaLDR\":" + String(metaLDR) + ",";
    json += "\"esteira\":" + String(esteiraLigada ? "true" : "false") + ",";
    json += "\"buzzer\":" + String(buzzerLigado ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
}

// ==========================
// Handlers
// ==========================
void handleRoot()
{
    server.send(200, "text/html", paginaHTML());
}

void handleToggleEsteira()
{
    if (!metaAtingida)
    {
        esteiraLigada = !esteiraLigada;
        digitalWrite(RELAY_PIN, esteiraLigada ? HIGH : LOW);
        if (esteiraLigada)
        {
            // Captura novo valor de referência do LDR
            int soma = 0;
            for (int i = 0; i < 10; i++)
            {
                soma += analogRead(LDR_PIN);
                delay(50);
            }
            ldrBaseValue = soma / 10;
            ldrThreshold = ldrBaseValue * ldrFactor;
        }
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleBuzzerToggle()
{
    buzzerLigado = !buzzerLigado;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleSetMeta()
{
    if (server.hasArg("meta"))
    {
        metaLDR = server.arg("meta").toInt();
        ldrCount = 0;
        metaAtingida = false;
        buzzerLigado = false;
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleReset()
{
    ldrCount = 0;
    metaAtingida = false;
    buzzerLigado = false;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

// ==========================
// Setup
// ==========================
void setup()
{
    Serial.begin(115200);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LDR_PIN, INPUT);

    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RELAY_PIN, LOW);

    WiFi.softAP(ssid, password);
    Serial.println("Access Point Iniciado. IP: " + WiFi.softAPIP().toString());

    server.on("/", handleRoot);
    server.on("/toggleEsteira", handleToggleEsteira);
    server.on("/buzzerToggle", handleBuzzerToggle);
    server.on("/setMeta", HTTP_POST, handleSetMeta);
    server.on("/reset", handleReset);
    server.on("/status", handleStatus);

    server.begin();
}

// ==========================
// Loop
// ==========================
void loop()
{
    server.handleClient();
    verificarLDR();

    digitalWrite(BUZZER_PIN, buzzerLigado ? HIGH : LOW);
}
