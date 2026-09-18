// ============================================================
// Tests de PeopleCounter (entorno native, Unity).
//
// Todos los frames son deterministas: NO se usa random().
// La profundidad de fondo es FLOOR_DISTANCE_MM y una persona
// es un blob 2x2 de 900 mm (igual que la simulación de
// main.cpp pero sin ruido aleatorio).
// ============================================================

#include <unity.h>
#include "PeopleCounter.h"

#include <stdint.h>

static const int N = PeopleCounter::MATRIX_SIZE;
static const uint16_t FLOOR = PeopleCounter::FLOOR_DISTANCE_MM;
static const uint16_t PERSON_DEPTH = 900;

// ---------------- Helpers de frames ----------------

static void makeEmptyFrame(uint16_t frame[N][N]) {
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            frame[y][x] = FLOOR;
        }
    }
}

// Blob 2x2 determinista, recortado en bordes (igual que main.cpp).
static void addPersonBlob(uint16_t frame[N][N], int startX, int startY) {
    for (int dy = 0; dy < 2; dy++) {
        int y = startY + dy;
        if (y < 0 || y >= N) {
            continue;
        }
        for (int dx = 0; dx < 2; dx++) {
            int x = startX + dx;
            if (x < 0 || x >= N) {
                continue;
            }
            frame[y][x] = PERSON_DEPTH;
        }
    }
}

static void setForegroundPixel(uint16_t frame[N][N], int x, int y) {
    if (x >= 0 && x < N && y >= 0 && y < N) {
        frame[y][x] = PERSON_DEPTH;
    }
}

static void feedFrame(PeopleCounter& counter, uint16_t frame[N][N]) {
    counter.processFrame(frame);
}

static void feedEmpty(PeopleCounter& counter, int count) {
    uint16_t frame[N][N];
    for (int i = 0; i < count; i++) {
        makeEmptyFrame(frame);
        feedFrame(counter, frame);
    }
}

// Persona que entra: barre de arriba (fuera) hacia abajo.
// y=-2 → invisible; y=-1 → parcialmente visible;
// cruza CROSSING_LINE_Y (3.5) al llegar a y=3
// (centroide 2.5 → 3.5); termina en y=5 (adentro).
static void runEntry(PeopleCounter& counter, int x = 3) {
    uint16_t frame[N][N];
    for (int y = -2; y <= 5; y++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, x, y);
        feedFrame(counter, frame);
    }
}

// Persona que sale: barre de abajo (adentro) hacia arriba.
// Cruza la línea al llegar a y=3 (centroide 4.5 → 3.5);
// termina en y=-2 (fuera).
static void runExit(PeopleCounter& counter, int x = 3) {
    uint16_t frame[N][N];
    for (int y = 6; y >= -2; y--) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, x, y);
        feedFrame(counter, frame);
    }
}

void setUp(void) {}
void tearDown(void) {}

// ============================================================
// TESTS OBLIGATORIOS
// ============================================================

