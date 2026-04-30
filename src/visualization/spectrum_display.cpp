// SPDX-License-Identifier: MIT

#include "spectrum_display.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace openspectrum
{

    // --- SpectrumPalette Implementation ---

    SpectrumPalette::SpectrumPalette()
    {
        generate_jet_palette();
    }

    void SpectrumPalette::set_color_map(ColorMap map)
    {
        m_color_map = map;
        switch (map)
        {
        case ColorMap::JET:
            generate_jet_palette();
            break;
        case ColorMap::VIRIDIS:
            generate_viridis_palette();
            break;
        case ColorMap::HOT:
            generate_hot_palette();
            break;
        case ColorMap::GRAyscale:
            generate_grayscale_palette();
            break;
        case ColorMap::BLUE_RED:
            generate_blue_red_palette();
            break;
        }
    }

    void SpectrumPalette::generate_jet_palette()
    {
        for (size_t i = 0; i < PALETTE_SIZE; ++i)
        {
            float t = static_cast<float>(i) / (PALETTE_SIZE - 1);
            float r, g, b;

            if (t < 0.125f)
            {
                r = 0.0f;
                g = 0.0f;
                b = 0.5f + 4.0f * t;
            }
            else if (t < 0.375f)
            {
                r = 0.0f;
                g = 4.0f * (t - 0.125f);
                b = 1.0f;
            }
            else if (t < 0.625f)
            {
                r = 4.0f * (t - 0.375f);
                g = 1.0f;
                b = 1.0f - 4.0f * (t - 0.375f);
            }
            else if (t < 0.875f)
            {
                r = 1.0f;
                g = 1.0f - 4.0f * (t - 0.625f);
                b = 0.0f;
            }
            else
            {
                r = 1.0f - 4.0f * (t - 0.875f);
                g = 0.0f;
                b = 0.0f;
            }

            m_palette[i] = RgbColor(
                static_cast<uint8_t>(r * 255),
                static_cast<uint8_t>(g * 255),
                static_cast<uint8_t>(b * 255));
        }
    }

    void SpectrumPalette::generate_viridis_palette()
    {
        // Simplified viridis approximation
        for (size_t i = 0; i < PALETTE_SIZE; ++i)
        {
            float t = static_cast<float>(i) / (PALETTE_SIZE - 1);
            float r = std::clamp(0.267f + 0.329f * t + 1.453f * t * t - 1.099f * t * t * t, 0.0f, 1.0f);
            float g = std::clamp(0.005f + 1.404f * t - 0.598f * t * t + 0.189f * t * t * t, 0.0f, 1.0f);
            float b = std::clamp(0.329f + 1.509f * t - 2.814f * t * t + 1.976f * t * t * t, 0.0f, 1.0f);

            m_palette[i] = RgbColor(
                static_cast<uint8_t>(r * 255),
                static_cast<uint8_t>(g * 255),
                static_cast<uint8_t>(b * 255));
        }
    }

    void SpectrumPalette::generate_hot_palette()
    {
        for (size_t i = 0; i < PALETTE_SIZE; ++i)
        {
            float t = static_cast<float>(i) / (PALETTE_SIZE - 1);
            m_palette[i] = RgbColor(
                static_cast<uint8_t>(255 * t),
                static_cast<uint8_t>(255 * t * t),
                static_cast<uint8_t>(255 * t * t * t));
        }
    }

    void SpectrumPalette::generate_grayscale_palette()
    {
        for (size_t i = 0; i < PALETTE_SIZE; ++i)
        {
            uint8_t val = static_cast<uint8_t>(i);
            m_palette[i] = RgbColor(val, val, val);
        }
    }

    void SpectrumPalette::generate_blue_red_palette()
    {
        for (size_t i = 0; i < PALETTE_SIZE; ++i)
        {
            float t = static_cast<float>(i) / (PALETTE_SIZE - 1);
            m_palette[i] = RgbColor(
                static_cast<uint8_t>(255 * t),
                0,
                static_cast<uint8_t>(255 * (1 - t)));
        }
    }

    RgbColor SpectrumPalette::get_color(float db_value, float min_db, float max_db) const
    {
        // Clamp value to range
        float clamped = std::clamp(db_value, min_db, max_db);

        // Normalize to [0, 1]
        float t = (clamped - min_db) / (max_db - min_db + 1e-10f);

        // Get palette index (clamped to valid range)
        size_t index = static_cast<size_t>(t * (PALETTE_SIZE - 1));
        index = std::min(index, PALETTE_SIZE - 1);

        return m_palette[index];
    }

    // --- SpectrumDisplay Implementation ---

    SpectrumDisplay::SpectrumDisplay(size_t width, size_t height)
        : m_width(width), m_height(height),
          m_pixels(width * height * 4, 0) // RGBA: 4 bytes per pixel
    {
        clear();
    }

    void SpectrumDisplay::set_db_range(float min_db, float max_db)
    {
        m_min_db = std::min(min_db, max_db);
        m_max_db = std::max(min_db, max_db);
    }

    void SpectrumDisplay::clear()
    {
        std::fill(m_pixels.begin(), m_pixels.end(), 0);
    }

    void SpectrumDisplay::update_spectrum(const std::vector<float> &db_values,
                                          const std::vector<float> &freq_bins,
                                          float center_freq_hz, float sample_rate_hz)
    {
        m_center_freq_hz = center_freq_hz;
        m_sample_rate_hz = sample_rate_hz;
        m_spectrum_data = db_values;

        if (m_autoscale && !db_values.empty())
        {
            float min_val = *std::min_element(db_values.begin(), db_values.end());
            float max_val = *std::max_element(db_values.begin(), db_values.end());
            set_db_range(min_val - 5.0f, max_val + 5.0f); // Add 5dB margin
        }

        render();
    }

    void SpectrumDisplay::render()
    {
        if (m_spectrum_data.empty())
        {
            clear();
            return;
        }

        // Clear background (black)
        clear();

        // Draw spectrum line or filled area
        const size_t num_bins = m_spectrum_data.size();
        const float bin_width = static_cast<float>(m_width) / num_bins;

        // Draw filled spectrum (from bottom to spectrum line)
        const float db_range = m_max_db - m_min_db;
        const float db_to_height = static_cast<float>(m_height) / db_range;

        for (size_t i = 0; i < num_bins; ++i)
        {
            float db = m_spectrum_data[i];
            // Invert Y-axis: higher dB = higher on screen
            float height = (db - m_min_db) * db_to_height;
            height = std::clamp(height, 0.0f, static_cast<float>(m_height));

            // Get color for this dB value
            auto color = m_palette.get_color(db, m_min_db, m_max_db);

            // Fill from bottom to spectrum line
            size_t x = static_cast<size_t>(i * bin_width);
            size_t next_x = static_cast<size_t>((i + 1) * bin_width);
            if (next_x > m_width)
                next_x = m_width;

            size_t bottom_y = m_height - 1;
            size_t top_y = static_cast<size_t>(m_height - height);

            if (top_y >= m_height)
                top_y = m_height - 1;

            // Fill vertical column
            for (size_t y = top_y; y <= bottom_y; ++y)
            {
                size_t idx = (y * m_width + x) * 4;
                if (idx + 4 <= m_pixels.size())
                {
                    m_pixels[idx] = color.red;
                    m_pixels[idx + 1] = color.green;
                    m_pixels[idx + 2] = color.blue;
                    m_pixels[idx + 3] = color.alpha;
                }
            }
        }
    }

} // namespace openspectrum