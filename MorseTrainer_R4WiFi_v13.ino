/*
 * ============================================================
 *  Morse Trainer – Arduino UNO R4 WiFi  (Paddle-Version v3.1)
 *  Interface : USB (Serial, 115200 Bd) + Bluetooth BLE
 *  Display   : 12×8 LED-Matrix onboard  → Arduino_LED_Matrix
 *
 *  Paddle-Belegung
 *    DIT-Taste  → Pin D2  (Punkt  '.')  → GND
 *    DASH-Taste → Pin D4  (Strich '-')  → GND
 *
 *  Erkennung (pro Taste unabhängig)
 *    • Mehrfach-Sampling (Majority-Vote) gegen Preller
 *    • Zustandsautomat: OFFEN → GEDRÜCKT → OFFEN
 *    • Genau EIN Symbol pro physischer Betätigung
 *    • Cooldown verhindert Doppel-Trigger durch Prellen
 *
 *  Bibliotheken
 *    ArduinoBLE         ← Bibliotheksmanager
 *    Arduino_LED_Matrix ← im R4-WiFi-Boardpaket enthalten
 * ============================================================
 */

#include <ArduinoBLE.h>
#include "Arduino_LED_Matrix.h"

// ═══════════════════════════════════════════════════════════
//  PIN-KONFIGURATION
// ═══════════════════════════════════════════════════════════
#define PIN_DIT   2
#define PIN_DASH  4

// ═══════════════════════════════════════════════════════════
//  TIMING  (Millisekunden) – mit Website synchron halten!
// ═══════════════════════════════════════════════════════════
const uint16_t CHAR_TIMEOUT_MS        = 380;  // fertiger, eindeutiger Buchstabe
const uint16_t CHAR_TIMEOUT_AMBIG_MS  = 620;  // Puffer noch Präfix (z.B. "-" → noch nicht T)
const uint16_t DISPLAY_HOLD_MS        = 1500;

// Paddle-Debounce (advanced)
const uint8_t  SAMPLE_COUNT       = 5;    // Anzahl Pin-Abfragen pro Zyklus
const uint16_t SAMPLE_INTERVAL_US   = 400; // Abstand zwischen Samples
const uint8_t  SAMPLE_THRESHOLD     = 4;  // mind. 4/5 LOW = gedrückt
const uint16_t PRESS_SETTLE_MS      = 2;   // stabiler Druck bevor Symbol senden
const uint16_t RELEASE_SETTLE_MS    = 3;   // stabiles Loslassen bevor neue Betätigung
const uint16_t SYMBOL_COOLDOWN_MS   = 28;  // Mindestabstand gleiche Taste (Preller-Schutz)

// ═══════════════════════════════════════════════════════════
//  PADDLE-ZUSTANDSAUTOMAT
// ═══════════════════════════════════════════════════════════
enum PaddleState : uint8_t {
  PADDLE_OPEN = 0,
  PADDLE_DEBOUNCE_PRESS,
  PADDLE_HELD,
  PADDLE_DEBOUNCE_RELEASE
};

struct PaddleKey {
  uint8_t     pin;
  char        symbol;
  PaddleState state;
  uint32_t    stateSince;
  uint32_t    lastSymbolAt;
  bool        rawPressed;
  bool        symbolSent;
};

// ═══════════════════════════════════════════════════════════
//  BLE  (UUIDs identisch zur Website)
// ═══════════════════════════════════════════════════════════
BLEService              morseService("19b10000-e8f2-537e-4f6c-d104768a1214");
BLEStringCharacteristic bleCharTX("19b10001-e8f2-537e-4f6c-d104768a1214",
                                 BLERead | BLENotify, 4);
BLEStringCharacteristic bleCharRX("19b10002-e8f2-537e-4f6c-d104768a1214",
                                 BLEWrite | BLEWriteWithoutResponse, 8);

// ═══════════════════════════════════════════════════════════
//  LED-MATRIX
// ═══════════════════════════════════════════════════════════
ArduinoLEDMatrix matrix;
bool fb[8][12];

void fbClear() { memset(fb, 0, sizeof(fb)); }

void fbSet(uint8_t row, uint8_t col, bool on = true) {
  if (row < 8 && col < 12) fb[row][col] = on;
}

