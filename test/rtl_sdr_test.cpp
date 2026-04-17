#include <hardware/rtl_sdr_device.h>
#include <iostream>

int main()
{
    RtlSdrDevice dev;
    if (!dev.open())
    {
        std::cerr << "Failed to open device\n";
        return 1;
    }
    dev.set_frequency(100000000); // 100 MHz
    auto samples = dev.read_samples(2048);
    std::cout << "Read " << samples.size() << " samples\n";
    std::cout << "First sample: " << samples[0].real() << "+" << samples[0].imag() << "i\n";
    return 0;
}