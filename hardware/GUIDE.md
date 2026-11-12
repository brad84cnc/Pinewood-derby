# Automating the track: Arduino timer, gate release and finish-line display

This guide covers an Arduino-based timer for a 3-lane Pinewood Derby track. It does three things:

1. **Releases the start gate** (servo) from a button on the box, or later from the laptop.
2. **Times each lane** with a finish-line sensor and starts the clock from a switch on the gate.
3. **Shows each lane's time and place at the finish line**: a 4-digit display per lane, plus a gold / silver / bronze LED for place.

It also sends the three times to the laptop over USB, so the race display app can fill them in for you.

> **Status: designed and simulated, not tested on real hardware.**
> The sketch (`derby_timer/derby_timer.ino`) was compiled and exercised against a software simulation of the Arduino. That covers the logic (timing, places, DNF, ties, serial commands). It has **not** been compiled with the real Arduino toolchain or run on a real board, sensors or track. Expect to adjust pin polarity, servo angles and sensor placement. Do the bench tests in section 7 before race day.
>
> The race display app (`index.html`) does **not** yet talk to the timer. For now, read the times off the finish-line display and type them into the app. Section 9 describes how the app would connect later.

---

## 1. How it fits together

```
 laptop (race display app)              TRACK
        ^                               +--------------------------------+
        | USB cable (serial)            |  START GATE                    |
        |                               |   servo pulls the gate pin     |
   +----+-----------------+             |   gate switch: opens => START  |
   |  Arduino Uno / Nano  |<------------+                                |
   |                      |             |   ...cars roll down the track  |
   |  button: ARM/RELEASE |             |                                |
   |                      |<------------+  FINISH LINE                   |
   +----+----+----+-------+   3 sensors |   [S1]   [S2]   [S3]  one per lane
        |    |    |                      +--------------------------------+
        |    |    |
        v    v    v
   [LANE 1] [LANE 2] [LANE 3]      each = 4-digit time display + one place LED
```

**Sequence of a heat**

1. Put the cars on the gate. Press the button (or send `ARM`). The Arduino closes the gate, checks the gate switch is closed and that no beam is blocked, turns the LEDs green and beeps. Displays show `----`.
2. Press the button again (or send `RELEASE`). The servo opens the gate.
3. The moment the **gate switch opens**, the clock starts. The Arduino timestamps the start in an interrupt, so the servo's own movement time is not counted.
4. As each car crosses the line, its lane's time is timestamped and shown on that lane's display immediately.
5. The heat ends when all three lanes have finished, or **3 seconds after the first finisher** (the remaining lanes show `dnF`), or 10 seconds after the start if nobody finishes.
6. Final result: each lane's LED shows its place (gold = 1st, silver = 2nd, bronze = 3rd, red = did not finish), the box beeps, and the results line goes to the laptop.

The LEDs are only updated at the very end of the heat on purpose: updating them briefly pauses the Arduino's interrupts, which could delay a finish timestamp by around 0.1 ms.

---

## 2. Parts list

Prices are rough (USD) and vary a lot between sellers.

| Part | Qty | Notes | ~Cost |
|---|---|---|---|
| Arduino Uno R3 (or Nano) | 1 | Must be an ATmega328P board. The sketch uses its port-B pin-change interrupt. | $10-25 |
| Finish-line sensor set | 3 | See section 4. IR break-beam pairs recommended. | $10-25 |
| Micro switch (lever type) or reed switch + magnet | 1 | Detects the gate opening. | $1-3 |
| Hobby servo (MG995 / MG996R class) | 1 | For the release. A small SG90 is only enough if your gate is very light. | $5-10 |
| TM1637 4-digit display modules | 3 | 0.56" modules are cheap and fine for testing. See the note on larger displays below. | $2-4 each |
| WS2812 / NeoPixel LEDs (strip of 3, or 3 "5 mm" LEDs) | 3 | One per lane, shows place colour. | $3-5 |
| Push button (momentary) | 1 | Arm / release. | $1 |
| Passive piezo buzzer | 1 | Optional. | $1 |
| 5 V, 2 A power supply (USB wall adapter + cable is fine) | 1 | Powers servo, displays and LEDs. The Arduino itself runs from the laptop's USB. | $5-10 |
| 330 ohm resistor, 1000 uF capacitor (6.3 V+) | 1 each | For the LED data line and supply smoothing. | $1 |
| Wire, breadboard or perfboard, project box, heat-shrink | | | $10 |
| USB cable (A-B for Uno) | 1 | Laptop connection. | $4 |

Roughly **$50-100 total**.

**A note on larger displays.** Spectators will want to read the time from across the room. Large (1.8"-2.3") 7-segment modules exist, but they come in many designs. **Only buy ones whose listing says it uses the TM1637 driver and runs at 5 V**, because the sketch drives TM1637 chips through the `TM1637` library. If a large module uses a different driver, the display code (`showTime`, `showDash`, `showDnf`) would need adjusting. Alternatively, put the display on a larger screen or TV via the laptop (see section 9).

---

## 3. Wiring

