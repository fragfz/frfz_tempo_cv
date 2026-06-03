# frfz_tempo_cv

A LV2 plugin for MOD Dwarf that derives CV signals from host tempo information. Outputs frequency and time duration values based on musical note divisions, with configurable scaling modes.

## Features

- **4 CV Outputs:**
  - 2 frequency outputs (0-10V CV range, Hz-based)
  - 2 time duration outputs (0-10V CV range, ms-based)

- **Tempo Handling:**
  - Synchronizes with host transport BPM via LV2 time extension
  - Falls back to 120 BPM when sync disabled or host unavailable
  - Graceful handling of missing transport information

- **Time Divisions:**
  - 8 common musical divisions: 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T
  - Triplet support for swing/groove effects

- **Frequency Scaling:**
  - Linear and logarithmic scaling modes
  - Configurable min/max frequency range (0.01-20 Hz)
  - Proper logarithmic mapping for musical pitch relationships

- **Time Duration:**
  - Configurable min/max time range (0.1-20000 ms)
  - Useful for delay/reverb time modulation

## Port Layout

### Outputs
- **Frequency 1 (index 0):** Primary frequency output
- **Frequency 2 (index 1):** Secondary frequency (octave up)
- **Time 1 (index 2):** Primary time duration in ms
- **Time 2 (index 3):** Secondary time duration (2x)

### Control Inputs
- **Time Division (index 4):** Select note division (enum, default: 1/4)
- **Min Frequency (index 5):** Minimum frequency (Hz, default: 0.1)
- **Max Frequency (index 6):** Maximum frequency (Hz, default: 20.0)
- **Frequency Scaling (index 7):** Linear or Logarithmic (enum, default: Logarithmic)
- **Min Time (index 8):** Minimum time duration (ms, default: 1.0)
- **Max Time (index 9):** Maximum time duration (ms, default: 20000.0)
- **Host Sync (index 10):** Enable/disable host sync (toggle, default: On)
- **Control (index 11):** Atom sequence for time position

## Building

### Requirements
- LV2 SDK headers
- GCC/Clang compiler with C99 support
- GNU Make
- Math library (libm)

### Build Steps

```bash
# Clone the repository
git clone https://github.com/fragfz/frfz_tempo_cv.git
cd frfz_tempo_cv

# Build the plugin
make

# Install to system LV2 path (optional)
make install PREFIX=/usr/local
```

### For MOD Dwarf

```bash
# Copy build output to MOD Dwarf plugin directory
scp -r frfz_tempo_cv.lv2 root@modwarf:/usr/lib/lv2/
```

## Compilation Details

### Calculation Formula

**Period:** `period_seconds = (60 / BPM) * division_factor`

**Frequency:** `freq_hz = 1 / period_seconds`

**Time:** `time_ms = period_seconds * 1000`

**Logarithmic Frequency Scaling:**
```
output_cv = (log(freq_hz) - log(min_freq)) / (log(max_freq) - log(min_freq)) * 10.0
```

**Linear Frequency Scaling:**
```
output_cv = (freq_hz - min_freq) / (max_freq - min_freq) * 10.0
```

### Division Factors

| Division | Factor | Period at 120 BPM |
|----------|--------|-------------------|
| 1/1      | 4.0    | 2.0 s (0.5 Hz)    |
| 1/2      | 2.0    | 1.0 s (1.0 Hz)    |
| 1/4      | 1.0    | 0.5 s (2.0 Hz)    |
| 1/4T     | 0.666  | 0.333 s (3.0 Hz)  |
| 1/8      | 0.5    | 0.25 s (4.0 Hz)   |
| 1/8T     | 0.333  | 0.166 s (6.0 Hz)  |
| 1/16     | 0.25   | 0.125 s (8.0 Hz)  |
| 1/16T    | 0.166  | 0.083 s (12.0 Hz) |

## Implementation Notes

### Based on mod-cv-clock

This plugin follows the architecture and tempo handling of the MOD Audio `mod-cv-clock` reference implementation:
- Same URID mapping and time extension usage
- Identical BPM extraction via `time:beatsPerMinute`
- Hard-real-time safe (no allocations in run())
- Optional host sync with fallback behavior

### LV2 Compliance

- Uses LV2 time extension for host transport sync
- URID mapping for atom-based control
- CVPort designation for MOD Dwarf compatibility
- Optional hard real-time capability

### No Audio Processing

This is a control-rate only plugin:
- No audio input ports
- No audio output ports
- CV outputs computed once per run() invocation
- All 4 outputs hold constant values across sample buffer

## Assumptions and Limitations

### Assumptions
1. Host provides valid BPM via `time:beatsPerMinute` atom
2. Sample rate >= 48 kHz (typical MOD Dwarf rate)
3. CV outputs clamp within 0-10V range
4. Time outputs clamp within user-specified range

### Limitations
1. No real-time parameter ramping (control-rate only)
2. Frequency output is continuous, not quantized to MIDI notes
3. No polyphonic modulation (single BPM stream)
4. Time outputs are linear (no envelope shaping)
5. No audio sidechain or spectral analysis

### Known Behavior
- Sync disabled → Falls back to 120 BPM
- Invalid BPM (<1.0) → Assumed 120 BPM
- Division out of range → Clipped to nearest valid division
- Frequency clamped to 0.1-20 Hz range internally
- Time outputs can exceed user range if calculation result is outside bounds

## MOD Dwarf Compatibility

✓ Tested structure follows MOD plugin cookbook
✓ Uses mod:ControlVoltagePlugin designation  
✓ CV output ports properly marked with mod:CVPort
✓ Hard real-time safe (no blocking operations)
✓ Graceful fallback if host sync unavailable

## License

GPL v2 or later (matching MOD Audio ecosystem convention)

## Author

fragfz - https://github.com/fragfz

## References

- MOD Plugin Cookbook: https://github.com/mod-audio/mod-plugin-cookbook
- mod-cv-clock Reference: https://github.com/mod-audio/mod-cv-plugins/tree/master/source/mod-cv-clock
- LV2 Specification: https://lv2plug.in
