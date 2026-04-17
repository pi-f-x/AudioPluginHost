/*
  ==============================================================================

    GpioBackend.cpp
    Created: 28 Mar 2026 2:11:03pm
    Author:  motzi

  ==============================================================================
*/

#include "GpioBackend.h"

#include <cmath>
#include <fstream>

namespace
{
   #if JUCE_LINUX
    static bool writeTextFile (const juce::String& path, const juce::String& value)
    {
        std::ofstream stream (path.toRawUTF8());

        if (! stream.is_open())
            return false;

        stream << value.toRawUTF8() << '\n';
        return stream.good();
    }

    static juce::String readTextFile (const juce::String& path)
    {
        std::ifstream stream (path.toRawUTF8());

        if (! stream.is_open())
            return {};

        std::string value;
        std::getline (stream, value);
        return juce::String (value);
    }

    static bool ensureGpioExported (int pin)
    {
        const auto gpioPath = juce::String ("/sys/class/gpio/gpio") + juce::String (pin);

        if (juce::File (gpioPath).exists())
            return true;

        if (! writeTextFile ("/sys/class/gpio/export", juce::String (pin)))
            return false;

        for (int i = 0; i < 20; ++i)
        {
            if (juce::File (gpioPath).exists())
                return true;

            juce::Thread::sleep (1);
        }

        return juce::File (gpioPath).exists();
    }

    static bool configureGpioDirection (int pin, const juce::String& direction)
    {
        if (! ensureGpioExported (pin))
            return false;

        return writeTextFile (juce::String ("/sys/class/gpio/gpio") + juce::String (pin) + "/direction",
                              direction);
    }

    static bool configureGpioActiveLow (int pin, bool activeLow)
    {
        if (! ensureGpioExported (pin))
            return false;

        return writeTextFile (juce::String ("/sys/class/gpio/gpio") + juce::String (pin) + "/active_low",
                              activeLow ? "1" : "0");
    }

    static bool writeGpioValue (int pin, bool value)
    {
        if (! ensureGpioExported (pin))
            return false;

        return writeTextFile (juce::String ("/sys/class/gpio/gpio") + juce::String (pin) + "/value",
                              value ? "1" : "0");
    }

    static bool readGpioValue (int pin, bool fallback)
    {
        if (! ensureGpioExported (pin))
            return fallback;

        const auto text = readTextFile (juce::String ("/sys/class/gpio/gpio") + juce::String (pin) + "/value");

        if (text.isEmpty())
            return fallback;

        return text.trim().getIntValue() != 0;
    }
   #endif
}

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

GpioBackend::GpioBackend()
    : GpioBackend (Config {})
{
}

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

#if JUCE_LINUX
    const auto gpioReady = configureGpioDirection (config.gpioFootswitch1, "in")
                        && configureGpioDirection (config.gpioFootswitch2, "in")
                        && configureGpioDirection (config.gpioFootswitch3, "in")
                        && configureGpioActiveLow (config.gpioFootswitch1, true)
                        && configureGpioActiveLow (config.gpioFootswitch2, true)
                        && configureGpioActiveLow (config.gpioFootswitch3, true)
                        && configureGpioDirection (config.gpioLed1, "out")
                        && configureGpioDirection (config.gpioLed2, "out")
                        && configureGpioDirection (config.gpioLed3, "out")
                        && writeGpioValue (config.gpioLed1, false)
                        && writeGpioValue (config.gpioLed2, false)
                        && writeGpioValue (config.gpioLed3, false);

   #if GPIO_BACKEND_HAS_I2C
    i2cFd = ::open (config.i2cDevice.toRawUTF8(), O_RDWR);
    if (i2cFd >= 0)
    {
        if (::ioctl (i2cFd, I2C_SLAVE, config.i2cAddress) >= 0)
            initialised = gpioReady;
        else
        {
            ::close (i2cFd);
            i2cFd = -1;
            initialised = gpioReady;
        }
    }
    else
    {
        initialised = gpioReady;
    }
   #else
    initialised = gpioReady;
   #endif
#else
    initialised = true;
#endif

    return initialised;
}

void GpioBackend::shutdown()
{
#if JUCE_LINUX
    if (config.gpioLed1 >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (config.gpioLed1)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (config.gpioLed1));
    }

    if (config.gpioLed2 >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (config.gpioLed2)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (config.gpioLed2));
    }

    if (config.gpioLed3 >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (config.gpioLed3)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (config.gpioLed3));
    }

    if (config.gpioFootswitch1 >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (config.gpioFootswitch1)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (config.gpioFootswitch1));
    }

    if (config.gpioFootswitch2 >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (config.gpioFootswitch2)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (config.gpioFootswitch2));
    }

    if (config.gpioFootswitch3 >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (config.gpioFootswitch3)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (config.gpioFootswitch3));
    }

   #if GPIO_BACKEND_HAS_I2C
    if (i2cFd >= 0)
    {
        ::close (i2cFd);
        i2cFd = -1;
    }
   #endif
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
            static_cast<uint8_t> (0xC5 | ((muxBits & 0x07) << 4)),
            0xE3
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

    state.footswitch1 = readGpioValue (config.gpioFootswitch1, false);
    state.footswitch2 = readGpioValue (config.gpioFootswitch2, false);
    state.footswitch3 = readGpioValue (config.gpioFootswitch3, false);

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

bool GpioBackend::setLedStatesImpl (bool led1On, bool led2On, bool led3On)
{
#if JUCE_LINUX
    const auto ok1 = writeGpioValue (config.gpioLed1, led1On);
    const auto ok2 = writeGpioValue (config.gpioLed2, led2On);
    const auto ok3 = writeGpioValue (config.gpioLed3, led3On);
    return ok1 && ok2 && ok3;
#else
    return true;
#endif
}
