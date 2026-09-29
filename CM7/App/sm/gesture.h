#ifndef SPOOKY_GESTURE_H
#define SPOOKY_GESTURE_H

/*
 * A resolved gesture: what the InputResolution machine hands to the Context machine
 * (decision 0009). InputResolution decides how a press resolves: click, hold, Shift
 * action or chord. Context decides what that gesture means in the current mode,
 * engine, menu or utility. A gesture travels between the two machines as one internal
 * event on the M7 queue (decision 0007), packed into one 32-bit argument.
 *
 * Portable C with no HAL calls. Shared by both ports; neither port calls the other.
 */

#include <stdint.h>

typedef enum GestureKind {
    GESTURE_CLICK = 0,     /* encoder button released before the hold threshold */
    GESTURE_HOLD,          /* encoder button reached the hold threshold (Normal layer) */
    GESTURE_TURN,          /* encoder detents in the Normal layer */
    GESTURE_BUTTON1_DOWN,  /* Button 1 pressed in the Normal layer */
    GESTURE_BUTTON1_UP,    /* release of a Button 1 press that was delivered */
    GESTURE_SHIFT_ENCODER, /* encoder button pressed in the Shift layer */
    GESTURE_SHIFT_BUTTON0, /* Shift plus Button 0, resolved on release */
    GESTURE_SHIFT_BUTTON1, /* Shift plus Button 1, resolved on release */
    GESTURE_SHIFT_CHORD,   /* Shift plus Buttons 0 and 1, resolved on the second press */
    GESTURE_KIND_COUNT
} GestureKind;

typedef struct Gesture {
    uint8_t kind;    /* GestureKind */
    uint8_t encoder; /* 0-3 for encoder gestures, otherwise 0 */
    int8_t detents;  /* signed detent count for GESTURE_TURN, otherwise 0 */
    uint8_t reserved;
} Gesture;

_Static_assert(sizeof(Gesture) == 4u, "a gesture fits one queue argument");

static inline uint32_t Gesture_Pack(Gesture gesture)
{
    return (uint32_t)gesture.kind | ((uint32_t)gesture.encoder << 8) |
           ((uint32_t)(uint8_t)gesture.detents << 16);
}

static inline Gesture Gesture_Unpack(uint32_t packed)
{
    Gesture gesture;
    gesture.kind = (uint8_t)(packed & 0xFFu);
    gesture.encoder = (uint8_t)((packed >> 8) & 0xFFu);
    gesture.detents = (int8_t)(uint8_t)((packed >> 16) & 0xFFu);
    gesture.reserved = 0u;
    return gesture;
}

#endif /* SPOOKY_GESTURE_H */
