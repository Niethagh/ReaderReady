#pragma once
#include <QString>
#include <QDir>
#include "ReaderSession.hpp"
#include "Rik2Model.hpp"

class Rik2Worker {
public:
    explicit Rik2Worker(ReaderSession& s) : s_(s) {}

    std::vector<uint8_t> getAtr();
    QString getSerial(const Rik2Layout& L);

    void readAll(const Rik2Layout& L, const QDir& outDir, std::function<void(const QString&)> log);
    void markupCard(const Rik2Layout& L, std::function<void(const QString&)> log);

private:
    ReaderSession& s_;

    void selectByPath(const std::vector<uint16_t>& path);
    void selectFid(uint16_t fid);
    std::vector<uint8_t> readTransparent(int size);
    std::vector<uint8_t> readLinearFixed(int recSize, int recCount);

    void updateTransparent(const std::vector<uint8_t>& data);
};
