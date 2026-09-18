#include <Wire.h>
#include <Adafruit_VL53L0X.h>

// ==============================
// PINES
// ==============================

#define SDA_PIN 21
#define SCL_PIN 22

#define XSHUT_A 25
#define XSHUT_B 26

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

// ==============================
// SENSORES
// ==============================

Adafruit_VL53L0X sensorA = Adafruit_VL53L0X();
Adafruit_VL53L0X sensorB = Adafruit_VL53L0X();

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


// ==============================
// SETUP
// ==============================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("Contador de personas ESP32 + 2x VL53L0X");

  pinMode(XSHUT_A, OUTPUT);
  pinMode(XSHUT_B, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);

  initSensors();

  Serial.println("Sistema listo.");
}


// ==============================
// LOOP
// ==============================

void loop() {

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


  // ==============================
  // MÁQUINA DE ESTADOS
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
}
