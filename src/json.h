// JSON minimo: parser y armado de textos. Lo justo para hablar con el server.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
    enum Tipo { Nulo, Booleano, Numero, Texto, Lista, Objeto };
    Tipo tipo = Nulo;
    bool b = false;
    double n = 0;
    std::string s;  // UTF-8
    std::vector<Json> lista;
    std::map<std::string, Json> objeto;

    static Json parsear(const std::string& texto);

    bool nulo() const { return tipo == Nulo; }
    bool esta(const char* clave) const { return tipo == Objeto && objeto.count(clave); }
    const Json& operator[](const char* clave) const;
    const Json& operator[](size_t i) const;
    size_t largo() const { return tipo == Lista ? lista.size() : 0; }
    std::string str(const char* si_no = "") const { return tipo == Texto ? s : si_no; }
    double num(double si_no = 0) const { return tipo == Numero ? n : si_no; }
    long long entero(long long si_no = 0) const { return tipo == Numero ? (long long)n : si_no; }
    bool bul(bool si_no = false) const { return tipo == Booleano ? b : si_no; }
};

// Escapa un texto UTF-8 para meterlo entre comillas en un JSON.
std::string json_texto(const std::string& s);