void matrixRender() {
  uint32_t frame[3] = {0, 0, 0};
  for (uint8_t r = 0; r < 8; r++) {
    for (uint8_t c = 0; c < 12; c++) {
      if (fb[r][c]) {
        uint8_t bitIdx = r * 12 + c;
        uint8_t word   = bitIdx / 32;
        uint8_t bit    = 31 - (bitIdx % 32);
        frame[word] |= (1UL << bit);
      }
    }
  }
  matrix.loadFrame(frame);
}

// ═══════════════════════════════════════════════════════════
//  FONT 5×7
// ═══════════════════════════════════════════════════════════
const uint8_t FONT5X7[][5] PROGMEM = {
  { 0b0111110, 0b1001001, 0b1001001, 0b1001001, 0b0111110 }, // A
  { 0b1111111, 0b1001001, 0b1001001, 0b1001001, 0b0110110 }, // B
  { 0b0111110, 0b1000001, 0b1000001, 0b1000001, 0b0100010 }, // C
  { 0b1111111, 0b1000001, 0b1000001, 0b1000001, 0b0111110 }, // D
  { 0b1111111, 0b1001001, 0b1001001, 0b1001001, 0b1000001 }, // E
  { 0b1111111, 0b0001001, 0b0001001, 0b0001001, 0b0000001 }, // F
  { 0b0111110, 0b1000001, 0b1001001, 0b1001001, 0b0111010 }, // G
  { 0b1111111, 0b0001000, 0b0001000, 0b0001000, 0b1111111 }, // H
  { 0b0000000, 0b1000001, 0b1111111, 0b1000001, 0b0000000 }, // I
  { 0b0100000, 0b1000000, 0b1000001, 0b0111111, 0b0000001 }, // J
  { 0b1111111, 0b0001000, 0b0010100, 0b0100010, 0b1000001 }, // K
  { 0b1111111, 0b1000000, 0b1000000, 0b1000000, 0b1000000 }, // L
  { 0b1111111, 0b0000010, 0b0000100, 0b0000010, 0b1111111 }, // M
  { 0b1111111, 0b0000010, 0b0000100, 0b0001000, 0b1111111 }, // N
  { 0b0111110, 0b1000001, 0b1000001, 0b1000001, 0b0111110 }, // O
  { 0b1111111, 0b0001001, 0b0001001, 0b0001001, 0b0000110 }, // P
  { 0b0111110, 0b1000001, 0b1010001, 0b0100001, 0b1011110 }, // Q
  { 0b1111111, 0b0001001, 0b0011001, 0b0101001, 0b1000110 }, // R
  { 0b1000110, 0b1001001, 0b1001001, 0b1001001, 0b0110001 }, // S
  { 0b0000001, 0b0000001, 0b1111111, 0b0000001, 0b0000001 }, // T
  { 0b0111111, 0b1000000, 0b1000000, 0b1000000, 0b0111111 }, // U
  { 0b0011111, 0b0100000, 0b1000000, 0b0100000, 0b0011111 }, // V
  { 0b0111111, 0b1000000, 0b0110000, 0b1000000, 0b0111111 }, // W
  { 0b1100011, 0b0010100, 0b0001000, 0b0010100, 0b1100011 }, // X
  { 0b0000111, 0b0001000, 0b1110000, 0b0001000, 0b0000111 }, // Y
  { 0b1100001, 0b1010001, 0b1001001, 0b1000101, 0b1000011 }, // Z
  { 0b0111110, 0b1010001, 0b1001001, 0b1000101, 0b0111110 }, // 0
  { 0b0000000, 0b1000010, 0b1111111, 0b1000000, 0b0000000 }, // 1
  { 0b1100010, 0b1010001, 0b1001001, 0b1001001, 0b1000110 }, // 2
  { 0b0100010, 0b1000001, 0b1001001, 0b1001001, 0b0110110 }, // 3
  { 0b0001111, 0b0001000, 0b0001000, 0b0001000, 0b1111111 }, // 4
  { 0b0100111, 0b1000101, 0b1000101, 0b1000101, 0b0111001 }, // 5
  { 0b0111100, 0b1001010, 0b1001001, 0b1001001, 0b0110000 }, // 6
  { 0b0000001, 0b1110001, 0b0001001, 0b0000101, 0b0000011 }, // 7
  { 0b0110110, 0b1001001, 0b1001001, 0b1001001, 0b0110110 }, // 8
  { 0b0000110, 0b1001001, 0b1001001, 0b1001001, 0b0111110 }, // 9
};