void test_single_person_enters(void) {
    PeopleCounter counter;
    runEntry(counter);

    TEST_ASSERT_EQUAL_INT(1, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(1, counter.getPeopleInside());
}

void test_single_person_exits(void) {
    PeopleCounter counter;
    counter.setInitialOccupancy(1);
    runExit(counter);

    TEST_ASSERT_EQUAL_INT(0, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(1, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
}

void test_two_people_enter(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Dos blobs separados (x=1 usa cols 1-2, x=5 usa cols 5-6).
    for (int y = -2; y <= 5; y++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 1, y);
        addPersonBlob(frame, 5, y);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(2, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(2, counter.getPeopleInside());
}

void test_simultaneous_entry_and_exit(void) {
    PeopleCounter counter;
    counter.setInitialOccupancy(1);
    uint16_t frame[N][N];

    // A entra por x=1 (baja), B sale por x=5 (sube).
    for (int i = 0; i <= 8; i++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 1, -2 + i);
        addPersonBlob(frame, 5, 6 - i);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(1, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(1, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(1, counter.getPeopleInside());
}

void test_person_approaches_but_does_not_cross(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Se acerca hasta y=2 (centroide 2.5 < 3.5) y vuelve.
    const int path[] = {-2, -1, 0, 1, 2, 1, 0, -1, -2};
    const int steps = sizeof(path) / sizeof(path[0]);
    for (int i = 0; i < steps; i++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 3, path[i]);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(0, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
}

void test_small_noise_blob_is_ignored(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Píxeles individuales aislados (< MIN_BLOB_PIXELS).
    makeEmptyFrame(frame);
    setForegroundPixel(frame, 0, 0);
    setForegroundPixel(frame, 3, 3);
    setForegroundPixel(frame, 6, 6);
    feedFrame(counter, frame);

    makeEmptyFrame(frame);
    setForegroundPixel(frame, 1, 5);
    setForegroundPixel(frame, 5, 1);
    feedFrame(counter, frame);

    TEST_ASSERT_EQUAL_INT(0, counter.getDetectedBlobCount());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
}

void test_person_is_not_counted_twice(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    runEntry(counter);

    // La persona permanece varios frames del otro lado
    // (incluso moviéndose, siempre del lado de adentro).
    const int path[] = {5, 6, 5, 4, 5, 6};
    const int steps = sizeof(path) / sizeof(path[0]);
    for (int i = 0; i < steps; i++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 3, path[i]);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(1, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(1, counter.getPeopleInside());
}

void test_temporary_track_loss(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Avanza hacia la línea, desaparece 1 frame
    // (< MAX_MISSED_FRAMES) y reaparece cerca: el track
    // debe conservarse y contar una sola entrada.
    const int path[] = {0, 1, 2};
    for (int i = 0; i < 3; i++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 3, path[i]);
        feedFrame(counter, frame);
    }

    makeEmptyFrame(frame);
    feedFrame(counter, frame);  // 1 frame perdida

    const int resume[] = {3, 4, 5};
    for (int i = 0; i < 3; i++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 3, resume[i]);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(1, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(1, counter.getPeopleInside());
    TEST_ASSERT_EQUAL_INT(1, counter.getActiveTrackCount());
}

void test_occupancy_never_goes_negative(void) {
    PeopleCounter counter;  // peopleInside arranca en 0
    runExit(counter);

    TEST_ASSERT_TRUE(counter.getPeopleInside() >= 0);
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
}

void test_repeated_entries_and_exits_regression(void) {
    PeopleCounter counter;

    for (int i = 0; i < 100; i++) {
        runEntry(counter);
        feedEmpty(counter, PeopleCounter::MAX_MISSED_FRAMES + 1);
    }

    TEST_ASSERT_EQUAL_INT(100, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(100, counter.getPeopleInside());

    for (int i = 0; i < 100; i++) {
        runExit(counter);
        feedEmpty(counter, PeopleCounter::MAX_MISSED_FRAMES + 1);
    }

    TEST_ASSERT_EQUAL_INT(100, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(100, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
}

// ============================================================
// TESTS EXTRA
// ============================================================

void test_stationary_person_does_not_count(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Quieta del lado de adentro (centroide 5.5).
    for (int i = 0; i < 10; i++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 3, 5);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(0, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
    TEST_ASSERT_EQUAL_INT(1, counter.getActiveTrackCount());
}

void test_track_removed_after_max_missed_frames(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    makeEmptyFrame(frame);
    addPersonBlob(frame, 3, 5);
    feedFrame(counter, frame);
    TEST_ASSERT_EQUAL_INT(1, counter.getActiveTrackCount());

    // Con MAX_MISSED_FRAMES frames perdidos sigue vivo.
    feedEmpty(counter, PeopleCounter::MAX_MISSED_FRAMES);
    TEST_ASSERT_EQUAL_INT(1, counter.getActiveTrackCount());

    // Un frame más lo elimina (missed > MAX_MISSED_FRAMES).
    feedEmpty(counter, 1);
    TEST_ASSERT_EQUAL_INT(0, counter.getActiveTrackCount());
}

void test_two_connected_components_detected(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    makeEmptyFrame(frame);
    addPersonBlob(frame, 0, 0);
    addPersonBlob(frame, 5, 5);
    feedFrame(counter, frame);

    TEST_ASSERT_EQUAL_INT(2, counter.getDetectedBlobCount());
}

void test_centroid_is_correct(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Blob 2x2 en (2,3): píxeles (2,3),(3,3),(2,4),(3,4).
    makeEmptyFrame(frame);
    addPersonBlob(frame, 2, 3);
    feedFrame(counter, frame);

    TEST_ASSERT_EQUAL_INT(1, counter.getDetectedBlobCount());
    PeopleCounter::BlobInfo blob = counter.getBlob(0);
    TEST_ASSERT_EQUAL_INT(4, blob.pixelCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, blob.centroidX);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.5f, blob.centroidY);
}

void test_blob_touching_edge(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Esquina (0,0): blob completo tocando el borde.
    makeEmptyFrame(frame);
    addPersonBlob(frame, 0, 0);
    feedFrame(counter, frame);
    TEST_ASSERT_EQUAL_INT(1, counter.getDetectedBlobCount());

    // Parcialmente fuera (startX=-1): solo la columna 0,
    // 2 píxeles en vertical → igual se detecta.
    PeopleCounter counter2;
    makeEmptyFrame(frame);
    addPersonBlob(frame, -1, 2);
    feedFrame(counter2, frame);
    TEST_ASSERT_EQUAL_INT(1, counter2.getDetectedBlobCount());
    TEST_ASSERT_EQUAL_INT(2, counter2.getBlob(0).pixelCount);
}

void test_partially_visible_person_still_counts_once(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Entra pegada al borde izquierdo (una sola columna
    // visible) y cruza la línea: debe contar 1 entrada.
    for (int y = -2; y <= 5; y++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, -1, y);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(1, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(1, counter.getPeopleInside());
}

void test_two_people_moving_in_parallel(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // Dos personas bajan en paralelo y cruzan juntas.
    for (int y = -2; y <= 6; y++) {
        makeEmptyFrame(frame);
        addPersonBlob(frame, 0, y);
        addPersonBlob(frame, 5, y);
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(2, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(2, counter.getPeopleInside());
}

void test_deterministic_noise_causes_no_drift(void) {
    PeopleCounter counter;
    uint16_t frame[N][N];

    // "Ruido" determinista: píxeles aislados separados por
    // ≥2 celdas (sin adyacencia-4), variando por frame.
    const int spots[5][6][2] = {
        {{0, 0}, {0, 4}, {4, 0}, {4, 4}, {7, 7}, {2, 6}},
        {{1, 1}, {1, 5}, {5, 1}, {5, 5}, {7, 0}, {3, 7}},
        {{0, 2}, {2, 0}, {6, 2}, {2, 6}, {6, 6}, {4, 7}},
        {{7, 3}, {3, 7}, {0, 7}, {7, 0}, {4, 2}, {2, 4}},
        {{6, 0}, {0, 6}, {3, 3}, {7, 5}, {5, 7}, {1, 3}},
    };
    for (int f = 0; f < 5; f++) {
        makeEmptyFrame(frame);
        for (int i = 0; i < 6; i++) {
            setForegroundPixel(frame, spots[f][i][0], spots[f][i][1]);
        }
        feedFrame(counter, frame);
    }

    TEST_ASSERT_EQUAL_INT(0, counter.getDetectedBlobCount());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalIn());
    TEST_ASSERT_EQUAL_INT(0, counter.getTotalOut());
    TEST_ASSERT_EQUAL_INT(0, counter.getPeopleInside());
}

// ---------------- Runner ----------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_single_person_enters);
    RUN_TEST(test_single_person_exits);
    RUN_TEST(test_two_people_enter);
    RUN_TEST(test_simultaneous_entry_and_exit);
    RUN_TEST(test_person_approaches_but_does_not_cross);
    RUN_TEST(test_small_noise_blob_is_ignored);
    RUN_TEST(test_person_is_not_counted_twice);
    RUN_TEST(test_temporary_track_loss);
    RUN_TEST(test_occupancy_never_goes_negative);
    RUN_TEST(test_repeated_entries_and_exits_regression);

    RUN_TEST(test_stationary_person_does_not_count);
    RUN_TEST(test_track_removed_after_max_missed_frames);
    RUN_TEST(test_two_connected_components_detected);
    RUN_TEST(test_centroid_is_correct);
    RUN_TEST(test_blob_touching_edge);
    RUN_TEST(test_partially_visible_person_still_counts_once);
    RUN_TEST(test_two_people_moving_in_parallel);
    RUN_TEST(test_deterministic_noise_causes_no_drift);

    return UNITY_END();
}
