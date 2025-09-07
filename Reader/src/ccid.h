#ifndef CCID_H
#define CCID_H
#pragma once
#include <vector>
#include <string>
#include <optional>
#include <cstdint>
#include <libusb-1.0/libusb.h>

struct UsbEndpoints {
    uint8_t bulkIn = 0;
    uint8_t bulkOut = 0;
    std::optional<uint8_t> intrIn;
    int interfaceNumber = -1;
};

struct DeviceSelector {
    uint16_t vid = 0x072F;
    uint16_t pid = 0x9000;
};

struct CcidResponse {
    uint8_t msgType = 0;
    std::vector<uint8_t> payload;
    uint8_t slot = 0;
    uint8_t status = 0;
    uint8_t error = 0;
    uint8_t chain = 0;
};

struct AcsResponse {
    uint8_t status = 0;                // 0x00 = OK
    std::vector<uint8_t> data;         // payload
};

enum class Proto { CCID, ACS };

class CcidReader {
public:
    CcidReader();
    ~CcidReader();

    void open(const DeviceSelector& sel);
    void close();

    std::string deviceInfo() const;
    bool hasInterrupt() const { return eps.intrIn.has_value(); }

    CcidResponse getSlotStatus(uint8_t slot = 0);
    CcidResponse powerOn(uint8_t slot = 0);
    CcidResponse powerOff(uint8_t slot = 0);
    CcidResponse xfrBlock(const std::vector<uint8_t>& apdu, uint8_t slot = 0);

    std::optional<std::vector<uint8_t>> readInterrupt(unsigned int timeoutMs = 2000);

private:
    libusb_context* ctx = nullptr;
    libusb_device_handle* handle = nullptr;
    UsbEndpoints eps{};
    uint32_t seq = 1;
    Proto proto = Proto::CCID;  // автоопределение по интерфейсу

    void findAndOpen(const DeviceSelector& sel);
    UsbEndpoints findCcidInterfaceAndEndpoints(libusb_device* dev);

    // CCID
    CcidResponse sendCcid(uint8_t msgType,
                          const std::vector<uint8_t>& data,
                          uint8_t slot, uint8_t seq);

    // ACS (legacy)
    AcsResponse sendAcs(uint8_t instruction, const std::vector<uint8_t>& data);
    AcsResponse acsGetStat();
    AcsResponse acsResetDefault();
    AcsResponse acsPowerOff();
    AcsResponse acsExchangeT0(const std::vector<uint8_t>& apdu);

    static uint32_t le32(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1])<<8) |
               (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
    }
};
#endif // CCID_H
