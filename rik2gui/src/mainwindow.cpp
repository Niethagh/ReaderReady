#include "mainwindow.hpp"
#include <QToolBar>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QStatusBar>
#include "Hex.hpp"

static QStandardItem* makeItem(const QString& text){ auto* i=new QStandardItem(text); i->setEditable(false); return i; }
static MainWindow* g_mainWin = nullptr;

MainWindow::MainWindow(){
    g_mainWin = this;
    setWindowTitle("РИК-2 GUI");
    resize(1000, 700);

    auto* tb = addToolBar("Main");
    auto aOpenLib = tb->addAction("Library...");
    auto aConn    = tb->addAction("Connect");
    auto aLoad    = tb->addAction("Load Layout");
    auto aRead    = tb->addAction("Read All");
    auto aMk      = tb->addAction("Markup");
    auto aOn      = tb->addAction("Power On (ATR)");
    auto aOff     = tb->addAction("Power Off");

    connect(aOpenLib,&QAction::triggered,this,&MainWindow::onOpenLib);
    connect(aConn,&QAction::triggered,this,&MainWindow::onConnect);
    connect(aLoad,&QAction::triggered,this,&MainWindow::onLoadLayout);
    connect(aRead,&QAction::triggered,this,&MainWindow::onReadAll);
    connect(aMk,&QAction::triggered,this,&MainWindow::onMarkup);
    connect(aOn,&QAction::triggered,this,&MainWindow::onPowerOn);
    connect(aOff,&QAction::triggered,this,&MainWindow::onPowerOff);

    auto* split = new QSplitter;
    tree_ = new QTreeView;
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"Name","FID","Type","Size"});
    tree_->setModel(model_);
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

    log_ = new QTextEdit; log_->setReadOnly(true);

    split->addWidget(tree_);
    split->addWidget(log_);
    setCentralWidget(split);

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]{
        session_.unload();
        worker_.reset();
    });

    status_ = new QLabel("Ready"); statusBar()->addWidget(status_, 1);
    prog_ = new QProgressBar; prog_->setMinimum(0); prog_->setMaximum(0); prog_->setVisible(false);
    statusBar()->addPermanentWidget(prog_);
}

void MainWindow::onOpenLib(){
    auto path = QFileDialog::getOpenFileName(this,"Select reader library",".","Shared objects (*.so);;All files (*)");
    if (path.isEmpty()) return;
    QString err;
    if (!session_.loadLibrary(path, &err)){
        QMessageBox::critical(this,"Load error", err);
        return;
    }
    libPath_ = path;
    status_->setText(QString("Библиотека: %1 (%2)").arg(path).arg(session_.libVersion()));
    worker_ = std::make_unique<Rik2Worker>(session_);
}

void MainWindow::onConnect(){
    try {

        if (!session_.isLoaded()) {
            QString err;
            if (!session_.loadLibrary(libPath_, &err)) {
                QMessageBox::critical(this, "Библиотека", "Не удалось загрузить библиотеку: " + err);
                return;
            }
        }
        if (!worker_) worker_ = std::make_unique<Rik2Worker>(session_);

        if (session_.isOpen()) {
            const QString backend = QString::fromStdString(session_.info().backend);
            status_->setText(QString("Подключено. Бэкенд: %1").arg(backend));
            log("Подключение к ридеру");
            return;
        }

        bool ok=true;
        QString vidStr = QInputDialog::getText(this,"VID","Hex VID:", QLineEdit::Normal, "072F",&ok);
        if (!ok) return;
        QString pidStr = QInputDialog::getText(this,"PID","Hex PID:", QLineEdit::Normal, "9000",&ok);
        if (!ok) return;

        bool okv=false, okp=false;
        uint16_t vid = vidStr.toUShort(&okv,16);
        uint16_t pid = pidStr.toUShort(&okp,16);
        if (!okv || !okp) {
            QMessageBox::warning(this, "Подключение", "Неверные значения VID/PID");
            return;
        }

        QString err;
        if (!session_.open(vid, pid, true, -1, 2000, &err)) {
            QMessageBox::critical(this,"Открытие ридера", err);
            return;
        }

        const QString backend = QString::fromStdString(session_.info().backend);
        status_->setText(QString("Подключено. Бэкенд: %1").arg(backend));
        log("Подключено к ридеру");
    }
    catch (const std::exception& ex) {
        QMessageBox::critical(this, "Ошибка", QString::fromUtf8(ex.what()));
    }
    catch (...) {
        QMessageBox::critical(this, "Ошибка", "Неизвестная ошибка во время подключения.");
    }
}

