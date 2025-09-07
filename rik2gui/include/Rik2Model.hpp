#pragma once
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QVector>
#include <QVariant>
#include <optional>
#include <vector>
#include <memory>

enum class EfType { DF, Transparent, LinearFixed, Cyclic };

struct SerialSpec {
    QString apdu;
    std::vector<uint16_t> efPath;
    EfType efType = EfType::Transparent;
    int size = 0;
};

struct Node {
    QString name;
    uint16_t fid = 0;
    EfType type = EfType::DF;
    int size = 0;
    int recordSize = 0;
    int recordCount = 0;
    QString saveAs;
    std::vector<std::vector<uint8_t>> createApdus;
    std::vector<std::unique_ptr<Node>> children;
};

struct Rik2Layout {
    QString schema;
    QString cardName;
    std::optional<QString> atrExpected;
    SerialSpec serial;
    std::unique_ptr<Node> root;
};

class Rik2Parser {
public:
    static Rik2Layout parseFile(const QString& path);
};
