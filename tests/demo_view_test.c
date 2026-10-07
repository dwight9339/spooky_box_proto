/* Demo-only OLED view (demo_view.c, full_spooky_proto-p04.3). */
#include "demo_view.h"

#include "demo_sequencer.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) do {                                               \
    if (!(condition)) {                                                     \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);       \
        ++failures;                                                         \
    }                                                                       \
} while (0)

#define WIDTH DEMO_VIEW_WIDTH

static DemoViewModel classic_model(void)
{
    DemoViewModel model;

    memset(&model, 0, sizeof(model));
    model.screen = DEMO_SCREEN_CLASSIC;
    model.session = DEMO_SESSION_IDLE;
    model.radio_ok = true;
    model.band = 0u;
    model.frequency_khz = 98100u;
    model.run_state = 0u;
    model.direction_up = true;
    model.rate_per_min = 60u;
    model.distance_channels = 2u;
    model.hold_seconds = 3u;
    model.emf_known = true;
    model.emf_uT = 120u;
    return model;
}

static uint8_t byte_at(uint8_t page, unsigned x)
{
    return DemoView_Frame()[(unsigned)page * WIDTH + x];
}

/* Writes every dirty page, lowest first, and returns the mask written. */
static uint8_t flush(void)
{
    uint8_t written = 0u;
    uint8_t page;
    const uint8_t *bytes;
    unsigned guard = 0u;

    while (DemoView_NextPage(&page, &bytes) && guard++ < 16u) {
        CHECK(bytes == DemoView_Frame() + (unsigned)page * WIDTH);
        written |= (uint8_t)(1u << page);
        DemoView_PageDone(page, true);
    }
    return written;
}

static void test_frequency_format(void)
{
    char text[16];

    CHECK(strcmp(DemoView_FormatFrequency(0u, 98100u, text, sizeof(text)), "MHz") == 0);
    CHECK(strcmp(text, "98.1") == 0);
    DemoView_FormatFrequency(0u, 98150u, text, sizeof(text));
    CHECK(strcmp(text, "98.15") == 0);
    DemoView_FormatFrequency(0u, 100000u, text, sizeof(text));
    CHECK(strcmp(text, "100.0") == 0);
    CHECK(strcmp(DemoView_FormatFrequency(1u, 1010u, text, sizeof(text)), "kHz") == 0);
    CHECK(strcmp(text, "1010") == 0);
    DemoView_FormatFrequency(2u, 9580u, text, sizeof(text));
    CHECK(strcmp(text, "9580") == 0);
    DemoView_FormatFrequency(0u, 0u, text, sizeof(text));
    CHECK(strcmp(text, "---") == 0);
}

static void test_glyphs_and_rule(void)
{
    DemoViewModel model = classic_model();

    DemoView_Init();
    DemoView_Compose(&model);
    /* "CLASSIC" at x = 0 on page 0, over the rule in the bottom pixel row. */
    CHECK(byte_at(0u, 0u) == (0x3Eu | 0x80u));
    CHECK(byte_at(0u, 4u) == (0x22u | 0x80u));
    CHECK(byte_at(0u, 5u) == 0x80u); /* the gap between characters */
    CHECK(byte_at(0u, 6u) == (0x7Fu | 0x80u)); /* 'L' */
    /* The frequency "98.1" in double size from x = 16: '9' column 0 is 0x46. */
    CHECK(byte_at(2u, 16u) == 0x3Cu);
    CHECK(byte_at(2u, 17u) == 0x3Cu);
    CHECK(byte_at(3u, 16u) == 0x30u);
    /* "FM" on page 2 at x = 0: 'F' column 0 is 0x7F. */
    CHECK(byte_at(2u, 0u) == 0x7Fu);
    /* Idle and no Shift: page 1 is empty and the header has no session label. */
    for (unsigned x = 0u; x < WIDTH; ++x) {
        CHECK(byte_at(1u, x) == 0u);
    }
    CHECK(byte_at(0u, WIDTH - 1u) == 0x80u);
}

