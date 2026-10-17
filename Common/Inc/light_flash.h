#ifndef SPOOKY_LIGHT_FLASH_H
#define SPOOKY_LIGHT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/* Radio onset flash of the encoder and button lights (decision 0031 items 5
 * to 8; Beads full_spooky_proto-54w.8.1).
 *
 * Portable: no hardware access. One envelope serves every light, since an
 * onset flashes all lit controls together. The boost is a fraction of the
 * headroom between a light's current perceived lightness and full, in
 * permille: a small, medium or large onset raises it by one, two or three
 * thirds, and it falls continuously by one third every 80 ms. A new onset
 * smaller than the boost still in progress is ignored; an equal or larger one
 * restarts the flash at its own size.
 *
 * Which lights flash (decision 0031 items 1 to 4) is the renderer's choice; it
 * applies the boost only to the lights that may flash. */

#define LIGHT_FLASH_ONSET_SIZES 3U
#define LIGHT_FLASH_FULL 1000U

typedef struct
{
  /* Boost of a small, medium and large onset, permille of the headroom:
   * 333, 667, 1000. */
  uint16_t boost_permille[LIGHT_FLASH_ONSET_SIZES];
  /* Time for a full boost to fall to none: 240 ms, one third per 80 ms. */
  uint16_t full_decay_ms;
} LightFlashConfig;

typedef struct
{
  LightFlashConfig config;
  uint16_t start_boost;  /* boost when the flash started, permille */
  uint32_t start_ms;
  bool active;
} LightFlash;

void LightFlash_DefaultConfig(LightFlashConfig *config);
/* Rejects a zero decay time and boosts that are zero, decrease or exceed full. */
bool LightFlash_Init(LightFlash *flash, const LightFlashConfig *config);
/* size 1..3 = small, medium, large, as RadioOnset reports them; others are
 * ignored. */
void LightFlash_OnOnset(LightFlash *flash, uint8_t size, uint32_t now_ms);
/* The boost now, permille of the headroom; 0 once the flash has decayed. */
uint16_t LightFlash_Boost(LightFlash *flash, uint32_t now_ms);
/* A light's perceived lightness (permille) raised by a boost, keeping its
 * colour: the caller scales each channel of an RGB light by the same result. */
uint16_t LightFlash_Apply(uint16_t lightness_permille, uint16_t boost_permille);

#endif /* SPOOKY_LIGHT_FLASH_H */
