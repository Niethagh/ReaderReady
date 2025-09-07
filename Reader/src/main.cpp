#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QLibrary>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <memory>
#include "ReaderApi.h"

using namespace smartio;

static std::vector<uint8_t> parseHex(const QString& hex){
    QString cleaned = hex;
    cleaned.remove(' ').remove(':');
    if (cleaned.isEmpty()) return {};
    if (cleaned.size() % 2 != 0) throw std::runtime_error("Нечётная длина hex-строки");
    std::vector<uint8_t> out; out.reserve(cleaned.size()/2);
    for (int i=0; i<cleaned.size(); i+=2){
        bool ok=false;
        uint8_t v = static_cast<uint8_t>(cleaned.mid(i,2).toUInt(&ok,16));
        if (!ok) throw std::runtime_error("Некорректная hex-строка");
        out.push_back(v);
    }
    return out;
}

static std::string toHex(const std::vector<uint8_t>& v){
    std::ostringstream os; os<<std::hex<<std::setfill('0');
    for (size_t i=0;i<v.size();++i){
        os<<std::setw(2)<<int(v[i]);
        if (i+1<v.size()) os<<' ';
    }
    return os.str();
}

static const char* presenceToStr(CardPresence p){
    switch (p){
    case CardPresence::NotPresent:      return "нет карты";
    case CardPresence::PresentInactive: return "карта вставлена, питание снято";
    case CardPresence::PresentActive:   return "карта вставлена и активна";
    default: return "статус неизвестен";
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("Reader");
    QCoreApplication::setApplicationVersion("0.2");

    QCommandLineParser p;
    p.setApplicationDescription(
        "Консольная утилита работы с ACR38 через универсальную библиотеку.\n"
        "Команды:\n"
        "  info                     — сведения о ридере и библиотеке\n"
        "  status                   — состояние карты\n"
        "  poweron                  — подать питание и вывести ATR\n"
        "  poweroff                 — снять питание\n"
        "  xfr <APDUhex>            — передать APDU (напр. \"00 A4 04 00 00\")\n"
        "  poll                     — ожидать события карты (вставка/извлечение)\n"
        );
    p.addHelpOption();
    p.addVersionOption();

    QCommandLineOption libOpt(QStringList() << "lib",
                              "Путь/имя библиотеки (.so), по умолчанию 'acr38usb'", "ПУТЬ", "acr38usb");
    QCommandLineOption vidOpt(QStringList() << "vid",
                              "USB Vendor ID (hex), по умолчанию 0x072F", "VID", "072F");
    QCommandLineOption pidOpt(QStringList() << "pid",
                              "USB Product ID (hex), по умолчанию 0x9000", "PID", "9000");
    QCommandLineOption protoOpt(QStringList() << "proto",
                                "ISO протокол: auto|t0|t1 (по умолчанию auto)", "P", "auto");
    QCommandLineOption ifOpt(QStringList() << "iface",
                             "Номер USB-интерфейса (-1 авто)", "N", "-1");
    QCommandLineOption timeoutOpt(QStringList() << "timeout",
                                  "Таймаут обмена, мс (по умолчанию 2000)", "MS", "2000");
    QCommandLineOption noDetachOpt(QStringList() << "no-detach",
                                   "Не отсоединять драйвер ядра/pcscd");
    p.addOption(libOpt);
    p.addOption(vidOpt); p.addOption(pidOpt);
    p.addOption(protoOpt); p.addOption(ifOpt);
    p.addOption(timeoutOpt); p.addOption(noDetachOpt);

    p.addPositionalArgument("command", "Команда (см. описание выше)");
    p.addPositionalArgument("args", "Аргументы команды", "[args]");
    p.process(app);
    auto pos = p.positionalArguments();
    if (pos.isEmpty()) p.showHelp(0);

    QString cmd = pos.at(0).toLower();

    bool okv=false, okp=false, okif=false, okt=false;
    uint16_t vid = p.value(vidOpt).toUShort(&okv,16);
    uint16_t pid = p.value(pidOpt).toUShort(&okp,16);
    int iface = p.value(ifOpt).toInt(&okif,10);
    unsigned timeout = p.value(timeoutOpt).toUInt(&okt,10);
    if (!okv || !okp || !okif || !okt){
        std::cerr << "Ошибка: некорректные значения VID/PID/iface/timeout\n";
        return 2;
    }

    IsoProtocol proto = IsoProtocol::Auto;
    QString ps = p.value(protoOpt).toLower();
    if (ps=="t0") proto = IsoProtocol::T0;
    else if (ps=="t1") proto = IsoProtocol::T1;
    else if (ps!="auto") { std::cerr << "Ошибка: неизвестный протокол: "<< ps.toStdString() << "\n"; return 2; }

    // --- загрузка библиотеки ---
    QLibrary lib(p.value(libOpt));
    if (!lib.load()){
        std::cerr << "Не удалось загрузить библиотеку: " << lib.errorString().toStdString() << "\n";
        return 1;
    }

    using CreateFn  = ICardReader*(*)();
    using DestroyFn = void(*)(ICardReader*);
    using VerFn     = const char*(*)();

    auto create  = reinterpret_cast<CreateFn>(lib.resolve("create_reader"));
    auto destroy = reinterpret_cast<DestroyFn>(lib.resolve("destroy_reader"));
    auto ver     = reinterpret_cast<VerFn>(lib.resolve("reader_library_version"));

    if (!create || !destroy || !ver){
        std::cerr << "Ошибка: в библиотеке отсутствуют требуемые экспортируемые функции\n";
        return 1;
    }

    std::unique_ptr<ICardReader, DestroyFn> rdr(create(), destroy);
    if (!rdr){
        std::cerr << "Ошибка: create_reader() вернула null\n";
        return 1;
    }

    OpenParams par;
    par.vid = vid; par.pid = pid;
    par.protocol = proto;
    par.detachKernelDriver = !p.isSet(noDetachOpt);
    par.interfaceHint = iface;
    par.ioTimeoutMs = timeout;

    try {
        rdr->open(par);

        if (cmd=="info"){
            auto inf = rdr->info();
            std::cout << "Библиотека: " << ver() << "\n"
                      << "Ридер     : " << inf.name << "\n"
                      << "VID:PID   : 0x" << std::hex << std::setfill('0')
                      << std::setw(4) << inf.vid << ":0x" << std::setw(4) << inf.pid << std::dec << "\n"
                      << "Бэкенд    : " << inf.backend << "\n"
                      << "Интерфейс/EP: bulk OUT=0x" << std::hex << int(inf.bulkOut)
                      << " IN=0x" << int(inf.bulkIn)
                      << (inf.hasInterrupt ? (std::string("  intr IN=0x") + [&]{std::ostringstream s;s<<std::hex<<int(inf.intrIn);return s.str();}()) : "")
                      << std::dec << "\n";
            return 0;
        }
        else if (cmd=="status"){
            auto s = rdr->cardStatus();
            std::cout << "Состояние карты: " << presenceToStr(s) << "\n";
            return 0;
        }
        else if (cmd=="poweron"){
            auto atr = rdr->powerOn();
            std::cout << "ATR: " << toHex(atr) << "\n";
            return 0;
        }
        else if (cmd=="poweroff"){
            rdr->powerOff();
            std::cout << "Питание снято\n";
            return 0;
        }
        else if (cmd=="xfr"){
            if (pos.size()<2){ std::cerr << "Использование: xfr <APDUhex>\n"; return 2; }
            auto apdu = parseHex(pos.at(1));
            auto r = rdr->transmit(apdu, timeout);
            std::cout << "R-APDU: " << toHex(r.data) << "\n";
            return 0;
        }
        else if (cmd=="poll"){
            std::cout << "Ожидание событий карты (Ctrl+C — выход)…\n";
            CardPresence last = rdr->cardStatus();
            std::cout << "Начальный статус: " << presenceToStr(last) << std::endl;
            while (true){
                bool evt = rdr->waitCardEvent(10000);
                CardPresence curr = rdr->cardStatus();
                if (curr != last){
                    std::cout << "Событие: " << presenceToStr(curr) << std::endl;
                    last = curr;
                } else if (evt) {
                    std::cout << "Событие без изменения статуса\n";
                }
                std::cout.flush();
            }
        }
        else {
            std::cerr << "Неизвестная команда: " << cmd.toStdString() << "\n";
            return 2;
        }
    } catch (const ReaderError& ex) {
        std::cerr << "Ошибка ридера: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex){
        std::cerr << "Ошибка: " << ex.what() << "\n";
        return 1;
    }
}