static void test_dirty_pages(void)
{
    DemoViewModel model = classic_model();
    DemoViewStatus status;

    DemoView_Init();
    DemoView_Compose(&model);
    CHECK(flush() == 0xFFu); /* the display contents are unknown at start */
    DemoView_Compose(&model);
    CHECK(flush() == 0u);

    model.frequency_khz = 98300u;
    DemoView_Compose(&model);
    CHECK(flush() == 0x0Cu); /* the big frequency only */

    model.session = DEMO_SESSION_RECORDING;
    model.session_seconds = 75u;
    DemoView_Compose(&model);
    CHECK(flush() == 0x01u); /* "REC 01:15" in the header */

    model.shift = true;
    DemoView_Compose(&model);
    CHECK(flush() == 0x02u);

    DemoView_Invalidate();
    CHECK(flush() == 0xFFu);

    /* A failed write leaves the page dirty. */
    model.run_state = 1u;
    DemoView_Compose(&model);
    {
        uint8_t page = 0xFFu;
        const uint8_t *bytes = NULL;

        CHECK(DemoView_NextPage(&page, &bytes));
        CHECK(page == 5u);
        DemoView_PageDone(page, false);
        CHECK(DemoView_NextPage(&page, &bytes));
        CHECK(page == 5u);
        DemoView_PageDone(page, true);
        CHECK(!DemoView_NextPage(&page, &bytes));
    }
    DemoView_GetStatus(&status);
    CHECK(status.pages_failed == 1u);
    CHECK(status.dirty_mask == 0u);
    CHECK(status.frames == 6u);
}

static void test_notice_inverts_bottom_line(void)
{
    DemoViewModel model = classic_model();

    DemoView_Init();
    DemoView_Compose(&model);
    CHECK(byte_at(7u, 0u) != 0xFFu);
    model.notice = DEMO_NOTICE_CARD_FULL;
    DemoView_Compose(&model);
    CHECK(byte_at(7u, 0u) == 0xFFu);
    CHECK(byte_at(7u, WIDTH - 1u) == 0xFFu);
    /* The text is centred: "REC FAULT: CARD FULL" is 20 cells, 120 pixels, from
     * x = 4; its 'R' column 0 (0x7F) shows inverted. */
    CHECK(byte_at(7u, 4u) == 0x80u);
    /* Pages above the notice are unchanged. */
    CHECK(byte_at(0u, 0u) == (0x3Eu | 0x80u));
}

static void test_band_menu_highlight(void)
{
    DemoViewModel model = classic_model();

    DemoView_Init();
    model.screen = DEMO_SCREEN_BAND_MENU;
    model.menu_highlight = 2u; /* SW */
    DemoView_Compose(&model);
    CHECK(byte_at(4u, WIDTH - 1u) == 0xFFu); /* row of item 2 inverted */
    CHECK(byte_at(2u, WIDTH - 1u) == 0x00u);
    CHECK(byte_at(3u, WIDTH - 1u) == 0x00u);
    CHECK(byte_at(5u, WIDTH - 1u) == 0x00u);
    /* 'B' of "BAND" in the header. */
    CHECK(byte_at(0u, 0u) == (0x7Fu | 0x80u));
}

static void test_screens_differ(void)
{
    static uint8_t frames[DEMO_SCREEN_COUNT][DEMO_VIEW_FRAME_BYTES];
    DemoViewModel model = classic_model();

    DemoView_Init();
    for (unsigned screen = 0u; screen < DEMO_SCREEN_COUNT; ++screen) {
        model.screen = (uint8_t)screen;
        DemoView_Compose(&model);
        memcpy(frames[screen], DemoView_Frame(), DEMO_VIEW_FRAME_BYTES);
    }
    for (unsigned a = 0u; a < DEMO_SCREEN_COUNT; ++a) {
        for (unsigned b = a + 1u; b < DEMO_SCREEN_COUNT; ++b) {
            CHECK(memcmp(frames[a], frames[b], DEMO_VIEW_FRAME_BYTES) != 0);
        }
    }
    /* An unknown screen falls back to Classic. */
    model.screen = 200u;
    DemoView_Compose(&model);
    CHECK(memcmp(DemoView_Frame(), frames[DEMO_SCREEN_CLASSIC], DEMO_VIEW_FRAME_BYTES) == 0);
}

