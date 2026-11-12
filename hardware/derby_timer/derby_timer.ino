/*
 * Pinewood Derby timer: Arduino Uno / Nano (ATmega328P)
 *
 * - Releases the start gate (servo) on a button press or a serial command.
 * - Starts timing when the start-gate switch opens.
 * - Stops each lane's clock when its finish-line sensor is triggered.
 * - Shows each lane's time on a 4-digit TM1637 display, and its place on a
 *   WS2812 LED (gold / silver / bronze, red = did not finish).
 * - Reports results to the laptop over USB serial (see hardware/GUIDE.md).
 *
 * Libraries (Arduino IDE > Tools > Manage Libraries):
 *   "TM1637" by Avishay Orpaz   (provides TM1637Display.h)
 *   "Adafruit NeoPixel"
 *   Servo (built in)
 *
 * NOT TESTED ON REAL HARDWARE. See "Testing" in GUIDE.md before race day.
 */

#include <Arduino.h>
#include <Servo.h>
#include <TM1637Display.h>
#include <Adafruit_NeoPixel.h>

// ---------------------------------------------------------------- settings
// Pin map. D8..D11 MUST stay together: they are port B, read by one interrupt.
const uint8_t PIN_LANE1  = 8;    // finish sensor, lane 1   (PB0)
const uint8_t PIN_LANE2  = 9;    // finish sensor, lane 2   (PB1)
const uint8_t PIN_LANE3  = 10;   // finish sensor, lane 3   (PB2)
const uint8_t PIN_GATE   = 11;   // start-gate switch       (PB3)
const uint8_t PIN_SERVO  = 6;
const uint8_t PIN_BUTTON = 4;    // arm / release button (to GND)
const uint8_t PIN_PIXELS = 5;    // WS2812 data (3 LEDs, one per lane)
const uint8_t PIN_BUZZER = A4;   // optional piezo buzzer
const uint8_t PIN_D1_CLK = 2,  PIN_D1_DIO = 3;    // lane 1 display
const uint8_t PIN_D2_CLK = A0, PIN_D2_DIO = A1;   // lane 2 display
const uint8_t PIN_D3_CLK = A2, PIN_D3_DIO = A3;   // lane 3 display

// Sensor polarity. Break-beam receivers are usually HIGH when the beam is
// clear and LOW when it is broken. Flip if yours is the other way round.
const uint8_t BEAM_BROKEN_LEVEL = LOW;
// Gate switch wired to GND: pressed (closed) = LOW while the gate is shut,
// released = HIGH when the gate drops.
const uint8_t GATE_OPEN_LEVEL = HIGH;

const uint8_t SERVO_CLOSED_DEG = 90;   // adjust to your gate
const uint8_t SERVO_OPEN_DEG   = 20;

const uint32_t MIN_VALID_US   = 300000UL;   // ignore sensor blips in the first 0.3 s
const uint32_t GRACE_MS       = 3000UL;     // after the first finisher, wait this long for the rest
uint32_t       timeoutMs      = 10000UL;    // no finisher at all within this long: all DNF

const uint8_t  LANES = 3;

// ----------------------------------------------------------------- state
enum State : uint8_t { IDLE, ARMED, RUNNING, DONE };

volatile uint8_t  state = IDLE;
volatile uint8_t  lastPins = 0;
volatile uint32_t startUs = 0;
volatile uint32_t finishUs[LANES];      // elapsed microseconds per lane
volatile uint8_t  finishedMask = 0;     // bit i set when lane i has finished

const uint8_t GATE_BIT = 0x08;          // PB3

Servo gateServo;
bool servoAttached = false;
TM1637Display disp[LANES] = {
  TM1637Display(PIN_D1_CLK, PIN_D1_DIO),
  TM1637Display(PIN_D2_CLK, PIN_D2_DIO),
  TM1637Display(PIN_D3_CLK, PIN_D3_DIO),
};
Adafruit_NeoPixel pixels(LANES, PIN_PIXELS, NEO_GRB + NEO_KHZ800);

uint8_t  shownMask = 0;
uint32_t firstFinishMs = 0;
uint32_t runStartMs = 0;

// ------------------------------------------------------------------- ISR
static inline bool beamBroken(uint8_t pins, uint8_t bit) {
  return ((pins & bit) != 0) == (BEAM_BROKEN_LEVEL == HIGH);
}
static inline bool gateIsOpen(uint8_t pins) {
  return ((pins & GATE_BIT) != 0) == (GATE_OPEN_LEVEL == HIGH);
}

