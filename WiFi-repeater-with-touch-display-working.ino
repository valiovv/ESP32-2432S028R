#include <WiFi.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

#define TIRQ_PIN 36
#define CS_PIN 33
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(CS_PIN, TIRQ_PIN);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 10];
lv_obj_t *kb;
lv_obj_t *ssid_ta, *pass_ta, *ap_list;
lv_obj_t *status_label;
String ssid, password;
bool ap_selected = false;

const char* ap_ssid = "ESP32-WIFI-EXTENDER";
const char* ap_password = "Osiris08";

// Define the STA SSID and password
#define STA_SSID "Your SSID"
#define STA_PASS "Yuor AP password"

IPAddress ap_ip(192, 168, 4, 1);
IPAddress ap_mask(255, 255, 255, 0);
IPAddress ap_leaseStart(192, 168, 4, 2);
IPAddress ap_dns(8, 8, 4, 4);

int numNetworks;
String scanResults[20];

// Display flush callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
    tft.pushColors(&color_p->full, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

// Touch callback
void my_touchpad_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {
    if (ts.tirqTouched()) {
        if (ts.touched()) {
            TS_Point p = ts.getPoint();
            data->point.x = map(p.x, 300, 3800, 0, 320);
            data->point.y = map(p.y, 300, 3800, 0, 240);
            data->state = LV_INDEV_STATE_PR;
        } else {
            data->state = LV_INDEV_STATE_REL;
        }
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}
// Wi-Fi connect function
void connectToWiFi() {
    lv_label_set_text(status_label, "Disconnected...");
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.printf("Attempting to connect to Wi-Fi SSID: %s with password: %s\n", ssid.c_str(), password.c_str());

    unsigned long startTime = millis();
    bool connected = false;

    while (millis() - startTime < 30000) {
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            connected = true;
            break;
        } else if (status == WL_CONNECT_FAILED) {
            lv_label_set_text(status_label, "Connection failed: Incorrect password");
            Serial.println("Connection failed: Incorrect password");
            return;
        } else {
            Serial.printf("Wi-Fi connection status: %d\n", status);
        }
        delay(500);
    }

    if (connected) {
        String ip = WiFi.localIP().toString();
        String successMsg = "Connected! IP: " + ip;
        lv_label_set_text(status_label, successMsg.c_str());
        Serial.println(successMsg);
    } else {
        lv_label_set_text(status_label, "Connection timed out.");
        Serial.println("Connection timed out.");
    }
}

// Event callback for keyboard input
void kb_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (ta != NULL) {
            String inputText = lv_textarea_get_text(ta);
            if (ta == ssid_ta) ssid = inputText.c_str();
            else if (ta == pass_ta) password = inputText.c_str();
        }
        lv_obj_del(kb);
        kb = NULL;
    }
}

// Event callback to display keyboard when textarea is focused
void ta_event_cb(lv_event_t *e) {
    if (kb == NULL) {
        kb = lv_keyboard_create(lv_scr_act());
        lv_keyboard_set_textarea(kb, lv_event_get_target(e));
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
    }
}
void setupDisplay() {
    lv_obj_t *ssid_label = lv_label_create(lv_scr_act());
    lv_label_set_text(ssid_label, "SSID:");
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 10, 10);

    ssid_ta = lv_textarea_create(lv_scr_act());
    lv_textarea_set_one_line(ssid_ta, true);
    lv_obj_align(ssid_ta, LV_ALIGN_TOP_LEFT, 60, 10);
    lv_obj_set_width(ssid_ta, 120);
    lv_obj_add_event_cb(ssid_ta, ta_event_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *pass_label = lv_label_create(lv_scr_act());
    lv_label_set_text(pass_label, "Password:");
    lv_obj_align(pass_label, LV_ALIGN_TOP_LEFT, 10, 50);

    pass_ta = lv_textarea_create(lv_scr_act());
    lv_textarea_set_password_mode(pass_ta, true);
    lv_textarea_set_one_line(pass_ta, true);
    lv_obj_align(pass_ta, LV_ALIGN_TOP_LEFT, 80, 50);
    lv_obj_set_width(pass_ta, 100);
    lv_obj_add_event_cb(pass_ta, ta_event_cb, LV_EVENT_FOCUSED, NULL);

    lv_obj_t *connect_btn = lv_btn_create(lv_scr_act());
    lv_obj_align(connect_btn, LV_ALIGN_TOP_RIGHT, -80, 10);
    lv_obj_t *label = lv_label_create(connect_btn);
    lv_label_set_text(label, "Connect");
    lv_obj_add_event_cb(connect_btn, [](lv_event_t * e) {
        connectToWiFi();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_btn = lv_btn_create(lv_scr_act());
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -80, 50);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan APs");
    lv_obj_add_event_cb(scan_btn, [](lv_event_t * e) {
        scanWiFi();
    }, LV_EVENT_CLICKED, NULL);

    ap_list = lv_list_create(lv_scr_act());
    lv_obj_set_size(ap_list, 140, 90);
    lv_obj_align(ap_list, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    status_label = lv_label_create(lv_scr_act());
 lv_label_set_text(status_label, "Written by VACC 2024\nSTA Disconnected\nPlease connect to AP"); // Set initial text

    lv_obj_align(status_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}
void setup() {
    Serial.begin(115200);
    Serial.setDebugOutput(true); // Enable debug output for detailed log
    SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI);
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

    setupDisplay();

    // Initialize WiFi in AP+STA mode
    WiFi.mode(WIFI_AP_STA);

    // Configure and start the AP
    WiFi.softAPConfig(ap_ip, ap_ip, ap_mask);
    WiFi.softAP(ap_ssid, ap_password);

    // Connect to the specified STA network
    WiFi.begin(STA_SSID, STA_PASS);

    // Register the WiFi event handler
    WiFi.onEvent(onEvent);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("STA Started");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("STA Connected");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.println("STA Got IP");
            Serial.println(WiFi.localIP());
            WiFi.softAP(ap_ssid, ap_password);
            WiFi.AP.enableNAPT(true);  // Enable NAT
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            Serial.println("STA Lost IP");
            WiFi.AP.enableNAPT(false);  // Disable NAT
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("STA Disconnected");
            WiFi.AP.enableNAPT(false);  // Disable NAT
            break;
        case ARDUINO_EVENT_WIFI_AP_START:
            Serial.println("AP Started");
            Serial.println(WiFi.softAPIP());
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.println("AP STA Connected");
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.println("AP STA Disconnected");
            break;
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            Serial.print("AP STA IP Assigned: ");
            Serial.println(IPAddress(info.wifi_ap_staipassigned.ip.addr));
            break;
        case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
            Serial.println("AP Probe Request Received");
            break;
        case ARDUINO_EVENT_WIFI_AP_STOP:
            Serial.println("AP Stopped");
            break;
        default:
            break;
    }
}
void scanWiFi() {
    numNetworks = WiFi.scanNetworks();
    lv_obj_clean(ap_list);  // Clear previous scan results
    for (int i = 0; i < numNetworks && i < 20; i++) {
        scanResults[i] = WiFi.SSID(i);
        lv_obj_t *btn = lv_list_add_btn(ap_list, LV_SYMBOL_WIFI, scanResults[i].c_str());
        lv_obj_add_event_cb(btn, [](lv_event_t * e) {
            ssid = lv_list_get_btn_text(ap_list, lv_event_get_target(e));
            lv_textarea_set_text(ssid_ta, ssid.c_str());
        }, LV_EVENT_CLICKED, NULL);
    }
    Serial.println("Wi-Fi scan complete.");
}
