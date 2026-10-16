// Build: g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror scripts/controller-rumble.cpp -o /tmp/ronin-controller-rumble
// Probe only: /tmp/ronin-controller-rumble
// Explicit gentle pulse: /tmp/ronin-controller-rumble --rumble
// This diagnostic never reads input events or grabs an input device.

#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
volatile std::sig_atomic_t interrupted = 0;

void OnSignal(int signal)
{
    interrupted = signal;
}

bool SendEffect(int fd, int effectId, int value)
{
    input_event event{};
    event.type = EV_FF;
    event.code = static_cast<decltype(event.code)>(effectId);
    event.value = value;
    ssize_t written;
    do
    {
        written = ::write(fd, &event, sizeof(event));
    }
    while (written < 0 && errno == EINTR);
    return written == static_cast<ssize_t>(sizeof(event));
}

class Device
{
public:
    explicit Device(int descriptor) : fd(descriptor) {}
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    ~Device()
    {
        if (effectId >= 0)
        {
            if (!SendEffect(fd, effectId, 0))
                std::cerr << "Could not send effect stop: " << std::strerror(errno) << '\n';
            if (::ioctl(fd, EVIOCRMFF, effectId) < 0)
                std::cerr << "Could not remove effect: " << std::strerror(errno) << '\n';
        }
        if (fd >= 0) ::close(fd);
    }

    int fd = -1;
    int effectId = -1;
};

std::vector<std::string> FindXboxDevices()
{
    std::vector<std::string> result;
    std::error_code error;
    const std::filesystem::directory_iterator entries("/sys/class/input", error);
    if (error) return result;
    for (const auto& entry : entries)
    {
        const std::string filename = entry.path().filename().string();
        if (filename.rfind("event", 0) != 0) continue;
        std::ifstream nameFile(entry.path() / "device/name");
        std::string name;
        std::getline(nameFile, name);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        if (name.find("xbox") != std::string::npos)
            result.push_back("/dev/input/" + filename);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void Usage()
{
    std::cout << "Usage: ronin-controller-rumble [--device /dev/input/eventN] [--rumble]\n"
        "Without --rumble, only prints device identity and FF_RUMBLE support.\n"
        "With --rumble, plays one gentle 250 ms standard-rumble pulse, then stops\n"
        "and removes it. No input events are read and the device is never grabbed.\n";
}
} // namespace

int main(int argc, char** argv)
{
    bool rumble = false;
    std::string path;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h")
        {
            Usage();
            return 0;
        }
        if (argument == "--rumble") rumble = true;
        else if (argument == "--device" && index + 1 < argc) path = argv[++index];
        else
        {
            std::cerr << "Unknown or incomplete argument: " << argument << '\n';
            Usage();
            return 2;
        }
    }

    if (path.empty())
    {
        const auto devices = FindXboxDevices();
        if (devices.empty())
        {
            std::cerr << "No Xbox-named event device found in /sys/class/input.\n";
            return 2;
        }
        if (devices.size() > 1)
        {
            std::cerr << "Multiple Xbox event devices found; select one with --device:\n";
            for (const auto& device : devices) std::cerr << "  " << device << '\n';
            return 2;
        }
        path = devices.front();
    }
    const std::string prefix = "/dev/input/event";
    const auto eventNumber = path.substr(std::min(prefix.size(), path.size()));
    if (path.rfind(prefix, 0) != 0 || eventNumber.empty() ||
        !std::all_of(eventNumber.begin(), eventNumber.end(), [](unsigned char digit) { return digit >= '0' && digit <= '9'; }))
    {
        std::cerr << "--device must name /dev/input/eventN.\n";
        return 2;
    }

    Device device(::open(path.c_str(), (rumble ? O_RDWR : O_RDONLY) | O_NONBLOCK | O_CLOEXEC));
    if (device.fd < 0)
    {
        std::cerr << "Cannot open " << path << ": " << std::strerror(errno) << '\n';
        return 2;
    }
    std::array<char, 256> name{};
    input_id identity{};
    if (::ioctl(device.fd, EVIOCGNAME(name.size()), name.data()) < 0 ||
        ::ioctl(device.fd, EVIOCGID, &identity) < 0)
    {
        std::cerr << "Cannot query device identity: " << std::strerror(errno) << '\n';
        return 2;
    }
    name.back() = '\0';
    std::cout << path << ": " << name.data() << " (vendor 0x" << std::hex << identity.vendor
        << ", product 0x" << identity.product << std::dec << ")\n";

    constexpr std::size_t BitsPerWord = sizeof(unsigned long) * 8;
    std::array<unsigned long, (FF_MAX + BitsPerWord) / BitsPerWord> features{};
    if (::ioctl(device.fd, EVIOCGBIT(EV_FF, sizeof(features)), features.data()) < 0)
    {
        std::cerr << "Cannot query force-feedback capability: " << std::strerror(errno) << '\n';
        return 2;
    }
    const bool supported = (features[FF_RUMBLE / BitsPerWord] & (1UL << (FF_RUMBLE % BitsPerWord))) != 0;
    std::cout << "FF_RUMBLE: " << (supported ? "supported" : "not supported") << '\n';
    if (!supported) return 3;
    if (!rumble)
    {
        std::cout << "Probe complete; no effect uploaded or played. Use --rumble to request a pulse.\n";
        return 0;
    }

    struct sigaction handler{};
    handler.sa_handler = OnSignal;
    ::sigemptyset(&handler.sa_mask);
    for (int signal : {SIGINT, SIGTERM, SIGHUP})
    {
        if (::sigaction(signal, &handler, nullptr) < 0)
        {
            std::cerr << "Cannot install cleanup signal handler: " << std::strerror(errno) << '\n';
            return 2;
        }
    }

    ff_effect effect{};
    effect.type = FF_RUMBLE;
    effect.id = -1;
    effect.u.rumble.strong_magnitude = 0x2800; // About 16% of the standard channel range.
    effect.u.rumble.weak_magnitude = 0x1800; // About 9% of the standard channel range.
    effect.replay.length = 250;
    effect.replay.delay = 0;
    if (::ioctl(device.fd, EVIOCSFF, &effect) < 0)
    {
        std::cerr << "Cannot upload rumble effect: " << std::strerror(errno) << '\n';
        return 2;
    }
    device.effectId = effect.id;
    if (interrupted) return 128 + interrupted;
    if (!SendEffect(device.fd, device.effectId, 1))
    {
        std::cerr << "Cannot start rumble effect: " << std::strerror(errno) << '\n';
        return 2;
    }
    std::cout << "Playing gentle 250 ms pulse.\n" << std::flush;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(280);
    while (!interrupted && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (interrupted)
    {
        std::cout << "Interrupted; stopping and removing effect.\n";
        return 128 + interrupted;
    }
    std::cout << "Pulse finished; stopping and removing effect.\n";
    return 0;
}
