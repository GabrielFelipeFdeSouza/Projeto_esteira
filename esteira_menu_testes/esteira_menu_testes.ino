#include <LiquidCrystal.h>
#include <Keypad.h>

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

// ================== Estados ==================
enum Estado : uint8_t {
  EST_PARADA = 1,          // "Esteira Parada"
  EST_CONFIG = 2,          // "Inserir quantidade:"
  EST_CONTANDO = 3,        // "Contagem de camisetas Iniciada"
  EST_LIMITE = 4,          // "Limite de camisetas atingida"
  EST_ABORTADA = 5,        // "Contagem abortada"
  EST_INDEFINIDA = 6       // "Contagem Indefinida"
};

Estado estado = EST_PARADA;

unsigned int alvo = 0;           // quantidade a atingir (modo finito)
unsigned int contagem = 0;       // contagem atual
String bufferQtd = "";           // entrada numérica no estado 2 (até 5 dígitos)

// Para exibir "x Camisetas contadas" ao abortar contagem indefinida
bool abortHasCount = false;
unsigned int lastAbortCount = 0;

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

// ================== Setup ==================
void setup() {
  lcd.begin(16, 2);
  mostrarEstadoParada();
}

// ================== Loop (FSM) ==================
void loop() {
  char k = keypad.getKey();
  if (!k) return;

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
        } else {
          estado = EST_PARADA;
          mostrarEstadoParada();
        }
      }
      break;

    case EST_CONTANDO:
      // Simulação de sensor: tecla '0' incrementa 1 unidade
      if (k == '0') {
        if (contagem < alvo) contagem++;
        mostrarEstadoContando();
        if (contagem >= alvo && alvo > 0) {
          estado = EST_LIMITE;
          mostrarEstadoLimite();
        }
      }
      // Abortado (modo finito): mostra mensagem padrão
      else if (k == '*') {
        abortHasCount = false; // no modo finito, mantém mensagem padrão
        estado = EST_ABORTADA;
        mostrarEstadoAbortada();
      }
      break;

    case EST_INDEFINIDA:
      // Simulação de sensor: tecla '0' incrementa indefinidamente
      if (k == '0') {
        contagem++;
        mostrarEstadoIndefinida();
      }
      // Abortado (modo indefinido): exibe "x Camisetas contadas"
      else if (k == '*') {
        lastAbortCount = contagem;
        abortHasCount = true;
        estado = EST_ABORTADA;
        mostrarEstadoAbortada();
      }
      break;

    case EST_LIMITE:
      // Qualquer tecla retorna ao início
      estado = EST_PARADA;
      mostrarEstadoParada();
      break;

    case EST_ABORTADA:
      // Qualquer tecla retorna ao início
      estado = EST_PARADA;
      mostrarEstadoParada();
      break;
  }
}