// Minimal JSON construction (objects/arrays of scalars + strings) for the
// ABI list payloads. Build-only — the core never parses JSON.

#ifndef ARCA_JSON_WRITER_H
#define ARCA_JSON_WRITER_H

#include <cstdint>
#include <string>

namespace arca {

class JsonWriter {
public:
    void begin_array() { sep(); buf_ += '['; fresh_ = true; }
    void end_array() { buf_ += ']'; fresh_ = false; }
    void begin_object() { sep(); buf_ += '{'; fresh_ = true; }
    void end_object() { buf_ += '}'; fresh_ = false; }

    void key(const char *k) {
        sep();
        append_string(k);
        buf_ += ':';
        fresh_ = true;  // value follows without comma
    }

    void value(const std::string &s) { sep(); append_string(s.c_str()); }
    void value(const char *s) { sep(); append_string(s); }
    void value(int64_t v) { sep(); buf_ += std::to_string(v); }
    void value(double v) { sep(); buf_ += std::to_string(v); }
    void value(bool v) { sep(); buf_ += v ? "true" : "false"; }
    void value_null() { sep(); buf_ += "null"; }

    const std::string &str() const { return buf_; }

private:
    void sep() {
        if (!fresh_ && !buf_.empty()) {
            char c = buf_.back();
            if (c != '[' && c != '{' && c != ':')
                buf_ += ',';
        }
        fresh_ = false;
    }

    void append_string(const char *s) {
        buf_ += '"';
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
             *p; p++) {
            switch (*p) {
            case '"': buf_ += "\\\""; break;
            case '\\': buf_ += "\\\\"; break;
            case '\b': buf_ += "\\b"; break;
            case '\f': buf_ += "\\f"; break;
            case '\n': buf_ += "\\n"; break;
            case '\r': buf_ += "\\r"; break;
            case '\t': buf_ += "\\t"; break;
            default:
                if (*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", *p);
                    buf_ += esc;
                } else {
                    buf_ += static_cast<char>(*p);
                }
            }
        }
        buf_ += '"';
    }

    std::string buf_;
    bool fresh_ = true;
};

} // namespace arca

#endif // ARCA_JSON_WRITER_H
