# Skyline: 8-Channel CV Sequencer

**Skyline** is a highly versatile, 8-channel CV sequencer for VCV Rack inspired by hardware sequencers like the Malekko Heavy Industry Voltage Block. Featuring independent sequence lengths, play directions, quantization, and real-time step locking per channel, Skyline provides extensive modulation and melodic control in a compact, mouse-friendly interface.

---

## Key Features

- **8 Independent Channels:** Every channel features its own CV output, sequence, playback direction, and scale quantization.
- **Continuous Live Recording:** Sliders continuously record live manual inputs directly into their respective channel's sequence.
- **Global Step-Lock:** Lock slider edits to a specific step across all 8 channels simultaneously for quick editing without needing complex physical hold gestures.
- **Comprehensive Quantization:** 16 distinct quantization scales available per channel, including standard, exotic, and pentatonic scales.
- **Smart Glide (Glide/Smooth):** Adaptive, tempo-synced glide that automatically adjusts based on the post-divided clock period.
- **16 Global Presets:** Quickly save and recall complete system states (all sequences, lengths, directions, scales, and clock divisions) into 16 non-volatile slots with custom animations.

---

## Inputs, Outputs & Global Controls

### Inputs & Outputs
- **CLOCK Input:** Clock source for advancing the sequencer. In **CV Mode**, this acts as an addressable CV input (0V to 10V) to select steps.
- **RESET / HLD Input:** In Clock/Slave modes, receiving a high gate resets all channels to step 1. In CV Mode, a high gate holds/freezes playback.
- **CV Outputs (1-8):** Dedicated CV outputs for each channel. Outputs are individually processed through the global Attenuate and Offset settings, as well as per-channel quantization.

### Global Panel Controls
- **Clock Mode Switch:** Three-way switch to determine how steps advance:
  - `CLK`: Traditional clock advance.
  - `CV`: Step position is addressed directly via CV input (0–10V maps to step 1–length).
  - `SLAVE`: Follows external synchronization.
- **OFFSET Knob:** Adds a global DC offset (from -5V to +5V) to all active, non-muted outputs.
- **ATTEN Knob:** Globally attenuates output levels (from 0% to 100%).
- **DIVIDE Knob:** Sets the clock division factor (1 to 16) for incoming clock pulses.

---

## The 6 Latched Modes

To edit advanced parameters, use the **six mode buttons** on the right side of the panel. These buttons are exclusive (only one can be active at a time) and feature color-coded RGB feedback:

### 1. 🟣 MUTE Mode (Purple)
- **Top Row Buttons (1–8):** Selects the target channel (`editChan`) and toggles that entire channel's mute state. 
- **Bottom Row Buttons (9–16):** Toggles individual step mutes for the currently selected channel.
- *Visual Indicator:* Muted steps or channels glow purple.

### 2. 🟢 LENGTH Mode (Green)
- **Top Row Buttons (1–8):** Selects the target channel (`editChan`).
- **Target Channel's Slider:** Adjusts the active sequence length (1 to 16 steps).
- *Visual Indicator:* Steps within the sequence length glow dim green; the end step glows bright green. Features a deadband safety threshold to prevent accidental shifts.

### 3. ⚪ SHIFT Mode (White)
- **Top Row Buttons (1–8):** Selects the target channel (`editChan`).
- **Bottom Row Buttons (9–16):** Perform immediate utility and direction actions for the selected channel:
  - **Button 9 (CLEAR):** Resets all CV values in the sequence to 0V.
  - **Button 10 (SMOOTH):** Toggles smart glide/glide state on the *current playing step*.
  - **Button 11 (RND):** Randomizes all CV step values on the selected channel.
  - **Button 12 (FREEZE):** Toggles playback freeze (keeps the active channel static at its current step).
  - **Button 13 (FWD):** Sets playback direction to Forward.
  - **Button 14 (REV):** Sets playback direction to Reverse.
  - **Button 15 (PEND):** Sets playback direction to Pendulum (ping-pong).
  - **Button 16 (RNDSEQ):** Sets playback direction to Random Step selection.

