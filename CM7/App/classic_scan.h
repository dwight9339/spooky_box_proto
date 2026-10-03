#ifndef SPOOKY_CLASSIC_SCAN_H
#define SPOOKY_CLASSIC_SCAN_H

#include <stdbool.h>
#include <stdint.h>

/* Classic scan engine core (spec 001, decision 0016 items 1 to 15 and 22).
 * Portable: no HAL, no radio calls. Band and frequency are shared tuning
 * state owned by the radio, so every call that needs them takes them as
 * arguments and the engine never stores a frequency (FR-022). The engine keeps
 * only its own parameters and run state, which survive navigation, Manual and
 * engine switches while the device stays powered. The caller issues the tunes
 * the engine asks for through the shared command policy, and publishes state
 * and change events (full_spooky_proto-54w.33). The activity hold is
 * full_spooky_proto-54w.32. */

/* Bands are indexed like RadioBand: FM, AM, SW, LW. */
#define CLASSIC_SCAN_BAND_COUNT 4U
#define CLASSIC_SCAN_RATE_STEPS_MAX 16U
#define CLASSIC_SCAN_DISTANCE_STEPS_MAX 24U

typedef enum
{
  CLASSIC_EDGE_WRAP = 0,
  CLASSIC_EDGE_BOUNCE,
  CLASSIC_EDGE_STOP,
  CLASSIC_EDGE_COUNT
} ClassicEdge;

typedef enum
{
  CLASSIC_RUN_RUNNING = 0,
  CLASSIC_RUN_PAUSED,
  /* Paused on an edge with the direction pointing out of the band: resuming
   * starts a new sweep from the opposite edge (decision 0016 items 14, 15).
   * Derived from position and direction, never stored. */
  CLASSIC_RUN_SWEEP_COMPLETE
} ClassicRunState;

/* A band's channels: minimum_khz + i * step_khz, up to maximum_khz. */
typedef struct
{
  uint32_t minimum_khz;
  uint32_t maximum_khz;
  uint32_t step_khz;
} ClassicTerritory;

/* Every decision 0016 starting value, configurable until bench trials set
 * them (item 23). */
typedef struct
{
  /* Jumps per minute for Encoder 0 detents, strictly increasing (item 4). */
  uint16_t rate_per_min[CLASSIC_SCAN_RATE_STEPS_MAX];
  uint8_t rate_count;
  uint8_t default_rate_index;
  /* Rate in effect is capped per band; the setting is kept (items 5, 6). */
  uint16_t band_max_rate_per_min[CLASSIC_SCAN_BAND_COUNT];
  /* Channels for Encoder 1 detents, strictly increasing; cut at each band's
   * cap floor(N / 2), which is then the last value (item 9). */
  uint16_t distance_channels[CLASSIC_SCAN_DISTANCE_STEPS_MAX];
  uint8_t distance_count;
  /* Each band's starting distance, a value of that band's sequence (item 10). */
  uint16_t default_distance[CLASSIC_SCAN_BAND_COUNT];
  /* Startup (item 22). */
  bool start_running;
  bool start_up;
  ClassicEdge start_edge;
} ClassicScanConfig;

/* Inputs read from shared tuning and the radio on every service pass. */
typedef struct
{
  uint8_t band;             /* current band */
  uint32_t frequency_khz;   /* last completed tune */
  bool tuning_valid;        /* the radio has completed a tune in this band */
  /* A tune is in flight, including one this engine asked for and the caller
   * has issued but the radio has not finished. */
  bool tune_in_flight;
  /* Classic may move: it is the active Field engine and Manual is not
   * hand-tuning. Menus and the utility root do not clear it (FR-021). */
  bool active;
  /* The receiver can retune and the command policy allows tuning now
   * (item 21). Reporting why not is the caller's. */
  bool can_tune;
} ClassicScanInput;

typedef struct
{
  /* False when the landing is the current channel (stop at an edge it
   * already reached), so no tune is needed. */
  bool tune;
  uint32_t frequency_khz;
  uint16_t channel_index;
  bool direction_changed;   /* bounce reversed the direction (item 13) */
  bool sweep_complete;      /* stop reached the edge and paused (item 14) */
} ClassicScanJump;

typedef struct
{
  ClassicRunState run_state;
  bool direction_up;
  ClassicEdge edge;
  uint16_t rate_setting_per_min;
  uint16_t rate_per_min;    /* in effect in this band */
  bool rate_limited;
  uint16_t distance_channels;
  uint32_t distance_khz;
  uint16_t channel_index;   /* nearest channel to the frequency */
  uint16_t channel_count;
} ClassicScanStatus;

void ClassicScan_DefaultConfig(ClassicScanConfig *config);
/* Rejects a NULL argument, empty or non-increasing tables, an out-of-range
 * default rate or edge, a zero band maximum rate, a territory that is not a
 * whole number of steps or has fewer than two channels, and a default
 * distance that is not in its band's sequence. On success the engine is in
 * its startup state; the first jump is due one period after the first
 * service pass that can move (item 8). */
bool ClassicScan_Init(const ClassicScanConfig *config,
                      const ClassicTerritory territories[CLASSIC_SCAN_BAND_COUNT]);

/* C-103. Pausing takes effect before the next jump. Resuming makes the next
 * jump at once, from the opposite edge if Classic is on an edge pointing out
 * of the band (items 8, 15). */
void ClassicScan_ToggleRun(void);
/* C-105. Applies to the next jump. */
void ClassicScan_ToggleDirection(void);
/* C-104, C-106 and C-110: signed detents, stopping at both ends. The rate
 * applies from the next due time; distance and edge behavior to the next
 * jump. Return true when the parameter changed. Distance belongs to `band`. */
bool ClassicScan_StepRate(int32_t detents);
bool ClassicScan_StepDistance(uint8_t band, int32_t detents);
bool ClassicScan_StepEdge(int32_t detents);

/* One pass of the foreground loop. Returns true with *jump filled when a jump
 * is due now; the caller then issues jump->frequency_khz if jump->tune and
 * reports tune_in_flight from then until the tune ends. While the engine
 * cannot move (inactive, unable to tune, no valid tuning) nothing is issued
 * and nothing is retried; once it can move again the next jump is due one
 * period later (item 21). */
bool ClassicScan_Service(uint32_t now_ms, const ClassicScanInput *input,
                         ClassicScanJump *jump);

bool ClassicScan_GetStatus(uint8_t band, uint32_t frequency_khz,
                           ClassicScanStatus *status);

/* Pure channel arithmetic over a territory (items 1, 2). */
uint16_t ClassicScan_ChannelCount(const ClassicTerritory *territory);
uint16_t ClassicScan_NearestChannel(const ClassicTerritory *territory,
                                    uint32_t frequency_khz);
uint32_t ClassicScan_ChannelKhz(const ClassicTerritory *territory,
                                uint16_t channel_index);

#endif /* SPOOKY_CLASSIC_SCAN_H */
