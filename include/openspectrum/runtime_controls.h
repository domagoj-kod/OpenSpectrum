// SPDX-License-Identifier: MIT
#pragma once

#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Forward declaration for RtlSdrDevice (defined in global namespace)
class RtlSdrDevice;

namespace openspectrum {

// Device constraints structure (adjustable per device type)
struct DeviceConstraints {
    uint32_t min_frequency_hz = 0;
    uint32_t max_frequency_hz = 1700000000;  // 1.7 GHz (RTL2832U: ~500kHz-1.7GHz)
    float min_gain_db = 0.0f;
    float max_gain_db = 49.6f;  // RTL2832U max
    std::vector<size_t> supported_fft_sizes = {512, 1024, 2048, 4096};
};

// Runtime control state with previous value tracking
class RuntimeControls {
public:
    RuntimeControls();
    ~RuntimeControls();

    // Update from keyboard event, returns true if values changed
    bool handle_keyboard(SDL_Keycode key, bool shift_held, bool ctrl_held);

    // Get formatted status string for display
    std::string get_status_string() const;

    // Get individual formatted values
    std::string format_frequency(uint32_t hz) const;
    std::string format_gain(float db) const;

    // Check if FFT needs reinitialization
    bool fft_size_changed() const;
    void clear_fft_change_flag();

    // Getters for current values
    uint32_t get_frequency() const { return frequency_hz; }
    float get_gain() const { return gain_db; }
    size_t get_fft_size() const { return fft_size; }



    // Setters (for initial configuration)
    void set_frequency(uint32_t hz);
    void set_gain(float db);
    void set_fft_size(size_t size);

    // Set constraints (for different device types)
    void set_constraints(const DeviceConstraints& constraints);
    const DeviceConstraints& get_constraints() const { return constraints; }

    // Check if reconfiguring (for display purposes)
    bool is_reconfiguring() const { return reconfiguring; }
    void set_reconfiguring(bool state) { reconfiguring = state; }

    // Status string caching
    bool status_changed() const { return m_status_dirty; }
    void clear_status_dirty() const { m_status_dirty = false; }

    // Apply all pending changes to device (batch update)
    void apply_to_device(RtlSdrDevice& dev);

private:
    DeviceConstraints constraints;

    // Current values
    uint32_t frequency_hz = 100000000;
    float gain_db = 20.0f;
    size_t fft_size = 4096;

    // Previous values (for logging)
    uint32_t frequency_prev = 100000000;
    float gain_prev = 20.0f;
    size_t fft_prev = 4096;

    // Step sizes (adjustable with modifiers)
    uint32_t freq_step = 1000000;    // 1 MHz default
    float gain_step = 1.0f;           // 1 dB default

    // Flags
    bool fft_changed = false;
    bool reconfiguring = false;

    // Status string caching for performance
    mutable std::string m_cached_status;
    mutable bool m_status_dirty = true;

    // Helper to find next FFT size index
    int find_fft_index(size_t size) const;
};

} // namespace openspectrum