static void test_unknown_and_unable_are_honest(void)
{
    DemoViewModel model = classic_model();
    static uint8_t known[DEMO_VIEW_FRAME_BYTES];

    DemoView_Init();
    DemoView_Compose(&model);
    memcpy(known, DemoView_Frame(), sizeof(known));
    model.emf_known = false;
    DemoView_Compose(&model);
    CHECK(memcmp(known, DemoView_Frame(), 7u * WIDTH) == 0);
    CHECK(memcmp(known + 7u * WIDTH, DemoView_Frame() + 7u * WIDTH, WIDTH) != 0);

    /* Unable to scan is never drawn like running (FR-027). */
    model = classic_model();
    DemoView_Compose(&model);
    memcpy(known, DemoView_Frame(), sizeof(known));
    model.run_state = 3u;
    model.unable_reason = 3u;
    DemoView_Compose(&model);
    CHECK(memcmp(known + 5u * WIDTH, DemoView_Frame() + 5u * WIDTH, WIDTH) != 0);
    model.radio_ok = false;
    DemoView_Compose(&model);
    CHECK(memcmp(known + 5u * WIDTH, DemoView_Frame() + 5u * WIDTH, WIDTH) != 0);
}

/* Each clip state draws its own Instrument body, and only a loaded clip shows a
 * length (p04.6, Principle II). */
static void test_instrument_clip_states(void)
{
    static uint8_t frames[DEMO_CLIP_VIEW_FAILED + 1u][DEMO_VIEW_FRAME_BYTES];
    DemoViewModel model = classic_model();

    DemoView_Init();
    model.screen = DEMO_SCREEN_INSTRUMENT;
    model.clip_capture = 12u;
    model.clip_tenths = 30u;
    for (unsigned clip = 0u; clip <= DEMO_CLIP_VIEW_FAILED; ++clip) {
        model.clip = (uint8_t)clip;
        DemoView_Compose(&model);
        memcpy(frames[clip], DemoView_Frame(), DEMO_VIEW_FRAME_BYTES);
    }
    for (unsigned a = 0u; a <= DEMO_CLIP_VIEW_FAILED; ++a) {
        for (unsigned b = a + 1u; b <= DEMO_CLIP_VIEW_FAILED; ++b) {
            CHECK(memcmp(frames[a], frames[b], DEMO_VIEW_FRAME_BYTES) != 0);
        }
    }
    /* The length is drawn from the clip only while it is ready. */
    model.clip = DEMO_CLIP_VIEW_FAILED;
    model.clip_tenths = 7u;
    DemoView_Compose(&model);
    CHECK(memcmp(DemoView_Frame(), frames[DEMO_CLIP_VIEW_FAILED], DEMO_VIEW_FRAME_BYTES) == 0);
    model.clip = DEMO_CLIP_VIEW_READY;
    DemoView_Compose(&model);
    CHECK(memcmp(DemoView_Frame(), frames[DEMO_CLIP_VIEW_READY], DEMO_VIEW_FRAME_BYTES) != 0);
    /* Looping and stopped differ. */
    model.clip_tenths = 30u;
    model.clip_playing = true;
    DemoView_Compose(&model);
    CHECK(memcmp(DemoView_Frame(), frames[DEMO_CLIP_VIEW_READY], DEMO_VIEW_FRAME_BYTES) != 0);
    /* Every clip notice has text on the bottom line. */
    for (unsigned reason = 0u; reason <= DEMO_CLIP_REASON_LOAD_FAILED; ++reason) {
        model.notice = DEMO_NOTICE_CLIP_FAILED;
        model.notice_arg = (uint8_t)reason;
        DemoView_Compose(&model);
        CHECK(byte_at(7u, 0u) == 0xFFu);
    }
}

