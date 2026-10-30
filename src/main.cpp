#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr uint8_t kHeader = 0xA5;
constexpr std::size_t kPacketSize = 6;
constexpr double kPi = 3.14159265358979323846;

volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

uint16_t crc16_ccitt_false(const uint8_t* data, std::size_t length) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0
                      ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                      : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::array<uint8_t, kPacketSize> make_packet(uint8_t red, uint8_t green, uint8_t blue) {
    std::array<uint8_t, kPacketSize> packet{
        kHeader, red, green, blue, 0, 0,
    };
    const uint16_t crc = crc16_ccitt_false(packet.data(), 4);
    packet[4] = static_cast<uint8_t>(crc & 0xFFu);
    packet[5] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    return packet;
}

struct Options {
    std::string port;
    int baud = 115200;
    int base_red = 0;
    int base_green = 160;
    int base_blue = 255;
    double frequency_hz = 0.5;
    double frames_per_second = 50.0;
    double gamma = 1.0;
    double duration_seconds = 0.0;
    bool hold = false;
    bool test_colors = false;
    bool dry_run = false;
    bool quiet = false;
    bool list_ports = false;
};

void print_usage(const char* program) {
    std::printf(
        "UART RGB breathing-light sender\n"
        "\n"
        "Usage:\n"
        "  %s --list-ports\n"
        "  %s --dry-run [options]\n"
        "  %s --port COM3 [options]\n"
        "\n"
        "Options:\n"
        "  --port <name>       Serial port, for example COM3 or /dev/ttyUSB0\n"
        "  --baud <number>     Baud rate, default 115200\n"
        "  --color <R,G,B>     Peak/base color, default 0,160,255\n"
        "  --freq <Hz>         Breathing frequency, default 0.5\n"
        "  --fps <number>      Frames sent per second, default 50\n"
        "  --gamma <number>    Brightness gamma, default 1.0\n"
        "  --seconds <number>  Stop after this many seconds, 0 means forever\n"
        "  --hold              Send a fixed color without breathing\n"
        "  --test-colors       Cycle red, green, blue, white, and off\n"
        "  --dry-run           Print packets without opening a serial port\n"
        "  --quiet             Do not print periodic status\n"
        "  --list-ports        List serial ports and exit\n"
        "  --help              Show this message\n",
        program, program, program);
}

bool parse_int(const char* text, int& value) {
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < -2147483647L || parsed > 2147483647L)
        return false;
    value = static_cast<int>(parsed);
    return true;
}

bool parse_double(const char* text, double& value) {
    char* end = nullptr;
    const double parsed = std::strtod(text, &end);
    if (end == text || *end != '\0' || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool parse_color(const char* text, int& red, int& green, int& blue) {
    int parsed_r = 0;
    int parsed_g = 0;
    int parsed_b = 0;
    char trailing = '\0';
    if (std::sscanf(text, "%d,%d,%d%c", &parsed_r, &parsed_g, &parsed_b, &trailing) != 3)
        return false;
    if (parsed_r < 0 || parsed_r > 255 || parsed_g < 0 || parsed_g > 255 || parsed_b < 0 ||
        parsed_b > 255)
        return false;
    red = parsed_r;
    green = parsed_g;
    blue = parsed_b;
    return true;
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        std::string inline_value;
        const std::size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            inline_value = argument.substr(equals + 1);
            argument.resize(equals);
        }

        auto next_value = [&](const char* option_name) -> const char* {
            if (!inline_value.empty())
                return inline_value.c_str();
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", option_name);
                return nullptr;
            }
            return argv[++i];
        };

        if (argument == "--port") {
            const char* value = next_value("--port");
            if (value == nullptr) return false;
            options.port = value;
        } else if (argument == "--baud") {
            const char* value = next_value("--baud");
            if (value == nullptr || !parse_int(value, options.baud)) return false;
        } else if (argument == "--color") {
            const char* value = next_value("--color");
            if (value == nullptr ||
                !parse_color(value, options.base_red, options.base_green, options.base_blue))
                return false;
        } else if (argument == "--freq") {
            const char* value = next_value("--freq");
            if (value == nullptr || !parse_double(value, options.frequency_hz)) return false;
        } else if (argument == "--fps") {
            const char* value = next_value("--fps");
            if (value == nullptr || !parse_double(value, options.frames_per_second)) return false;
        } else if (argument == "--gamma") {
            const char* value = next_value("--gamma");
            if (value == nullptr || !parse_double(value, options.gamma)) return false;
        } else if (argument == "--seconds") {
            const char* value = next_value("--seconds");
            if (value == nullptr || !parse_double(value, options.duration_seconds)) return false;
        } else if (argument == "--hold") {
            options.hold = true;
        } else if (argument == "--test-colors") {
            options.test_colors = true;
        } else if (argument == "--dry-run") {
            options.dry_run = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--list-ports") {
            options.list_ports = true;
        } else if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
            return false;
        }
    }

    if (options.list_ports)
        return true;
    if (!options.dry_run && options.port.empty()) {
        std::fprintf(stderr, "--port is required unless --dry-run or --list-ports is used\n");
        return false;
    }
    if (options.baud <= 0) {
        std::fprintf(stderr, "--baud must be positive\n");
        return false;
    }
    if (options.frequency_hz <= 0.0 || options.frequency_hz > 10.0) {
        std::fprintf(stderr, "--freq must be in (0, 10]\n");
        return false;
    }
    if (options.frames_per_second <= 0.0 || options.frames_per_second > 200.0) {
        std::fprintf(stderr, "--fps must be in (0, 200]\n");
        return false;
    }
    if (options.gamma <= 0.0 || options.gamma > 5.0) {
        std::fprintf(stderr, "--gamma must be in (0, 5]\n");
        return false;
    }
    if (options.duration_seconds < 0.0) {
        std::fprintf(stderr, "--seconds cannot be negative\n");
        return false;
    }
    return true;
}

