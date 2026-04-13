/*
  ==============================================================================

    GpioBackend.cpp
    Created: 28 Mar 2026 2:11:03pm
    Author:  motzi

  ==============================================================================
*/

#include "GpioBackend.h"

#include <cmath>

#if JUCE_LINUX
 #include <fcntl.h>
 #include <unistd.h>
 #if __has_include(<linux/i2c-dev.h>)
  #include <linux/i2c-dev.h>
  #include <sys/ioctl.h>
  #define GPIO_BACKEND_HAS_I2C 1
 #else
  #define GPIO_BACKEND_HAS_I2C 0
 #endif
#endif

GpioBackend::GpioBackend (Config cfg)
    : config (std::move (cfg))
{
}

GpioBackend::~GpioBackend()
{
    shutdown();
}

bool GpioBackend::initialise()
{
    if (initialised)
        return true;

   #if JUCE_LINUX && GPIO_BACKEND_HAS_I2C
    i2cFd = ::open (config.i2cDevice.toRawUTF8(), O_RDWR);
    if (i2cFd >= 0)
    {
        if (::ioctl (i2cFd, I2C_SLAVE, config.i2cAddress) >= 0)
            initialised = true;
        else
        {
            ::close (i2cFd);
            i2cFd = -1;
        }
    }
   #else
    initialised = true;
   #endif

    return initialised;
}

void GpioBackend::shutdown()
{
   #if JUCE_LINUX && GPIO_BACKEND_HAS_I2C
    if (i2cFd >= 0)
    {
        ::close (i2cFd);
        i2cFd = -1;
    }
   #endif

    initialised = false;
}

bool GpioBackend::isInitialised() const
{
    return initialised;
}

bool GpioBackend::pollInputs (InputState& state)
{
    if (! initialised)
        return false;

    state.timestampSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    return pollInputsImpl (state);
}

bool GpioBackend::setLedStates (bool led1On, bool led2On, bool led3On)
{
    if (! initialised)
        return false;

    return setLedStatesImpl (led1On, led2On, led3On);
}

bool GpioBackend::pollInputsImpl (InputState& state)
{
   #if JUCE_LINUX && GPIO_BACKEND_HAS_I2C
    auto readAdcChannel = [this] (int muxBits, float fallback)
    {
        if (i2cFd < 0)
            return fallback;

        uint8_t configReg[3] =
        {
            0x01,
            static_cast<uint8_t> (0xC3 | ((muxBits & 0x07) << 4)),
            0x83
        };

        if (::write (i2cFd, configReg, 3) != 3)
            return fallback;

        juce::Thread::sleep (2);

        uint8_t ptrReg = 0x00;
        if (::write (i2cFd, &ptrReg, 1) != 1)
            return fallback;

        uint8_t data[2] = { 0, 0 };
        if (::read (i2cFd, data, 2) != 2)
            return fallback;

        const int16_t raw = static_cast<int16_t> ((data[0] << 8) | data[1]);
        const float normalised = static_cast<float> (raw + 32768) / 65535.0f;
        return juce::jlimit (0.0f, 1.0f, normalised);
    };

    state.poti1 = readAdcChannel (0x04, state.poti1);
    state.poti2 = readAdcChannel (0x05, state.poti2);

    state.footswitch1 = false;
    state.footswitch2 = false;
    state.footswitch3 = false;

    return true;
   #else
    const auto t = static_cast<float> (state.timestampSeconds);
    state.poti1 = 0.5f + 0.5f * std::sin (t * 0.25f);
    state.poti2 = 0.5f + 0.5f * std::sin (t * 0.17f + 1.1f);

    state.footswitch1 = (static_cast<int> (t) % 8) == 0;
    state.footswitch2 = (static_cast<int> (t) % 13) == 0;
    state.footswitch3 = (static_cast<int> (t) % 21) == 0;
    return true;
   #endif
}

bool GpioBackend::setLedStatesImpl (bool, bool, bool)
{
    return true;
}
