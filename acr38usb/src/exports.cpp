#include "ReaderApi.h"
#include "acr38usb.h"

using namespace smartio;

extern "C" {

READER_API ICardReader* create_reader() {
    try { return new Acr38Usb(); }
    catch (...) { return nullptr; }
}

READER_API void destroy_reader(ICardReader* p) {
    delete p;
}

READER_API const char* reader_library_version() {
    return "acr38usb 0.3";
}

}
