#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <driver/ledc.h>
#include <math.h>

// Define pins for the touchscreen
#define TIRQ_PIN 36
#define CS_PIN 33
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK

TFT_eSPI tft = TFT_eSPI();                    // Create TFT object
XPT2046_Touchscreen ts(CS_PIN, TIRQ_PIN);     // Create touchscreen object

// LVGL display buffer
static lv_color_t buf1[320 * 10];
static lv_disp_draw_buf_t draw_buf;

// Waveform types
enum Waveform { SINE, TRIANGLE, SQUARE, SAWTOOTH };
Waveform currentWaveform = SINE;

const int SAMPLES = 256;

// PWM settings
#define PWM_FREQ 5000
#define PWM_RESOLUTION LEDC_TIMER_10_BIT
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_PIN 22

uint16_t wave[SAMPLES];
int frequency = 1000; // Frequency in Hz
int amplitude = 255;  // Amplitude of the wave (0-255 for 8-bit resolution)

// LVGL objects
lv_obj_t *label;
lv_obj_t *slider;
lv_obj_t *freq_display; // Text area to display current frequency
// Touchscreen calibration constants (these need to be set according to your specific touchscreen)
const int TOUCH_X_MIN = 300;
const int TOUCH_X_MAX = 3800;
const int TOUCH_Y_MIN = 300;
const int TOUCH_Y_MAX = 3800;

const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;

// Function declarations
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data);
void slider_event_cb(lv_event_t * e);
void btn_event_cb(lv_event_t * e);
void kb_event_cb(lv_event_t * e);
void numpad_btn_event_cb(lv_event_t * e);
void keyboard_btn_event_cb(lv_event_t * e); // Added function declaration for keyboard button
void generate_waveform(Waveform waveform);
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
    tft.pushColors(&color_p->full, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    if (ts.tirqTouched()) {
        if (ts.touched()) {
            TS_Point p = ts.getPoint();
            // Add debugging to check the raw touchscreen values
            Serial.print("Touch Point - X: ");
            Serial.print(p.x);
            Serial.print(", Y: ");
            Serial.println(p.y);

            // Map the touchscreen coordinates to display resolution
            data->point.x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH);
            data->point.y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT);

            // Add boundary checks to ensure the values are within the expected range
            if (data->point.x < 0) data->point.x = 0;
            if (data->point.x >= SCREEN_WIDTH) data->point.x = SCREEN_WIDTH - 1;
            if (data->point.y < 0) data->point.y = 0;
            if (data->point.y >= SCREEN_HEIGHT) data->point.y = SCREEN_HEIGHT - 1;

            data->state = LV_INDEV_STATE_PR;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
void slider_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int new_frequency = lv_slider_get_value(slider);

    // Update global frequency variable
    frequency = new_frequency;

    // Get the user data which is the label
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);

    // Update the label text
    lv_label_set_text_fmt(label, "Frequency: %d Hz", frequency);
    lv_textarea_set_text(freq_display, String(frequency).c_str()); // Update frequency display text area
}

void btn_event_cb(lv_event_t * e) {
    // Switch waveform type
    if (currentWaveform == SINE) currentWaveform = TRIANGLE;
    else if (currentWaveform == TRIANGLE) currentWaveform = SQUARE;
    else if (currentWaveform == SQUARE) currentWaveform = SAWTOOTH;
    else currentWaveform = SINE;

    // Update button label
    const char *waveform_name = (currentWaveform == SINE) ? "Sine" :
                                (currentWaveform == TRIANGLE) ? "Triangle" :
                                (currentWaveform == SQUARE) ? "Square" :
                                                              "Sawtooth";
    lv_label_set_text(lv_obj_get_child(e->target, 0), waveform_name);
}

void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY) {
        lv_obj_t *kb = lv_event_get_target(e);
        lv_obj_t *ta = lv_keyboard_get_textarea(kb);

        if (ta) {
            const char *text = lv_textarea_get_text(ta);

            if (text && strlen(text) > 0) {
                frequency = atoi(text);
                lv_textarea_set_text(ta, ""); // Clear the text area after input

                // Get the user data which is the label and slider
                lv_obj_t **user_data = (lv_obj_t **)lv_event_get_user_data(e);
                lv_obj_t *label = user_data[0];
                lv_obj_t *slider = user_data[1];

                if (label) {
                    lv_label_set_text_fmt(label, "Frequency: %d Hz", frequency);
                }

                if (slider) {
                    lv_slider_set_value(slider, frequency, LV_ANIM_OFF);
                }

                lv_textarea_set_text(freq_display, String(frequency).c_str()); // Update frequency display text area

                lv_obj_del(kb); // Close keyboard
                delete[] user_data; // Free user data memory
            }
        }
    }
}

