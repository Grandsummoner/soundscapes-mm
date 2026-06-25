# Skyline

Skyline is an 8-channel CV sequencer designed for **VCV Rack v2** and optimized to compile for the **4ms Metamodule**. Heavily inspired by Eurorack sequencers like the Malekko Voltage Block, it provides flexible, high-density modulation, built-in quantization, and preset management in a single 20HP panel.

The codebase is structured to compile natively on both desktop platforms and the Metamodule hardware with 100% layout and functionality parity.

---

## Features

* **8 Independent Channels:** Output up to 8 unique CV sequences concurrently, with dedicated physical outputs and visual edit targets.
* **16 Steps per Channel:** Program CV values, mute states, and glide settings independently for each step.
* **16 Quantizer Scales:** Quantize CV values to Major, Minor, Pentatonic, Locrian, Blues, and more (derived from the Voltage Block manual).
* **Mode-Based Editing:** Quickly adjust muting, loop lengths, scale quantizing, and global shifts using the tactile button interface.
* **Step Utilities:** Set clear, glide/smooth, randomization, loop freeze, and playback directions (Forward, Reverse, Pendulum, Random) per channel.
* **16 Save/Recall Slots:** Save complete sequencer states (CV, mutes, lengths, clock settings) and instantly recall them on the fly.
* **Dual-Target Compilation:** Preprocessor-separated widget dimensions allow a full-range 60px vertical fader travel on desktop VCV Rack, while preserving the original 40px visual constraints and memory requirements when compiled for the 4ms Metamodule hardware.

---

## Technical Specifications

* **Panel Width:** 20HP (300 px)
* **Panel Height:** 128.5mm (380 px)
* **Outputs:** 8 CV Outputs (`-5V` to `+10V` range, depending on Attenuate and Offset settings).
* **Inputs:** 1 Clock Input, 1 Reset / Hold Input.
* **Parameter Ranges:** Sliders output a base value of `0.0V` to `4.0V`.

---

## Interface & Controls

### Global Controls
* **Clock Mode Switch:** 
  * `CLK`: Advances the active sequence on clock triggers. 
  * `CV`: Directly addresses active steps dynamically using incoming CV (0–10V mapped to active sequence length).
  * `SLAVE`: Synchronizes sequence tracking.
* **Offset:** Adds global offset (`-5.0V` to `+5.0V`) to the selected channel outputs.
* **Attenuate:** Multiplies selected channel output CV between `0.0` and `1.0`.
* **Divide:** Configures clock division (divides incoming clock by 1 to 16).

### Functional Modes (Latching Buttons)
Activating a mode lets you edit parameters using the bottom 16 step buttons:

* **MUTE:** Buttons 1–8 mute the corresponding output channels. Buttons 9–16 mute individual steps for the selected channel.
* **LENGTH:** Sets loop lengths. Tap step buttons 1–16 to set the step length (e.g., tap step 8 to set an 8-step loop).
* **SCALE:** Selects quantization scales for the active channel (Major, Minor, Pentatonic, Locrian, etc.) matching step buttons 1–16.
* **SHIFT:** Activating Shift exposes secondary step functions:
  * **Step 8 (CLEAR):** Clears all steps on the current channel.
  * **Step 9 (SMOOTH):** Toggles portamento/glide on the current active step.
  * **Step 10 (RND):** Randomizes the step CV on the active channel.
  * **Step 11 (FREEZE):** Pauses/Freezes sequence advancement on the active channel.
  * **Step 12–15 (PLAYBACK DIRECTIONS):** Sets playback direction:
    * Step 12: Forward (`FWD`)
    * Step 13: Reverse (`REV`)
    * Step 14: Pendulum (`PEND`)
    * Step 15: Random Sequence (`RNDSEQ`)
* **SAVE / RECALL:** Saves or recalls the entire sequencer state into one of the 16 preset slots (assigned to step buttons 1–16).

---
h
   make dist
