#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

// ============================================================
// MATRIZ ToF SIMULADA
// ============================================================

#define MATRIX_SIZE 8

// Sensor a 2.5 metros del piso
const uint16_t FLOOR_DISTANCE_MM = 2500;

// Si algo está al menos 500 mm por encima del piso,
// lo consideramos foreground.
const uint16_t HEIGHT_THRESHOLD_MM = 500;

// Matriz de profundidad actual
uint16_t depthMatrix[MATRIX_SIZE][MATRIX_SIZE];

// Máscara binaria de ocupación
bool occupied[MATRIX_SIZE][MATRIX_SIZE];

// ============================================================
// CONTADORES
// ============================================================

int peopleInside = 0;
int totalIn = 0;
int totalOut = 0;

// ============================================================
// TRACKING
// ============================================================

float previousCentroidY = -1;
float currentCentroidY = -1;

// Línea virtual que divide exterior/interior
const float CROSSING_LINE_Y = 3.5;

// Para evitar contar dos veces la misma persona
bool alreadyCounted = false;

// ============================================================
// SIMULACIÓN
// ============================================================

// Fila superior del blob de la persona
int simulatedPersonY = -2;

unsigned long lastFrameTime = 0;
const unsigned long FRAME_INTERVAL = 700;

// ============================================================
// OLED
// ============================================================

void updateDisplay(const char *lastEvent = "-") {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("PEOPLE COUNTER");

    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 16);
    display.print("Inside:");
    display.println(peopleInside);

    display.setTextSize(1);

    display.setCursor(0, 42);
    display.print("IN:");
    display.print(totalIn);

    display.setCursor(55, 42);
    display.print("OUT:");
    display.print(totalOut);

    display.setCursor(0, 54);
    display.print("Last: ");
    display.print(lastEvent);

    display.display();
}

// ============================================================
// GENERAR FRAME VACÍO
// ============================================================

void clearDepthMatrix() {
    for (int y = 0; y < MATRIX_SIZE; y++) {
        for (int x = 0; x < MATRIX_SIZE; x++) {

            // Pequeña variación para que parezca una medición real
            int noise = random(-10, 11);

            depthMatrix[y][x] =
                FLOOR_DISTANCE_MM + noise;
        }
    }
}

// ============================================================
// SIMULAR PERSONA
// ============================================================

void addSimulatedPerson() {

    // Persona representada como un blob 2×2
    // centrado aproximadamente en X = 3.5

    for (int dy = 0; dy < 2; dy++) {

        int y = simulatedPersonY + dy;

        if (y < 0 || y >= MATRIX_SIZE) {
            continue;
        }

        for (int x = 3; x <= 4; x++) {

            // La cabeza/cuerpo está a aprox. 900 mm del sensor
            depthMatrix[y][x] =
                900 + random(-30, 31);
        }
    }
}

// ============================================================
// CONVERTIR PROFUNDIDAD → FOREGROUND
// ============================================================

void calculateForegroundMask() {

    for (int y = 0; y < MATRIX_SIZE; y++) {

        for (int x = 0; x < MATRIX_SIZE; x++) {

            int height =
                FLOOR_DISTANCE_MM - depthMatrix[y][x];

            occupied[y][x] =
                height > HEIGHT_THRESHOLD_MM;
        }
    }
}

// ============================================================
// MOSTRAR MATRIZ
// ============================================================

void printOccupancyMatrix() {

    Serial.println();
    Serial.println("-------- FRAME --------");

    for (int y = 0; y < MATRIX_SIZE; y++) {

        for (int x = 0; x < MATRIX_SIZE; x++) {

            if (occupied[y][x]) {
                Serial.print("#");
            } else {
                Serial.print(".");
            }
        }

        // Dibujamos visualmente la línea virtual
        if (y == 3) {
            Serial.print("   <-- LINEA");
        }

        Serial.println();
    }
}

// ============================================================
// CALCULAR CENTROIDE
// ============================================================

bool calculateCentroid(float &centroidX, float &centroidY) {

    int count = 0;
    float sumX = 0;
    float sumY = 0;

    for (int y = 0; y < MATRIX_SIZE; y++) {

        for (int x = 0; x < MATRIX_SIZE; x++) {

            if (occupied[y][x]) {

                sumX += x;
                sumY += y;

                count++;
            }
        }
    }

    if (count == 0) {
        return false;
    }

    centroidX = sumX / count;
    centroidY = sumY / count;

    return true;
}

// ============================================================
// DETECTAR CRUCE
// ============================================================

void detectCrossing(float centroidY) {

    currentCentroidY = centroidY;

    if (
        previousCentroidY >= 0 &&
        !alreadyCounted
    ) {

        // Exterior → Interior
        if (
            previousCentroidY < CROSSING_LINE_Y &&
            currentCentroidY >= CROSSING_LINE_Y
        ) {

            totalIn++;
            peopleInside++;

            alreadyCounted = true;

            Serial.println();
            Serial.println(">>> PERSONA ENTRANDO <<<");

            Serial.print("Personas dentro: ");
            Serial.println(peopleInside);

            updateDisplay("IN");
        }

        // Interior → Exterior
        else if (
            previousCentroidY > CROSSING_LINE_Y &&
            currentCentroidY <= CROSSING_LINE_Y
        ) {

            totalOut++;

            if (peopleInside > 0) {
                peopleInside--;
            }

            alreadyCounted = true;

            Serial.println();
            Serial.println(">>> PERSONA SALIENDO <<<");

            Serial.print("Personas dentro: ");
            Serial.println(peopleInside);

            updateDisplay("OUT");
        }
    }

    previousCentroidY = currentCentroidY;
}

// ============================================================
// PROCESAR FRAME
// ============================================================

void processFrame() {

    clearDepthMatrix();

    addSimulatedPerson();

    calculateForegroundMask();

    printOccupancyMatrix();

    float centroidX;
    float centroidY;

    bool personDetected =
        calculateCentroid(centroidX, centroidY);

    if (personDetected) {

        Serial.print("Centroide: X=");
        Serial.print(centroidX, 2);

        Serial.print(" Y=");
        Serial.println(centroidY, 2);

        detectCrossing(centroidY);

    } else {

        Serial.println("Sin persona detectada");

        previousCentroidY = -1;
    }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    Wire.begin(21, 22);

    delay(500);

    randomSeed(analogRead(0));

    Serial.println();
    Serial.println("==============================");
    Serial.println(" PEOPLE COUNTER - ToF 8x8");
    Serial.println("==============================");

    if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS
    )) {

        Serial.println(
            "ERROR: No se pudo iniciar OLED"
        );

        while (true) {
            delay(1000);
        }
    }

    Serial.println(
        "OLED inicializada correctamente"
    );

    updateDisplay();

    Serial.println();
    Serial.println(
        "Iniciando simulacion..."
    );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

    if (
        millis() - lastFrameTime >= FRAME_INTERVAL
    ) {

        lastFrameTime = millis();

        processFrame();

        // Mover persona hacia el interior
        simulatedPersonY++;

        // Terminó de atravesar la matriz
        if (simulatedPersonY > MATRIX_SIZE) {

            Serial.println();
            Serial.println(
                "=== FIN DEL RECORRIDO ==="
            );

            Serial.println(
                "Reiniciando en 3 segundos..."
            );

            delay(3000);

            simulatedPersonY = -2;

            previousCentroidY = -1;
            currentCentroidY = -1;

            alreadyCounted = false;
        }
    }
}