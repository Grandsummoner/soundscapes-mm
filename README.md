# Soundscape (WIP)

**Soundscape MM** is an 8-channel architectural control voltage (CV) sequencer, linear attenuator matrix, and multi-state modulation processor designed for high-performance deployment in both virtual environments (**VCV Rack**) and standalone embedded hardware hardware platform (**4ms MetaModule**).

Derived from dense performance paradigms, Soundscape establishes a strict flat-hierarchy interface layout. It completely eliminates menu-diving by implementing a 1:1 spatial vertical alignment matching steps, state displays, indicator LEDs, and active signal outputs.

---

## Key Architectural Enhancements

* **Transient VU-Meter Visual Feedback Layer:** To bridge the tactile gap between hardware manipulation and mouse interaction, tweaking any of the three top global Master Knobs (`SYNC/CLOCK`, `OFFSET/BIAS`, `RHYTHM/DENSITY`) instantly flips the 8 per-channel 7-segment displays from mode character states (`C`, `P`, `G`) into vertical, high-fidelity real-time bar graphs representing current channel attenuator voltages. The displays automatically time out and revert to mode states 500ms after the control stabilizes.
* **Ergonomic Layout Refactoring:** Vertical manual attenuation faders are isolated cleanly in an explicit performance grid, with diagnostic indicator LEDs positioned directly on top of the physical CV output jacks for explicit signal tracing.
* **Flat Core UI Logic Tree:** Replaces matrix selection interfaces with dedicated hardware controls. Features a global 3-way toggle switch for immediate operational mode transitions (`INT`, `EXT`, `MOD`) and a dedicated 2x3 macro button array for sequence utility management.
---
