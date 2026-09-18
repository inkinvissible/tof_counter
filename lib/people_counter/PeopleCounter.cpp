#include "PeopleCounter.h"

// sqrtf sin arrastrar <cmath> completo; <math.h> existe tanto
// en el toolchain ESP32 como en el compilador native.
#include <math.h>

PeopleCounter::PeopleCounter() {
    reset();
}

void PeopleCounter::reset() {
    blobCount_ = 0;
    nextTrackId_ = 1;
    peopleInside_ = 0;
    totalIn_ = 0;
    totalOut_ = 0;
    lastEventCount_ = 0;

    for (int y = 0; y < MATRIX_SIZE; y++) {
        for (int x = 0; x < MATRIX_SIZE; x++) {
            occupied_[y][x] = false;
            visited_[y][x] = false;
        }
    }

    for (int i = 0; i < MAX_BLOBS; i++) {
        blobs_[i].id = 0;
        blobs_[i].pixelCount = 0;
        blobs_[i].centroidX = 0.0f;
        blobs_[i].centroidY = 0.0f;
    }

    for (int i = 0; i < MAX_TRACKS; i++) {
        tracks_[i].id = 0;
        tracks_[i].active = false;
        tracks_[i].x = 0.0f;
        tracks_[i].y = 0.0f;
        tracks_[i].previousY = 0.0f;
        tracks_[i].missedFrames = 0;
        tracks_[i].alreadyCounted = false;
    }
}

void PeopleCounter::setInitialOccupancy(int value) {
    peopleInside_ = (value > 0) ? value : 0;
}

void PeopleCounter::processFrame(
    const uint16_t frame[MATRIX_SIZE][MATRIX_SIZE]
) {
    lastEventCount_ = 0;
    calculateForegroundMask(frame);
    findBlobs();
    updateTracks();
}

int PeopleCounter::getPeopleInside() const {
    return peopleInside_;
}

int PeopleCounter::getTotalIn() const {
    return totalIn_;
}

int PeopleCounter::getTotalOut() const {
    return totalOut_;
}

int PeopleCounter::getDetectedBlobCount() const {
    return blobCount_;
}

PeopleCounter::BlobInfo PeopleCounter::getBlob(int index) const {
    BlobInfo info = {0, 0.0f, 0.0f};
    if (index < 0 || index >= blobCount_) {
        return info;
    }
    info.pixelCount = blobs_[index].pixelCount;
    info.centroidX = blobs_[index].centroidX;
    info.centroidY = blobs_[index].centroidY;
    return info;
}

int PeopleCounter::getActiveTrackCount() const {
    int count = 0;
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (tracks_[i].active) {
            count++;
        }
    }
    return count;
}

int PeopleCounter::getTrackSlotCount() const {
    return MAX_TRACKS;
}

PeopleCounter::TrackInfo PeopleCounter::getTrackSlot(int index) const {
    TrackInfo info = {0, false, 0.0f, 0.0f, 0.0f, 0, false};
    if (index < 0 || index >= MAX_TRACKS) {
        return info;
    }
    info.id = tracks_[index].id;
    info.active = tracks_[index].active;
    info.x = tracks_[index].x;
    info.y = tracks_[index].y;
    info.previousY = tracks_[index].previousY;
    info.missedFrames = tracks_[index].missedFrames;
    info.alreadyCounted = tracks_[index].alreadyCounted;
    return info;
}

int PeopleCounter::getLastEventCount() const {
    return lastEventCount_;
}

PeopleCounter::CrossingEvent PeopleCounter::getLastEvent(int index) const {
    CrossingEvent empty = {0, CrossingDirection::None};
    if (index < 0 || index >= lastEventCount_) {
        return empty;
    }
    return lastEvents_[index];
}

// ============================================================
// PROFUNDIDAD → FOREGROUND
// ============================================================

void PeopleCounter::calculateForegroundMask(
    const uint16_t frame[MATRIX_SIZE][MATRIX_SIZE]
) {
    for (int y = 0; y < MATRIX_SIZE; y++) {
        for (int x = 0; x < MATRIX_SIZE; x++) {
            int height =
                static_cast<int>(FLOOR_DISTANCE_MM) -
                static_cast<int>(frame[y][x]);

            occupied_[y][x] =
                height >
                static_cast<int>(HEIGHT_THRESHOLD_MM);
        }
    }
}

// ============================================================
// CONNECTED COMPONENTS (BFS, conectividad 4)
// ============================================================

void PeopleCounter::resetVisited() {
    for (int y = 0; y < MATRIX_SIZE; y++) {
        for (int x = 0; x < MATRIX_SIZE; x++) {
            visited_[y][x] = false;
        }
    }
}