#ifdef _WIN32
std::vector<std::string> list_serial_ports() {
    std::vector<std::string> ports;
    for (int number = 1; number <= 256; ++number) {
        char device[32] = {0};
        char target[256] = {0};
        std::snprintf(device, sizeof(device), "COM%d", number);
        if (QueryDosDeviceA(device, target, sizeof(target)) != 0)
            ports.emplace_back(device);
    }
    return ports;
}
#else
std::vector<std::string> list_serial_ports() {
    return {};
}
#endif

class SerialPort {
public:
    SerialPort() = default;
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    ~SerialPort() {
        close();
    }

    bool open_port(const std::string& device, int baud) {
#ifdef _WIN32
        const std::string full_name = "\\\\.\\" + device;
        handle_ = CreateFileA(full_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            set_error("CreateFile failed");
            return false;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (GetCommState(handle_, &dcb) == 0) {
            set_error("GetCommState failed");
            return false;
        }
        dcb.BaudRate = static_cast<DWORD>(baud);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        if (SetCommState(handle_, &dcb) == 0) {
            set_error("SetCommState failed");
            return false;
        }

        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = 20;
        timeouts.ReadTotalTimeoutConstant = 20;
        timeouts.WriteTotalTimeoutConstant = 500;
        if (SetCommTimeouts(handle_, &timeouts) == 0) {
            set_error("SetCommTimeouts failed");
            return false;
        }
        return true;
#else
        fd_ = ::open(device.c_str(), O_WRONLY | O_NOCTTY);
        if (fd_ < 0) {
            set_error(std::strerror(errno));
            return false;
        }

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            set_error(std::strerror(errno));
            return false;
        }
        cfmakeraw(&tty);
        tty.c_cflag |= CLOCAL | CREAD;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CRTSCTS;

        speed_t speed = B115200;
        switch (baud) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:
            set_error("unsupported Unix baud rate");
            return false;
        }
        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);
        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            set_error(std::strerror(errno));
            return false;
        }
        return true;
#endif
    }

    bool write_packet(const std::array<uint8_t, kPacketSize>& packet) {
#ifdef _WIN32
        DWORD written = 0;
        if (WriteFile(handle_, packet.data(), static_cast<DWORD>(packet.size()), &written,
                      nullptr) == 0 ||
            written != packet.size()) {
            set_error("WriteFile failed");
            return false;
        }
        return true;
#else
        std::size_t offset = 0;
        while (offset < packet.size()) {
            const ssize_t written =
                ::write(fd_, packet.data() + offset, packet.size() - offset);
            if (written <= 0) {
                set_error(std::strerror(errno));
                return false;
            }
            offset += static_cast<std::size_t>(written);
        }
        return true;
#endif
    }

    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    const std::string& error() const {
        return error_;
    }

