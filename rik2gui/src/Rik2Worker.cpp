#include "Rik2Worker.hpp"
#include "Hex.hpp"
#include <QFile>
#include <QFileInfo>

std::vector<uint8_t> Rik2Worker::getAtr(){ return s_.powerOn(); }

QString Rik2Worker::getSerial(const Rik2Layout& L){
    // 1) APDU-способ
    if (!L.serial.apdu.isEmpty()){
        auto c = hexToBytes(L.serial.apdu.toStdString());
        auto r = s_.transmit(c, 2000);
        return QString::fromStdString(bytesToHex(r));
    }
    // 2) EF-способ
    if (!L.serial.efPath.empty()){
        selectByPath(L.serial.efPath);
        std::vector<uint8_t> data;
        if (L.serial.efType==EfType::Transparent){
            data = readTransparent(L.serial.size);
        } else {
            data = readLinearFixed(L.serial.size, 1);
        }
        return QString::fromStdString(bytesToHex(data));
    }
    return "Н/Д";
}

void Rik2Worker::selectFid(uint16_t fid){
    std::vector<uint8_t> apdu = {0x00,0xA4,0x00,0x0C,0x02, (uint8_t)(fid>>8),(uint8_t)(fid&0xFF)};
    (void)s_.transmit(apdu, 2000);
}
void Rik2Worker::selectByPath(const std::vector<uint16_t>& path){
    for (auto fid: path) selectFid(fid);
}

std::vector<uint8_t> Rik2Worker::readTransparent(int size){
    std::vector<uint8_t> out; out.reserve(size);
    int remaining = size, off=0;
    while (remaining>0){
        int chunk = std::min(remaining, 0xFF);
        std::vector<uint8_t> apdu = {0x00,0xB0,(uint8_t)(off>>8),(uint8_t)(off&0xFF),(uint8_t)chunk};
        auto r = s_.transmit(apdu, 2000);
        out.insert(out.end(), r.begin(), r.end());
        off += chunk; remaining -= chunk;
    }
    return out;
}
std::vector<uint8_t> Rik2Worker::readLinearFixed(int recSize, int recCount){
    std::vector<uint8_t> out; out.reserve(recSize*recCount);
    for (int rec=1; rec<=recCount; ++rec){
        std::vector<uint8_t> apdu = {0x00,0xB2,(uint8_t)rec,0x04,(uint8_t)recSize};
        auto r = s_.transmit(apdu, 2000);
        if ((int)r.size()<recSize) r.resize(recSize,0x00);
        out.insert(out.end(), r.begin(), r.begin()+recSize);
    }
    return out;
}
void Rik2Worker::updateTransparent(const std::vector<uint8_t>& data){
    int remaining = (int)data.size(), off=0;
    while (remaining>0){
        int chunk = std::min(remaining, 0xFF);
        std::vector<uint8_t> apdu = {0x00,0xD6,(uint8_t)(off>>8),(uint8_t)(off&0xFF),(uint8_t)chunk};
        apdu.insert(apdu.end(), data.begin()+off, data.begin()+off+chunk);
        (void)s_.transmit(apdu, 5000);
        off += chunk; remaining -= chunk;
    }
}

static void ensureDir(const QDir& base, const QString& relative){
    QDir d(base);
    auto parts = relative.split('/', Qt::SkipEmptyParts);
    for (int i=0;i<parts.size()-1;++i){ d.mkpath(parts[i]); d.cd(parts[i]); }
}

static void traverseRead(Node* n, std::vector<uint16_t>& path, ReaderSession& s, const QDir& outDir, std::function<void(const QString&)> log){
    if (n->type==EfType::DF){
        path.push_back(n->fid);
        for (auto& ch : n->children) traverseRead(ch.get(), path, s, outDir, log);
        path.pop_back();
        return;
    }

    std::vector<uint16_t> sel = path;
    sel.push_back(n->fid);
    for (auto fid: sel){
        std::vector<uint8_t> a = {0x00,0xA4,0x00,0x0C,0x02,(uint8_t)(fid>>8),(uint8_t)(fid&0xFF)};
        (void)s.transmit(a, 2000);
    }
    std::vector<uint8_t> data;
    if (n->type==EfType::Transparent){
        int remaining=n->size, off=0;
        while (remaining>0){
            int chunk = std::min(remaining, 0xFF);
            std::vector<uint8_t> apdu = {0x00,0xB0,(uint8_t)(off>>8),(uint8_t)(off&0xFF),(uint8_t)chunk};
            auto r = s.transmit(apdu, 2000);
            data.insert(data.end(), r.begin(), r.end());
            off += chunk; remaining -= chunk;
        }
    } else if (n->type==EfType::LinearFixed){
        for (int rec=1; rec<=n->recordCount; ++rec){
            std::vector<uint8_t> apdu = {0x00,0xB2,(uint8_t)rec,0x04,(uint8_t)n->recordSize};
            auto r = s.transmit(apdu, 2000);
            if ((int)r.size()<n->recordSize) r.resize(n->recordSize,0x00);
            data.insert(data.end(), r.begin(), r.begin()+n->recordSize);
        }
    } else {
    }

    if (!n->saveAs.isEmpty()){
        QString rel = n->saveAs;
        ensureDir(outDir, rel);
        QFile f(outDir.filePath(rel));
        if (f.open(QIODevice::WriteOnly)){
            f.write((const char*)data.data(), (qint64)data.size());
            f.close();
            log(QString("Сохранён файл %1 (%2 байт)").arg(rel).arg(data.size()));
        } else {
            log(QString("Не удалось сохранить файл %1").arg(rel));
        }
    }
}

void Rik2Worker::readAll(const Rik2Layout& L, const QDir& outDir, std::function<void(const QString&)> log){

    (void)getAtr();

    std::vector<uint16_t> path;
    path.push_back(L.root->fid);
    for (auto& ch : L.root->children){
        traverseRead(ch.get(), path, s_, outDir, log);
    }
    log("Считывание всех файлов завершено");
}

void Rik2Worker::markupCard(const Rik2Layout& L, std::function<void(const QString&)> log){
    std::function<void(Node*, std::vector<uint16_t>&)> walk = [&](Node* n, std::vector<uint16_t>& path){
        if (n->type==EfType::DF){
            std::vector<uint16_t> sel = path; sel.push_back(n->fid);
            selectByPath(sel);
            path.push_back(n->fid);
            for (auto& ch: n->children) walk(ch.get(), path);
            path.pop_back();
            return;
        }

        selectByPath(path);
        for (auto& capdu : n->createApdus){
            (void)s_.transmit(capdu, 5000);
        }
        selectFid(n->fid);
        log(QString("Подготовлен EF %1 (FID %2)").arg(n->name).arg(n->fid,4,16,QLatin1Char('0')));
    };

    std::vector<uint16_t> path;
    selectFid(L.root->fid);
    path.push_back(L.root->fid);
    for (auto& ch: L.root->children) walk(ch.get(), path);
    log("Разметка: выполнена");
}
