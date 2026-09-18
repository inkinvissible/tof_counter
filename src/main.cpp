#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "PeopleCounter.h"

// ============================================================
// main.cpp — capa de hardware y simulación.
//
// Responsabilidades:
//   - inicializar ESP32 / OLED / Serial
//   - generar/simular los frames ToF
//   - pasar cada frame a PeopleCounter
//   - leer los resultados (contadores + eventos)
//   - actualizar OLED y logs Serial
//
// Toda la lógica del algoritmo vive en PeopleCounter
// (lib/people_counter), que es C++ puro y testeable en native.
// ============================================================

static constexpr int MATRIX_SIZE = PeopleCounter::MATRIX_SIZE;

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
// ESTADO
// ============================================================

PeopleCounter counter;

uint16_t depthMatrix[MATRIX_SIZE][MATRIX_SIZE];

// ============================================================
// SIMULACIÓN
// ============================================================

int personAY = -2;
int personDirection = 1;

unsigned long lastFrameTime = 0;
const unsigned long FRAME_INTERVAL = 700;

// ============================================================
// OLED
// ============================================================

void updateDisplay(int detected = 0) {

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("TOF PEOPLE COUNTER");

    display.drawLine(
        0,
        10,
        127,
        10,
        SSD1306_WHITE
    );

    display.setTextSize(2);
    display.setCursor(0, 16);

    display.print("Seen:");
    display.println(detected);

    display.setTextSize(1);

    display.setCursor(0, 43);
    display.print("IN:");
    display.print(counter.getTotalIn());

    display.setCursor(55, 43);
    display.print("OUT:");
    display.print(counter.getTotalOut());

    display.setCursor(0, 55);
    display.print("Inside:");
    display.print(counter.getPeopleInside());

    display.display();
}

// ============================================================
// SIMULACIÓN DE MATRIZ ToF
// ============================================================

void clearDepthMatrix() {

    for (int y = 0; y < MATRIX_SIZE; y++) {

        for (int x = 0; x < MATRIX_SIZE; x++) {

            int noise = random(-10, 11);

            depthMatrix[y][x] =
                PeopleCounter::FLOOR_DISTANCE_MM + noise;
        }
    }
}

// ============================================================
// AGREGAR UNA PERSONA SIMULADA
// ============================================================

void addPersonBlob(
    int startX,
    int startY
) {

    // Persona = blob 2x2

    for (int dy = 0; dy < 2; dy++) {

        int y = startY + dy;

        if (
            y < 0 ||
            y >= MATRIX_SIZE
        ) {
            continue;
        }

        for (int dx = 0; dx < 2; dx++) {

            int x = startX + dx;

            if (
                x < 0 ||
                x >= MATRIX_SIZE
            ) {
                continue;
            }

            depthMatrix[y][x] =
                900 + random(-30, 31);
        }
    }
}

// ============================================================
// GENERAR ESCENARIO
// ============================================================

void generateScenario() {

    clearDepthMatrix();

    // Persona se acerca por el exterior
    addPersonBlob(
        3,
        personAY
    );
}

// ============================================================
// VISUALIZACIÓN (solo para Serial; el algoritmo usa su
// propia máscara interna dentro de PeopleCounter)
// ============================================================

void printOccupancyMatrix() {

    Serial.println();
    Serial.println("-------- FRAME --------");

    for (int y = 0; y < MATRIX_SIZE; y++) {

        for (int x = 0; x < MATRIX_SIZE; x++) {

            int height =
                static_cast<int>(PeopleCounter::FLOOR_DISTANCE_MM) -
                static_cast<int>(depthMatrix[y][x]);

            bool fg =
                height >
                static_cast<int>(PeopleCounter::HEIGHT_THRESHOLD_MM);

            Serial.print(fg ? "#" : ".");
        }

        if (y == 3) {
            Serial.print(
                "   <-- LINEA VIRTUAL"
            );
        }

        Serial.println();
    }
}

// ============================================================
// IMPRIMIR BLOBS (lee el estado de PeopleCounter)
// ============================================================

void printBlobs() {

    Serial.println();

    Serial.print(
        "Blobs detectados: "
    );

    Serial.println(counter.getDetectedBlobCount());

    for (
        int i = 0;
        i < counter.getDetectedBlobCount();
        i++
    ) {
        PeopleCounter::BlobInfo blob = counter.getBlob(i);

        Serial.print("Blob #");
        Serial.println(i + 1);

        Serial.print("  Pixels: ");
        Serial.println(blob.pixelCount);

        Serial.print("  Centroide: X=");
        Serial.print(blob.centroidX, 2);

        Serial.print(" Y=");
        Serial.println(blob.centroidY, 2);
    }
}