PeopleCounter::Blob PeopleCounter::exploreComponent(
    int startX,
    int startY,
    int blobId
) {
    static const int QUEUE_CAPACITY = MATRIX_SIZE * MATRIX_SIZE;

    int queueX[QUEUE_CAPACITY];
    int queueY[QUEUE_CAPACITY];

    int queueStart = 0;
    int queueEnd = 0;

    queueX[queueEnd] = startX;
    queueY[queueEnd] = startY;
    queueEnd++;

    visited_[startY][startX] = true;

    int pixelCount = 0;

    float sumX = 0.0f;
    float sumY = 0.0f;

    // Direcciones: derecha, izquierda, abajo, arriba.
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    while (queueStart < queueEnd) {
        int x = queueX[queueStart];
        int y = queueY[queueStart];
        queueStart++;

        pixelCount++;
        sumX += static_cast<float>(x);
        sumY += static_cast<float>(y);

        for (int direction = 0; direction < 4; direction++) {
            int nx = x + dx[direction];
            int ny = y + dy[direction];

            // Fuera de matriz
            if (nx < 0 || nx >= MATRIX_SIZE ||
                ny < 0 || ny >= MATRIX_SIZE) {
                continue;
            }

            // Ya visitado
            if (visited_[ny][nx]) {
                continue;
            }

            // No pertenece al foreground
            if (!occupied_[ny][nx]) {
                continue;
            }

            visited_[ny][nx] = true;

            queueX[queueEnd] = nx;
            queueY[queueEnd] = ny;
            queueEnd++;
        }
    }

    Blob result;
    result.id = blobId;
    result.pixelCount = pixelCount;
    result.centroidX = sumX / static_cast<float>(pixelCount);
    result.centroidY = sumY / static_cast<float>(pixelCount);
    return result;
}

void PeopleCounter::findBlobs() {
    resetVisited();

    blobCount_ = 0;

    for (int y = 0; y < MATRIX_SIZE; y++) {
        for (int x = 0; x < MATRIX_SIZE; x++) {
            if (!occupied_[y][x] || visited_[y][x]) {
                continue;
            }

            Blob candidate = exploreComponent(x, y, blobCount_ + 1);

            // Eliminamos ruido muy pequeño
            if (candidate.pixelCount < MIN_BLOB_PIXELS) {
                continue;
            }

            if (blobCount_ < MAX_BLOBS) {
                blobs_[blobCount_] = candidate;
                blobCount_++;
            }
        }
    }
}

// ============================================================
// TRACKING (nearest-neighbor greedy)
// ============================================================

float PeopleCounter::distanceBetween(float x1, float y1,
                                     float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    return sqrtf(dx * dx + dy * dy);
}

int PeopleCounter::createTrack(const Blob& blob) {
    for (int i = 0; i < MAX_TRACKS; i++) {
        if (!tracks_[i].active) {
            tracks_[i].active = true;
            tracks_[i].id = nextTrackId_++;

            tracks_[i].x = blob.centroidX;
            tracks_[i].y = blob.centroidY;

            tracks_[i].previousY = blob.centroidY;

            tracks_[i].missedFrames = 0;
            tracks_[i].alreadyCounted = false;

            return i;
        }
    }

    return -1;
}

void PeopleCounter::pushEvent(int trackId,
                              CrossingDirection direction) {
    if (lastEventCount_ >= MAX_EVENTS_PER_FRAME) {
        return;
    }
    lastEvents_[lastEventCount_].trackId = trackId;
    lastEvents_[lastEventCount_].direction = direction;
    lastEventCount_++;
}

void PeopleCounter::checkTrackCrossing(Track& track) {
    if (track.alreadyCounted) {
        return;
    }

    // ENTRADA: venía de arriba de la línea y ahora está
    // sobre ella o debajo.
    if (track.previousY < CROSSING_LINE_Y &&
        track.y >= CROSSING_LINE_Y) {
        totalIn_++;
        peopleInside_++;

        track.alreadyCounted = true;
        pushEvent(track.id, CrossingDirection::In);
    }
    // SALIDA: venía de debajo de la línea y ahora está
    // sobre ella o encima.
    else if (track.previousY > CROSSING_LINE_Y &&
             track.y <= CROSSING_LINE_Y) {
        totalOut_++;

        if (peopleInside_ > 0) {
            peopleInside_--;
        }

        track.alreadyCounted = true;
        pushEvent(track.id, CrossingDirection::Out);
    }
}

void PeopleCounter::associateBlobsToTracks(bool trackUsed[MAX_TRACKS]) {
    for (int b = 0; b < blobCount_; b++) {
        int bestTrack = -1;
        float bestDistance = 9999.0f;

        for (int t = 0; t < MAX_TRACKS; t++) {
            if (!tracks_[t].active || trackUsed[t]) {
                continue;
            }

            float distance = distanceBetween(
                blobs_[b].centroidX,
                blobs_[b].centroidY,
                tracks_[t].x,
                tracks_[t].y
            );

            if (distance < bestDistance &&
                distance <= MAX_TRACK_DISTANCE) {
                bestDistance = distance;
                bestTrack = t;
            }
        }

        if (bestTrack >= 0) {
            Track& track = tracks_[bestTrack];

            track.previousY = track.y;

            track.x = blobs_[b].centroidX;
            track.y = blobs_[b].centroidY;

            track.missedFrames = 0;

            trackUsed[bestTrack] = true;

            checkTrackCrossing(track);
        } else {
            int newTrack = createTrack(blobs_[b]);

            if (newTrack >= 0) {
                trackUsed[newTrack] = true;
            }
        }
    }
}

void PeopleCounter::expireMissingTracks(const bool trackUsed[MAX_TRACKS]) {
    for (int t = 0; t < MAX_TRACKS; t++) {
        if (!tracks_[t].active) {
            continue;
        }

        if (!trackUsed[t]) {
            tracks_[t].missedFrames++;

            if (tracks_[t].missedFrames > MAX_MISSED_FRAMES) {
                tracks_[t].active = false;
            }
        }
    }
}

void PeopleCounter::updateTracks() {
    bool trackUsed[MAX_TRACKS];
    for (int i = 0; i < MAX_TRACKS; i++) {
        trackUsed[i] = false;
    }

    associateBlobsToTracks(trackUsed);
    expireMissingTracks(trackUsed);
}
