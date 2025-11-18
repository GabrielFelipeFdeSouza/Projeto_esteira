
#include <LiquidCrystal.h>
#include <Keypad.h>
#include <WiFi.h>
#include <WebServer.h>

// ================== LCD (HD44780 4-bit) ==================
LiquidCrystal lcd(21, 22, 26, 27, 14, 13); // RS,E,D4,D5,D6,D7

// ================== Teclado 3x4 ==================
const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'#','0','*'},
  {'9','8','7'},
  {'6','5','4'},
  {'3','2','1'}
};
// R1,R2,R3,R4  /  C1,C2,C3
byte rowPins[ROWS] = {19, 18, 5, 4};   // linhas
byte colPins[COLS] = {25, 23, 33};     // colunas
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================== Pinos físicos ==================

const int BUZZER_PIN = 2;   // ALTERADA
const int RELAY_PIN  = 15;  // ALTERADO
const int LDR_PIN    = 34;  // VERIFICAR

// ================== Estados (FSM) ==================
enum Estado : uint8_t {
  EST_PARADA = 1,          // "Esteira Parada"
  EST_CONFIG = 2,          // "Inserir quantidade:"
  EST_CONTANDO = 3,        // "Contagem de camisetas Iniciada"
  EST_LIMITE = 4,          // "Limite de camisetas atingida"
  EST_ABORTADA = 5,        // "Contagem abortada"
  EST_INDEFINIDA = 6       // "Contagem Indefinida"
};

Estado estado = EST_PARADA;

// ================== Variáveis de contagem/estado ==================
unsigned int alvo = 0;           // quantidade a atingir (modo finito)  -- equivale metaLDR
unsigned int contagem = 0;       // contagem atual
String bufferQtd = "";           // entrada numérica no estado 2 (até 5 dígitos)

// Para exibir "x Camisetas contadas" ao abortar contagem indefinida
bool abortHasCount = false;
unsigned int lastAbortCount = 0;

// Variáveis físicas
bool esteiraLigada = false;   // controla RELAY_PIN
bool metaAtingida = false;    // já alcançou o alvo
bool buzzerLigado = false;    // estado do buzzer (ligado/desligado)

// ================== LDR / debounce ==================
int ldrBaseValue = 3000; // valor médio de luz com esteira ligada (calibração -> verificar)
float ldrFactor = 0.7;   // threshold = base * factor
int ldrThreshold = 2000;

int ultimoLdrEstado = HIGH; // 1 = não-luz-obstrucao, 0 = obstruído (baixo)
unsigned long ultimoDebounce = 0;
unsigned long debounceDelay = 300; // ms

// ================== Wi-Fi Access Point ==================
const char *ssid = "Esteira_AP";
const char *password = "12345678";
WebServer server(80);

// ================== Helpers de LCD ==================
void lcdTitulo(const char* linha0, const String& linha1 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linha0);
  lcd.setCursor(0, 1);
  if (linha1.length() > 0) {
    lcd.print(linha1.substring(0, 16));
  }
}

void mostrarEstadoParada() {
  abortHasCount = false; // limpa qualquer mensagem especial
  lcdTitulo("Iniciar contagem:", "Press (* ou #)");
}

void mostrarEstadoConfig() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inserir qtd:");
  lcd.setCursor(0, 1);
  lcd.print(bufferQtd);
}

void mostrarEstadoContando() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Esteira Ativa");
  lcd.setCursor(0, 1);
  lcd.print(String(contagem) + "/" + String(alvo));
}

void mostrarEstadoIndefinida() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Cont. Indefinida");
  lcd.setCursor(0, 1);
  lcd.print(String(contagem) + " camisetas");
}

void mostrarEstadoLimite() {
  lcdTitulo("Contagem Finalizada", "Pressione tecla");
}

void mostrarEstadoAbortada() {
  if (abortHasCount) {
    // Mensagem especial com total contado
    lcdTitulo("Op. Cancelada", String(lastAbortCount) + " Camisetas");
  } else {
    lcdTitulo("Op. Cancelada", "Pressione tecla");
  }
}

