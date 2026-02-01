/**
 * @file stopwatch.ino
 * @brief Arduino sketch for an ESP32-C3 project featuring a stopwatch, clock, and 3D animation.
 *
 * This project uses an SSD1306 OLED display, a DS1302 RTC module, and two buttons to provide
 * a multi-mode interface. Modes include a stopwatch, a real-time clock synchronized with the DS1302,
 * and a 3D starfield animation with a rotating ship model.
 *
 * @author Bogdanov Renat
 * @date 2026
 * @version 1.0
 */

// --- Libraries ---
#include <Arduino.h>
#include <U8g2lib.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <Wire.h>

// --- Pin Definitions for ESP32-C3 ---
// OLED Display (SSD1306 I2C)
#define OLED_SCL_PIN 9
#define OLED_SDA_PIN 8
// RTC Module (DS1302)
#define RTC_CLK_PIN 6
#define RTC_DAT_PIN 7
#define RTC_RST_PIN 4
// Buttons (INPUT_PULLUP, active LOW)
#define BTN_1_PIN 2
#define BTN_2_PIN 3

// --- Constants ---
static constexpr uint8_t DISPLAY_WIDTH = 128;
static constexpr uint8_t DISPLAY_HEIGHT = 64;
static constexpr uint8_t DEBOUNCE_MS = 30;
static constexpr uint8_t REFRESH_RATE_MS = 33; // ~30 FPS
static constexpr uint8_t NUM_STARS = 30;
static constexpr uint8_t NUM_SHIP_VERTICES = 5;
static constexpr uint8_t NUM_SHIP_EDGES = 8;

// --- Hardware Objects ---
// Display: SSD1306 128x64 I2C, rotated 180 degrees
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);

// RTC: DS1302 connected via ThreeWire library
ThreeWire rtcWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN);
RtcDS1302<ThreeWire> rtc(rtcWire);

// --- Application Logic ---

/**
 * @enum AppMode
 * @brief Represents the different operating modes of the device.
 */
enum class AppMode {
    STOPWATCH,
    CLOCK,
    ANIMATION
};

AppMode currentMode = AppMode::STOPWATCH; ///< Current application mode.

/**
 * @enum StopwatchState
 * @brief Represents the states of the stopwatch.
 */
enum class StopwatchState {
    STOPPED,
    RUNNING,
    PAUSED
};

StopwatchState swState = StopwatchState::STOPPED; ///< Current state of the stopwatch.
unsigned long swStartTime = 0;                    ///< Time when the stopwatch started running (millis).
unsigned long swElapsedTime = 0;                  ///< Total elapsed time when paused/stopped (millis).
unsigned long displayTime = 0;                    ///< Time displayed on the stopwatch screen (millis).

/**
 * @struct ButtonState
 * @brief Holds the debounced state of a button.
 */
struct ButtonState {
    int pin;
    bool lastState;
    unsigned long lastChange;
    bool isPressed;
    bool handled;
};

ButtonState btn1 = {BTN_1_PIN, HIGH, 0, false, false}; ///< State for button 1.
ButtonState btn2 = {BTN_2_PIN, HIGH, 0, false, false}; ///< State for button 2.

/**
 * @struct Star
 * @brief Represents a single star in the starfield animation.
 */
struct Star {
    float x, y, z;
};

Star stars[NUM_STARS]; ///< Array of stars for the animation.

/**
 * @brief Predefined vertices for the 3D ship model.
 * The ship is centered at the origin, pointing along the positive Z-axis.
 * Coordinates: {X (width), Y (height), Z (depth)}
 */
const float shipVertices[NUM_SHIP_VERTICES][3] = {
    {0.0f, 0.0f, 15.0f},   // 0. Nose
    {12.0f, 0.0f, -8.0f},  // 1. Right Wing
    {-12.0f, 0.0f, -8.0f}, // 2. Left Wing
    {0.0f, 4.0f, -8.0f},   // 3. Top of Cockpit
    {0.0f, -4.0f, -8.0f}   // 4. Bottom of Hull
};