void fbDrawChar(char ch, uint8_t startRow, uint8_t startCol) {
  int8_t idx = -1;
  if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
  else if (ch >= '0' && ch <= '9') idx = 26 + (ch - '0');
  if (idx < 0) return;

  for (uint8_t col = 0; col < 5; col++) {
    uint8_t colData = pgm_read_byte(&FONT5X7[idx][col]);
    for (uint8_t row = 0; row < 7; row++) {
      fbSet(startRow + row, startCol + col, (colData >> row) & 1);
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  MORSE-ALPHABET
// ═══════════════════════════════════════════════════════════
const char* MORSE_TABLE[] = {
  ".-", "-...", "-.-.", "-..", ".",
  "..-.", "--.", "....", "..", ".---",
  "-.-", ".-..", "--", "-.", "---",
  ".--.", "--.-", ".-.", "...", "-",
  "..-", "...-", ".--", "-..-", "-.--",
  "--..",
  "-----", ".----", "..---", "...--", "....-",
  ".....", "-....", "--...", "---..", "----."
};
const char MORSE_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// ═══════════════════════════════════════════════════════════
//  ANWENDUNGSZUSTAND
// ═══════════════════════════════════════════════════════════
char     decodedChar    = ' ';
String   morseBuffer    = "";
uint32_t lastSymbolAt   = 0;
uint32_t displayHoldUntil = 0;

PaddleKey dit  = { PIN_DIT,  '.', PADDLE_OPEN, 0, 0, false, false };
PaddleKey dash = { PIN_DASH, '-', PADDLE_OPEN, 0, 0, false, false };

char decodeMorse(const String& code) {
  for (uint8_t i = 0; i < sizeof(MORSE_CHARS) - 1; i++) {
    if (code == MORSE_TABLE[i]) return MORSE_CHARS[i];
  }
  return '?';
}

// true wenn morseBuffer Anfang eines LÄNGEREN Codes ist (z.B. "-" → noch "9" möglich)
bool isPrefixOfLongerCode(const String& buf) {
  if (buf.length() == 0) return false;
  for (uint8_t i = 0; i < sizeof(MORSE_CHARS) - 1; i++) {
    const char* code = MORSE_TABLE[i];
    const uint8_t len = strlen(code);
    if (len > buf.length() && strncmp(code, buf.c_str(), buf.length()) == 0) {
      return true;
    }
  }
  return false;
}

uint16_t charTimeoutMs() {
  if (isPrefixOfLongerCode(morseBuffer)) return CHAR_TIMEOUT_AMBIG_MS;
  return CHAR_TIMEOUT_MS;
}

void updateDisplay() {
  fbClear();

  if ((decodedChar >= 'A' && decodedChar <= 'Z') ||
      (decodedChar >= '0' && decodedChar <= '9')) {
    fbDrawChar(decodedChar, 0, 0);
  }

  uint8_t x = 0;
  for (uint8_t i = 0; i < morseBuffer.length() && x < 12; i++) {
    if (morseBuffer[i] == '.') {
      fbSet(7, x);
      x += 2;
    } else if (morseBuffer[i] == '-') {
      if (x     < 12) fbSet(7, x);
      if (x + 1 < 12) fbSet(7, x + 1);
      x += 3;
    }
  }

  matrixRender();
}

void bleNotifySymbol(char symbol) {
  if (!BLE.connected()) return;
  // Leerzeichen-Prefix: BLE notify bei zwei gleichen Symbolen (z.B. "..")
  bleCharTX.writeValue(" ");
  bleCharTX.writeValue(String(symbol));
}

// Majority-Vote über mehrere Samples – filtert Einzelimpulse zuverlässig
bool samplePressed(uint8_t pin) {
  uint8_t lows = 0;
  for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
    if (digitalRead(pin) == LOW) lows++;
    if (i + 1 < SAMPLE_COUNT) delayMicroseconds(SAMPLE_INTERVAL_US);
  }
  return lows >= SAMPLE_THRESHOLD;
}

bool cooldownElapsed(const PaddleKey& key) {
  return key.lastSymbolAt == 0 ||
         (millis() - key.lastSymbolAt) >= SYMBOL_COOLDOWN_MS;
}

void emitSymbol(PaddleKey& key) {
  Serial.print(key.symbol);
  bleNotifySymbol(key.symbol);

  morseBuffer += key.symbol;
  lastSymbolAt = millis();
  key.lastSymbolAt = millis();
  key.symbolSent = true;

  displayHoldUntil = 0;   // neue Eingabe → kein altes T/E mehr anzeigen
  decodedChar = ' ';
  updateDisplay();
}

// Advanced Debounce: Symbol nur beim bestätigten Drücken (Key-Down), kein Nachsenden
void processPaddle(PaddleKey& key) {
  const bool pressed = samplePressed(key.pin);
  const uint32_t now = millis();

  if (pressed != key.rawPressed) {
    key.rawPressed = pressed;
    key.stateSince = now;
  }

  switch (key.state) {
    case PADDLE_OPEN:
      if (pressed && (now - key.stateSince) >= PRESS_SETTLE_MS) {
        key.state = PADDLE_DEBOUNCE_PRESS;
        key.stateSince = now;
      } else if (!pressed) {
        key.symbolSent = false;
      }
      break;

    case PADDLE_DEBOUNCE_PRESS:
      if (!pressed) {
        key.state = PADDLE_OPEN;
        key.stateSince = now;
        break;
      }
      if (!key.symbolSent && (now - key.stateSince) >= PRESS_SETTLE_MS &&
          cooldownElapsed(key)) {
        emitSymbol(key);
        key.state = PADDLE_HELD;
        key.stateSince = now;
      }
      break;

    case PADDLE_HELD:
      if (!pressed) {
        key.state = PADDLE_DEBOUNCE_RELEASE;
        key.stateSince = now;
      }
      break;

    case PADDLE_DEBOUNCE_RELEASE:
      if (pressed) {
        key.state = PADDLE_HELD;
        key.stateSince = now;
        break;
      }
      if ((now - key.stateSince) >= RELEASE_SETTLE_MS) {
        key.state = PADDLE_OPEN;
        key.stateSince = now;
        key.symbolSent = false;
      }
      break;
  }
}

void finalizeChar() {
  if (morseBuffer.length() == 0) return;

  decodedChar = decodeMorse(morseBuffer);
  updateDisplay();

  displayHoldUntil = millis() + DISPLAY_HOLD_MS;
  morseBuffer = "";
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_DIT,  INPUT_PULLUP);
  pinMode(PIN_DASH, INPUT_PULLUP);

  matrix.begin();
  updateDisplay();

  if (!BLE.begin()) {
    Serial.println("[BLE] Init fehlgeschlagen – USB-only");
  } else {
    BLE.setLocalName("Arduino-MorseTrainer");
    BLE.setAdvertisedService(morseService);
    morseService.addCharacteristic(bleCharTX);
    morseService.addCharacteristic(bleCharRX);
    BLE.addService(morseService);
    bleCharTX.setValue("");
    bleCharRX.setValue("");
    BLE.advertise();
    Serial.println("[BLE] Bereit als 'Arduino-MorseTrainer'");
  }

  Serial.println("[USB] Bereit @ 115200 Baud | Dit=D2  Dash=D4");
  Serial.print("[CFG] CHAR_TIMEOUT_MS=");
  Serial.print(CHAR_TIMEOUT_MS);
  Serial.print(" / AMBIG=");
  Serial.println(CHAR_TIMEOUT_AMBIG_MS);
}

void loop() {
  BLE.poll();

  processPaddle(dit);
  processPaddle(dash);

  if (morseBuffer.length() > 0 &&
      (millis() - lastSymbolAt) >= charTimeoutMs()) {
    finalizeChar();
  }

  if (displayHoldUntil > 0 && millis() >= displayHoldUntil) {
    displayHoldUntil = 0;
    decodedChar = ' ';
    updateDisplay();
  }

  if (bleCharRX.written()) {
    String val = bleCharRX.value();
    if (val.length() > 0) {
      Serial.print("[RX-BLE Website-Ziel]: ");
      Serial.println(val[0]);
    }
  }

  while (Serial.available()) {
    (void)Serial.read();
  }
}