// Fires on any change of D8..D11. Keep it short: it only timestamps events.
ISR(PCINT0_vect) {
  uint32_t now = micros();
  uint8_t pins = PINB & 0x0F;
  uint8_t changed = pins ^ lastPins;
  lastPins = pins;

  if (state == ARMED) {
    if ((changed & GATE_BIT) && gateIsOpen(pins)) {
      startUs = now;
      state = RUNNING;
    }
  } else if (state == RUNNING) {
    uint32_t dt = now - startUs;
    for (uint8_t i = 0; i < LANES; i++) {
      uint8_t bit = 1 << i;
      if ((changed & bit) && beamBroken(pins, bit) && !(finishedMask & bit) && dt >= MIN_VALID_US) {
        finishUs[i] = dt;
        finishedMask |= bit;
      }
    }
  }
}

// --------------------------------------------------------------- helpers
// Print elapsed microseconds as seconds with 4 decimals, e.g. 3.1234
void printSeconds(uint32_t us) {
  uint32_t v = (us + 50) / 100;     // tenths of a millisecond, rounded
  Serial.print(v / 10000);
  Serial.print('.');
  uint16_t frac = v % 10000;
  if (frac < 1000) Serial.print('0');
  if (frac < 100)  Serial.print('0');
  if (frac < 10)   Serial.print('0');
  Serial.print(frac);
}

void showTime(uint8_t lane, uint32_t us) {
  if (us < 10000000UL) {
    uint32_t ms = (us + 500) / 1000;            // 3.123
    if (ms > 9999) ms = 9999;
    disp[lane].showNumberDecEx(ms, 0b10000000, true);
  } else {
    uint32_t cs = (us + 5000) / 10000;          // 12.34
    if (cs > 9999) cs = 9999;
    disp[lane].showNumberDecEx(cs, 0b01000000, true);
  }
}

void showDash(uint8_t lane) {
  const uint8_t seg[4] = {0x40, 0x40, 0x40, 0x40};
  disp[lane].setSegments(seg);
}

void showDnf(uint8_t lane) {
  const uint8_t seg[4] = {0x5E, 0x54, 0x71, 0x00};   // d n F
  disp[lane].setSegments(seg);
}

void setAllPixels(uint32_t colour) {
  for (uint8_t i = 0; i < LANES; i++) pixels.setPixelColor(i, colour);
  pixels.show();
}

void beep(uint16_t ms) {
  tone(PIN_BUZZER, 2000, ms);       // leave the buzzer unconnected if you don't have one
}

void moveServo(uint8_t deg) {
  if (!servoAttached) { gateServo.attach(PIN_SERVO); servoAttached = true; }
  gateServo.write(deg);
}

void releaseServo() {
  if (servoAttached) { gateServo.detach(); servoAttached = false; }
}

// Place of lane i among the lanes that finished: 1 + number strictly faster.
uint8_t placeOf(uint8_t i, uint8_t mask, const uint32_t t[]) {
  uint8_t place = 1;
  for (uint8_t j = 0; j < LANES; j++)
    if (j != i && (mask & (1 << j)) && t[j] < t[i]) place++;
  return place;
}

void clearDisplays() {
  for (uint8_t i = 0; i < LANES; i++) { disp[i].setBrightness(7); showDash(i); }
}

// ------------------------------------------------------------- operations
void doReset() {
  noInterrupts();
  state = IDLE;
  finishedMask = 0;
  interrupts();
  shownMask = 0;
  moveServo(SERVO_CLOSED_DEG);
  clearDisplays();
  setAllPixels(0);
  Serial.println(F("READY"));
}

void doArm() {
  if (state == ARMED || state == RUNNING) { Serial.println(F("ERR BUSY")); return; }

  moveServo(SERVO_CLOSED_DEG);
  delay(500);                                   // let the gate close
  uint8_t pins = PINB & 0x0F;
  if (gateIsOpen(pins)) { Serial.println(F("ERR GATE")); return; }
  for (uint8_t i = 0; i < LANES; i++) {
    if (beamBroken(pins, 1 << i)) { Serial.print(F("ERR BEAM ")); Serial.println(i + 1); return; }
  }

  noInterrupts();
  for (uint8_t i = 0; i < LANES; i++) finishUs[i] = 0;
  finishedMask = 0;
  lastPins = PINB & 0x0F;
  state = ARMED;
  interrupts();
  shownMask = 0;
  clearDisplays();
  setAllPixels(pixels.Color(0, 40, 0));         // green = ready
  beep(100);
  Serial.println(F("ARMED"));
}