/* With the granular voice, a ready clip shows the page's four parameters; the
 * pages differ, and the plain loop keeps its own screen (p04.7). */
static void test_grain_pages(void)
{
    static uint8_t first[DEMO_VIEW_FRAME_BYTES];
    DemoViewModel model = classic_model();

    DemoView_Init();
    model.screen = DEMO_SCREEN_INSTRUMENT;
    model.clip = DEMO_CLIP_VIEW_READY;
    model.clip_capture = 4u;
    model.clip_tenths = 30u;
    model.clip_playing = true;
    model.grains_active = 7u;
    Granular_DefaultParams(&model.grain);
    DemoView_Compose(&model);
    memcpy(first, DemoView_Frame(), sizeof(first));
    /* "GRAIN 1/2": 'G' column 0 is 0x3E, over the header rule. */
    CHECK(byte_at(0u, 0u) == (0x3Eu | 0x80u));
    /* "E0 POS" on page 2. */
    CHECK(byte_at(2u, 0u) == 0x7Fu);
    model.instrument_page = 1u;
    DemoView_Compose(&model);
    CHECK(memcmp(first + 2u * WIDTH, DemoView_Frame() + 2u * WIDTH, 4u * WIDTH) != 0);
    CHECK(memcmp(first + 6u * WIDTH, DemoView_Frame() + 6u * WIDTH, WIDTH) == 0);
    /* A parameter change redraws only its row. */
    model.instrument_page = 0u;
    model.grain.density = 40u;
    DemoView_Compose(&model);
    CHECK(memcmp(first + 3u * WIDTH, DemoView_Frame() + 3u * WIDTH, WIDTH) == 0);
    CHECK(memcmp(first + 4u * WIDTH, DemoView_Frame() + 4u * WIDTH, WIDTH) != 0);
    model.voice_loop = true;
    DemoView_Compose(&model);
    CHECK(memcmp(first, DemoView_Frame(), DEMO_VIEW_FRAME_BYTES) != 0);
    CHECK(byte_at(0u, 0u) == (0x00u | 0x80u)); /* "INSTRUMENT": 'I' column 0 is 0x00 */
}

/* The step view (p04.14): bars as tall as the step values, hollow when off,
 * the playhead above them and the selection below; an edit inverts the bottom
 * line. The view menu inverts its highlighted item, and while the transport runs
 * the position knob reads as an offset. */
