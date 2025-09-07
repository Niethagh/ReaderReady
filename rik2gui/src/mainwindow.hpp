#pragma once
#include <QMainWindow>
#include <QTreeView>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QSplitter>
#include "ReaderSession.hpp"
#include "Rik2Model.hpp"
#include "Rik2Worker.hpp"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void onOpenLib();
    void onConnect();
    void onLoadLayout();
    void onReadAll();
    void onMarkup();
    void onPowerOn();
    void onPowerOff();

private:
    void rebuildTree();
    void log(const QString& s);

    ReaderSession session_;
    std::optional<Rik2Layout> layout_;
    std::unique_ptr<Rik2Worker> worker_;

    QTreeView* tree_;
    QStandardItemModel* model_;
    QTextEdit* log_;
    QLabel* status_;
    QProgressBar* prog_;
    QString libPath_ = "acr38usb";
};
