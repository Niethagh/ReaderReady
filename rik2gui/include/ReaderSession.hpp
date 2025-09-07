#pragma once
#include <QString>
#include <QLibrary>
#include <functional>
#include <memory>
#include "ReaderApi.h"

class ReaderSession {
public:
    ReaderSession();
    ~ReaderSession();

    bool loadLibrary(const QString& path, QString* err=nullptr);
    bool open(uint16_t vid, uint16_t pid, bool detach, int iface, unsigned timeoutMs, QString* err=nullptr);
    void close();
    void unload();

    std::vector<uint8_t> powerOn();
    void powerOff();
    std::vector<uint8_t> transmit(const std::vector<uint8_t>& capdu, unsigned timeoutMs = 2000);
    smartio::CardPresence status() const;
    smartio::ReaderInfo info() const;

    bool isLoaded() const { return (bool)create_; }
    bool isOpen() const { return rdr_!=nullptr; }
    const char* libVersion() const { return ver_? ver_() : ""; }

private:
    QLibrary lib_;
    using CreateFn  = smartio::ICardReader*(*)();
    using DestroyFn = void(*)(smartio::ICardReader*);
    using VerFn     = const char*(*)();

    CreateFn  create_ = nullptr;
    DestroyFn destroy_ = nullptr;
    VerFn     ver_     = nullptr;

    smartio::ICardReader* rdr_ = nullptr;
};
