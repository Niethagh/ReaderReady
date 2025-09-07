#include "ccid.h"
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cstring>

namespace {
constexpr uint8_t CCID_CLASS = 0x0B; // Smart Card
// CCID Bulk-OUT сообщения
constexpr uint8_t PC_to_RDR_IccPowerOn   = 0x62;
constexpr uint8_t PC_to_RDR_IccPowerOff  = 0x63;
constexpr uint8_t PC_to_RDR_GetSlotStatus= 0x65;
constexpr uint8_t PC_to_RDR_XfrBlock     = 0x6F;

// CCID ответы (Bulk-IN)
constexpr uint8_t RDR_to_PC_DataBlock    = 0x80;
constexpr uint8_t RDR_to_PC_SlotStatus   = 0x81;

// --- ACS legacy protocol (non-CCID) ---
constexpr uint8_t ACS_HDR                 = 0x01;
constexpr uint8_t INS_GET_ACR_STAT        = 0x01;
constexpr uint8_t INS_RESET_DEFAULT       = 0x80;
constexpr uint8_t INS_POWER_OFF           = 0x81;
constexpr uint8_t INS_EXCHANGE_T0         = 0xA0;

std::string libusb_err(int r){
    std::ostringstream os;
    os << "libusb error (" << r << "): " << libusb_error_name(r);
    return os.str();
}
} // namespace

CcidReader::CcidReader() {
    if (int r = libusb_init(&ctx); r != 0) {
        throw std::runtime_error(libusb_err(r));
    }
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_NONE);
}

CcidReader::~CcidReader() {
    close();
    if (ctx) libusb_exit(ctx);
}

void CcidReader::open(const DeviceSelector& sel){
    findAndOpen(sel);
}

void CcidReader::close(){
    if (handle) {
        if (eps.interfaceNumber >= 0) {
            libusb_release_interface(handle, eps.interfaceNumber);
        }
        libusb_close(handle);
    }
    handle = nullptr;
    eps = {};
}

void CcidReader::findAndOpen(const DeviceSelector& sel){
    libusb_device** list = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    if (cnt < 0) throw std::runtime_error("libusb_get_device_list failed");

    libusb_device* chosen = nullptr;
    for (ssize_t i=0; i<cnt; ++i) {
        libusb_device* d = list[i];
        libusb_device_descriptor dd{};
        if (libusb_get_device_descriptor(d, &dd) == 0) {
            if (dd.idVendor == sel.vid && dd.idProduct == sel.pid) {
                // Проверим наличие интерфейса CCID
                try {
                    auto probe = findCcidInterfaceAndEndpoints(d);
                    chosen = d;
                    eps = probe;
                    break;
                } catch (...) {
                    // не CCID — пропустим
                }
            }
        }
    }
    if (!chosen) {
        libusb_free_device_list(list, 1);
        throw std::runtime_error("ACR38 (VID:PID not found or no CCID interface)");
    }

    if (int r = libusb_open(chosen, &handle); r != 0) {
        libusb_free_device_list(list, 1);
        throw std::runtime_error(libusb_err(r));
    }
    libusb_free_device_list(list, 1);

    if (libusb_kernel_driver_active(handle, eps.interfaceNumber) == 1) {
        libusb_detach_kernel_driver(handle, eps.interfaceNumber);
    }
    if (int r = libusb_claim_interface(handle, eps.interfaceNumber); r != 0) {
        throw std::runtime_error(libusb_err(r));
    }
}

