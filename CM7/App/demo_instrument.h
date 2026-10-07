#ifndef SPOOKY_DEMO_INSTRUMENT_H
#define SPOOKY_DEMO_INSTRUMENT_H

/*
 * Demo-only Instrument pages for the granular voice (decision 0011 item 16,
 * full_spooky_proto-p04.7). Provisional: these assignments do not settle C-025.
 *
 * - Page 1: position, grain size, density and pitch on Encoders 0-3.
 * - Page 2: spray, nothing, envelope and level on Encoders 0-3. Slice
 *   quantization was removed (decision 0020 item 6); its Encoder 1 slot is
 *   unassigned.
 * - An Encoder 3 click advances the page and wraps (C-024).
 * - Button 1 has no Instrument function (C-029 stays Proposed).
 *
 * The values persist across visits to Instrument and new clips. Portable C
 * with no HAL calls.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "granular.h"

#define DEMO_INSTRUMENT_PAGES 2U
#define DEMO_INSTRUMENT_ENCODERS 4U

typedef enum
{
  DEMO_PARAM_POSITION = 0,
  DEMO_PARAM_SIZE,
  DEMO_PARAM_DENSITY,
  DEMO_PARAM_PITCH,
  DEMO_PARAM_SPRAY,
  DEMO_PARAM_ENVELOPE,
  DEMO_PARAM_LEVEL,
  DEMO_PARAM_COUNT
} DemoParam;

typedef struct
{
  uint8_t page;             /* 0 or 1 */
  GranularParams params;
} DemoInstrument;

void DemoInstrument_Init(DemoInstrument *instrument);
/* Turns the parameter on the encoder of the current page by signed detents.
 * True if its value changed (false at a range end). */
bool DemoInstrument_Turn(DemoInstrument *instrument, uint8_t encoder, int32_t detents);
/* C-024: the next page, wrapping after the last. */
void DemoInstrument_NextPage(DemoInstrument *instrument);
/* The parameter an encoder adjusts on a page; DEMO_PARAM_COUNT for none. */
DemoParam DemoInstrument_Param(uint8_t page, uint8_t encoder);
/* Short upper-case name, at most 5 characters; "-" for an unassigned slot. */
const char *DemoInstrument_Name(DemoParam param);
/* The value with its unit, at most 7 characters: "45%", "120MS", "20/S",
 * "+7ST"; empty for an unassigned slot. */
void DemoInstrument_FormatValue(const GranularParams *params, DemoParam param, char *text,
                                size_t size);

#endif /* SPOOKY_DEMO_INSTRUMENT_H */
