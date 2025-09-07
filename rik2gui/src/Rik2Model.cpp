#include "Rik2Model.hpp"
#include "Hex.hpp"
#include <stdexcept>

static EfType parseType(const QString& s){
    const QString t = s.toLower();
    if (t=="df") return EfType::DF;
    if (t=="transparent") return EfType::Transparent;
    if (t=="linear-fixed") return EfType::LinearFixed;
    if (t=="cyclic") return EfType::Cyclic;
    throw std::runtime_error(("Неизвестный тип узла: " + s).toStdString());
}

static uint16_t parseFidQt(const QString& fid){
    return parseFid(fid.toStdString());
}

static std::unique_ptr<Node> parseNode(const QJsonObject& o){
    auto n = std::make_unique<Node>();
    n->name = o.value("name").toString("?");
    n->fid  = parseFidQt(o.value("fid").toString());
    n->type = parseType(o.value("type").toString());

    if (n->type==EfType::Transparent) {
        n->size = o.value("size").toInt(0);
        if (n->size<=0) throw std::runtime_error("Для прозрачного EF требуется положительный 'size'");
    } else if (n->type==EfType::LinearFixed) {
        n->recordSize = o.value("recordSize").toInt(0);
        n->recordCount= o.value("recordCount").toInt(0);
        if (n->recordSize<=0 || n->recordCount<=0) throw std::runtime_error("Для линейного EF требуются 'recordSize' и 'recordCount' > 0");
    }

    n->saveAs = o.value("saveAs").toString();

    if (o.contains("createApdus")) {
        auto arr = o.value("createApdus").toArray();
        for (auto v: arr) {
            auto s = v.toString().toStdString();
            n->createApdus.push_back(hexToBytes(s));
        }
    }

    if (n->type==EfType::DF) {
        auto arr = o.value("children").toArray();
        for (auto v: arr) n->children.push_back(parseNode(v.toObject()));
    }
    return n;
}

Rik2Layout Rik2Parser::parseFile(const QString& path){
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Невозможно открыть расположение: "+path.toStdString());
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) throw std::runtime_error("Корневой элемент разметки должен быть объектом JSON");
    auto o = doc.object();

    Rik2Layout L;
    L.schema = o.value("schema").toString();
    auto card = o.value("card").toObject();
    L.cardName = card.value("name").toString("РИК-2");
    QString atr = card.value("atrExpected").toString();
    if (!atr.isEmpty()) L.atrExpected = atr;

    // serial
    auto s = card.value("serial").toObject();
    if (s.contains("apdu")) {
        L.serial.apdu = s.value("apdu").toString();
    } else if (s.contains("efPath")) {
        auto a = s.value("efPath").toArray();
        for (auto v: a) L.serial.efPath.push_back(parseFidQt(v.toString()));
        L.serial.efType = s.value("type").toString()=="linear-fixed" ? EfType::LinearFixed : EfType::Transparent;
        L.serial.size = s.value("size").toInt(0);
    }

    L.root = parseNode(o.value("root").toObject());
    if (!L.root || L.root->type!=EfType::DF) throw std::runtime_error("Корневой узел должен быть DF (каталог)");
    return L;
}
