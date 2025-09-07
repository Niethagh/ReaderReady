#include "acr38usb.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace smartio {
namespace {
constexpr uint8_t USB_CLASS_CCID = 0x0B;

constexpr uint8_t PC_to_RDR_IccPowerOn    = 0x62;
constexpr uint8_t PC_to_RDR_IccPowerOff   = 0x63;
constexpr uint8_t PC_to_RDR_GetSlotStatus = 0x65;
constexpr uint8_t PC_to_RDR_XfrBlock      = 0x6F;
constexpr uint8_t RDR_to_PC_DataBlock     = 0x80;
constexpr uint8_t RDR_to_PC_SlotStatus    = 0x81;

constexpr uint8_t ACS_HDR           = 0x01;
constexpr uint8_t ACS_GET_ACR_STAT  = 0x01;
constexpr uint8_t ACS_RESET_DEFAULT = 0x80;
constexpr uint8_t ACS_POWER_OFF     = 0x81;
constexpr uint8_t ACS_EXCHANGE_T0   = 0xA0;

static uint32_t le32(const uint8_t* p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

}

std::string Acr38Usb::libusbErr(int r){
    std::ostringstream os; os<<"libusb("<<r<<"): "<<libusb_error_name(r);
    return os.str();
}

Acr38Usb::Acr38Usb() {
    if (int r = libusb_init(&ctx_); r != 0) throw ReaderError(libusbErr(r));
    libusb_set_option(ctx_, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_NONE);
}

Acr38Usb::~Acr38Usb() {
    try { close(); } catch (...) {}
    if (ctx_) libusb_exit(ctx_);
}

void Acr38Usb::open(const OpenParams& p){
    if (h_) close();
    ioTimeoutMs_ = p.ioTimeoutMs;
    iso_ = p.protocol;
    findAndClaim(p);
}

void Acr38Usb::close(){
    releaseIf();
    if (h_) { libusb_close(h_); h_ = nullptr; }
    ifNum_ = -1; epBulkIn_ = epBulkOut_ = 0; epIntrIn_.reset();
}

ReaderInfo Acr38Usb::info() const {
    ReaderInfo i;
    i.vid = vid_; i.pid = pid_;
    i.bulkIn = epBulkIn_; i.bulkOut = epBulkOut_;
    i.hasInterrupt = epIntrIn_.has_value();
    i.intrIn = i.hasInterrupt ? *epIntrIn_ : 0;
    i.backend = (backend_ == Backend::CCID) ? "CCID" : "ACS";
    i.name = "ACR38 USB Reader";
    return i;
}

void Acr38Usb::findAndClaim(const OpenParams& p){
    vid_ = p.vid; pid_ = p.pid;

    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx_, &list);
    if (n < 0) throw ReaderError("libusb_get_device_list завершилась ошибкой");

    libusb_device* chosen = nullptr;
    libusb_device_descriptor dd{};
    libusb_config_descriptor* cfg = nullptr;

    for (ssize_t i=0;i<n && !chosen;++i){
        libusb_device* d = list[i];
        libusb_device_descriptor t{};
        if (libusb_get_device_descriptor(d,&t)==0 && t.idVendor==p.vid && t.idProduct==p.pid){
            if (libusb_get_active_config_descriptor(d, &cfg)!=0){
                if (libusb_get_config_descriptor(d, 0, &cfg)!=0) continue;
            }
            for (uint8_t ii=0; ii<cfg->bNumInterfaces; ++ii){
                const auto& alt = cfg->interface[ii];
                for (int a=0;a<alt.num_altsetting;++a){
                    const auto* ifd = &alt.altsetting[a];
                    uint8_t in=0, out=0; std::optional<uint8_t> intr;
                    for (uint8_t e=0;e<ifd->bNumEndpoints;++e){
                        const auto& ep = ifd->endpoint[e];
                        const uint8_t addr = ep.bEndpointAddress;
                        const uint8_t type = ep.bmAttributes & 0x03;
                        const bool dirIn = (addr & 0x80)!=0;
                        if (type==LIBUSB_TRANSFER_TYPE_BULK && dirIn)  in = addr;
                        if (type==LIBUSB_TRANSFER_TYPE_BULK && !dirIn) out = addr;
                        if (type==LIBUSB_TRANSFER_TYPE_INTERRUPT && dirIn) intr = addr;
                    }
                    if (in && out && (p.interfaceHint==-1 || p.interfaceHint==ifd->bInterfaceNumber)) {
                        epBulkIn_ = in; epBulkOut_ = out; epIntrIn_ = intr;
                        ifNum_ = ifd->bInterfaceNumber;
                        backend_ = (ifd->bInterfaceClass == USB_CLASS_CCID) ? Backend::CCID : Backend::ACS;
                        chosen = d; dd = t; break;
                    }
                }
            }
            libusb_free_config_descriptor(cfg); cfg = nullptr;
        }
    }
    libusb_free_device_list(list, 1);