// ==========================
// Leitura do LDR e contagem (usada em loop)
// - Detecta transição de HIGH -> LOW (leitura < threshold) com debounce
// - Só conta enquanto esteiraLigada == true e estado é counting/indefinido
// ==========================
void verificarLDR()
{
  // Só precisa ler durante as operações de contagem e se a esteira estiver ligada
  if (!esteiraLigada) {
    // também atualiza ultimoLdrEstado para evitar contagens fantasma quando ligar
    int leitura_now = analogRead(LDR_PIN);
    ultimoLdrEstado = (leitura_now >= ldrThreshold) ? HIGH : LOW;
    return;
  }

  int leitura = analogRead(LDR_PIN); 
  int estadoAtual = (leitura >= ldrThreshold) ? HIGH : LOW;

  // Detecta borda de HIGH -> LOW
  if (estadoAtual == LOW && ultimoLdrEstado == HIGH) {
    unsigned long now = millis();
    if (now - ultimoDebounce > debounceDelay) {
      ultimoDebounce = now;
      // Conta apenas se FSM permitir contagens agora
      if (estado == EST_CONTANDO) {
        // no modo finito, conte apenas até o alvo
        if (alvo == 0 || contagem < alvo) {
          contagem++;
          mostrarEstadoContando();
          if (alvo > 0 && contagem >= alvo && !metaAtingida) {
            // atingiu meta
            metaAtingida = true;
            esteiraLigada = false;
            digitalWrite(RELAY_PIN, LOW);
            buzzerLigado = true;
            estado = EST_LIMITE;
            mostrarEstadoLimite();
          }
        }
      } else if (estado == EST_INDEFINIDA) {
        // incrementa indefinidamente
        contagem++;
        mostrarEstadoIndefinida();
      }
    }
  }

  // atualizar último estado sem afetar debounce logic (quando leitura volta a HIGH)
  if (estadoAtual == HIGH) {
    ultimoLdrEstado = HIGH;
  } else {
    // não sobrescrever para LOW até após debounce; já feito no evento acima quando aceito
    // mas manter valor para detectar a próxima transição
    ultimoLdrEstado = LOW;
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
    body { font-family: Arial, sans-serif; text-align: center; background-color: #f1f1f1; margin: 0; padding: 20px; }
    .container { background: white; border-radius: 12px; max-width: 500px; margin: auto; box-shadow: 0 0 10px rgba(0,0,0,0.1); padding: 20px; }
    button, input[type=number] { padding: 12px 18px; margin: 8px; font-size: 16px; border-radius: 8px; border: none; cursor: pointer; }
    .on { background-color: #4CAF50; color: white; }
    .off { background-color: #f44336; color: white; }
    .info { font-size: 18px; margin: 10px 0; }
  </style>
</head>
<body>
  <div class="container">
    <h2>Controle da Esteira</h2>

    <div class="info"><strong>Contagem:</strong> <span id="contagem">0</span></div>
    <div class="info"><strong>Meta:</strong> <span id="meta">0</span></div>
    <div class="info"><strong>Status Esteira:</strong> <span id="statusEsteira">Desligada</span></div>
    <div class="info"><strong>Status Buzzer:</strong> <span id="statusBuzzer">Desligado</span></div>

    <form id="formMeta">
      <input type="number" id="metaInput" name="meta" placeholder="Digite a meta (0 = indefinido)" min="0" required>
      <button type="submit" class="on">Definir Meta</button>
    </form>

    <p><button onclick="fetch('/toggleEsteira')" class="on">Ligar/Desligar Esteira</button></p>
    <p><button onclick="fetch('/buzzerToggle')" class="off">Ligar/Desligar Buzzer</button></p>
    <p><button onclick="fetch('/reset')">Resetar Contagem</button></p>
  </div>

  <script>
    document.getElementById('formMeta').addEventListener('submit', function(e){
      e.preventDefault();
      const m = document.getElementById('metaInput').value;
      fetch('/setMeta', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:'meta=' + encodeURIComponent(m)})
        .then(()=>{ document.getElementById('metaInput').value=''; });
    });

    setInterval(() => {
      fetch('/status')
        .then(res => res.json())
        .then(data => {
          document.getElementById("contagem").textContent = data.contagem;
          document.getElementById("meta").textContent = data.alvo;
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
    json += "\"contagem\":" + String(contagem) + ",";
    json += "\"alvo\":" + String(alvo) + ",";
    json += "\"esteira\":" + String(esteiraLigada ? "true" : "false") + ",";
    json += "\"buzzer\":" + String(buzzerLigado ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
}

// ==========================
// Handlers Web
// ==========================
void handleRoot()
{
    server.send(200, "text/html", paginaHTML());
}

void calibrarLdrBase()
{
    // média de 10 leituras rápidas para formar referência
    long soma = 0;
    for (int i = 0; i < 10; i++) {
      soma += analogRead(LDR_PIN);
      delay(20);
    }
    ldrBaseValue = soma / 10;
    ldrThreshold = (int)(ldrBaseValue * ldrFactor);
}

void handleToggleEsteira()
{
    if (!metaAtingida) {
        esteiraLigada = !esteiraLigada;
        digitalWrite(RELAY_PIN, esteiraLigada ? HIGH : LOW);
        if (esteiraLigada) {
            calibrarLdrBase();
            // ajustar último estado para evitar contagem imediata
            int leitura_now = analogRead(LDR_PIN);
            ultimoLdrEstado = (leitura_now >= ldrThreshold) ? HIGH : LOW;
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
    if (server.hasArg("meta")) {
        unsigned int m = (unsigned int) server.arg("meta").toInt();
        alvo = m;
        contagem = 0;
        metaAtingida = false;
        buzzerLigado = false;
        // quando meta==0 interpretaremos como "indefinida" (igual ao '000' do teclado)
        if (alvo == 0) {
          estado = EST_INDEFINIDA;
          mostrarEstadoIndefinida();
        } else {
          estado = EST_CONTANDO;
          mostrarEstadoContando();
        }
    }
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleReset()
{
    contagem = 0;
    alvo = 0;
    metaAtingida = false;
    buzzerLigado = false;
    esteiraLigada = false;
    digitalWrite(RELAY_PIN, LOW);
    estado = EST_PARADA;
    mostrarEstadoParada();
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

// ==========================
// Setup
// ==========================
void setup() {
  Serial.begin(115200);

  // LCD
  lcd.begin(16, 2);
  mostrarEstadoParada();

  // Pinos
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  // LDR_PIN (34) é ADC, não precisa pinMode, mas não faz mal:
  pinMode(LDR_PIN, INPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  // WiFi AP
  WiFi.softAP(ssid, password);
  Serial.print("AP Iniciado. IP: ");
  Serial.println(WiFi.softAPIP());

  // Rotas do servidor
  server.on("/", handleRoot);
  server.on("/toggleEsteira", handleToggleEsteira);
  server.on("/buzzerToggle", handleBuzzerToggle);
  server.on("/setMeta", HTTP_POST, handleSetMeta);
  server.on("/reset", handleReset);
  server.on("/status", handleStatus);
  server.begin();

  // calibragem inicial (opcional)
  calibrarLdrBase();
  Serial.print("LDR base: "); Serial.print(ldrBaseValue);
  Serial.print(" threshold: "); Serial.println(ldrThreshold);
}

// ==========================
// Loop principal (FSM + web + ldr)
// ==========================
void loop() {
  // atendimento web
  server.handleClient();

  // leitura de tecla (teclado)
  char k = keypad.getKey();
  if (k) {
    switch (estado) {

      case EST_PARADA:
        if (k == '*' || k == '#') {
          bufferQtd = "";
          estado = EST_CONFIG;
          mostrarEstadoConfig();
        }
        break;

      case EST_CONFIG:
        // Inserção numérica (permite zeros à esquerda para aceitar "000")
        if (k >= '0' && k <= '9') {
          if (bufferQtd.length() < 5) {
            bufferQtd += k;
          }
          mostrarEstadoConfig();
        }
        // Backspace com '*'
        else if (k == '*') {
          if (bufferQtd.length() > 0) {
            bufferQtd.remove(bufferQtd.length() - 1);
          }
          mostrarEstadoConfig();
        }
        // Confirmar com '#'
        else if (k == '#') {
          if (bufferQtd.length() > 0) {
            contagem = 0;

            // Se '000' => contagem indefinida
            if (bufferQtd == "000") {
              alvo = 0;
              estado = EST_INDEFINIDA;
              mostrarEstadoIndefinida();
            } else {
              alvo = bufferQtd.toInt();
              if (alvo > 0) {
                estado = EST_CONTANDO;
                mostrarEstadoContando();
              } else {
                // alvo inválido (0, ou conversão ruim) => volta ao início
                estado = EST_PARADA;
                mostrarEstadoParada();
              }
            }
            // quando iniciar contagem via teclado, ligamos a esteira automaticamente
            if (estado == EST_CONTANDO || estado == EST_INDEFINIDA) {
              if (!metaAtingida) {
                esteiraLigada = true;
                digitalWrite(RELAY_PIN, HIGH);
                calibrarLdrBase();
              }
            }
          } else {
            estado = EST_PARADA;
            mostrarEstadoParada();
          }
        }
        break;

      case EST_CONTANDO:
        // tecla '0' pode simular um pulso de sensor para testes locais
        if (k == '0') {
          if (contagem < alvo) contagem++;
          mostrarEstadoContando();
          if (contagem >= alvo && alvo > 0) {
            estado = EST_LIMITE;
            metaAtingida = true;
            esteiraLigada = false;
            digitalWrite(RELAY_PIN, LOW);
            buzzerLigado = true;
            mostrarEstadoLimite();
          }
        }
        // Abortado (modo finito): mostra mensagem padrão
        else if (k == '*') {
          abortHasCount = false; // no modo finito, mantém mensagem padrão
          estado = EST_ABORTADA;
          // Ao abortar, desligamos a esteira
          esteiraLigada = false;
          digitalWrite(RELAY_PIN, LOW);
          mostrarEstadoAbortada();
        }
        break;

      case EST_INDEFINIDA:
        // tecla '0' pode simular um pulso de sensor para testes locais
        if (k == '0') {
          contagem++;
          mostrarEstadoIndefinida();
        }
        // Abortado (modo indefinido): exibe "x Camisetas contadas"
        else if (k == '*') {
          lastAbortCount = contagem;
          abortHasCount = true;
          estado = EST_ABORTADA;
          // Ao abortar, desligamos a esteira
          esteiraLigada = false;
          digitalWrite(RELAY_PIN, LOW);
          mostrarEstadoAbortada();
        }
        break;

      case EST_LIMITE:
        // Qualquer tecla retorna ao início
        estado = EST_PARADA;
        // reset de flags
        metaAtingida = true; // já atingiu
        mostrarEstadoParada();
        break;

      case EST_ABORTADA:
        // Qualquer tecla retorna ao início
        estado = EST_PARADA;
        mostrarEstadoParada();
        // opcional: zera contagem? mantive como documento: apenas retorna ao estado inicial
        break;
    }
  } // fim if(k)

  // leitura do LDR (eventos de sensor)
  verificarLDR();

  // atualizar buzzer
  digitalWrite(BUZZER_PIN, buzzerLigado ? HIGH : LOW);

  // Se buzzer estiver ligado por meta atingida, você pode querer
  // desligá-lo automaticamente após X segundos — aqui deixei manual (via web ou reset)
}
