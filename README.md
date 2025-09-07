Установка зависимостей (варианты):

```bash
# Qt5
sudo apt-get update
sudo apt-get install -y cmake g++ libusb-1.0-0 libusb-1.0-0-dev \
                        qtbase5-dev libqt5widgets5 libqt5gui5 libqt5core5a qtwayland5

# Qt6 (если собираете с Qt6)
# sudo apt-get install -y cmake g++ libusb-1.0-0 libusb-1.0-0-dev qt6-base-dev

Ридер без sudo: добавьте udev-правило для 072f:9000:

echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="072f", ATTR{idProduct}=="9000", MODE="0666"' \
| sudo tee /etc/udev/rules.d/99-acr38.rules >/dev/null

sudo udevadm control --reload-rules
sudo udevadm trigger

Если ридер «занят» службой PC/SC, для тестов можно временно остановить:
sudo systemctl stop pcscd (и запустить обратно после проверки).

Положите libacr38usb.so в /usr/local/lib и выполните sudo ldconfig, либо используйте:

export LD_LIBRARY_PATH=$PWD:$LD_LIBRARY_PATH
QT_IM_MODULE=xim ./rik2gui

Работа:

Консольная утилита Reader:
./Reader --help
./Reader info
./Reader status
./Reader poweron
./Reader xfr "00 A4 04 00 00"

GUI rik2gui:
«Библиотека» — укажите /usr/local/lib/libacr38usb.so (или оставьте acr38usb, если установлено).

«Подключить» — введите VID 072F и PID 9000.

«Загрузить разметку…» — выберите JSON-описание структуры «РИК-2».

«Подача питания (ATR)» — в лог попадёт ATR и серийный номер (если задан в разметке).

«Считать все» — выберите папку; файлы сохранятся согласно полю saveAs.

«Разметить» — выполняет APDU из createApdus для подготовки новой карты (осторожно: изменяет карту).

Если используете неустановленную .so, запустите с локальным путём:
LD_LIBRARY_PATH=/путь/к/acr38usb/build:$LD_LIBRARY_PATH ./rik2gui