    if (!chosen) throw ReaderError("ACR38: не найден интерфейс с парой Bulk IN/OUT");

    if (libusb_open(chosen, &h_) != 0) throw ReaderError("Не удалось открыть устройство (libusb_open)");
    if (p.detachKernelDriver && libusb_kernel_driver_active(h_, ifNum_)==1)
        libusb_detach_kernel_driver(h_, ifNum_);
    if (int r = libusb_claim_interface(h_, ifNum_); r != 0)
        throw ReaderError(std::string("Не удалось занять интерфейс: ") + libusbErr(r));

    vid_ = dd.idVendor; pid_ = dd.idProduct;
}

void Acr38Usb::releaseIf(){
    if (h_ && ifNum_>=0) {
        libusb_release_interface(h_, ifNum_);
    }
}

std::vector<uint8_t> Acr38Usb::ccidSend(uint8_t msgType,
                                        const std::vector<uint8_t>& data,
                                        uint8_t slot,
                                        unsigned timeoutMs)
{

    std::vector<uint8_t> out(10 + data.size());
    out[0] = msgType;
    const uint32_t L = (uint32_t)data.size();
    out[1] = (uint8_t)(L & 0xFF);
    out[2] = (uint8_t)((L>>8)&0xFF);
    out[3] = (uint8_t)((L>>16)&0xFF);
    out[4] = (uint8_t)((L>>24)&0xFF);
    out[5] = slot;
    out[6] = (uint8_t)(ccidSeq_++);
    out[7] = out[8] = out[9] = 0;
    if (!data.empty()) std::memcpy(out.data()+10, data.data(), data.size());

    int tr=0;
    int r = libusb_bulk_transfer(h_, epBulkOut_, out.data(), (int)out.size(), &tr, (int)timeoutMs);
    if (r!=0 || tr!=(int)out.size()) throw ReaderError(std::string("CCID: ошибка передачи Bulk OUT: ") + libusbErr(r));

    std::vector<uint8_t> buf; buf.reserve(1024);
    auto read_chunk = [&](int to)->int{
        uint8_t tmp[256]; int got=0;
        int rr = libusb_bulk_transfer(h_, epBulkIn_, tmp, (int)sizeof(tmp), &got, to);
        if (rr==LIBUSB_ERROR_TIMEOUT) return 0;
        if (rr!=0) throw ReaderError(std::string("CCID: ошибка приёма Bulk IN: ") + libusbErr(rr));
        buf.insert(buf.end(), tmp, tmp+got);
        return got;
    };
    for (int i=0; i<5 && buf.size()<10; ++i) read_chunk((int)timeoutMs);
    if (buf.size()<10) throw ReaderError("CCID: отсутствует/неполный заголовок");
    const uint32_t dwLen = le32(&buf[1]);
    const size_t need = 10u + (size_t)dwLen;
    while (buf.size()<need) {
        if (read_chunk((int)timeoutMs)==0) break;
    }
    if (buf.size()<need) throw ReaderError("CCID: не полный ответ");
    return buf;
}

std::vector<uint8_t> Acr38Usb::acsSend(uint8_t ins,
                                       const std::vector<uint8_t>& data,
                                       unsigned timeoutMs)
{

    const uint16_t N = (uint16_t)data.size();
    std::vector<uint8_t> out; out.reserve(4+N);
    out.push_back(ACS_HDR);
    out.push_back(ins);
    out.push_back(uint8_t((N>>8)&0xFF));
    out.push_back(uint8_t(N & 0xFF));
    out.insert(out.end(), data.begin(), data.end());

    int tr=0;
    int r = libusb_bulk_transfer(h_, epBulkOut_, out.data(), (int)out.size(), &tr, (int)timeoutMs);
    if (r!=0 || tr!=(int)out.size()) throw ReaderError(std::string("ACS: ошибка передачи Bulk OUT: ") + libusbErr(r));

    std::vector<uint8_t> buf; buf.reserve(512);
    auto read_chunk = [&](int to)->int{
        uint8_t tmp[256]; int got=0;
        int rr = libusb_bulk_transfer(h_, epBulkIn_, tmp, (int)sizeof(tmp), &got, to);
        if (rr==LIBUSB_ERROR_TIMEOUT) return 0;
        if (rr!=0) throw ReaderError(std::string("ACS: ошибка приёма Bulk IN: ") + libusbErr(rr));
        buf.insert(buf.end(), tmp, tmp+got);
        return got;
    };
    for (int i=0;i<5 && buf.size()<4;++i) read_chunk((int)timeoutMs);
    if (buf.size()<4 || buf[0]!=ACS_HDR) throw ReaderError("ACS: отсутствует/неполный заголовок");
    const uint8_t status = buf[1];
    const uint16_t len = (uint16_t(buf[2])<<8) | buf[3];
    while (buf.size() < size_t(4+len)) {
        if (read_chunk((int)timeoutMs)==0) break;
    }
    if (buf.size() < size_t(4+len)) throw ReaderError("ACS: неполный ответ устройства");

    std::vector<uint8_t> resp; resp.reserve(4+len);
    resp.push_back(ACS_HDR); resp.push_back(status);
    resp.push_back((uint8_t)((len>>8)&0xFF)); resp.push_back((uint8_t)(len&0xFF));
    resp.insert(resp.end(), buf.begin()+4, buf.begin()+4+len);
    return resp;
}