void MainWindow::onLoadLayout(){
    auto path = QFileDialog::getOpenFileName(this,"Load RIK-2 layout",".","JSON (*.json)");
    if (path.isEmpty()) return;
    try{
        layout_ = Rik2Parser::parseFile(path);
        rebuildTree();
        status_->setText(QString("Layout: %1").arg(layout_->cardName));
        log(QString("Загружена разметка: %1").arg(path));
    } catch(const std::exception& ex){
        QMessageBox::critical(this,"Layout error", ex.what());
    }
}

void MainWindow::rebuildTree(){
    model_->removeRows(0, model_->rowCount());
    if (!layout_) return;
    auto root = layout_->root.get();
    auto* mf = new QStandardItem(QString("%1 (%2)").arg(root->name).arg(root->fid,4,16,QLatin1Char('0')).toUpper());
    auto* mf_fid = makeItem(QString("%1").arg(root->fid,4,16,QLatin1Char('0')).toUpper());
    auto* mf_type= makeItem("DF");
    auto* mf_size= makeItem("");

    QList<QStandardItem*> row{mf,mf_fid,mf_type,mf_size};
    model_->appendRow(row);

    std::function<void(Node*, QStandardItem*)> add = [&](Node* n, QStandardItem* parent){
        if (n->type==EfType::DF){
            auto* a = makeItem(QString("%1 (%2)").arg(n->name).arg(n->fid,4,16,QLatin1Char('0')).toUpper());
            QList<QStandardItem*> r{a, makeItem(QString("%1").arg(n->fid,4,16,QLatin1Char('0')).toUpper()), makeItem("DF"), makeItem("")};
            parent->appendRow(r);
            for (auto& ch : n->children) add(ch.get(), a);
        } else {
            QString type = (n->type==EfType::Transparent)?"transparent": (n->type==EfType::LinearFixed)?"linear-fixed":"cyclic";
            QString size = (n->type==EfType::Transparent)? QString::number(n->size)
                                                            : QString("%1x%2").arg(n->recordCount).arg(n->recordSize);
            QList<QStandardItem*> r{
                makeItem(n->name),
                makeItem(QString("%1").arg(n->fid,4,16,QLatin1Char('0')).toUpper()),
                makeItem(type),
                makeItem(size)
            };
            parent->appendRow(r);
        }
    };
    for (auto& ch : root->children) add(ch.get(), mf);
    tree_->expandAll();
}

void MainWindow::onReadAll(){
    if (!session_.isOpen() || !layout_){ QMessageBox::warning(this,"Read All","Connect and load layout first."); return; }
    auto dir = QFileDialog::getExistingDirectory(this,"Select output folder",".");
    if (dir.isEmpty()) return;
    prog_->setVisible(true); log("Начато считывание всех файлов…");
    try{
        worker_->readAll(*layout_, QDir(dir), [&](const QString& s){ log(s); });
        log("Считывание всех файлов завершено.");
    } catch(const std::exception& ex){
        QMessageBox::critical(this,"Read All error", ex.what());
    }
    prog_->setVisible(false);
}

void MainWindow::onMarkup(){
    if (!session_.isOpen() || !layout_){ QMessageBox::warning(this,"Markup","Connect and load layout first."); return; }
    if (QMessageBox::question(this,"Разметка","Выполнить разметку карты согласно загруженной разметке?\nЭто может изменить содержимое карты!")!=QMessageBox::Yes) return;
    prog_->setVisible(true); log("Начата разметка карты…");
    try{
        worker_->markupCard(*layout_, [&](const QString& s){ log(s); });
        log("Разметка карты завершена.");
    } catch(const std::exception& ex){
        QMessageBox::critical(this,"Markup error", ex.what());
    }
    prog_->setVisible(false);
}

void MainWindow::onPowerOn(){
    if (!session_.isOpen()){ QMessageBox::warning(this,"ATR","Connect first."); return; }
    try{
        auto atr = worker_->getAtr();
        log(QString("ATR: %1").arg(QString::fromStdString(bytesToHex(atr))));
        if (layout_ && layout_->atrExpected.has_value()){
            QString exp = layout_->atrExpected.value().trimmed().toLower();
            if (!exp.isEmpty()){
                QString got = QString::fromStdString(bytesToHex(atr)).toLower();
                if (got.replace(" ","")==exp.replace(" ","")) log("ATR соответствует ожидаемому.");
                else log("ATR НЕ соответствует ожидаемому.");
            }
        }
        QString ser = worker_->getSerial(*layout_);
        log(QString("Serial: %1").arg(ser));
        status_->setText(QString("ATR: %1 байт").arg(atr.size()));
    } catch(const std::exception& ex){
        QMessageBox::critical(this,"ATR error", ex.what());
    }
}
void MainWindow::onPowerOff(){
    if (!session_.isOpen()) return;
    try{ session_.powerOff(); log("Power off"); } catch(const std::exception& ex){ QMessageBox::critical(this,"PowerOff error", ex.what()); }
}

void MainWindow::log(const QString& s){
    log_->append(s);
}
