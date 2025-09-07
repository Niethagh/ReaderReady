#ifndef READERAPI_H
#define READERAPI_H
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

#if defined(_WIN32)
#define READER_API declspec(dllexport)
#else
#define READER_API __attribute((visibility("default")))
#endif

namespace smartio {

struct ReaderError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class IsoProtocol { Auto, T0, T1 };

struct OpenParams {
    uint16_t vid = 0x072F;
    uint16_t pid = 0x9000;
    IsoProtocol protocol = IsoProtocol::Auto;
    bool detachKernelDriver = true;
    int interfaceHint = -1;
    unsigned ioTimeoutMs = 2000;
};

struct ReaderInfo {
    std::string name;
    uint16_t vid = 0, pid = 0;
    std::string backend;
    uint8_t bulkIn = 0, bulkOut = 0, intrIn = 0;
    bool hasInterrupt = false;
};

enum class CardPresence { NotPresent, PresentInactive, PresentActive, Unknown };

struct XfrResult {
    std::vector<uint8_t> data;
};

class ICardReader {
public:
    virtual ~ICardReader() = default;

    virtual void open(const OpenParams& params) = 0;
    virtual void close() = 0;
    virtual ReaderInfo info() const = 0;

    virtual CardPresence cardStatus() = 0;
    virtual std::vector<uint8_t> powerOn() = 0;
    virtual void powerOff() = 0;
    virtual bool waitCardEvent(unsigned timeoutMs) = 0;

    virtual XfrResult transmit(const std::vector<uint8_t>& capdu,
                               unsigned timeoutMs = 2000) = 0;

    virtual std::vector<uint8_t> vendorControl(const std::vector<uint8_t>& payload) = 0;
};

extern "C" {
READER_API ICardReader* create_reader();
READER_API void         destroy_reader(ICardReader*);
READER_API const char*  reader_library_version();
}

} // namespace smartio
#endif // READERAPI_H