void doRelease() {
  if (state != ARMED) { Serial.println(F("ERR NOTARMED")); return; }
  moveServo(SERVO_OPEN_DEG);                    // the gate switch opening starts the clock
  Serial.println(F("GO"));
}

void finishRace() {
  noInterrupts();
  state = DONE;
  uint8_t mask = finishedMask;
  uint32_t t[LANES];
  for (uint8_t i = 0; i < LANES; i++) t[i] = finishUs[i];
  interrupts();

  Serial.print(F("RESULT"));
  for (uint8_t i = 0; i < LANES; i++) {
    Serial.print(' ');
    if (mask & (1 << i)) {
      printSeconds(t[i]);
      showTime(i, t[i]);
    } else {
      Serial.print(F("DNF"));
      showDnf(i);
    }
  }
  Serial.println();

  // LEDs last, only now: NeoPixel updates briefly disable interrupts,
  // which would delay finish timestamps if done mid-race.
  for (uint8_t i = 0; i < LANES; i++) {
    uint32_t colour;
    if (!(mask & (1 << i))) colour = pixels.Color(60, 0, 0);               // DNF red
    else switch (placeOf(i, mask, t)) {
      case 1:  colour = pixels.Color(255, 170, 0);   break;                // gold
      case 2:  colour = pixels.Color(150, 150, 190); break;                // silver
      case 3:  colour = pixels.Color(190, 80, 10);   break;                // bronze
      default: colour = pixels.Color(40, 40, 40);    break;
    }
    pixels.setPixelColor(i, colour);
  }
  pixels.show();
  releaseServo();
  beep(300);
}

// Serial commands from the laptop, one per line.
void handleLine(char *line) {
  for (char *p = line; *p; p++) *p = toupper(*p);
  if (!strcmp(line, "ARM"))          doArm();
  else if (!strcmp(line, "RELEASE")) doRelease();
  else if (!strcmp(line, "RESET"))   doReset();
  else if (!strcmp(line, "PING"))    Serial.println(F("PONG"));
  else if (!strncmp(line, "TIMEOUT ", 8)) {
    long v = atol(line + 8);
    if (v >= 1000 && v <= 99000) { timeoutMs = v; Serial.println(F("OK")); }
    else Serial.println(F("ERR RANGE"));
  }
  else if (line[0]) Serial.println(F("ERR UNKNOWN"));
}

void pollSerial() {
  static char buf[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { buf[n] = 0; n = 0; handleLine(buf); }
    else if (n < sizeof(buf) - 1) buf[n++] = c;
  }
}

// Short press: arm (when idle or done) or release (when armed).
void pollButton() {
  static bool last = HIGH;
  static uint32_t changedAt = 0;
  bool now = digitalRead(PIN_BUTTON);
  if (now != last && millis() - changedAt > 40) {
    changedAt = millis();
    last = now;
    if (now == LOW) {
      if (state == ARMED) doRelease();
      else if (state == IDLE || state == DONE) doArm();
    }
  }
}

// ------------------------------------------------------------------ setup
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LANE1, INPUT_PULLUP);
  pinMode(PIN_LANE2, INPUT_PULLUP);
  pinMode(PIN_LANE3, INPUT_PULLUP);
  pinMode(PIN_GATE,  INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pixels.begin();

  lastPins = PINB & 0x0F;
  PCICR  |= (1 << PCIE0);        // enable pin-change interrupts for port B
  PCMSK0 |= 0x0F;                // D8..D11

  doReset();
}

// ------------------------------------------------------------------- loop
void loop() {
  pollSerial();
  pollButton();

  // Note when the clock started (by the gate switch, however the gate was opened).
  static uint8_t prevState = IDLE;
  if (state == RUNNING && prevState != RUNNING) runStartMs = millis();
  prevState = state;

  if (state == RUNNING) {
    noInterrupts();
    uint8_t mask = finishedMask;
    uint32_t t[LANES];
    for (uint8_t i = 0; i < LANES; i++) t[i] = finishUs[i];
    interrupts();

    // Show each lane's time the moment it finishes (TM1637 writes do not block interrupts).
    uint8_t fresh = mask & ~shownMask;
    for (uint8_t i = 0; i < LANES; i++) {
      if (fresh & (1 << i)) showTime(i, t[i]);
    }
    if (fresh && !shownMask) firstFinishMs = millis();
    shownMask |= fresh;

    bool allIn   = (mask == (1 << LANES) - 1);
    bool graceUp = mask && (millis() - firstFinishMs > GRACE_MS);
    bool timeUp  = (millis() - runStartMs > timeoutMs);
    if (allIn || graceUp || timeUp) finishRace();
  }
}