/**
 * @brief Predefined edges connecting the ship's vertices.
 * Each edge is defined by two vertex indices.
 */
const int shipEdges[NUM_SHIP_EDGES][2] = {
    {0, 1}, {0, 2}, {0, 3}, {0, 4}, // Connections from Nose
    {1, 3}, {3, 2}, {2, 4}, {4, 1}  // Connections forming the body frame
};

// --- Function Prototypes ---
void readButtons();
void updateButtonState(ButtonState& btn);
void handleInputLogic();
void initializeStars();
void renderCurrentScreen();
void renderAnimation();
void renderStopwatch();
void renderClock();

// --- Setup and Loop ---

/**
 * @brief Arduino setup function. Initializes hardware and application state.
 */
void setup() {
    display.setBusClock(400000UL);
    display.begin();
    display.enableUTF8Print();

    rtc.Begin();
    if (!rtc.IsDateTimeValid()) {
        // Common issue with DS1302 power loss.
        // Consider setting a default time here or flagging an error.
        // For example: RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
        // rtc.SetDateTime(compiled);
    }

    pinMode(BTN_1_PIN, INPUT_PULLUP);
    pinMode(BTN_2_PIN, INPUT_PULLUP);

    initializeStars();
}

/**
 * @brief Arduino main loop. Handles input, logic, and display updates.
 */
void loop() {
    readButtons();
    handleInputLogic();

    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > REFRESH_RATE_MS) {
        lastDisplayUpdate = millis();
        display.clearBuffer();
        renderCurrentScreen();
        display.sendBuffer();
    }
}

// --- Core Logic Functions ---

/**
 * @brief Reads all buttons and updates their debounced states.
 */
void readButtons() {
    updateButtonState(btn1);
    updateButtonState(btn2);
}

/**
 * @brief Updates the state of a single button with debouncing.
 * @param btn Reference to the ButtonState struct to update.
 */
void updateButtonState(ButtonState& btn) {
    bool currentState = digitalRead(btn.pin);

    if (currentState != btn.lastState) {
        btn.lastChange = millis();
    }

    if ((millis() - btn.lastChange) > DEBOUNCE_MS) {
        if (currentState == LOW && !btn.isPressed) {
            btn.isPressed = true;
            btn.handled = false; // Mark as unhandled for action processing
        } else if (currentState == HIGH) {
            btn.isPressed = false;
        }
    }
    btn.lastState = currentState;
}

/**
 * @brief Processes button presses based on the current application mode.
 */
void handleInputLogic() {
    if (currentMode == AppMode::ANIMATION) {
        // Any button press in animation mode returns to clock
        if ((btn1.isPressed && !btn1.handled) || (btn2.isPressed && !btn2.handled)) {
            btn1.handled = true;
            btn2.handled = true;
            currentMode = AppMode::CLOCK;
        }
        return;
    }

    // Button 1: General action (Start/Pause Stopwatch, Cycle to Animation)
    if (btn1.isPressed && !btn1.handled) {
        btn1.handled = true;
        switch (currentMode) {
            case AppMode::STOPWATCH:
                if (swState == StopwatchState::STOPPED || swState == StopwatchState::PAUSED) {
                    swState = StopwatchState::RUNNING;
                    swStartTime = millis();
                } else if (swState == StopwatchState::RUNNING) {
                    swState = StopwatchState::PAUSED;
                    swElapsedTime += (millis() - swStartTime);
                }
                break;
            case AppMode::CLOCK:
                currentMode = AppMode::ANIMATION;
                break;
            default:
                break; // No action for other modes
        }
    }

    // Button 2: Reset/Back action
    if (btn2.isPressed && !btn2.handled) {
        btn2.handled = true;
        switch (currentMode) {
            case AppMode::STOPWATCH:
                if (swState == StopwatchState::STOPPED || swState == StopwatchState::PAUSED) {
                    swState = StopwatchState::STOPPED;
                    swElapsedTime = 0;
                    displayTime = 0; // Also reset the display time variable
                }
                // Fallthrough to mode switching logic for stopwatch
                [[fallthrough]];
            case AppMode::CLOCK:
                currentMode = (currentMode == AppMode::CLOCK) ? AppMode::STOPWATCH : AppMode::CLOCK;
                break;
            case AppMode::ANIMATION:
                currentMode = AppMode::CLOCK; // Explicitly go to clock from animation
                break;
        }
    }
}

