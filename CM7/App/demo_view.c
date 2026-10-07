#include "demo_view.h"

#include <stdio.h>
#include <string.h>

#include "demo_instrument.h"
#include "demo_sequencer.h"

#define GLYPH_WIDTH 5U
#define CELL_WIDTH 6U
#define BIG_CELL_WIDTH 12U
#define LINE_CHARS (DEMO_VIEW_WIDTH / CELL_WIDTH)

/* 5x8 ASCII font, 0x20 to 0x7E, one byte per column, bit 0 at the top. */
static const uint8_t font[][GLYPH_WIDTH] =
{
  {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00},
  {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
  {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
  {0x36, 0x49, 0x56, 0x20, 0x50}, {0x00, 0x08, 0x07, 0x03, 0x00},
  {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00},
  {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, {0x08, 0x08, 0x3E, 0x08, 0x08},
  {0x00, 0x80, 0x70, 0x30, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08},
  {0x00, 0x00, 0x60, 0x60, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
  {0x72, 0x49, 0x49, 0x49, 0x46}, {0x21, 0x41, 0x49, 0x4D, 0x33},
  {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x41, 0x21, 0x11, 0x09, 0x07},
  {0x36, 0x49, 0x49, 0x49, 0x36}, {0x46, 0x49, 0x49, 0x29, 0x1E},
  {0x00, 0x00, 0x14, 0x00, 0x00}, {0x00, 0x40, 0x34, 0x00, 0x00},
  {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14},
  {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x59, 0x09, 0x06},
  {0x3E, 0x41, 0x5D, 0x59, 0x4E}, {0x7C, 0x12, 0x11, 0x12, 0x7C},
  {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
  {0x7F, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x49, 0x49, 0x49, 0x41},
  {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x41, 0x51, 0x73},
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
  {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
  {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x1C, 0x02, 0x7F},
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
  {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
  {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x26, 0x49, 0x49, 0x49, 0x32},
  {0x03, 0x01, 0x7F, 0x01, 0x03}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
  {0x63, 0x14, 0x08, 0x14, 0x63}, {0x03, 0x04, 0x78, 0x04, 0x03},
  {0x61, 0x59, 0x49, 0x4D, 0x43}, {0x00, 0x7F, 0x41, 0x41, 0x41},
  {0x02, 0x04, 0x08, 0x10, 0x20}, {0x00, 0x41, 0x41, 0x41, 0x7F},
  {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
  {0x00, 0x03, 0x07, 0x08, 0x00}, {0x20, 0x54, 0x54, 0x78, 0x40},
  {0x7F, 0x28, 0x44, 0x44, 0x38}, {0x38, 0x44, 0x44, 0x44, 0x28},
  {0x38, 0x44, 0x44, 0x28, 0x7F}, {0x38, 0x54, 0x54, 0x54, 0x18},
  {0x00, 0x08, 0x7E, 0x09, 0x02}, {0x18, 0xA4, 0xA4, 0x9C, 0x78},
  {0x7F, 0x08, 0x04, 0x04, 0x78}, {0x00, 0x44, 0x7D, 0x40, 0x00},
  {0x20, 0x40, 0x40, 0x3D, 0x00}, {0x7F, 0x10, 0x28, 0x44, 0x00},
  {0x00, 0x41, 0x7F, 0x40, 0x00}, {0x7C, 0x04, 0x78, 0x04, 0x78},
  {0x7C, 0x08, 0x04, 0x04, 0x78}, {0x38, 0x44, 0x44, 0x44, 0x38},
  {0xFC, 0x18, 0x24, 0x24, 0x18}, {0x18, 0x24, 0x24, 0x18, 0xFC},
  {0x7C, 0x08, 0x04, 0x04, 0x08}, {0x48, 0x54, 0x54, 0x54, 0x24},
  {0x04, 0x04, 0x3F, 0x44, 0x24}, {0x3C, 0x40, 0x40, 0x20, 0x7C},
  {0x1C, 0x20, 0x40, 0x20, 0x1C}, {0x3C, 0x40, 0x30, 0x40, 0x3C},
  {0x44, 0x28, 0x10, 0x28, 0x44}, {0x4C, 0x90, 0x90, 0x90, 0x7C},
  {0x44, 0x64, 0x54, 0x4C, 0x44}, {0x00, 0x08, 0x36, 0x41, 0x00},
  {0x00, 0x00, 0x77, 0x00, 0x00}, {0x00, 0x41, 0x36, 0x08, 0x00},
  {0x02, 0x01, 0x02, 0x04, 0x02}
};

_Static_assert(sizeof(font) / sizeof(font[0]) == (0x7EU - 0x20U + 1U),
               "the font covers printable ASCII");

static const char *const band_names[] = {"FM", "AM", "SW", "LW"};
static const char *const engine_names[] = {"CLASSIC", "MANUAL"};
static const char *const edge_names[] = {"WRAP", "BOUNCE", "STOP"};

static uint8_t frame[DEMO_VIEW_FRAME_BYTES];
static uint8_t shown[DEMO_VIEW_FRAME_BYTES];
static uint8_t dirty_mask;
static DemoViewStatus counters;

static const char *BandName(uint8_t band)
{
  return (band < (sizeof(band_names) / sizeof(band_names[0]))) ? band_names[band] : "--";
}

static const uint8_t *Glyph(char character)
{
  const unsigned char code = (unsigned char)character;

  if ((code < 0x20U) || (code > 0x7EU))
  {
    return font['?' - 0x20];
  }
  return font[code - 0x20U];
}

/* Text on one page row; x past the right edge is clipped. */
static void Text(uint8_t page, uint32_t x, const char *text)
{
  uint8_t *row = &frame[(uint32_t)page * DEMO_VIEW_WIDTH];

  for (; (*text != '\0') && (x < DEMO_VIEW_WIDTH); ++text)
  {
    const uint8_t *glyph = Glyph(*text);
    uint32_t column;

    for (column = 0U; (column < GLYPH_WIDTH) && ((x + column) < DEMO_VIEW_WIDTH); ++column)
    {
      row[x + column] |= glyph[column];
    }
    x += CELL_WIDTH;
  }
}

static void TextRight(uint8_t page, const char *text)
{
  const uint32_t width = (uint32_t)strlen(text) * CELL_WIDTH;

  Text(page, (width < DEMO_VIEW_WIDTH) ? (DEMO_VIEW_WIDTH - width + 1U) : 0U, text);
}

static void TextCentred(uint8_t page, const char *text)
{
  const uint32_t width = (uint32_t)strlen(text) * CELL_WIDTH;

  Text(page, (width < DEMO_VIEW_WIDTH) ? ((DEMO_VIEW_WIDTH - width) / 2U) : 0U, text);
}

/* Spreads the low four bits of a column byte over eight, for double height. */
static uint8_t Stretch(uint8_t nibble)
{
  uint8_t out = 0U;
  uint32_t bit;

  for (bit = 0U; bit < 4U; ++bit)
  {
    if ((nibble & (1U << bit)) != 0U)
    {
      out |= (uint8_t)(3U << (bit * 2U));
    }
  }
  return out;
}

/* Double-size text over pages page and page + 1. */
static void BigText(uint8_t page, uint32_t x, const char *text)
{
  uint8_t *top = &frame[(uint32_t)page * DEMO_VIEW_WIDTH];
  uint8_t *bottom = top + DEMO_VIEW_WIDTH;

  for (; (*text != '\0') && (x < DEMO_VIEW_WIDTH); ++text)
  {
    const uint8_t *glyph = Glyph(*text);
    uint32_t column;

    for (column = 0U; column < (GLYPH_WIDTH * 2U); ++column)
    {
      const uint8_t source = glyph[column / 2U];

      if ((x + column) >= DEMO_VIEW_WIDTH)
      {
        break;
      }
      top[x + column] |= Stretch((uint8_t)(source & 0x0FU));
      bottom[x + column] |= Stretch((uint8_t)(source >> 4U));
    }
    x += BIG_CELL_WIDTH;
  }
}

static void BigTextCentred(uint8_t page, const char *text)
{
  const uint32_t width = (uint32_t)strlen(text) * BIG_CELL_WIDTH;

  BigText(page, (width < DEMO_VIEW_WIDTH) ? ((DEMO_VIEW_WIDTH - width) / 2U) : 0U, text);
}

static void Invert(uint8_t page)
{
  uint8_t *row = &frame[(uint32_t)page * DEMO_VIEW_WIDTH];
  uint32_t x;

  for (x = 0U; x < DEMO_VIEW_WIDTH; ++x)
  {
    row[x] = (uint8_t)~row[x];
  }
}

/* A rule along the bottom pixel row of a page. */
static void Rule(uint8_t page)
{
  uint8_t *row = &frame[(uint32_t)page * DEMO_VIEW_WIDTH];
  uint32_t x;

  for (x = 0U; x < DEMO_VIEW_WIDTH; ++x)
  {
    row[x] |= 0x80U;
  }
}

static void SessionLabel(const DemoViewModel *model, char *text, size_t size)
{
  switch (model->session)
  {
    case DEMO_SESSION_RECORDING:
      (void)snprintf(text, size, "REC %02lu:%02lu",
                     (unsigned long)((model->session_seconds / 60U) % 100U),
                     (unsigned long)(model->session_seconds % 60U));
      break;
    case DEMO_SESSION_FINALIZING:
      (void)snprintf(text, size, "SAVING");
      break;
    case DEMO_SESSION_PREPARING:
      (void)snprintf(text, size, "PREPARING");
      break;
    default:
      /* No session: the rolling window, which a save would keep. */
      if (model->buffer == DEMO_BUFFER_RUNNING)
      {
        (void)snprintf(text, size, "BUF %lus",
                       (unsigned long)((model->buffer_seconds > 99U) ? 99U
                                                                      : model->buffer_seconds));
      }
      else if (model->buffer == DEMO_BUFFER_FAULT)
      {
        (void)snprintf(text, size, "BUF FAULT");
      }
      else if (model->buffer == DEMO_BUFFER_WAITING)
      {
        (void)snprintf(text, size, "NO BUF");
      }
      else
      {
        text[0] = '\0';
      }
      break;
  }
}

/* Page 0: what has the controls on the left, the session on the right. Shift
 * shows on page 1, which every screen leaves empty. */
static void Header(const DemoViewModel *model, const char *title)
{
  char session[16];

  Text(0U, 0U, title);
  SessionLabel(model, session, sizeof(session));
  TextRight(0U, session);
  Rule(0U);
  if (model->shift)
  {
    TextRight(1U, "SHIFT");
  }
}

const char *DemoView_FormatFrequency(uint8_t band, uint32_t frequency_khz, char *text,
                                     size_t size)
{
  const uint32_t hundredths = (frequency_khz % 1000U) / 10U;

  if ((text == NULL) || (size == 0U))
  {
    return "";
  }
  if (frequency_khz == 0U)
  {
    (void)snprintf(text, size, "---");
    return "";
  }
  if (band != 0U)
  {
    (void)snprintf(text, size, "%lu", (unsigned long)frequency_khz);
    return "kHz";
  }
  if ((hundredths % 10U) == 0U)
  {
    (void)snprintf(text, size, "%lu.%lu", (unsigned long)(frequency_khz / 1000U),
                   (unsigned long)(hundredths / 10U));
  }
  else
  {
    (void)snprintf(text, size, "%lu.%02lu", (unsigned long)(frequency_khz / 1000U),
                   (unsigned long)hundredths);
  }
  return "MHz";
}

/* Frequency in double size on pages 2 and 3, the unit beside it on page 3. */
static void Frequency(const DemoViewModel *model)
{
  char number[12];
  const char *unit = DemoView_FormatFrequency(model->band, model->frequency_khz, number,
                                              sizeof(number));

  Text(2U, 0U, BandName(model->band));
  BigText(2U, 16U, number);
  Text(3U, 16U + ((uint32_t)strlen(number) * BIG_CELL_WIDTH) + 2U, unit);
}

static void RunLine(const DemoViewModel *model, char *text, size_t size)
{
  static const char *const reasons[] = {"", "RADIO OFF", "RADIO FAULT", "RECORDING"};
  const char *direction = model->direction_up ? "UP" : "DOWN";

  if (!model->radio_ok)
  {
    (void)snprintf(text, size, "RADIO NOT RUNNING");
    return;
  }
  switch (model->run_state)
  {
    case 0U:
      (void)snprintf(text, size, "SCANNING %s", direction);
      break;
    case 1U:
      (void)snprintf(text, size, "PAUSED %s", direction);
      break;
    case 2U:
      (void)snprintf(text, size, "SWEEP COMPLETE");
      break;
    case 3U:
      (void)snprintf(text, size, "CAN'T SCAN: %s",
                     (model->unable_reason < (sizeof(reasons) / sizeof(reasons[0])))
                       ? reasons[model->unable_reason] : "?");
      break;
    case 4U:
      (void)snprintf(text, size, "HOLDING %s", direction);
      break;
    default:
      (void)snprintf(text, size, "?");
      break;
  }
}

static const char *NoticeText(const DemoViewModel *model, char *text, size_t size)
{
  const char *band = BandName(model->notice_arg);

  switch (model->notice)
  {
    case DEMO_NOTICE_BAND_UNAVAILABLE:
      return "BAND: NOT WHILE REC";
    case DEMO_NOTICE_MODE_UNAVAILABLE:
      return "MODE: NOT WHILE REC";
    case DEMO_NOTICE_BAND_SWITCHING:
      (void)snprintf(text, size, "BAND -> %s ...", band);
      return text;
    case DEMO_NOTICE_BAND_CHANGED:
      (void)snprintf(text, size, "BAND NOW %s", band);
      return text;
    case DEMO_NOTICE_BAND_FAILED:
      return "BAND SWITCH FAILED";
    case DEMO_NOTICE_RADIO_FAULT:
      return "RADIO FAULT";
    case DEMO_NOTICE_SESSION_STARTED:
      return "RECORDING";
    case DEMO_NOTICE_SESSION_SAVED:
      return "SESSION SAVED";
    case DEMO_NOTICE_SESSION_LIMIT:
      return "SAVED: FILE LIMIT";
    case DEMO_NOTICE_SESSION_REJECTED:
      return "SESSION NOT STARTED";
    case DEMO_NOTICE_SESSION_ABORTED:
      return "REC FAULT: ABORTED";
    case DEMO_NOTICE_CARD_FULL:
      return "REC FAULT: CARD FULL";
    case DEMO_NOTICE_SESSION_CANCELLED:
      return "SESSION CANCELLED";
    case DEMO_NOTICE_SAVE_WRITING:
      return "SAVING CAPTURE...";
    case DEMO_NOTICE_SAVE_DONE:
      return "CAPTURE SAVED";
    case DEMO_NOTICE_SAVE_BUSY:
      return "SAVE BUSY";
    case DEMO_NOTICE_SAVE_UNAVAILABLE:
      return "SAVE: NO BUFFER";
    case DEMO_NOTICE_SAVE_IN_SESSION:
      return "SAVE: NOT IN SESSION";
    case DEMO_NOTICE_SAVE_FAILED:
      return "SAVE FAILED";
    case DEMO_NOTICE_BUFFER_FAULT:
      return "BUFFER FAULT";
    case DEMO_NOTICE_NOT_WHILE_RECORDING:
      return "NOT WHILE RECORDING";
    case DEMO_NOTICE_BUSY:
      return "BUSY, TRY AGAIN";
    case DEMO_NOTICE_CLIP_LOADED:
      return "CLIP LOADED";
    case DEMO_NOTICE_CLIP_FAILED:
      switch (model->notice_arg)
      {
        case DEMO_CLIP_REASON_SAVE_BUSY:
          return "CLIP: SAVE BUSY";
        case DEMO_CLIP_REASON_SAVE_UNAVAILABLE:
          return "CLIP: NO BUFFER";
        case DEMO_CLIP_REASON_SAVE_FAILED:
          return "CLIP: SAVE FAILED";
        case DEMO_CLIP_REASON_LOAD_FAILED:
        default:
          return "CLIP LOAD FAILED";
      }
    case DEMO_NOTICE_SESSION_IN_INSTRUMENT:
      return "SESSION: FIELD ONLY";
    default:
      return NULL;
  }
}

static void ClassicScreen(const DemoViewModel *model)
{
  char text[LINE_CHARS + 8U];

  Header(model, "CLASSIC");
  Frequency(model);
  RunLine(model, text, sizeof(text));
  Text(5U, 0U, text);
  (void)snprintf(text, sizeof(text), "RATE %u%s", (unsigned)model->rate_per_min,
                 model->rate_limited ? " MAX" : "");
  Text(6U, 0U, text);
  (void)snprintf(text, sizeof(text), "DIST %u", (unsigned)model->distance_channels);
  TextRight(6U, text);
  Text(7U, 0U, (model->edge < (sizeof(edge_names) / sizeof(edge_names[0])))
                 ? edge_names[model->edge] : "?");
  if (model->hold_seconds == 0U)
  {
    (void)snprintf(text, sizeof(text), "HOLD OFF");
  }
  else
  {
    (void)snprintf(text, sizeof(text), "HOLD %uS", (unsigned)model->hold_seconds);
  }
  TextCentred(7U, text);
  if (model->emf_known && (model->emf_uT >= 10000U))
  {
    (void)snprintf(text, sizeof(text), "%lumT", (unsigned long)(model->emf_uT / 1000U));
  }
  else if (model->emf_known)
  {
    (void)snprintf(text, sizeof(text), "%luuT", (unsigned long)model->emf_uT);
  }
  else
  {
    (void)snprintf(text, sizeof(text), "EMF --");
  }
  TextRight(7U, text);
}

static void ManualScreen(const DemoViewModel *model)
{
  Header(model, "MANUAL");
  Frequency(model);
  Text(5U, 0U, "TUNING NOT IN DEMO");
  Text(7U, 0U, "SHIFT+E1: CLASSIC");
}

static void MenuScreen(const DemoViewModel *model, bool band_menu)
{
  const char *const *names = band_menu ? band_names : engine_names;
  const uint32_t count = band_menu ? 4U : 1U; /* only Classic is selectable */
  uint32_t item;

  Header(model, band_menu ? "BAND" : "ENGINE");
  for (item = 0U; item < count; ++item)
  {
    const uint8_t page = (uint8_t)(2U + item);

    Text(page, 12U, names[item]);
    if (item == model->menu_highlight)
    {
      Text(page, 0U, ">");
      Invert(page);
    }
  }
  Text(7U, 0U, "E0 SELECT  E1 BACK");
}

static void PromptScreen(const DemoViewModel *model, bool start)
{
  Header(model, "SESSION");
  BigTextCentred(2U, start ? "START?" : "STOP?");
  TextCentred(5U, "E0: CONFIRM");
  TextCentred(6U, "RELEASE B0: CANCEL");
}

/* The clip as it saves, loads and plays (p04.6). Only a loaded clip is named
 * with a length; a failed one is never shown as loaded (Principle II). */
/* The granular voice's page (p04.7): the four encoders' parameters, then the
 * clip and the sounding grains. */
/* "96" for a whole tempo, "96.5" otherwise. */
static void FormatTempo(uint32_t bpm_x100, char *text, size_t size)
{
  if ((bpm_x100 % 100U) == 0U)
  {
    (void)snprintf(text, size, "%lu", (unsigned long)(bpm_x100 / 100U));
  }
  else
  {
    (void)snprintf(text, size, "%lu.%lu", (unsigned long)(bpm_x100 / 100U),
                   (unsigned long)((bpm_x100 % 100U) / 10U));
  }
}

static void GrainScreen(const DemoViewModel *model)
{
  char text[LINE_CHARS + 8U];
  uint8_t encoder;

  (void)snprintf(text, sizeof(text), "GRAIN %u/%u%s", (unsigned)model->instrument_page + 1U,
                 (unsigned)DEMO_INSTRUMENT_PAGES, model->seq_running ? " SEQ" : "");
  Header(model, text);
  for (encoder = 0U; encoder < DEMO_INSTRUMENT_ENCODERS; ++encoder)
  {
    const DemoParam param = DemoInstrument_Param(model->instrument_page, encoder);
    const uint8_t page = (uint8_t)(2U + encoder);

    (void)snprintf(text, sizeof(text), "E%u %s", (unsigned)encoder, DemoInstrument_Name(param));
    Text(page, 0U, text);
    if ((param == DEMO_PARAM_POSITION) && model->seq_running)
    {
      /* While the sequence drives position, the knob is an offset around 50 %
       * (decision 0020 item 7, p04.14). */
      (void)snprintf(text, sizeof(text), "%+d%%",
                     ((int)model->grain.position_permille - 500) / 10);
    }
    else
    {
      DemoInstrument_FormatValue(&model->grain, param, text, sizeof(text));
    }
    TextRight(page, text);
  }
  (void)snprintf(text, sizeof(text), "C%03lu %lu.%luS",
                 (unsigned long)(model->clip_capture % 1000U),
                 (unsigned long)((model->clip_tenths / 10U) % 100U),
                 (unsigned long)(model->clip_tenths % 10U));
  Text(6U, 0U, text);
  if (model->clip_playing)
  {
    (void)snprintf(text, sizeof(text), "%u GR", (unsigned)model->grains_active);
  }
  else
  {
    (void)snprintf(text, sizeof(text), "STOPPED");
  }
  TextRight(6U, text);
  Rule(5U);
}

/* The view menu (p04.14), in the form of the Field menus (decision 0009). */
static void ViewMenuScreen(const DemoViewModel *model)
{
  uint32_t item;

  Header(model, "VIEW");
  for (item = 0U; item < (uint32_t)DEMO_SEQ_ITEM_COUNT; ++item)
  {
    const uint8_t page = (uint8_t)(2U + item);

    Text(page, 12U, DemoSequencer_ItemName((uint8_t)item));
    if (item == model->seq_item)
    {
      Text(page, 0U, ">");
      Invert(page);
    }
  }
  Text(7U, 0U, "E0 SELECT  E1 BACK");
}

#define STEP_BAR_TOP_PAGE 2U
#define STEP_BAR_PAGES 4U
#define STEP_BAR_HEIGHT (STEP_BAR_PAGES * 8U)

/* Sets the pixels of one column from y_top (inclusive) down to the bar's base. */
static void BarColumn(uint32_t x, uint32_t y_top)
{
  uint32_t y;

  for (y = y_top; y < STEP_BAR_HEIGHT; ++y)
  {
    frame[((STEP_BAR_TOP_PAGE + (y / 8U)) * DEMO_VIEW_WIDTH) + x] |= (uint8_t)(1U << (y % 8U));
  }
}

/* The step view (p04.14): one 8-pixel column per step, a bar as tall as the
 * step's clip position, filled when on and hollow when off; the playing step is
 * marked above the bars and the selected step below them. */
static void StepScreen(const DemoViewModel *model)
{
  char text[LINE_CHARS + 8U];
  char tempo[12];
  const uint32_t length = (model->seq_length > 16U) ? 16U : model->seq_length;
  const bool editing = (model->seq_focus == DEMO_SEQ_FOCUS_STEP_EDIT) ||
                       (model->seq_focus == DEMO_SEQ_FOCUS_SETTING_EDIT);
  uint32_t step;

  FormatTempo(model->seq_bpm_x100, tempo, sizeof(tempo));
  (void)snprintf(text, sizeof(text), "SEQ %s %s", tempo,
                 DemoSequencer_DivisionName(model->seq_steps_per_beat));
  Header(model, text);
  for (step = 0U; step < length; ++step)
  {
    const uint32_t x = step * 8U;
    const uint32_t height = 1U + ((model->seq_values[step] * (STEP_BAR_HEIGHT - 1U)) / 1000U);
    const uint32_t top = STEP_BAR_HEIGHT - height;
    uint32_t column;

    if ((model->seq_on_mask & (1U << step)) != 0U)
    {
      for (column = 1U; column <= 6U; ++column)
      {
        BarColumn(x + column, top);
      }
    }
    else
    {
      BarColumn(x + 1U, top);
      BarColumn(x + 6U, top);
      for (column = 2U; column <= 5U; ++column)
      {
        frame[((STEP_BAR_TOP_PAGE + (top / 8U)) * DEMO_VIEW_WIDTH) + x + column] |=
          (uint8_t)(1U << (top % 8U));
        frame[((STEP_BAR_TOP_PAGE + STEP_BAR_PAGES - 1U) * DEMO_VIEW_WIDTH) + x + column] |=
          0x80U;
      }
    }
    if (step == model->seq_playhead)
    {
      for (column = 1U; column <= 6U; ++column)
      {
        frame[(1U * DEMO_VIEW_WIDTH) + x + column] |= 0xC0U;
      }
    }
    if ((step == model->seq_step) && (model->seq_focus <= DEMO_SEQ_FOCUS_STEP_EDIT))
    {
      for (column = 1U; column <= 6U; ++column)
      {
        frame[(6U * DEMO_VIEW_WIDTH) + x + column] |= editing ? 0x0FU : 0x03U;
      }
    }
  }
  if (model->seq_focus <= DEMO_SEQ_FOCUS_STEP_EDIT)
  {
    const uint32_t selected = model->seq_step % 16U;

    (void)snprintf(text, sizeof(text), "%s %02u %s %u%%", editing ? "EDIT" : "STEP",
                   (unsigned)selected + 1U,
                   ((model->seq_on_mask & (1U << selected)) != 0U) ? "ON" : "OFF",
                   (unsigned)(model->seq_values[selected] / 10U));
  }
  else
  {
    const bool tempo_selected = model->seq_setting == DEMO_SEQ_SETTING_TEMPO;

    (void)snprintf(text, sizeof(text), "%sTEMPO %s %sDIV %s", tempo_selected ? ">" : " ",
                   tempo, tempo_selected ? " " : ">",
                   DemoSequencer_DivisionName(model->seq_steps_per_beat));
  }
  Text(7U, 0U, text);
  if (model->seq_focus <= DEMO_SEQ_FOCUS_STEP_EDIT)
  {
    TextRight(7U, model->seq_running ? "PLAY" : "STOP");
  }
  if (editing)
  {
    Invert(7U);
  }
}

static void InstrumentScreen(const DemoViewModel *model)
{
  char text[LINE_CHARS + 8U];

  if ((model->clip == DEMO_CLIP_VIEW_READY) && !model->voice_loop)
  {
    if (model->seq_view == DEMO_SEQ_VIEW_MENU)
    {
      ViewMenuScreen(model);
    }
    else if (model->seq_view == DEMO_SEQ_VIEW_STEPS)
    {
      StepScreen(model);
    }
    else
    {
      GrainScreen(model);
    }
    return;
  }
  Header(model, "INSTRUMENT");
  switch (model->clip)
  {
    case DEMO_CLIP_VIEW_SAVING:
      BigTextCentred(2U, "SAVING");
      TextCentred(5U, "CAPTURE FOR CLIP");
      break;
    case DEMO_CLIP_VIEW_LOADING:
      BigTextCentred(2U, "LOADING");
      (void)snprintf(text, sizeof(text), "CLIP FROM C%03lu",
                     (unsigned long)(model->clip_capture % 1000U));
      TextCentred(5U, text);
      break;
    case DEMO_CLIP_VIEW_READY:
      (void)snprintf(text, sizeof(text), "%lu.%luS",
                     (unsigned long)((model->clip_tenths / 10U) % 100U),
                     (unsigned long)(model->clip_tenths % 10U));
      BigTextCentred(2U, text);
      (void)snprintf(text, sizeof(text), "C%03lu %s", (unsigned long)(model->clip_capture % 1000U),
                     model->clip_playing ? "LOOPING" : "STOPPED");
      TextCentred(5U, text);
      break;
    case DEMO_CLIP_VIEW_FAILED:
      BigTextCentred(2U, "NO CLIP");
      TextCentred(5U, "LAST CLIP FAILED");
      break;
    case DEMO_CLIP_VIEW_NONE:
    default:
      BigTextCentred(2U, "NO CLIP");
      TextCentred(5U, "SHIFT+B0+B1 IN FIELD");
      break;
  }
  TextCentred(6U, "SHIFT+B0: FIELD");
}

static void UtilityScreen(const DemoViewModel *model)
{
  Header(model, "UTILITY");
  TextCentred(3U, "EMPTY IN DEMO");
  TextCentred(5U, "E1: BACK");
}

void DemoView_Init(void)
{
  (void)memset(frame, 0, sizeof(frame));
  (void)memset(shown, 0, sizeof(shown));
  (void)memset(&counters, 0, sizeof(counters));
  dirty_mask = 0xFFU;
}

void DemoView_Invalidate(void)
{
  dirty_mask = 0xFFU;
}

void DemoView_Compose(const DemoViewModel *model)
{
  char notice[LINE_CHARS + 8U];
  const char *notice_text;
  uint8_t page;

  if (model == NULL)
  {
    return;
  }
  (void)memset(frame, 0, sizeof(frame));
  switch (model->screen)
  {
    case DEMO_SCREEN_MANUAL:
      ManualScreen(model);
      break;
    case DEMO_SCREEN_BAND_MENU:
      MenuScreen(model, true);
      break;
    case DEMO_SCREEN_ENGINE_MENU:
      MenuScreen(model, false);
      break;
    case DEMO_SCREEN_PROMPT_START:
      PromptScreen(model, true);
      break;
    case DEMO_SCREEN_PROMPT_STOP:
      PromptScreen(model, false);
      break;
    case DEMO_SCREEN_INSTRUMENT:
      InstrumentScreen(model);
      break;
    case DEMO_SCREEN_UTILITY:
      UtilityScreen(model);
      break;
    case DEMO_SCREEN_CLASSIC:
    default:
      ClassicScreen(model);
      break;
  }
  notice_text = NoticeText(model, notice, sizeof(notice));
  if (notice_text != NULL)
  {
    (void)memset(&frame[7U * DEMO_VIEW_WIDTH], 0, DEMO_VIEW_WIDTH);
    TextCentred(7U, notice_text);
    Invert(7U);
  }
  for (page = 0U; page < DEMO_VIEW_PAGES; ++page)
  {
    const uint32_t offset = (uint32_t)page * DEMO_VIEW_WIDTH;

    if (memcmp(&frame[offset], &shown[offset], DEMO_VIEW_WIDTH) != 0)
    {
      dirty_mask |= (uint8_t)(1U << page);
    }
  }
  ++counters.frames;
}

bool DemoView_NextPage(uint8_t *page, const uint8_t **bytes)
{
  uint8_t index;

  if ((page == NULL) || (bytes == NULL))
  {
    return false;
  }
  for (index = 0U; index < DEMO_VIEW_PAGES; ++index)
  {
    if ((dirty_mask & (1U << index)) != 0U)
    {
      *page = index;
      *bytes = &frame[(uint32_t)index * DEMO_VIEW_WIDTH];
      return true;
    }
  }
  return false;
}

void DemoView_PageDone(uint8_t page, bool ok)
{
  const uint32_t offset = (uint32_t)page * DEMO_VIEW_WIDTH;

  if (page >= DEMO_VIEW_PAGES)
  {
    return;
  }
  if (!ok)
  {
    ++counters.pages_failed;
    return;
  }
  (void)memcpy(&shown[offset], &frame[offset], DEMO_VIEW_WIDTH);
  dirty_mask &= (uint8_t)~(1U << page);
  ++counters.pages_written;
}

void DemoView_GetStatus(DemoViewStatus *status)
{
  if (status == NULL)
  {
    return;
  }
  *status = counters;
  status->dirty_mask = dirty_mask;
}

const uint8_t *DemoView_Frame(void)
{
  return frame;
}
