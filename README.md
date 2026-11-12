# Pinewood Derby Race Display

A single-file, offline race manager and TV display. No install, no internet needed: just open `index.html` in Chrome/Edge/Firefox.

## Casting to a TV
1. Open `index.html` on the laptop and cast/mirror the screen to the TV (Chromecast, AirPlay, HDMI).
2. **Best setup (two windows):** open a second window on the TV display, go to `index.html#tv` (or click **📺 TV mode**) and press **Fullscreen**. Enter times in the first window; the TV window updates instantly. (Both windows must be in the same browser.)
3. Single-screen setup: switch to TV mode only when you want to show the board; press `Esc` to exit.

## Running a race day
1. **Setup**: rename/add classes (defaults: Lion, Tiger, Wolf, Bear, Webelos, Arrow of Light, X Class). Add cars with **+ Add car** or **+ Paste list of names** (car numbers auto-assign).
   Enter each car's weight in ounces in the **oz** box; cars over the **Max weight** (default 5 oz, editable at the top of Setup, blank = no limit) turn red and show OVERWEIGHT on the lane card.
   Click the 📷 box next to a car to add its photo: **Take photo** uses the laptop webcam, or **Choose file…** picks an image (e.g. a phone photo sent to the laptop). Photos are shrunk to about 480px and appear on the lane cards, the TV view and the standings.
2. Click **Build heats** per class. With 3 lanes, every car runs once in each lane (heat *i* puts cars *i*, *i+1*, *i+2* in lanes 1, 2, 3). Classes with fewer than 3 cars get short heats. **Shuffle & build** randomizes matchups.
3. **Race**: pick the class, type each lane's time and press `Enter` to jump to the next lane. `Enter` on the last lane saves the heat and loads the next one. Click any heat number to fix a result.
4. **Standings**: choose how to rank — **Average time**, **Total time**, **Heat points** (1st=1, 2nd=2, 3rd=3; lowest wins) or **Fastest run**. Ties are broken by the secondary measure; exact ties share a place. Cars that haven't finished all runs show dashed provisional places.
5. **★ Overall** shows each class leader and the fastest runs of the day.

## Data
Everything is saved in the browser (`localStorage`). Use **Setup → Export backup** after the event (or periodically) and **Import backup** to restore on another machine.