CardPresence Acr38Usb::cardStatus(){
    if (!h_) throw ReaderError("Ридер не открыт");
    if (backend_ == Backend::CCID) {
        auto r = ccidSend(PC_to_RDR_GetSlotStatus, {});
        uint8_t bStatus = r[7] & 0x03;
        if      (bStatus==0) return CardPresence::PresentActive;
        else if (bStatus==1) return CardPresence::PresentInactive;
        else if (bStatus==2) return CardPresence::NotPresent;
        else return CardPresence::Unknown;
    } else {
        auto r = acsSend(ACS_GET_ACR_STAT, {});
        uint8_t status = r[1]; (void)status;
        if (r.size()<5) throw ReaderError("ACS STAT: неправильный");
        uint8_t cstat = r.back();
        if      (cstat==0x00) return CardPresence::NotPresent;
        else if (cstat==0x01) return CardPresence::PresentInactive;
        else if (cstat==0x03) return CardPresence::PresentActive;
        else return CardPresence::Unknown;
    }
}

std::vector<uint8_t> Acr38Usb::powerOn(){
    if (!h_) throw ReaderError("Закрытый");
    if (backend_ == Backend::CCID) {
        auto r = ccidSend(PC_to_RDR_IccPowerOn, {});
        const uint32_t L = le32(&r[1]);
        std::vector<uint8_t> atr(r.begin()+10, r.begin()+10+L);
        return atr;
    } else {
        auto r = acsSend(ACS_RESET_DEFAULT, {});
        if (r[1]!=0x00) throw ReaderError("ACS: сброс карты (RESET) завершился ошибкой");
        const uint16_t L = (uint16_t(r[2])<<8) | r[3];
        std::vector<uint8_t> atr(r.begin()+4, r.begin()+4+L);
        return atr;
    }
}

void Acr38Usb::powerOff(){
    if (!h_) throw ReaderError("Закрытый");
    if (backend_ == Backend::CCID) {
        (void)ccidSend(PC_to_RDR_IccPowerOff, {});
    } else {
        auto r = acsSend(ACS_POWER_OFF, {});
        if (r[1]!=0x00) throw ReaderError("ACS: снятие питания завершилось ошибкой");
    }
}

bool Acr38Usb::waitCardEvent(unsigned timeoutMs){
    if (!h_) throw ReaderError("Закрытый");
    if (!epIntrIn_) {
        (void)timeoutMs;
        return false;
    }
    uint8_t tmp[64]; int got=0;
    int rr = libusb_interrupt_transfer(h_, *epIntrIn_, tmp, (int)sizeof(tmp), &got, (int)timeoutMs);
    if (rr==LIBUSB_ERROR_TIMEOUT) return false;
    if (rr!=0) throw ReaderError(std::string("Ошибка interrupt IN: ") + libusbErr(rr));
    return got>0;
}

XfrResult Acr38Usb::transmit(const std::vector<uint8_t>& capdu, unsigned timeoutMs){
    if (!h_) throw ReaderError("Закрытый");
    if (backend_ == Backend::CCID) {
        auto r = ccidSend(PC_to_RDR_XfrBlock, capdu, 0, timeoutMs);
        const uint32_t L = le32(&r[1]);
        XfrResult xr;
        xr.data.assign(r.begin()+10, r.begin()+10+L);
        return xr;
    } else {
        auto r = acsSend(ACS_EXCHANGE_T0, capdu, timeoutMs);
        if (r[1]!=0x00) throw ReaderError("ACS: обмен по T=0 завершился ошибкой");
        const uint16_t L = (uint16_t(r[2])<<8) | r[3];
        XfrResult xr;
        xr.data.assign(r.begin()+4, r.begin()+4+L);
        return xr;
    }
}

std::vector<uint8_t> Acr38Usb::vendorControl(const std::vector<uint8_t>& payload){
    (void)payload;
    return {};
}

} // namespace smartio
