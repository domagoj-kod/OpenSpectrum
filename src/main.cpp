#include "hardware/rtl_sdr_device.h"
#include <kiss_fft.h>
#include <vector>
#include <complex>
#include <iostream>
#include <csignal>
#include <atomic>
#include <cmath>

static std::atomic<bool> g_running{true};

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
        std::cout << "\nCaught signal " << signum << ", shutting down...\n";
        g_running = false;
    }
}

int main()
{
    // Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 1. Open hardware
    RtlSdrDevice dev;
    if (!dev.open())
    {
        std::cerr << "Failed to open RTL-SDR device\n";
        return 1;
    }

    dev.set_sample_rate(2048000);
    dev.set_frequency(100000000); // 100 MHz

    // CRITICAL: flush USB buffer before first read
    dev.reset_buffer();

    std::cout << "Running — press Ctrl+C to stop\n";

    // 2. FFT setup
    constexpr size_t FFT_SIZE = 2048;
    kiss_fft_cfg cfg = kiss_fft_alloc(FFT_SIZE, 0, nullptr, nullptr);
    if (!cfg)
    {
        std::cerr << "Failed to allocate KissFFT\n";
        return 1;
    }

    std::vector<kiss_fft_cpx> in(FFT_SIZE), out(FFT_SIZE);

    // 3. Main loop
    while (g_running)
    {
        // Read samples — throws on hard failure
        std::vector<std::complex<float>> samples;
        try
        {
            samples = dev.read_samples(FFT_SIZE);
        }
        catch (const std::exception &e)
        {
            std::cerr << "Read error: " << e.what() << "\n";
            break;
        }

        // Fill FFT input (apply (-1)^n shift so DC ends up centered)
        for (size_t i = 0; i < FFT_SIZE; ++i)
        {
            float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            in[i].r = sign * samples[i].real();
            in[i].i = sign * samples[i].imag();
        }

        kiss_fft(cfg, in.data(), out.data());

        // Print magnitudes (dB) for first half of spectrum
        for (size_t i = 0; i < FFT_SIZE / 2; ++i)
        {
            float mag = std::hypot(out[i].r, out[i].i);
            float db = 20.0f * std::log10(mag + 1e-12f); // +epsilon avoids log(0)
            std::cout << i << "\t" << db << " dB\n";
        }
        std::cout << std::flush;
    }

    // 4. Cleanup
    kiss_fft_free(cfg);
    dev.close();
    std::cout << "Shutdown complete\n";
    return 0;
}