void printTracks() {

    Serial.println();
    Serial.println("Tracks activos:");

    for (int i = 0; i < counter.getTrackSlotCount(); i++) {

        PeopleCounter::TrackInfo track = counter.getTrackSlot(i);

        if (!track.active) {
            continue;
        }

        Serial.print("  Track #");
        Serial.print(track.id);

        Serial.print(" -> X=");
        Serial.print(track.x, 2);

        Serial.print(" Y=");
        Serial.print(track.y, 2);

        Serial.print(" counted=");
        Serial.println(
            track.alreadyCounted
                ? "YES"
                : "NO"
        );
    }
}

// Informa creaciones/bajas de tracks comparando los IDs
// activos antes y después del frame. Replica los mensajes
// "NUEVO TRACK" / "FINALIZADO" que antes imprimía la lógica
// de tracking, sin que PeopleCounter dependa de Serial.
void printTrackLifecycle(
    const int idsBefore[PeopleCounter::MAX_TRACKS],
    int countBefore
) {
    // Altas
    for (int i = 0; i < counter.getTrackSlotCount(); i++) {
        PeopleCounter::TrackInfo track = counter.getTrackSlot(i);

        if (!track.active) {
            continue;
        }

        bool known = false;
        for (int j = 0; j < countBefore; j++) {
            if (idsBefore[j] == track.id) {
                known = true;
                break;
            }
        }

        if (!known) {
            Serial.print("NUEVO TRACK #");
            Serial.println(track.id);
        }
    }

    // Bajas
    for (int j = 0; j < countBefore; j++) {
        bool stillActive = false;
        for (int i = 0; i < counter.getTrackSlotCount(); i++) {
            PeopleCounter::TrackInfo track = counter.getTrackSlot(i);
            if (track.active && track.id == idsBefore[j]) {
                stillActive = true;
                break;
            }
        }

        if (!stillActive) {
            Serial.print("TRACK #");
            Serial.print(idsBefore[j]);
            Serial.println(" FINALIZADO");
        }
    }
}

void printCrossingEvents() {
    for (int i = 0; i < counter.getLastEventCount(); i++) {
        PeopleCounter::CrossingEvent event = counter.getLastEvent(i);

        Serial.println();
        Serial.print(">>> TRACK #");
        Serial.print(event.trackId);

        if (event.direction == PeopleCounter::CrossingDirection::In) {
            Serial.println(" ENTRO <<<");
        } else if (
            event.direction == PeopleCounter::CrossingDirection::Out
        ) {
            Serial.println(" SALIO <<<");
        } else {
            continue;
        }

        Serial.print("Inside: ");
        Serial.println(counter.getPeopleInside());
    }
}

// ============================================================
// PROCESAR FRAME
// ============================================================

void processFrame() {

    generateScenario();

    // Snapshot de tracks activos para el log de altas/bajas.
    int idsBefore[PeopleCounter::MAX_TRACKS];
    int countBefore = 0;
    for (int i = 0; i < counter.getTrackSlotCount(); i++) {
        PeopleCounter::TrackInfo track = counter.getTrackSlot(i);
        if (track.active) {
            idsBefore[countBefore++] = track.id;
        }
    }

    counter.processFrame(depthMatrix);

    printOccupancyMatrix();

    printBlobs();

    printTrackLifecycle(idsBefore, countBefore);
    printCrossingEvents();

    printTracks();

    updateDisplay(counter.getDetectedBlobCount());
}

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    Wire.begin(
        21,
        22
    );

    delay(500);

    randomSeed(
        analogRead(0)
    );

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        " PEOPLE COUNTER - MULTI BLOB"
    );

    Serial.println(
        "================================"
    );

    if (
        !display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDRESS
        )
    ) {

        Serial.println(
            "ERROR OLED"
        );

        while (true) {
            delay(1000);
        }
    }

    updateDisplay();

    Serial.println(
        "Sistema iniciado"
    );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

    if (
        millis() -
        lastFrameTime >=
        FRAME_INTERVAL
    ) {

        lastFrameTime =
            millis();

        processFrame();

        personAY += personDirection;

        // Llegó cerca de la línea, pero NO la cruza.
        // Se arrepiente y vuelve.
        if (personAY >= 2) {
            personDirection = -1;
        }

        // Cuando vuelve a desaparecer por arriba:
        if (personAY < -2) {

            Serial.println();
            Serial.println(
                "===== FIN ESCENARIO SIN CRUCE ====="
            );

            Serial.print("IN total: ");
            Serial.println(counter.getTotalIn());

            Serial.print("OUT total: ");
            Serial.println(counter.getTotalOut());

            Serial.print("Inside: ");
            Serial.println(counter.getPeopleInside());

            delay(3000);

            personAY = -2;
            personDirection = 1;
        }
    }
}
