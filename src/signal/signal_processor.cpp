// SPDX-License-Identifier: MIT

#include "signal_processor.h"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace openspectrum
{

    SignalProcessor::SignalProcessor(size_t fft_size)
        : m_fft_size(fft_size)
    {
        precompute_window(fft_size);
    }

    void SignalProcessor::precompute_window(size_t size)
    {
        m_fft_size = size;
        m_window_coeffs.resize(size);

        switch (m_window)
        {
        case WindowFunction::RECTANGLE:
            std::fill(m_window_coeffs.begin(), m_window_coeffs.end(), 1.0f);
            break;
        case WindowFunction::HANN:
            compute_hann();
            break;
        case WindowFunction::HAMMING:
            compute_hamming();
            break;
        case WindowFunction::BLACKMAN:
            compute_blackman();
            break;
        case WindowFunction::BLACKMAN_HARRIS:
            compute_blackman_harris();
            break;
        case WindowFunction::FLAT_TOP:
            compute_flat_top();
            break;
        }
    }

    void SignalProcessor::compute_hann()
    {
        const float pi = std::numbers::pi_v<float>;
        for (size_t i = 0; i < m_fft_size; ++i)
        {
            m_window_coeffs[i] = 0.5f * (1.0f - std::cos(2.0f * pi * static_cast<float>(i) / (m_fft_size - 1)));
        }
    }

    void SignalProcessor::compute_hamming()
    {
        const float pi = std::numbers::pi_v<float>;
        for (size_t i = 0; i < m_fft_size; ++i)
        {
            m_window_coeffs[i] = 0.54f - 0.46f * std::cos(2.0f * pi * static_cast<float>(i) / (m_fft_size - 1));
        }
    }

    void SignalProcessor::compute_blackman()
    {
        const float pi = std::numbers::pi_v<float>;
        const float a0 = 0.42659f;
        const float a1 = 0.49656f;
        const float a2 = 0.076849f;

        for (size_t i = 0; i < m_fft_size; ++i)
        {
            float n = static_cast<float>(i) / (m_fft_size - 1);
            m_window_coeffs[i] = a0 - a1 * std::cos(2.0f * pi * n) + a2 * std::cos(4.0f * pi * n);
        }
    }

    void SignalProcessor::compute_blackman_harris()
    {
        const float pi = std::numbers::pi_v<float>;
        const float a0 = 0.35875f;
        const float a1 = 0.48829f;
        const float a2 = 0.14128f;
        const float a3 = 0.01168f;

        for (size_t i = 0; i < m_fft_size; ++i)
        {
            float n = static_cast<float>(i) / (m_fft_size - 1);
            m_window_coeffs[i] = a0 - a1 * std::cos(2.0f * pi * n) +
                                 a2 * std::cos(4.0f * pi * n) - a3 * std::cos(6.0f * pi * n);
        }
    }

    void SignalProcessor::compute_flat_top()
    {
        const float pi = std::numbers::pi_v<float>;
        const float a0 = 1.0f;
        const float a1 = 1.93f;
        const float a2 = 1.29f;
        const float a3 = 0.388f;
        const float a4 = 0.032f;

        for (size_t i = 0; i < m_fft_size; ++i)
        {
            float n = static_cast<float>(i) / (m_fft_size - 1);
            m_window_coeffs[i] = a0 - a1 * std::cos(2.0f * pi * n) +
                                 a2 * std::cos(4.0f * pi * n) -
                                 a3 * std::cos(6.0f * pi * n) +
                                 a4 * std::cos(8.0f * pi * n);
        }
    }

    float SignalProcessor::get_window_coeff(size_t index) const
    {
        // Security: bounds-checked access
        if (index >= m_window_coeffs.size())
        {
            return 1.0f; // Fallback to rectangle window
        }
        return m_window_coeffs[index];
    }

    void SignalProcessor::apply_window(std::vector<std::complex<float>> &samples)
    {
        // Security: ensure samples size matches window size
        if (samples.size() != m_fft_size)
        {
            precompute_window(samples.size());
        }

        for (size_t i = 0; i < samples.size(); ++i)
        {
            float w = get_window_coeff(i);
            samples[i].real(samples[i].real() * w);
            samples[i].imag(samples[i].imag() * w);
        }
    }

    void SignalProcessor::remove_dc(std::vector<std::complex<float>> &samples)
    {
        float mean_real = 0.0f;
        float mean_imag = 0.0f;

        for (const auto &s : samples)
        {
            mean_real += s.real();
            mean_imag += s.imag();
        }

        mean_real /= static_cast<float>(samples.size());
        mean_imag /= static_cast<float>(samples.size());

        for (auto &s : samples)
        {
            s.real(s.real() - mean_real);
            s.imag(s.imag() - mean_imag);
        }
    }

} // namespace openspectrum