### 4. 🔵 SCALE Mode (Blue)
- **Top Row Buttons (1–8):** Selects the target channel (`editChan`).
- **Target Channel's Slider:** Selects one of the 16 quantization scales (see Scale Reference below).
- *Visual Indicator:* The corresponding step button (1–16) lights up to indicate the chosen scale index.

### 5. 🟡 SAVE Mode (Amber)
- Pressing any of the **16 Step Buttons** saves all sequences, lengths, scales, directions, and clock divisions to that slot.
- *Visual Indicator:* Slots containing valid presets glow dim amber; empty slots are unlit. When saved, a progressive amber sweep animation fills the step buttons left-to-right up to the saved slot.

### 6. 🩵 RECALL Mode (Cyan)
- Pressing any of the **16 Step Buttons** instantly recalls the saved preset slot.
- *Visual Indicator:* Occupied slots glow dim cyan. On selection, the active slot briefly flashes bright cyan.

---

## User Interface & Advanced Behaviors

### Navy-Blue Edit Ring & Highlight
- **Edit Ring:** Drawn behind the channel's output LED using NanoVG, a navy-blue glowing ring slowly pulses around the active `editChan` (the target for Mute, Length, Scale, or Shift edits).
- **SlimFader Track Highlight:** When in utility modes, the slider track for the active target channel gains a navy-blue background highlight, ensuring you always know which slider is active before making an adjustment.

### Global Step-Lock (No Mode Active)
In normal operation (when no latched mode is active), clicking any of the **16 Step Buttons** toggles **Global Step-Lock** for that specific step index:
- **Locked (Step Button Medium Red):** All 8 sliders will continuously write their values directly to that locked step on their respective channels, regardless of where the playhead is. This makes it incredibly easy to program specific steps manually.
- **Unlocked (Playhead Only):** Sliders write dynamically to their channel's active playhead (traditional live-recording).

### Smart Slider Capturing
- **Step Hold Window:** Moving a slider within 80ms after a clock pulse still applies the change to the step that *just* finished playing (`lastSeqPos`). This compensates for human timing latencies during manual live recording.
- **Deadband Prevention:** While in **Length** or **Scale** modes, the sliders use a deadband snapshot. You must move the slider past a threshold (`0.15V` delta) before it starts modifying parameters, preventing you from accidentally changing lengths or scales just by switching channels.

### Adaptive Glide (Smooth)
If a step has **SMOOTH** enabled, its output transitions gradually. Instead of a fixed rate, the transition time automatically scales to match about 90% of the active clock period (derived from your clock and divide settings, with a 250ms minimum floor). This ensures your glides sound proportional whether your clock is running at 30 BPM or 300 BPM.

---

## Quantizer Scale Reference

Skyline includes 16 quantization tables. When a channel is quantized, its 0–4V raw sequence range is mapped to the selected musical scale:

| Index | Scale Name | Semitones (within octave) | Size |
| :---: | :--- | :--- | :---: |
| **0** | Unquantized | Smooth voltage output | Continuous |
| **1** | Japanese (In) | 0, 1, 5, 7, 10 | 5 |
| **2** | Major Pentatonic | 0, 2, 4, 7, 9 | 5 |
| **3** | Minor Pentatonic | 0, 3, 5, 7, 10 | 5 |
| **4** | Blues | 0, 3, 5, 6, 7, 10 | 6 |
| **5** | Locrian | 0, 1, 3, 4, 6, 8, 10 | 7 |
| **6** | Arabian | 0, 2, 4, 5, 6, 8, 10 | 7 |
| **7** | Phrygian | 0, 1, 3, 5, 7, 8, 10 | 7 |
| **8** | Natural Minor | 0, 2, 3, 5, 7, 8, 10 | 7 |
| **9** | Dorian | 0, 2, 3, 5, 7, 9, 10 | 7 |
| **10** | Mixolydian | 0, 2, 4, 5, 7, 9, 10 | 7 |
| **11** | Persian | 0, 1, 4, 5, 7, 8, 11 | 7 |
| **12** | Double Harmonic | 0, 1, 4, 5, 7, 8, 11 | 7 |
| **13** | Major | 0, 2, 4, 5, 7, 9, 11 | 7 |
| **14** | Lydian | 0, 2, 4, 6, 7, 9, 11 | 7 |
| **15** | Chromatic | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 | 12 |