All grounds connect together (Arduino GND, external 5 V supply GND, sensors, switches). **Power the servo, displays and LEDs from the external 5 V supply, not the Arduino's 5 V pin.** Servos and bright displays can draw more than USB can supply, and a brown-out in the middle of a heat would corrupt a time.

### Pin map (must match the sketch exactly)

| Arduino pin | Connects to | Notes |
|---|---|---|
| **D8** | Lane 1 finish sensor output | Port B, pin-change interrupt |
| **D9** | Lane 2 finish sensor output | Port B, pin-change interrupt |
| **D10** | Lane 3 finish sensor output | Port B, pin-change interrupt |
| **D11** | Start-gate switch (other side to GND) | Port B. Closed (pressed) = gate shut |
| **D6** | Servo signal (orange/yellow wire) | Servo power from the external supply |
| **D4** | Arm / release button (other side to GND) | Uses the internal pull-up |
| **D5** | WS2812 data, through a 330 ohm resistor | LED power from the external supply |
| **D2 / D3** | Lane 1 display CLK / DIO | |
| **A0 / A1** | Lane 2 display CLK / DIO | |
| **A2 / A3** | Lane 3 display CLK / DIO | |
| **A4** | Piezo buzzer (other side to GND) | Optional |
| 5 V ext. | Servo (+), displays VCC, LED VCC, sensor emitters/receivers | Externally supplied |
| GND | Everything | Arduino GND tied to the external supply's GND |

**Do not move D8-D11 to other pins.** The sketch reads all four with one port read (`PINB`) inside a single interrupt, which is what keeps the timing tight.

Add the 1000 uF capacitor across the external supply's 5 V and GND near the LEDs and servo.

### Sensor and switch polarity

Two constants at the top of the sketch set polarity. Check them on the bench (section 7):

```cpp
const uint8_t BEAM_BROKEN_LEVEL = LOW;   // most break-beam receivers: HIGH = clear, LOW = broken
const uint8_t GATE_OPEN_LEVEL   = HIGH;  // gate switch to GND: pressed (LOW) = shut, released (HIGH) = open
```

### Release with a solenoid instead of a servo

If your gate uses a pull-solenoid: drive it through a **logic-level MOSFET** (or a relay board) from the external supply, **with a flyback diode across the solenoid coil**, never directly from an Arduino pin. In the sketch, replace the body of `moveServo()` with a `digitalWrite` to the MOSFET pin (HIGH = pulled = gate open).

---

## 4. Finish-line sensors

This is the part that needs the most care, and the right layout depends on **your** track.

