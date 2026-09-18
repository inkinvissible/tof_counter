#include <Wire.h>

// ============================================================
// MODO SIMULACIÓN WOKWI (ámbito académico)
// ------------------------------------------------------------
// Wokwi no dispone de parte oficial VL53L0X, por lo que el
// entorno [env:wokwi] (build_flags = -DWOKWI_SIM) compila este
// mismo archivo con entradas simuladas:
//
//   Sensor real              | Simulación Wokwi
//   -------------------------|-------------------------------
//   VL53L0X A (0x30, XSHUT 25) | Pulsador SIM_A en GPIO 32
//   VL53L0X B (0x31, XSHUT 26) | Pulsador SIM_B en GPIO 33
//   Presencia por distancia    | LOW = persona, HIGH = libre
//                              (INPUT_PULLUP)
//
// La MÁQUINA DE ESTADOS (IDLE / A_FIRST / B_FIRST / WAIT_CLEAR)
// y los contadores son LOS MISMOS en ambos modos. Solo cambia
// la adquisición de presencia/distancia.
//
// Secuencias para contar (igual en hardware y Wokwi):
//   A y luego B (< 1.5 s) = ENTRADA
//   B y luego A (< 1.5 s) = SALIDA
//
// Compilación:
//   Hardware real -> pio run -e esp32dev
//   Simulación    -> pio run -e wokwi
// ============================================================

#ifdef WOKWI_SIM
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#else
#include <Adafruit_VL53L0X.h>
#endif

// ==============================
// PINES
// ==============================

#define SDA_PIN 21
#define SCL_PIN 22

#ifndef WOKWI_SIM
#define XSHUT_A 25
#define XSHUT_B 26
#else
// En Wokwi los pines 25/26 se reutilizan como LEDs indicadores
// de presencia (académico): HIGH = OCUPADO, LOW = LIBRE.
#define SIM_A_BUTTON 32
#define SIM_B_BUTTON 33
#define SIM_A_LED 25
#define SIM_B_LED 26

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C
#endif

// Direcciones I2C nuevas
#define ADDRESS_A 0x30
#define ADDRESS_B 0x31

// ==============================
// CONFIGURACIÓN
// ==============================

// Persona detectada si está más cerca que esto
const uint16_t DETECT_MM = 1300;

// Se considera nuevamente libre por encima de esto.
// La diferencia evita parpadeos cerca del límite.
const uint16_t RELEASE_MM = 1500;

// Tiempo máximo entre sensor A y sensor B
const unsigned long MAX_CROSS_TIME = 1500;

#ifdef WOKWI_SIM
// Distancias ficticias solo para que el log Serial de Wokwi
// tenga el mismo formato que el hardware real.
const uint16_t SIM_CLOSE_MM = 800;
const uint16_t SIM_FAR_MM = 2000;
#endif

// ==============================
// SENSORES
// ==============================

#ifndef WOKWI_SIM
Adafruit_VL53L0X sensorA = Adafruit_VL53L0X();
Adafruit_VL53L0X sensorB = Adafruit_VL53L0X();
#else
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#endif

// ==============================
// VARIABLES
// ==============================

bool presenceA = false;
bool presenceB = false;

bool previousA = false;
bool previousB = false;

unsigned long sequenceStart = 0;

unsigned long entradas = 0;
unsigned long salidas = 0;

enum State {
  IDLE,
  A_FIRST,
  B_FIRST,
  WAIT_CLEAR
};

State state = IDLE;


#ifndef WOKWI_SIM
// ==============================
// INICIALIZAR LOS DOS VL53L0X
// ==============================

void initSensors() {

  // Apagamos ambos sensores
  digitalWrite(XSHUT_A, LOW);
  digitalWrite(XSHUT_B, LOW);

  delay(20);

  // --------------------------
  // SENSOR A
  // --------------------------

  digitalWrite(XSHUT_A, HIGH);
  delay(20);

  if (!sensorA.begin(ADDRESS_A)) {
    Serial.println("ERROR: no se encontró SENSOR A");
    while (1) {
      delay(10);
    }
  }

  // --------------------------
  // SENSOR B
  // --------------------------

  digitalWrite(XSHUT_B, HIGH);
  delay(20);

  if (!sensorB.begin(ADDRESS_B)) {
    Serial.println("ERROR: no se encontró SENSOR B");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Sensores inicializados correctamente.");
}


// ==============================
// LEER DISTANCIA
// ==============================

uint16_t readDistance(Adafruit_VL53L0X &sensor) {

  VL53L0X_RangingMeasurementData_t measure;

  sensor.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    return measure.RangeMilliMeter;
  }

  // 8190 = lectura inválida/fuera de rango
  return 8190;
}


// ==============================
// DETECTAR PRESENCIA
// CON HISTÉRESIS
// ==============================

bool updatePresence(uint16_t distance, bool currentPresence) {

  // Si la lectura fue inválida, mantenemos el estado anterior.
  if (distance == 8190) {
    return currentPresence;
  }

  if (!currentPresence) {

    if (distance < DETECT_MM) {
      return true;
    }

  } else {

    if (distance > RELEASE_MM) {
      return false;
    }
  }

  return currentPresence;
}

#else
// ==============================
// OLED (solo Wokwi)
// Muestra el mismo conteo que el Serial.
// ==============================

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("TOF COUNTER (WOKWI)");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 14);
  display.print("IN:");
  display.println(entradas);

  display.setCursor(0, 34);
  display.print("OUT:");
  display.println(salidas);

  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("Dentro: ");
  display.print((long)entradas - (long)salidas);
  display.print(presenceA ? " A*" : "");
  display.print(presenceB ? " B*" : "");

  display.display();
}
#endif


