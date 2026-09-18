#pragma once

// ============================================================
// PeopleCounter — núcleo puro del contador de personas.
//
// C++ puro, sin dependencias de hardware:
//   NO Arduino.h, NO Wire, NO OLED, NO Serial,
//   NO millis(), NO random(), NO Wokwi, NO I2C.
//
// Responsabilidades:
//   - segmentación foreground (depth -> foreground)
//   - connected components (BFS, conectividad 4)
//   - centroides
//   - tracking multi-objeto (nearest-neighbor)
//   - detección de cruce de línea virtual
//   - conteo totalIn / totalOut / peopleInside
//
// El algoritmo es el mismo que usaba src/main.cpp antes del
// refactor; solo se movió aquí para hacerlo testeable en el
// entorno `native` de PlatformIO.
// ============================================================

#include <stdint.h>
#include <stddef.h>

class PeopleCounter {
public:
    // ---- Parámetros (antes: números dispersos en main.cpp) ----
    static constexpr int MATRIX_SIZE = 8;
    static constexpr uint16_t FLOOR_DISTANCE_MM = 2500;
    static constexpr uint16_t HEIGHT_THRESHOLD_MM = 500;
    static constexpr int MIN_BLOB_PIXELS = 2;
    static constexpr int MAX_BLOBS = 8;
    static constexpr int MAX_TRACKS = 8;
    static constexpr float MAX_TRACK_DISTANCE = 2.5f;
    static constexpr int MAX_MISSED_FRAMES = 2;
    static constexpr float CROSSING_LINE_Y = 3.5f;

    // Máximo de eventos de cruce que puede generar un frame.
    // Un track solo puede cruzar una vez por frame, así que
    // MAX_TRACKS es una cota segura.
    static constexpr int MAX_EVENTS_PER_FRAME = MAX_TRACKS;

    enum class CrossingDirection {
        None,
        In,
        Out
    };

    struct CrossingEvent {
        int trackId;
        CrossingDirection direction;
    };

    struct BlobInfo {
        int pixelCount;
        float centroidX;
        float centroidY;
    };

    struct TrackInfo {
        int id;
        bool active;
        float x;
        float y;
        float previousY;
        int missedFrames;
        bool alreadyCounted;
    };

    PeopleCounter();

    // Vuelve al estado inicial (contadores en 0, sin tracks).
    void reset();

    // Fija la ocupación inicial (p. ej. 1 antes de simular
    // una salida). No toca totalIn/totalOut. Clampeado a >= 0.
    void setInitialOccupancy(int value);

    // Procesa un frame de profundidad de
    // MATRIX_SIZE x MATRIX_SIZE (mm).
    void processFrame(
        const uint16_t frame[MATRIX_SIZE][MATRIX_SIZE]
    );

    int getPeopleInside() const;
    int getTotalIn() const;
    int getTotalOut() const;

    // Blobs válidos (>= MIN_BLOB_PIXELS) del último frame.
    int getDetectedBlobCount() const;
    BlobInfo getBlob(int index) const;

    int getActiveTrackCount() const;

    // Inspección de slots de tracking (útil en tests).
    int getTrackSlotCount() const;
    TrackInfo getTrackSlot(int index) const;

    // Eventos de cruce generados por el ÚLTIMO frame.
    // Permite que main.cpp actualice OLED/Serial sin que
    // esta clase dependa de Serial.
    int getLastEventCount() const;
    CrossingEvent getLastEvent(int index) const;

private:
    struct Blob {
        int id;
        int pixelCount;
        float centroidX;
        float centroidY;
    };

    struct Track {
        int id;
        bool active;
        float x;
        float y;
        float previousY;
        int missedFrames;
        bool alreadyCounted;
    };

    // Estado interno (antes: globales en main.cpp).
    bool occupied_[MATRIX_SIZE][MATRIX_SIZE];
    bool visited_[MATRIX_SIZE][MATRIX_SIZE];

    Blob blobs_[MAX_BLOBS];
    int blobCount_;

    Track tracks_[MAX_TRACKS];
    int nextTrackId_;

    int peopleInside_;
    int totalIn_;
    int totalOut_;

    CrossingEvent lastEvents_[MAX_EVENTS_PER_FRAME];
    int lastEventCount_;

    void calculateForegroundMask(
        const uint16_t frame[MATRIX_SIZE][MATRIX_SIZE]
    );
    void resetVisited();
    Blob exploreComponent(int startX, int startY, int blobId);
    void findBlobs();

    // Asociación blob <-> track. Diseñada como un paso
    // separado para que en el futuro sea fácil reemplazar
    // el nearest-neighbor greedy por otro algoritmo
    // (p. ej. húngaro o Kalman) sin tocar el resto.
    void updateTracks();
    void associateBlobsToTracks(bool trackUsed[MAX_TRACKS]);
    void expireMissingTracks(const bool trackUsed[MAX_TRACKS]);

    int createTrack(const Blob& blob);
    void checkTrackCrossing(Track& track);
    void pushEvent(int trackId, CrossingDirection direction);

    static float distanceBetween(float x1, float y1,
                                 float x2, float y2);
};