UsbEndpoints CcidReader::findCcidInterfaceAndEndpoints(libusb_device* dev){
    libusb_config_descriptor* cfg = nullptr;
    int r = libusb_get_active_config_descriptor(dev, &cfg);
    if (r != 0 || !cfg) {
        if (libusb_get_config_descriptor(dev, 0, &cfg) != 0 || !cfg) {
            throw std::runtime_error("CCID: cannot obtain any config descriptor");
        }
    }

    UsbEndpoints out{};
    bool found = false;

    for (uint8_t i=0; i<cfg->bNumInterfaces && !found; ++i){
        const auto& alt = cfg->interface[i];
        for (int a=0; a<alt.num_altsetting && !found; ++a){
            const auto* ifd = &alt.altsetting[a];

            // bulk IN/OUT (+ optional interrupt IN) — подходят и для CCID, и для ACS
            uint8_t bin=0, bout=0; std::optional<uint8_t> intr;
            for (uint8_t e=0; e<ifd->bNumEndpoints; ++e){
                const auto& ep = ifd->endpoint[e];
                const uint8_t addr = ep.bEndpointAddress;
                const uint8_t type = ep.bmAttributes & 0x03;
                const bool dirIn = (addr & 0x80)!=0;
                if (type==LIBUSB_TRANSFER_TYPE_BULK && dirIn)  bin = addr;
                if (type==LIBUSB_TRANSFER_TYPE_BULK && !dirIn) bout = addr;
                if (type==LIBUSB_TRANSFER_TYPE_INTERRUPT && dirIn) intr = addr;
            }
            if (bin && bout) {
                out.bulkIn = bin; out.bulkOut = bout; out.intrIn = intr;
                out.interfaceNumber = ifd->bInterfaceNumber;
                // CCID или ACS?
                proto = (ifd->bInterfaceClass == CCID_CLASS) ? Proto::CCID : Proto::ACS;
                found = true;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    if (!found) throw std::runtime_error("Interface not found (no Bulk IN/OUT pair)");
    return out;
}

AcsResponse CcidReader::sendAcs(uint8_t instruction, const std::vector<uint8_t>& data) {
    // Команда: 01 | INS | LEN_MS | LEN_LS | DATA...
    const uint16_t N = static_cast<uint16_t>(data.size());
    std::vector<uint8_t> out; out.reserve(4 + N);
    out.push_back(ACS_HDR);
    out.push_back(instruction);
    out.push_back(uint8_t((N >> 8) & 0xFF));
    out.push_back(uint8_t(N & 0xFF));
    out.insert(out.end(), data.begin(), data.end());

    int tr=0;
    int r = libusb_bulk_transfer(handle, eps.bulkOut, out.data(), (int)out.size(), &tr, 3000);
    if (r != 0 || tr != (int)out.size()) {
        throw std::runtime_error("ACS bulk OUT failed: " + std::string(libusb_error_name(r)));
    }

    // Ответ: 01 | STATUS | LEN_MS | LEN_LS | DATA...
    std::vector<uint8_t> buf; buf.reserve(512);
    auto read_chunk = [&](int timeout_ms)->int {
        uint8_t tmp[256]; int got=0;
        int rr = libusb_bulk_transfer(handle, eps.bulkIn, tmp, (int)sizeof(tmp), &got, timeout_ms);
        if (rr == LIBUSB_ERROR_TIMEOUT) return 0;
        if (rr != 0) throw std::runtime_error("ACS bulk IN failed: " + std::string(libusb_error_name(rr)));
        buf.insert(buf.end(), tmp, tmp+got);
        return got;
    };

    int tries=0;
    while (buf.size() < 4 && tries++ < 5) { read_chunk(1000); }
    if (buf.size() < 4 || buf[0] != ACS_HDR) throw std::runtime_error("ACS: no/short header");

    const uint8_t status = buf[1];
    const uint16_t len = (uint16_t(buf[2])<<8) | buf[3];
    while (buf.size() < size_t(4 + len)) { if (read_chunk(1000) == 0) break; }
    if (buf.size() < size_t(4 + len)) throw std::runtime_error("ACS: incomplete response");

    AcsResponse resp;
    resp.status = status;
    resp.data.assign(buf.begin()+4, buf.begin()+4+len);
    return resp;
}

AcsResponse CcidReader::acsGetStat()        { return sendAcs(INS_GET_ACR_STAT, {}); }
AcsResponse CcidReader::acsResetDefault()   { return sendAcs(INS_RESET_DEFAULT, {}); }
AcsResponse CcidReader::acsPowerOff()       { return sendAcs(INS_POWER_OFF, {}); }
AcsResponse CcidReader::acsExchangeT0(const std::vector<uint8_t>& apdu) {
    return sendAcs(INS_EXCHANGE_T0, apdu);
}

std::string CcidReader::deviceInfo() const {
    if (!handle) return "Not opened";
    libusb_device* dev = libusb_get_device(handle);
    libusb_device_descriptor dd{};
    libusb_get_device_descriptor(dev, &dd);
    std::ostringstream os;
    os << "VID:PID " << std::hex << std::setfill('0')
       << "0x" << std::setw(4) << dd.idVendor << ":0x" << std::setw(4) << dd.idProduct
       << std::dec
       << "  IF=" << eps.interfaceNumber
       << "  EP bulk OUT=0x" << std::hex << int(eps.bulkOut)
       << " IN=0x" << int(eps.bulkIn)
       << std::dec;
    if (eps.intrIn) os << "  EP intr IN=0x" << std::hex << int(*eps.intrIn) << std::dec;
    return os.str();
}

CcidResponse CcidReader::sendCcid(uint8_t msgType,
                                  const std::vector<uint8_t>& data,
                                  uint8_t slot, uint8_t seqNum)
{
    // --- PC_to_RDR (Bulk OUT) ---
    std::vector<uint8_t> out;
    out.resize(10 + data.size());
    out[0] = msgType;
    uint32_t L = static_cast<uint32_t>(data.size());
    out[1] = uint8_t(L & 0xFF);
    out[2] = uint8_t((L >> 8) & 0xFF);
    out[3] = uint8_t((L >> 16) & 0xFF);
    out[4] = uint8_t((L >> 24) & 0xFF);
    out[5] = slot;
    out[6] = seqNum;
    out[7] = 0; out[8] = 0; out[9] = 0;
    if (!data.empty()) std::memcpy(out.data()+10, data.data(), data.size());

    int transferred = 0;
    int r = libusb_bulk_transfer(handle, eps.bulkOut, out.data(), (int)out.size(), &transferred, 3000);
    if (r != 0 || transferred != (int)out.size()) {
        throw std::runtime_error("bulk OUT failed: " + std::string(libusb_error_name(r)));
    }

    // --- RDR_to_PC (Bulk IN) ---
    std::vector<uint8_t> buf;
    buf.reserve(1024);

    auto read_chunk = [&](int timeout_ms)->int {
        uint8_t tmp[256];
        int tr = 0;
        int rr = libusb_bulk_transfer(handle, eps.bulkIn, tmp, (int)sizeof(tmp), &tr, timeout_ms);
        if (rr == LIBUSB_ERROR_TIMEOUT) return 0;
        if (rr != 0) throw std::runtime_error("bulk IN failed: " + std::string(libusb_error_name(rr)));
        buf.insert(buf.end(), tmp, tmp + tr);
        return tr;
    };

    // 1) дочитываем до заголовка (>=10 байт)
    int tries = 0;
    while (buf.size() < 10 && tries++ < 5) {
        int got = read_chunk(1000);
        if (got == 0) continue; // просто ждём
    }
    if (buf.size() < 10) {
        throw std::runtime_error("CCID: no/short header");
    }

    // 2) знаем dwLength — дочитаем тело
    uint32_t dwLen = le32(&buf[1]);
    size_t need = 10u + (size_t)dwLen;
    while (buf.size() < need) {
        int got = read_chunk(1000);
        if (got == 0) break; // дадим шанс выйти по таймауту
    }
    if (buf.size() < need) {
        // некоторые ридеры шлют ZLP или задерживают хвост — ещё одна попытка
        int got = read_chunk(2000);
        (void)got; // игнорируем значение; проверим размер ещё раз
    }
    if (buf.size() < need) {
        throw std::runtime_error("CCID: incomplete response");
    }

    // 3) разбор ответа
    CcidResponse resp;
    resp.msgType = buf[0];
    resp.slot    = buf[5];
    resp.status  = buf[7];
    resp.error   = buf[8];
    resp.chain   = buf[9];
    if (dwLen) {
        resp.payload.assign(buf.begin()+10, buf.begin()+10+dwLen);
    }
    return resp;
}

CcidResponse CcidReader::getSlotStatus(uint8_t slot){
    if (proto == Proto::CCID) {
        return sendCcid(PC_to_RDR_GetSlotStatus, {}, slot, seq++);
    }
    // ACS: берём C_STAT из конца ответа GET_ACR_STAT
    auto a = acsGetStat();
    CcidResponse r;
    r.msgType = RDR_to_PC_SlotStatus;
    r.slot = 0;
    r.error = (a.status == 0x00) ? 0x00 : 0xFF;

    uint8_t cstat = (!a.data.empty()) ? a.data.back() : 0xFF; // 00=нет карты, 01=вставлена не запитана, 03=включена
    uint8_t icc = 3; // unknown
    if (cstat == 0x00) icc = 2;         // not present
    else if (cstat == 0x01) icc = 1;    // present, inactive
    else if (cstat == 0x03) icc = 0;    // present, active
    r.status = icc; // биты [1:0] как в CCID

    return r;
}

CcidResponse CcidReader::powerOn(uint8_t /*slot*/){
    if (proto == Proto::CCID) {
        return sendCcid(PC_to_RDR_IccPowerOn, {}, 0, seq++);
    }
    auto a = acsResetDefault();
    if (a.status != 0x00) throw std::runtime_error("ACS RESET failed");
    CcidResponse r;
    r.msgType = RDR_to_PC_DataBlock;
    r.payload = std::move(a.data); // ATR
    r.slot = 0; r.status = 0; r.error = 0; r.chain = 0;
    return r;
}

CcidResponse CcidReader::powerOff(uint8_t /*slot*/){
    if (proto == Proto::CCID) {
        return sendCcid(PC_to_RDR_IccPowerOff, {}, 0, seq++);
    }
    auto a = acsPowerOff();
    if (a.status != 0x00) throw std::runtime_error("ACS POWER_OFF failed");
    CcidResponse r;
    r.msgType = RDR_to_PC_SlotStatus;
    r.slot = 0; r.status = 0; r.error = 0; r.chain = 0;
    return r;
}

CcidResponse CcidReader::xfrBlock(const std::vector<uint8_t>& apdu, uint8_t /*slot*/){
    if (proto == Proto::CCID) {
        return sendCcid(PC_to_RDR_XfrBlock, apdu, 0, seq++);
    }
    auto a = acsExchangeT0(apdu); // вернёт Data...SW1 SW2
    if (a.status != 0x00) throw std::runtime_error("ACS EXCHANGE_T0 failed");
    CcidResponse r;
    r.msgType = RDR_to_PC_DataBlock;
    r.payload = std::move(a.data);
    r.slot = 0; r.status = 0; r.error = 0; r.chain = 0;
    return r;
}

std::optional<std::vector<uint8_t>> CcidReader::readInterrupt(unsigned int timeoutMs){
    if (!eps.intrIn) return std::nullopt;
    std::vector<uint8_t> buf(64);
    int tr = 0;
    int r = libusb_interrupt_transfer(handle, *eps.intrIn, buf.data(), (int)buf.size(), &tr, timeoutMs);
    if (r == LIBUSB_ERROR_TIMEOUT) return std::nullopt;
    if (r != 0) throw std::runtime_error("interrupt IN failed: " + libusb_err(r));
    buf.resize(tr);
    return buf;
}