**Recommended: a vertical beam, receiver in the track surface.**
- Drill a small hole at the finish line in each lane (in the middle of the lane, where the car's body passes, not under a wheel).
- Set an IR phototransistor/receiver flush in each hole, facing up. Run the wires underneath.
- Mount the matching IR emitters on a crossbar above the track, aimed straight down at each receiver.
- When a car passes, it blocks the beam and the receiver output flips.

**Alternative: a horizontal beam across the lane.** Emitters on one side, receivers on the other. This is easy to build but check two things: the beam must pass **above** the centre guide strip and **below** the car body, which can be very little room, and low cars or wheels-only contact may not break the beam reliably. Test with your actual cars.

**Choosing the sensors**
- Use sensors whose receiver includes a **38 kHz demodulator** (like a TV-remote receiver) paired with a modulated emitter. They ignore room lighting, sunlight and the TV. Plain photodiode/LDR setups are easily fooled by lighting. Check the product listing says "modulated" or "38 kHz".
- If you use a laser module, keep it **Class 1-2 (under 1 mW)** and never aim it where someone could look into it. IR is simpler and safer.
- Cheap reflective IR modules (the "obstacle avoidance" boards) can work from above but are fussier about car colour and height.

**Position matters for fairness.** All three sensors must sit on the **same line across the track** (use a straightedge). A sensor placed 1 cm further down the track adds a consistent error to that lane.

**Software protection against noise.** The sketch ignores sensor activity in the first 0.3 s after the start, and only the *first* trigger per lane per heat counts.

---

## 5. Start gate and release

- **Gate switch:** mount a lever micro switch so the closed gate presses it and the gate opening releases it. The clock starts on this release, so the switch must change state as the gate opens, not after a delay. Adjust the lever so it triggers early in the gate's travel.
- **Servo:** mount it so it pulls the gate's release pin or lever, and adjust the two angles in the sketch:

```cpp
const uint8_t SERVO_CLOSED_DEG = 90;   // gate shut
const uint8_t SERVO_OPEN_DEG   = 20;   // gate open
```

- **Mechanical release is more important than the electronics.** All three cars must leave at the same instant. If the gate drops unevenly, the sensors will faithfully time an unfair race. Watch a few releases in slow motion (a phone camera works).
- If your gate is released by hand, leave the servo unconnected. Pressing **ARM**, then opening the gate by hand, still starts the clock (the gate switch does it).

---

## 6. Software setup

1. Install the **Arduino IDE** (or `arduino-cli`).
2. In the IDE: **Tools > Manage Libraries**, install:
   - **TM1637** by Avishay Orpaz
   - **Adafruit NeoPixel**
   (The `Servo` library is built in.)
3. Open `hardware/derby_timer/derby_timer.ino`, select **Board: Arduino Uno** and your port, and upload.
4. Open **Serial Monitor** at **115200 baud**, line ending **Newline**. You should see `READY`.

---

## 7. Testing before race day

Do these in order. Each step catches a different kind of problem.

1. **Bench, no track.** In Serial Monitor type `PING`: expect `PONG`. Type `ARM`:
   - `ERR GATE` means the gate switch reads open. Close/press it or flip `GATE_OPEN_LEVEL`.
   - `ERR BEAM 2` means lane 2's sensor reads blocked. Clear it or flip `BEAM_BROKEN_LEVEL`.
   - `ARMED` means all good: LEDs turn green, displays show `----`.
2. **Switch and sensors.** After `ARMED`, type `RELEASE` (servo opens), then open the gate switch by hand. Wave an object through each sensor in turn. Each lane's display should show a time and the result line should appear, e.g. `RESULT 2.5000 DNF 2.9000`.
3. **Place LEDs.** Run a few fake races triggering lanes in different orders. Check 1st = gold, 2nd = silver, 3rd = bronze, a lane that never triggers = red `dnF`.
4. **Wrong-way polarity.** If a heat ends instantly or never starts, polarity is flipped. Fix the two constants.
5. **Dry run on the track** with a real car (or a weighted block): about 10 runs per lane. Times should be consistent (typically within a few thousandths of a second for the same car).
6. **Lane-to-lane check.** Run the **same car** down each of the three lanes several times. If one lane is consistently faster or slower than the others, check the sensor alignment and gate release before trusting results.
7. **Interference check.** Test with the room lights, the TV on, and any sunlight that will be there on the day.

---

## 8. Using it on race day

- Place the cars, press **ARM**, check green LEDs, press again to **release**.
- The finish-line display shows each lane's time as it finishes, then the place colours.
- Type the times into the race display app (until the app is connected to the timer, section 9).
- Press **ARM** again for the next heat. The displays and LEDs reset.
- If a sensor misbehaves, the app's manual entry always works as a backup.

**Timing precision.** The Arduino counts in 4-microsecond steps and the results are shown to 0.0001 s, but real accuracy is set by the sensors and how they are mounted. A receiver's response time adds a small delay that is the same in every lane, so it affects all cars equally.

---

## 9. Serial protocol (for connecting the app later)

The Arduino talks over USB serial at **115200 baud**, one text line per message, ending in a newline.

**From the Arduino**

| Line | Meaning |
|---|---|
| `READY` | Powered up, or reset |
| `ARMED` | Armed: gate closed, beams clear |
| `GO` | Gate release commanded |
| `RESULT 2.5000 2.6012 DNF` | Heat finished: lane 1, 2, 3 times in seconds (4 decimals), `DNF` if the car didn't finish |
| `PONG` | Reply to `PING` |
| `OK` | Setting accepted |
| `ERR GATE` / `ERR BEAM <lane>` / `ERR BUSY` / `ERR NOTARMED` / `ERR RANGE` / `ERR UNKNOWN` | Command refused |

**To the Arduino**

| Line | Meaning |
|---|---|
| `ARM` | Close gate, check, and arm |
| `RELEASE` | Open the gate (must be armed) |
| `RESET` | Back to idle, gate closed, displays cleared |
| `PING` | Connectivity check |
| `TIMEOUT <ms>` | No-finisher timeout, 1000-99000 (default 10000) |

**What the app would do.** `index.html` would add a **Connect timer** button using the browser's **Web Serial** feature (Chrome and Edge only; not Firefox or Safari). When a `RESULT` line arrives, the app would fill the three lane boxes for the current heat. `DNF` already maps to the app's existing DNF entry. It would also add **Arm** / **Release** buttons and optional auto-save and advance. Manual entry would stay as the fallback. This is not built yet.

**Testing the protocol without hardware:** `python3 hardware/simulate_timer.py` behaves like the Arduino over stdin/stdout (`--selftest` runs built-in checks).

---

## 10. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `ERR GATE` on arm | Gate switch reads open: not pressed, wired wrong, or `GATE_OPEN_LEVEL` flipped |
| `ERR BEAM n` on arm | That lane's beam is blocked or misaligned, or `BEAM_BROKEN_LEVEL` is flipped |
| Clock never starts | Gate switch doesn't change state when the gate opens |
| A lane shows `dnF` though the car finished | Beam not aligned or too weak, car too low or too high to break it, or timed out |
| Times jump around between runs | Ambient light on unmodulated sensors, loose wiring, or a sloppy gate release |
| One lane always a bit slower/faster | Sensor not on the same line as the others |
| Displays flicker or reset | Power: use the external 5 V supply and add the capacitor |
| LEDs show wrong colours | Strip is RGB, not GRB: change `NEO_GRB` to `NEO_RGB` in the sketch |
| Servo twitches | Shared/weak power: separate supply, common ground |
