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
    static int boardPinToBcm (int boardPin)
    {
        switch (boardPin)
        {
            case 3:  return 2;
            case 5:  return 3;
            case 7:  return 4;
            case 8:  return 14;
            case 10: return 15;
            case 11: return 17;
            case 12: return 18;
            case 13: return 27;
            case 15: return 22;
            case 16: return 23;
            case 18: return 24;
            case 19: return 10;
            case 21: return 9;
            case 22: return 25;
            case 23: return 11;
            case 24: return 8;
            case 26: return 7;
            case 27: return 0;
            case 28: return 1;
            case 29: return 5;
            case 31: return 6;
            case 32: return 12;
            case 33: return 13;
            case 35: return 19;
            case 36: return 16;
            case 37: return 26;
            case 38: return 20;
            case 40: return 21;
            default: return -1;
        }
    }

    static int resolveGpioPin (int configuredPin, bool useBoardPinNumbers)
    {
        if (configuredPin < 0)
            return -1;

        if (! useBoardPinNumbers)
            return configuredPin;

        return boardPinToBcm (configuredPin);
    }

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
        if (pin < 0)
            return false;

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
    gpioLed1Resolved = resolveGpioPin (config.gpioLed1, config.useBoardPinNumbers);
    gpioLed2Resolved = resolveGpioPin (config.gpioLed2, config.useBoardPinNumbers);
    gpioLed3Resolved = resolveGpioPin (config.gpioLed3, config.useBoardPinNumbers);

    gpioFootswitch1Resolved = resolveGpioPin (config.gpioFootswitch1, config.useBoardPinNumbers);
    gpioFootswitch2Resolved = resolveGpioPin (config.gpioFootswitch2, config.useBoardPinNumbers);
    gpioFootswitch3Resolved = resolveGpioPin (config.gpioFootswitch3, config.useBoardPinNumbers);

    const auto gpioReady = configureGpioDirection (gpioFootswitch1Resolved, "in")
                        && configureGpioDirection (gpioFootswitch2Resolved, "in")
                        && configureGpioDirection (gpioFootswitch3Resolved, "in")
                        && configureGpioActiveLow (gpioFootswitch1Resolved, true)
                        && configureGpioActiveLow (gpioFootswitch2Resolved, true)
                        && configureGpioActiveLow (gpioFootswitch3Resolved, true)
                        && configureGpioDirection (gpioLed1Resolved, "out")
                        && configureGpioDirection (gpioLed2Resolved, "out")
                        && configureGpioDirection (gpioLed3Resolved, "out")
                        && writeGpioValue (gpioLed1Resolved, false)
                        && writeGpioValue (gpioLed2Resolved, false)
                        && writeGpioValue (gpioLed3Resolved, false);

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
    if (gpioLed1Resolved >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (gpioLed1Resolved)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (gpioLed1Resolved));
    }

    if (gpioLed2Resolved >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (gpioLed2Resolved)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (gpioLed2Resolved));
    }

    if (gpioLed3Resolved >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (gpioLed3Resolved)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (gpioLed3Resolved));
    }

    if (gpioFootswitch1Resolved >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (gpioFootswitch1Resolved)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (gpioFootswitch1Resolved));
    }

    if (gpioFootswitch2Resolved >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (gpioFootswitch2Resolved)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (gpioFootswitch2Resolved));
    }

    if (gpioFootswitch3Resolved >= 0)
    {
        if (juce::File (juce::String ("/sys/class/gpio/gpio") + juce::String (gpioFootswitch3Resolved)).exists())
            writeTextFile ("/sys/class/gpio/unexport", juce::String (gpioFootswitch3Resolved));
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

    state.footswitch1 = readGpioValue (gpioFootswitch1Resolved, false);
    state.footswitch2 = readGpioValue (gpioFootswitch2Resolved, false);
    state.footswitch3 = readGpioValue (gpioFootswitch3Resolved, false);

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
    const auto ok1 = writeGpioValue (gpioLed1Resolved, led1On);
    const auto ok2 = writeGpioValue (gpioLed2Resolved, led2On);
    const auto ok3 = writeGpioValue (gpioLed3Resolved, led3On);
    return ok1 && ok2 && ok3;
#else
    return true;
#endif
}