private:
    void set_error(const std::string& message) {
#ifdef _WIN32
        error_ = message + ", Windows error " + std::to_string(GetLastError());
#else
        error_ = message;
#endif
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    std::string error_;
};

struct Rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

Rgb calculate_color(const Options& options, double elapsed_seconds) {
    if (options.test_colors) {
        static constexpr Rgb kTestColors[] = {
            {255, 0, 0},
            {0, 255, 0},
            {0, 0, 255},
            {255, 255, 255},
            {0, 0, 0},
        };
        const std::size_t index =
            static_cast<std::size_t>(elapsed_seconds / 2.0) % std::size(kTestColors);
        return kTestColors[index];
    }

    double brightness = 1.0;
    if (!options.hold) {
        brightness =
            0.5 * (1.0 - std::cos(2.0 * kPi * options.frequency_hz * elapsed_seconds));
        if (options.gamma != 1.0)
            brightness = std::pow(brightness, options.gamma);
    }

    return Rgb{
        static_cast<uint8_t>(std::lround(options.base_red * brightness)),
        static_cast<uint8_t>(std::lround(options.base_green * brightness)),
        static_cast<uint8_t>(std::lround(options.base_blue * brightness)),
    };
}

void print_packet_status(double elapsed_seconds, const Rgb& color,
                         const std::array<uint8_t, kPacketSize>& packet) {
    std::printf("%8.3f s  RGB=(%3u,%3u,%3u)  %02X %02X %02X %02X %02X %02X\n",
                elapsed_seconds, static_cast<unsigned>(color.red),
                static_cast<unsigned>(color.green), static_cast<unsigned>(color.blue),
                packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        if (argc == 1)
            print_usage(argv[0]);
        return 1;
    }

    if (options.list_ports) {
#ifdef _WIN32
        const auto ports = list_serial_ports();
        if (ports.empty()) {
            std::printf("No COM ports found.\n");
        } else {
            for (const std::string& port : ports)
                std::printf("%s\n", port.c_str());
        }
#else
        std::printf("On Linux, try: ls /dev/ttyUSB* /dev/ttyACM* /dev/ttyTHS*\n");
#endif
        return 0;
    }

    std::signal(SIGINT, handle_signal);
#ifndef _WIN32
    std::signal(SIGTERM, handle_signal);
#endif

    SerialPort serial;
    if (!options.dry_run) {
        if (!serial.open_port(options.port, options.baud)) {
            std::fprintf(stderr, "cannot open %s: %s\n", options.port.c_str(),
                         serial.error().c_str());
            return 2;
        }
        std::printf("opened %s at %d 8N1\n", options.port.c_str(), options.baud);
    }

    const long long period_microseconds =
        static_cast<long long>(std::llround(1000000.0 / options.frames_per_second));
    const auto start = std::chrono::steady_clock::now();
    auto next_wake = start;
    long long scheduled_microseconds = 0;
    double next_status_seconds = 0.0;
    long long frames_sent = 0;

    while (!g_stop) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed_seconds =
            std::chrono::duration<double>(now - start).count();
        if (options.duration_seconds > 0.0 &&
            elapsed_seconds >= options.duration_seconds)
            break;

        const Rgb color = calculate_color(options, elapsed_seconds);
        const auto packet = make_packet(color.red, color.green, color.blue);

        if (!options.dry_run && !serial.write_packet(packet)) {
            std::fprintf(stderr, "serial write failed: %s\n", serial.error().c_str());
            return 3;
        }
        ++frames_sent;

        if (!options.quiet && elapsed_seconds >= next_status_seconds) {
            print_packet_status(elapsed_seconds, color, packet);
            next_status_seconds += 0.25;
        }

        scheduled_microseconds += period_microseconds;
        next_wake = start + std::chrono::microseconds(scheduled_microseconds);
        if (next_wake < std::chrono::steady_clock::now())
            next_wake = std::chrono::steady_clock::now() + std::chrono::microseconds(1);
        std::this_thread::sleep_until(next_wake);
    }

    if (!options.dry_run) {
        const auto off_packet = make_packet(0, 0, 0);
        serial.write_packet(off_packet);
        serial.close();
    }

    std::printf("stopped after %lld frames\n", frames_sent);
    return 0;
}