// ==============================
// SETUP
// ==============================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
#ifdef WOKWI_SIM
  Serial.println("Contador de personas ESP32 (MODO SIMULACION WOKWI)");
  Serial.println("A = boton GPIO32, B = boton GPIO33 (LOW = persona)");
  Serial.println("A y luego B = ENTRADA | B y luego A = SALIDA");

  pinMode(SIM_A_BUTTON, INPUT_PULLUP);
  pinMode(SIM_B_BUTTON, INPUT_PULLUP);
  pinMode(SIM_A_LED, OUTPUT);
  pinMode(SIM_B_LED, OUTPUT);
  digitalWrite(SIM_A_LED, LOW);
  digitalWrite(SIM_B_LED, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR OLED en Wokwi");
    while (1) {
      delay(1000);
    }
  }

  updateDisplay();
  Serial.println("Sistema listo (Wokwi).");
#else
  Serial.println("Contador de personas ESP32 + 2x VL53L0X");

  pinMode(XSHUT_A, OUTPUT);
  pinMode(XSHUT_B, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);

  initSensors();

  Serial.println("Sistema listo.");
#endif
}


// ==============================
// LOOP
// ==============================

void loop() {

#ifdef WOKWI_SIM
  // --- Adquisición simulada: pulsador presionado = persona ---
  previousA = presenceA;
  previousB = presenceB;

  presenceA = (digitalRead(SIM_A_BUTTON) == LOW);
  presenceB = (digitalRead(SIM_B_BUTTON) == LOW);

  uint16_t distanceA = presenceA ? SIM_CLOSE_MM : SIM_FAR_MM;
  uint16_t distanceB = presenceB ? SIM_CLOSE_MM : SIM_FAR_MM;

  bool activatedA = presenceA && !previousA;
  bool activatedB = presenceB && !previousB;
#else
  // Leer sensores
  uint16_t distanceA = readDistance(sensorA);
  uint16_t distanceB = readDistance(sensorB);

  // Guardamos estado anterior
  previousA = presenceA;
  previousB = presenceB;

  // Actualizamos presencia
  presenceA = updatePresence(distanceA, presenceA);
  presenceB = updatePresence(distanceB, presenceB);

  // Detectar momento exacto en que alguien entra
  // en la zona de cada sensor
  bool activatedA = presenceA && !previousA;
  bool activatedB = presenceB && !previousB;
#endif


  // ==============================
  // MÁQUINA DE ESTADOS (común a hardware y Wokwi)
  // ==============================

  switch (state) {

    // --------------------------
    // Nadie cruzando
    // --------------------------

    case IDLE:

      if (activatedA) {

        state = A_FIRST;
        sequenceStart = millis();

        Serial.println("A detectado primero");

      } else if (activatedB) {

        state = B_FIRST;
        sequenceStart = millis();

        Serial.println("B detectado primero");
      }

      break;


    // --------------------------
    // A -> esperando B
    // --------------------------

    case A_FIRST:

      if (activatedB &&
          millis() - sequenceStart <= MAX_CROSS_TIME) {

        entradas++;

        Serial.println();
        Serial.println(">>> ENTRADA <<<");

        Serial.print("Entradas: ");
        Serial.println(entradas);

        Serial.print("Salidas: ");
        Serial.println(salidas);

        Serial.print("Personas dentro: ");
        Serial.println((long)entradas - (long)salidas);

        Serial.println();

        state = WAIT_CLEAR;
      }

      // Timeout
      else if (millis() - sequenceStart > MAX_CROSS_TIME) {

        Serial.println("Secuencia A cancelada");

        state = WAIT_CLEAR;
      }

      break;


    // --------------------------
    // B -> esperando A
    // --------------------------

    case B_FIRST:

      if (activatedA &&
          millis() - sequenceStart <= MAX_CROSS_TIME) {

        salidas++;

        Serial.println();
        Serial.println("<<< SALIDA >>>");

        Serial.print("Entradas: ");
        Serial.println(entradas);

        Serial.print("Salidas: ");
        Serial.println(salidas);

        Serial.print("Personas dentro: ");
        Serial.println((long)entradas - (long)salidas);

        Serial.println();

        state = WAIT_CLEAR;
      }

      // Timeout
      else if (millis() - sequenceStart > MAX_CROSS_TIME) {

        Serial.println("Secuencia B cancelada");

        state = WAIT_CLEAR;
      }

      break;


    // --------------------------
    // Esperamos que la persona
    // salga de ambos sensores
    // --------------------------

    case WAIT_CLEAR:

      if (!presenceA && !presenceB) {

        state = IDLE;

        Serial.println("Zona libre");
      }

      break;
  }


  // ==============================
  // DEBUG
  // ==============================

  static unsigned long lastPrint = 0;

  if (millis() - lastPrint > 300) {

    lastPrint = millis();

    Serial.print("A: ");
    Serial.print(distanceA);
    Serial.print(" mm ");

    Serial.print(presenceA ? "[OCUPADO]" : "[LIBRE]");

    Serial.print("   |   B: ");

    Serial.print(distanceB);
    Serial.print(" mm ");

    Serial.println(presenceB ? "[OCUPADO]" : "[LIBRE]");
  }

#ifdef WOKWI_SIM
  digitalWrite(SIM_A_LED, presenceA ? HIGH : LOW);
  digitalWrite(SIM_B_LED, presenceB ? HIGH : LOW);

  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay > 200) {
    lastDisplay = millis();
    updateDisplay();
  }
#endif
}
