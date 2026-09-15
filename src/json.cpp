#include "json.h"

#include <cstdlib>
#include <cstring>

namespace {

struct Lector {
    const char* p;
    const char* fin;

    void blancos() {
        while (p < fin && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    }

    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) {
            s += (char)cp;
        } else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < fin; i++, p++) {
            char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        }
        return v;
    }

    std::string texto() {
        std::string s;
        p++;  // la comilla
        while (p < fin && *p != '"') {
            if (*p == '\\' && p + 1 < fin) {
                p++;
                switch (*p) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u': {
                        p++;
                        unsigned cp = hex4();
                        // Par sustituto.
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < fin && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            unsigned bajo = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (bajo - 0xDC00);
                        }
                        utf8(s, cp);
                        continue;
                    }
                    default: s += *p;
                }
                p++;
            } else {
                s += *p++;
            }
        }
        if (p < fin) p++;
        return s;
    }

    Json valor() {
        Json j;
        blancos();
        if (p >= fin) return j;
        char c = *p;
        if (c == '{') {
            j.tipo = Json::Objeto;
            p++;
            blancos();
            while (p < fin && *p != '}') {
                blancos();
                if (*p != '"') break;
                std::string clave = texto();
                blancos();
                if (p < fin && *p == ':') p++;
                j.objeto[clave] = valor();
                blancos();
                if (p < fin && *p == ',') p++;
                blancos();
            }
            if (p < fin) p++;
        } else if (c == '[') {
            j.tipo = Json::Lista;
            p++;
            blancos();
            while (p < fin && *p != ']') {
                j.lista.push_back(valor());
                blancos();
                if (p < fin && *p == ',') p++;
                blancos();
            }
            if (p < fin) p++;
        } else if (c == '"') {
            j.tipo = Json::Texto;
            j.s = texto();
        } else if (c == 't' || c == 'f') {
            j.tipo = Json::Booleano;
            j.b = c == 't';
            p += j.b ? 4 : 5;
        } else if (c == 'n') {
            p += 4;
        } else {
            j.tipo = Json::Numero;
            char* fin_num = nullptr;
            j.n = strtod(p, &fin_num);
            if (fin_num == p) p++;
            else p = fin_num;
        }
        return j;
    }
};

const Json vacio;

}  // namespace

Json Json::parsear(const std::string& texto) {
    Lector l{texto.data(), texto.data() + texto.size()};
    return l.valor();
}

const Json& Json::operator[](const char* clave) const {
    if (tipo != Objeto) return vacio;
    auto it = objeto.find(clave);
    return it == objeto.end() ? vacio : it->second;
}

const Json& Json::operator[](size_t i) const {
    if (tipo != Lista || i >= lista.size()) return vacio;
    return lista[i];
}

std::string json_texto(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof buf, "\\u%04x", c);
                    r += buf;
                } else {
                    r += (char)c;
                }
        }
    }
    return r + "\"";
}
