#ifndef ACR38USB_H
#define ACR38USB_H

#pragma once
#include "ReaderApi.h"
#include <optional>
#include <libusb-1.0/libusb.h>

namespace smartio {

class Acr38Usb final : public ICardReader {
public:
    Acr38Usb();
    ~Acr38Usb() override;

    void open(const OpenParams& params) override;
    void close() override;
    ReaderInfo info() const override;

    CardPresence cardStatus() override;
    std::vector<uint8_t> powerOn() override;
    void powerOff() override;
    bool waitCardEvent(unsigned timeoutMs) override;

    XfrResult transmit(const std::vector<uint8_t>& capdu,
                       unsigned timeoutMs) override;

    std::vector<uint8_t> vendorControl(const std::vector<uint8_t>& payload) override;

private:
    libusb_context* ctx_ = nullptr;
    libusb_device_handle* h_ = nullptr;
    int ifNum_ = -1;
    uint8_t epBulkIn_ = 0, epBulkOut_ = 0;
    std::optional<uint8_t> epIntrIn_;
    uint16_t vid_ = 0, pid_ = 0;

    enum class Backend { CCID, ACS } backend_ = Backend::CCID;
    IsoProtocol iso_ = IsoProtocol::Auto;

    unsigned ioTimeoutMs_ = 2000;
    uint32_t ccidSeq_ = 1;

    void findAndClaim(const OpenParams& p);
    void releaseIf();

    std::vector<uint8_t> ccidSend(uint8_t msgType,
                                  const std::vector<uint8_t>& data,
                                  uint8_t slot = 0,
                                  unsigned timeoutMs = 2000);

    std::vector<uint8_t> acsSend(uint8_t ins,
                                 const std::vector<uint8_t>& data,
                                 unsigned timeoutMs = 2000);


    static std::string libusbErr(int r);
};

} // namespace smartio

#endif // ACR38USB_H