void numpad_btn_event_cb(lv_event_t * e) {
    lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER); // Set keyboard mode to numpad
    lv_keyboard_set_textarea(kb, freq_display); // Set the keyboard to edit the frequency display
    lv_obj_set_width(kb, SCREEN_WIDTH - 20); // Adjust width to fit better
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 10, 0); // Adjust horizontal position

    // Pass both label and slider as user data
    lv_obj_t **user_data = new lv_obj_t *[2];
    user_data[0] = label;
    user_data[1] = slider;
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, user_data);
}

void keyboard_btn_event_cb(lv_event_t * e) {
    lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(kb, freq_display); // Set the keyboard to edit the frequency display
    lv_obj_set_width(kb, SCREEN_WIDTH - 20); // Adjust width to fit better
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 10, 0); // Adjust horizontal position

    // Pass both label and slider as user data
    lv_obj_t **user_data = new lv_obj_t *[2];
    user_data[0] = label;
    user_data[1] = slider;
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, user_data);
}
void setup() {
    // Initialize Serial for debugging
    Serial.begin(115200);

    // Initialize SPI for touchscreen
    SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI);
    
    // Initialize the TFT and touchscreen
    tft.begin();
    tft.setRotation(1);
    ts.begin();
    ts.setRotation(1);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 320 * 10);
    
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Create LVGL objects
    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Frequency: 1000 Hz");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    
    slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(slider, 200);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider, 20, 78000); // Update maximum frequency to 78,000 Hz
    lv_slider_set_value(slider, 1000, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, label); // Pass label as user data

    // Create text area to display current frequency
    freq_display = lv_textarea_create(lv_scr_act());
    lv_textarea_set_one_line(freq_display, true);
    lv_textarea_set_text(freq_display, "1000 Hz");
    lv_obj_align(freq_display, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_width(freq_display, 100);
    lv_textarea_set_cursor_pos(freq_display, LV_TEXTAREA_CURSOR_LAST); // Move cursor out of view

    // Create button for NumPad
    lv_obj_t *numpad_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(numpad_btn, 100, 50);
    lv_obj_align(numpad_btn, LV_ALIGN_BOTTOM_LEFT, 10, -20);
    lv_obj_t *numpad_btn_label = lv_label_create(numpad_btn);
    lv_label_set_text(numpad_btn_label, "NumPad");
    lv_obj_add_event_cb(numpad_btn, numpad_btn_event_cb, LV_EVENT_CLICKED, NULL);

    // Create button for Keyboard
    lv_obj_t *keyboard_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(keyboard_btn, 100, 50);
    lv_obj_align(keyboard_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -20);
    lv_obj_t *keyboard_btn_label = lv_label_create(keyboard_btn);
    lv_label_set_text(keyboard_btn_label, "Keyboard");
    lv_obj_add_event_cb(keyboard_btn, keyboard_btn_event_cb, LV_EVENT_CLICKED, NULL);

    Serial.println("Setup complete");

    // Setup PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 78000, // Set frequency to 78,000 Hz
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = PWM_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel = PWM_CHANNEL,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
}

void generate_waveform(Waveform waveform) {
    for (int i = 0; i < SAMPLES; i++) {
        switch (waveform) {
            case SINE:
                wave[i] = (uint16_t)(511 * (1 + sin(2 * PI * i / SAMPLES)));
                break;
            case TRIANGLE:
                wave[i] = (i < SAMPLES / 2) ? (uint16_t)(1023 * 2 * i / SAMPLES) : (uint16_t)(1023 * (2 - 2 * i / SAMPLES));
                break;
            case SQUARE:
                wave[i] = (i < SAMPLES / 2) ? 1023 : 0;
                break;
            case SAWTOOTH:
                wave[i] = (uint16_t)(1023 * i / SAMPLES);
                break;
        }
    }
}
void loop() {
    lv_timer_handler();
    delay(5);

    if (frequency > 0) {
        generate_waveform(currentWaveform);
        for (int i = 0; i < SAMPLES; i++) {
            ledc_set_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL, wave[i]);
            ledc_update_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL);
            delayMicroseconds(1000000 / (frequency * SAMPLES));
        }
    }
}