// --- Animation & Rendering ---

/**
 * @brief Initializes the star positions for the starfield animation.
 */
void initializeStars() {
    for (uint8_t i = 0; i < NUM_STARS; ++i) {
        stars[i].x = random(-100, 100);
        stars[i].y = random(-100, 100);
        stars[i].z = random(1, 100); // Start at a random distance in front
    }
}

/**
 * @brief Dispatches rendering to the correct screen based on the current mode.
 */
void renderCurrentScreen() {
    switch (currentMode) {
        case AppMode::ANIMATION:
            renderAnimation();
            break;
        case AppMode::STOPWATCH:
            renderStopwatch();
            break;
        case AppMode::CLOCK:
            renderClock();
            break;
    }
}

/**
 * @brief Renders the 3D starfield and rotating ship animation.
 */
void renderAnimation() {
    const int centerX = DISPLAY_WIDTH / 2;
    const int centerY = DISPLAY_HEIGHT / 2;

    // --- Update and Draw Stars ---
    for (uint8_t i = 0; i < NUM_STARS; ++i) {
        stars[i].z -= 5.0f; // Move stars towards the viewer

        // Respawn star if it passes the viewer
        if (stars[i].z <= 0) {
            stars[i].x = random(-100, 100);
            stars[i].y = random(-100, 100);
            stars[i].z = 100.0f;
        }

        // Project 3D star position to 2D screen coordinates
        float screenX = (stars[i].x / stars[i].z) * 120.0f + centerX;
        float screenY = (stars[i].y / stars[i].z) * 120.0f + centerY;

        // Draw star if it's within the screen bounds
        if (screenX >= 0 && screenX < DISPLAY_WIDTH && screenY >= 0 && screenY < DISPLAY_HEIGHT) {
            display.drawPixel(static_cast<uint8_t>(screenX), static_cast<uint8_t>(screenY));
        }
    }

    // --- Update and Draw 3D Ship ---
    float t = millis() / 1000.0f; // Time in seconds for smooth animation

    // Calculate subtle rotation angles for a "floating" effect
    float rotX = sin(t * 1.1f) * 0.3f; // Pitch (Nose up/down)
    float rotY = sin(t * 0.7f) * 0.4f; // Yaw (Nose left/right)
    float rotZ = sin(t * 0.7f) * 0.2f; // Roll (Tilt)

    // Calculate subtle screen offset for the ship
    int shiftX = centerX + static_cast<int>(sin(t * 0.7f) * 15.0f);
    int shiftY = centerY + static_cast<int>(sin(t * 1.1f) * 8.0f);

    int projX[NUM_SHIP_VERTICES];
    int projY[NUM_SHIP_VERTICES];

    // Transform and project each vertex of the ship model
    for (uint8_t i = 0; i < NUM_SHIP_VERTICES; ++i) {
        float x = shipVertices[i][0];
        float y = shipVertices[i][1];
        float z = shipVertices[i][2];

        // Apply rotations: X (Pitch), then Y (Yaw), then Z (Roll)
        float y_rot_x = y * cos(rotX) - z * sin(rotX);
        float z_rot_x = y * sin(rotX) + z * cos(rotX);

        float x_rot_y = x * cos(rotY) - z_rot_x * sin(rotY);
        float z_rot_xy = x * sin(rotY) + z_rot_x * cos(rotY);

        float x_rot_z = x_rot_y * cos(rotZ) - y_rot_x * sin(rotZ);
        float y_rot_z = x_rot_y * sin(rotZ) + y_rot_x * cos(rotZ);

        // Apply perspective projection
        const float perspectiveDistance = 60.0f;
        const float perspectiveScaleFactor = 200.0f;
        float scale = perspectiveScaleFactor / (perspectiveDistance + z_rot_xy);

        projX[i] = shiftX + static_cast<int>(x_rot_z * scale);
        projY[i] = shiftY + static_cast<int>(y_rot_z * scale);
    }

    // Draw the wireframe of the ship
    display.setDrawColor(1); // Set pixel color to white
    for (uint8_t i = 0; i < NUM_SHIP_EDGES; ++i) {
        display.drawLine(projX[shipEdges[i][0]], projY[shipEdges[i][0]],
                         projX[shipEdges[i][1]], projY[shipEdges[i][1]]);
    }
}

