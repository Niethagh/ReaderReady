// src/ReaderSession.cpp
#include "ReaderSession.hpp"
#include <stdexcept>

ReaderSession::ReaderSession() {}
ReaderSession::~ReaderSession() { unload(); }  // единственный деструктор

bool ReaderSession::loadLibrary(const QString& path, QString* err){
    lib_.setFileName(path);
    if (!lib_.load()){
        if (err) *err = "Не удалось загрузить библиотеку: " + lib_.errorString();
        return false;
    }
    create_  = reinterpret_cast<CreateFn>(lib_.resolve("create_reader"));
    destroy_ = reinterpret_cast<DestroyFn>(lib_.resolve("destroy_reader"));
    ver_     = reinterpret_cast<VerFn>(lib_.resolve("reader_library_version"));
    if (!create_ || !destroy_ || !ver_) {
        if (err) *err = "В библиотеке отсутствуют необходимые точки входа (create_reader/destroy_reader/reader_library_version)";
        lib_.unload(); create_ = nullptr; destroy_ = nullptr; ver_ = nullptr;
        return false;
    }
    return true;
}

bool ReaderSession::open(uint16_t vid, uint16_t pid, bool detach, int iface, unsigned timeoutMs, QString* err){
    if (!create_) { if (err) *err = "Библиотека не загружена"; return false; }
    close(); // закрыть предыдущий, если был
    rdr_ = create_();
    if (!rdr_) { if (err) *err = "create_reader() вернула null"; return false; }
    try {
        smartio::OpenParams p;
        p.vid = vid; p.pid = pid;
        p.detachKernelDriver = detach;
        p.interfaceHint = iface;
        p.ioTimeoutMs = timeoutMs;
        rdr_->open(p);
        return true;
    } catch (const std::exception& ex) {
        if (err) *err = QString::fromUtf8(ex.what());
        destroy_(rdr_); rdr_ = nullptr;
        return false;
    }
}

void ReaderSession::close(){
    if (rdr_) {
        try { rdr_->close(); } catch (...) {}
        destroy_(rdr_);
        rdr_ = nullptr;
    }
    // ВАЖНО: библиотеку здесь не выгружаем
}

void ReaderSession::unload(){
    close();
    if (lib_.isLoaded()) {
        lib_.unload();
        create_ = nullptr;
        destroy_ = nullptr;
        ver_ = nullptr;
    }
}

std::vector<uint8_t> ReaderSession::powerOn(){
    if (!rdr_) throw std::runtime_error("Ридер не открыт");
    return rdr_->powerOn();
}
void ReaderSession::powerOff(){
    if (!rdr_) throw std::runtime_error("Ридер не открыт");
    rdr_->powerOff();
}
std::vector<uint8_t> ReaderSession::transmit(const std::vector<uint8_t>& c, unsigned t){
    if (!rdr_) throw std::runtime_error("Ридер не открыт");
    return rdr_->transmit(c, t).data;
}
smartio::CardPresence ReaderSession::status() const {
    if (!rdr_) throw std::runtime_error("Ридер не открыт");
    return rdr_->cardStatus();
}
smartio::ReaderInfo ReaderSession::info() const {
    if (!rdr_) throw std::runtime_error("Ридер не открыт");
    return rdr_->info();
}