static void test_step_view(void)
{
    static uint8_t grain[DEMO_VIEW_FRAME_BYTES];
    DemoViewModel model = classic_model();

    DemoView_Init();
    model.screen = DEMO_SCREEN_INSTRUMENT;
    model.clip = DEMO_CLIP_VIEW_READY;
    model.clip_playing = true;
    Granular_DefaultParams(&model.grain);
    model.seq_length = 16u;
    model.seq_bpm_x100 = 9650u;
    model.seq_steps_per_beat = 2u;
    model.seq_playhead = 0xFFu;
    model.seq_on_mask = 0xFFFFu;
    for (unsigned step = 0u; step < 16u; ++step) {
        model.seq_values[step] = (uint16_t)(step * 1000u / 16u);
    }
    DemoView_Compose(&model);
    memcpy(grain, DemoView_Frame(), sizeof(grain));
    model.seq_view = DEMO_SEQ_VIEW_STEPS;
    DemoView_Compose(&model);
    CHECK(memcmp(grain, DemoView_Frame(), sizeof(grain)) != 0);
    /* Step 1 (value 0) is one pixel at the base; step 16 (937) is 30 tall. */
    CHECK(byte_at(5u, 1u) == 0x80u && byte_at(4u, 1u) == 0x00u);
    CHECK(byte_at(2u, 121u) == 0xFCu && byte_at(5u, 121u) == 0xFFu);
    CHECK(byte_at(2u, 0u) == 0x00u && byte_at(2u, 7u) == 0x00u); /* gaps between steps */
    /* Off: only the sides, the top and the base. Step 9 (500) is 16 tall. */
    model.seq_on_mask = 0xFEFFu; /* step 9 off */
    DemoView_Compose(&model);
    CHECK(byte_at(4u, 65u) == 0xFFu && byte_at(4u, 70u) == 0xFFu);
    CHECK(byte_at(4u, 66u) == 0x01u && byte_at(5u, 66u) == 0x80u);
    /* Playhead and selection marks; nothing selected while the settings row has focus. */
    model.seq_playhead = 3u;
    model.seq_step = 2u;
    DemoView_Compose(&model);
    CHECK((byte_at(1u, 25u) & 0xC0u) == 0xC0u && (byte_at(1u, 17u) & 0xC0u) == 0x00u);
    CHECK(byte_at(6u, 17u) == 0x03u);
    model.seq_focus = DEMO_SEQ_FOCUS_STEP_EDIT;
    DemoView_Compose(&model);
    CHECK(byte_at(6u, 17u) == 0x0Fu && byte_at(7u, 100u) == 0xFFu);
    model.seq_focus = DEMO_SEQ_FOCUS_SETTINGS;
    DemoView_Compose(&model);
    CHECK(byte_at(6u, 17u) == 0x00u && byte_at(7u, 127u) == 0x00u);
    model.seq_focus = DEMO_SEQ_FOCUS_SETTING_EDIT;
    DemoView_Compose(&model);
    CHECK(byte_at(7u, 127u) == 0xFFu);
    /* The view menu inverts its highlighted item. */
    model.seq_view = DEMO_SEQ_VIEW_MENU;
    model.seq_item = DEMO_SEQ_ITEM_SEQUENCER;
    DemoView_Compose(&model);
    CHECK(byte_at(3u, 127u) == 0xFFu && byte_at(2u, 127u) == 0x00u);
    /* On the main page the position becomes an offset while running. */
    model.seq_view = DEMO_SEQ_VIEW_ENGINE;
    model.seq_running = true;
    DemoView_Compose(&model);
    CHECK(memcmp(grain + 2u * WIDTH, DemoView_Frame() + 2u * WIDTH, WIDTH) != 0);
    CHECK(memcmp(grain + 3u * WIDTH, DemoView_Frame() + 3u * WIDTH, 3u * WIDTH) == 0);
}

static void test_wide_values_are_clipped(void)
{
    DemoViewModel model = classic_model();

    DemoView_Init();
    model.band = 2u;
    model.frequency_khz = 4294967295u;  /* 10 digits: wider than the display */
    model.rate_per_min = 65535u;
    model.rate_limited = true;
    model.distance_channels = 65535u;
    model.hold_seconds = 65535u;
    model.emf_uT = 4294967295u;
    model.session = DEMO_SESSION_RECORDING;
    model.session_seconds = 4294967295u;
    model.notice = DEMO_NOTICE_BAND_SWITCHING;
    model.notice_arg = 250u;
    DemoView_Compose(&model);
    CHECK(flush() == 0xFFu);
    model.screen = DEMO_SCREEN_INSTRUMENT;
    model.clip = DEMO_CLIP_VIEW_READY;
    model.clip_capture = 4294967295u;
    model.clip_tenths = 4294967295u;
    model.notice = DEMO_NOTICE_CLIP_FAILED;
    DemoView_Compose(&model);
    CHECK(flush() != 0u);
}

int main(void)
{
    test_frequency_format();
    test_glyphs_and_rule();
    test_dirty_pages();
    test_notice_inverts_bottom_line();
    test_band_menu_highlight();
    test_screens_differ();
    test_unknown_and_unable_are_honest();
    test_instrument_clip_states();
    test_grain_pages();
    test_step_view();
    test_wide_values_are_clipped();
    if (failures != 0u) {
        printf("%u failure(s)\n", failures);
        return 1;
    }
    printf("demo_view_test passed\n");
    return 0;
}
