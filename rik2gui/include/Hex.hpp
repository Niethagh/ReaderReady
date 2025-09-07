#pragma once
#include <vector>
#include <string>
#include <stdexcept>
#include <cctype>
#include <cstdint>

inline std::vector<std::uint8_t> hexToBytes(const std::string& s){
    auto nib = [](char c)->int {
        if (c>='0' && c<='9') return c-'0';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (c>='A' && c<='F') return c-'A'+10;
        return -1;
    };
    std::vector<std::uint8_t> out; out.reserve(s.size()/2);
    int hi = -1;
    for (char c: s){
        if (c==' '||c==':'||c=='\t'||c=='\n'||c=='\r') continue;
        int v = nib(c); if (v<0) throw std::runtime_error("Некорректная hex-строка");
        if (hi<0) hi=v;
        else { out.push_back(static_cast<std::uint8_t>((hi<<4)|v)); hi=-1; }
    }
    if (hi>=0) throw std::runtime_error("Нечётная длина hex-строки");
    return out;
}

inline std::string bytesToHex(const std::vector<std::uint8_t>& v){
    static const char* H="0123456789abcdef";
    std::string s; s.reserve(v.size()*3);
    for (size_t i=0;i<v.size();++i){
        std::uint8_t b=v[i];
        s.push_back(H[(b>>4)&0xF]); s.push_back(H[b&0xF]);
        if (i+1<v.size()) s.push_back(' ');
    }
    return s;
}

inline std::uint16_t parseFid(const std::string& fidHex){
    auto b = hexToBytes(fidHex);
    if (b.size()!=2) throw std::runtime_error("FID должен состоять из 2 байт");
    return (std::uint16_t(b[0])<<8) | b[1];
}
