#include <WiFi.h>
#include <WebServer.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <driver/dac.h>
#include <math.h>

// Define pins for the touchscreen
#define TIRQ_PIN 36
#define CS_PIN 33
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK

// Touchscreen calibration constants (these need to be set according to your specific touchscreen)
const int TOUCH_X_MIN = 300;
const int TOUCH_X_MAX = 3800;
const int TOUCH_Y_MIN = 300;
const int TOUCH_Y_MAX = 3800;

const int SCREEN_WIDTH = 320;
const int SCREEN_HEIGHT = 240;

TFT_eSPI tft = TFT_eSPI();                    // Create TFT object
XPT2046_Touchscreen ts(CS_PIN, TIRQ_PIN);     // Create touchscreen object

// LVGL display buffer
static lv_color_t buf1[320 * 5]; // Smaller display buffer
static lv_disp_draw_buf_t draw_buf;

// Waveform types
enum Waveform { SINE, TRIANGLE, SQUARE };
Waveform currentWaveform = SINE;

const int dacPin = DAC_CHANNEL_2;  // DAC2 pin (GPIO 26)
int frequency = 1000;              // Frequency in Hz
int amplitude = 255;               // Amplitude of the wave (0-255 for 8-bit resolution)

// Wi-Fi credentials (replace with your own)
const char* ssid = "Sensors";
const char* password = "Osiris08";

// Global variables for WebServer
WebServer server(80);

// Global variables for LVGL objects
lv_obj_t *label;
lv_obj_t *slider;
lv_obj_t *canvas; // Canvas object for displaying signal shape
static lv_color_t cbuf[60 * 30]; // Smaller buffer for the canvas (60x30 pixels)

// Function declarations
void setupWiFi();
void handleRoot();
void handleSet();
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data);
void slider_event_cb(lv_event_t * e);
void btn_event_cb(lv_event_t * e);
void kb_event_cb(lv_event_t * e);
void ta_event_cb(lv_event_t * e);
void num_pad_btn_event_cb(lv_event_t * e);
void drawWaveform();
void generateWaveform();
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
}

void btn_event_cb(lv_event_t * e) {
    // Switch waveform type
    if (currentWaveform == SINE) currentWaveform = TRIANGLE;
    else if (currentWaveform == TRIANGLE) currentWaveform = SQUARE;
    else currentWaveform = SINE;

    // Update button label
    const char *waveform_name = (currentWaveform == SINE) ? "Sine" :
                                (currentWaveform == TRIANGLE) ? "Triangle" :
                                                                "Square";
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

                lv_obj_del(kb); // Close keyboard
                delete[] user_data; // Free user data memory
            }
        }
    }
}

void ta_event_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);

    if (ta) {
        lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
        lv_obj_set_width(kb, SCREEN_WIDTH); // Set keyboard width to screen width
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0); // Center keyboard horizontally

        lv_keyboard_set_textarea(kb, ta);

        // Pass both label and slider as user data
        lv_obj_t **user_data = new lv_obj_t *[2];
        user_data[0] = label;
        user_data[1] = slider;
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, user_data);
    }
}