/**
 * @brief Renders the stopwatch screen with elapsed time and status.
 */
void renderStopwatch() {
    // Calculate current display time based on state
    unsigned long currentTime = millis();
    if (swState == StopwatchState::RUNNING) {
        displayTime = (currentTime - swStartTime) + swElapsedTime;
    } else {
        displayTime = swElapsedTime;
    }

    // Format time into minutes, seconds, and centiseconds
    int mins = displayTime / 60000;
    int secs = (displayTime % 60000) / 1000;
    int cs = (displayTime % 1000) / 10; // Centiseconds

    // --- Draw Title ---
    display.setFont(u8g2_font_7x13_t_cyrillic);
    const char* title = "СЕКУНДОМЕР";
    int titleWidth = display.getUTF8Width(title);
    display.setCursor((DISPLAY_WIDTH - titleWidth) / 2, 12);
    display.print(title);
    display.drawLine(0, 14, DISPLAY_WIDTH, 14); // Underline title

    // --- Draw Time ---
    display.setFont(u8g2_font_logisoso20_tn);
    char timeBuffer[10];
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", mins, secs, cs);
    int timeWidth = display.getStrWidth(timeBuffer);
    display.setCursor((DISPLAY_WIDTH - timeWidth) / 2, 46); // Centered time
    display.print(timeBuffer);

    // --- Draw Status ---
    display.setFont(u8g2_font_6x13_t_cyrillic);
    display.setCursor(2, 62);
    switch (swState) {
        case StopwatchState::RUNNING:
            display.print("СТАРТ");
            // Flash a circle to indicate running
            if ((currentTime / 500) % 2) {
                display.drawDisc(120, 58, 3);
            }
            break;
        case StopwatchState::PAUSED:
            display.print("ПАУЗА");
            break;
        case StopwatchState::STOPPED:
            display.print("СБРОС");
            break;
    }
}

/**
 * @brief Renders the clock screen showing current time and date from the RTC.
 */
void renderClock() {
    RtcDateTime now = rtc.GetDateTime();

    // --- Draw Title ---
    display.setFont(u8g2_font_7x13_t_cyrillic);
    const char* title = "ЧАСЫ";
    int titleWidth = display.getUTF8Width(title);
    display.setCursor((DISPLAY_WIDTH - titleWidth) / 2, 12);
    display.print(title);
    display.drawLine(0, 14, DISPLAY_WIDTH, 14); // Underline title

    // --- Draw Time ---
    display.setFont(u8g2_font_logisoso20_tn);
    char timeBuffer[9]; // HH:MM:SS + null terminator
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", now.Hour(), now.Minute(), now.Second());
    int timeWidth = display.getStrWidth(timeBuffer);
    display.setCursor((DISPLAY_WIDTH - timeWidth) / 2, 46); // Centered time
    display.print(timeBuffer);

    // --- Draw Date ---
    display.setFont(u8g2_font_6x10_tn);
    char dateBuffer[12]; // DD-MM-YYYY + null terminator
    snprintf(dateBuffer, sizeof(dateBuffer), "%02d-%02d-%04d", now.Day(), now.Month(), now.Year());
    int dateWidth = display.getStrWidth(dateBuffer);
    display.setCursor((DISPLAY_WIDTH - dateWidth) / 2, 62);
    display.print(dateBuffer);
}