void num_pad_btn_event_cb(lv_event_t * e) {
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_user_data(e);

    if (ta) {
        lv_obj_t *num_kb = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_mode(num_kb, LV_KEYBOARD_MODE_NUMBER); // Set numeric mode
        lv_obj_set_width(num_kb, SCREEN_WIDTH); // Set keyboard width to screen width
        lv_obj_align(num_kb, LV_ALIGN_BOTTOM_MID, 0, 0); // Center keyboard horizontally

        lv_keyboard_set_textarea(num_kb, ta);

        // Pass both label and slider as user data
        lv_obj_t **user_data = new lv_obj_t *[2];
        user_data[0] = label;
        user_data[1] = slider;
        lv_obj_add_event_cb(num_kb, kb_event_cb, LV_EVENT_ALL, user_data);
    }
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
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 320 * 5);
    
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

    // Initialize DAC pin
    dac_output_enable((dac_channel_t)dacPin);

    // Create LVGL objects
    label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Frequency: 1000 Hz");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    
    slider = lv_slider_create(lv_scr_act());
    lv_obj_set_width(slider, 200);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider, 20, 10000); // Update maximum frequency to 10,000 Hz
    lv_slider_set_value(slider, 1000, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, label); // Pass label as user data

    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 100, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Sine");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL); // Pass NULL as user data

    lv_obj_t *ta = lv_textarea_create(lv_scr_act());
    lv_textarea_set_one_line(ta, true);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 50); // Align the textarea above the slider
    lv_obj_set_width(ta, 100);
    lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_FOCUSED, ta);

    // Create a new button to activate the numeric pad
    lv_obj_t *num_pad_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(num_pad_btn, 80, 30); // Smaller size
    lv_obj_align(num_pad_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(num_pad_btn, lv_color_hex(0x00FF00), 0); // Green color
    lv_obj_t *num_pad_btn_label = lv_label_create(num_pad_btn);
    lv_label_set_text(num_pad_btn_label, "Num Pad");
    lv_obj_add_event_cb(num_pad_btn, num_pad_btn_event_cb, LV_EVENT_CLICKED, ta); // Pass the text area as user data

    // Setup Wi-Fi and start the web server
    setupWiFi();

    // Display the IP address on the device
    lv_obj_t *ip_label = lv_label_create(lv_scr_act());
    lv_label_set_text_fmt(ip_label, "IP Address: %s", WiFi.localIP().toString().c_str());
    lv_obj_align(ip_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Create a canvas for displaying the signal shape
    canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, cbuf, 60, 30, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(canvas, 60, 30); // Set canvas size
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 10, 10); // Align canvas to top-left

    Serial.println("Setup complete");
}

void setupWiFi() {
    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    // Display the IP address
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Start the server
    server.on("/", handleRoot);
    server.on("/set", handleSet);
    server.begin();
    Serial.println("HTTP server started");
}

void handleRoot() {
    String html = "<html><body>";
    html += "<h1>Adjust Frequency and Waveform</h1>";
    html += "<form action='/set' method='GET'>";
    html += "Frequency (Hz): <input type='number' name='frequency'><br><br>";
    html += "Waveform: ";
    html += "<select name='waveform'>";
    html += "<option value='sine'>Sine</option>";
    html += "<option value='square'>Square</option>";
    html += "<option value='triangle'>Triangle</option>";
    html += "<option value='sawtooth'>Sawtooth</option>";
    html += "</select><br><br>";
    html += "<input type='submit' value='Set'>";
    html += "</form>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleSet() {
    String frequencyStr = server.arg("frequency");
    String waveform = server.arg("waveform");

    if (frequencyStr.length() > 0) {
        frequency = frequencyStr.toInt();
        lv_slider_set_value(slider, frequency, LV_ANIM_OFF);
        lv_label_set_text_fmt(label, "Frequency: %d Hz", frequency);
    }

    if (waveform.length() > 0) {
        lv_label_set_text_fmt(label, "Waveform: %s", waveform.c_str());
    }

    server.send(200, "text/html", "Settings updated! <br><a href='/'>Back</a>");
}


void loop() {
    // Handle web server requests
    server.handleClient();
    
    // Handle LVGL updates
    lv_timer_handler();
    delay(5);
    
    // Draw waveform on canvas
    drawWaveform();
}
void drawWaveform() {
    static int sample = 0;
    lv_color_t color = lv_color_hex(0xFF0000); // Red color
    lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER); // White background

    for (int x = 0; x < 60; x++) { // Update loop to match canvas width
        float value;
        int period = 10000 / frequency;

        switch (currentWaveform) {
            case SINE:
                value = sin(2 * PI * frequency * sample / 10000.0);
                break;
            case TRIANGLE:
                value = 2 * abs(2 * (sample / 10000.0 * frequency - floor(sample / 10000.0 * frequency + 0.5))) - 1;
                break;
            case SQUARE:
                value = (sample % period < (period / 2)) ? 1 : -1;
                break;
        }

        int y = (value + 1) * 15; // Scale value to canvas height
        lv_canvas_set_px(canvas, x, 15 - y, color); // Draw pixel on canvas

        sample = (sample + 1) % 10000;
    }
}
