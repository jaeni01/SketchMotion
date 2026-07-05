#pragma once
// ProtoJson.h - Bridge Protocol용 미니 JSON (파서 + 직렬화 헬퍼)
// App(BridgeClient)과 MockBridge가 공유한다. 외부 라이브러리 없음.
// 스키마가 우리 통제 하에 있으므로 완전한 JSON 표준이 아니라
// 프로토콜에 필요한 부분집합(객체/배열/수/문자열/불)만 지원한다.
#include <cctype>
#include <charconv>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace proto {

class JVal {
public:
    enum class Type { Null, Bool, Num, Str, Arr, Obj };

    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    bool IsObj() const { return type == Type::Obj; }
    bool IsArr() const { return type == Type::Arr; }

    const JVal* Get(const std::string& key) const {
        if (type != Type::Obj) return nullptr;
        for (const auto& [k, v] : obj)
            if (k == key) return &v;
        return nullptr;
    }
    double NumOr(const std::string& key, double def) const {
        const JVal* v = Get(key);
        return v && v->type == Type::Num ? v->num : def;
    }
    std::string StrOr(const std::string& key, const std::string& def) const {
        const JVal* v = Get(key);
        return v && v->type == Type::Str ? v->str : def;
    }
};

class JParser {
public:
    static std::optional<JVal> Parse(const std::string& text) {
        JParser p(text);
        JVal v;
        if (!p.Value(v)) return std::nullopt;
        return v;
    }

private:
    explicit JParser(const std::string& t) : m_t(t) {}

    void Ws() { while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i; }
    bool Eat(char c) { Ws(); if (m_i < m_t.size() && m_t[m_i] == c) { ++m_i; return true; } return false; }

    bool Value(JVal& out) {
        Ws();
        if (m_i >= m_t.size()) return false;
        const char c = m_t[m_i];
        if (c == '{') return Object(out);
        if (c == '[') return Array(out);
        if (c == '"') return String(out);
        if (c == 't' || c == 'f') return Boolean(out);
        if (c == 'n') { m_i += 4; out.type = JVal::Type::Null; return true; }
        return Number(out);
    }
    bool Object(JVal& out) {
        out.type = JVal::Type::Obj;
        if (!Eat('{')) return false;
        if (Eat('}')) return true;
        do {
            JVal key;
            if (!String(key) || !Eat(':')) return false;
            JVal val;
            if (!Value(val)) return false;
            out.obj.emplace_back(std::move(key.str), std::move(val));
        } while (Eat(','));
        return Eat('}');
    }
    bool Array(JVal& out) {
        out.type = JVal::Type::Arr;
        if (!Eat('[')) return false;
        if (Eat(']')) return true;
        do {
            JVal v;
            if (!Value(v)) return false;
            out.arr.push_back(std::move(v));
        } while (Eat(','));
        return Eat(']');
    }
    bool String(JVal& out) {
        out.type = JVal::Type::Str;
        if (!Eat('"')) return false;
        out.str.clear();
        while (m_i < m_t.size() && m_t[m_i] != '"') {
            if (m_t[m_i] == '\\' && m_i + 1 < m_t.size()) ++m_i;
            out.str.push_back(m_t[m_i++]);
        }
        return m_i < m_t.size() && m_t[m_i++] == '"';
    }
    bool Boolean(JVal& out) {
        out.type = JVal::Type::Bool;
        if (m_t.compare(m_i, 4, "true") == 0) { out.b = true; m_i += 4; return true; }
        if (m_t.compare(m_i, 5, "false") == 0) { out.b = false; m_i += 5; return true; }
        return false;
    }
    bool Number(JVal& out) {
        Ws();
        out.type = JVal::Type::Num;
        const char* begin = m_t.data() + m_i;
        const char* end = m_t.data() + m_t.size();
        const auto [ptr, ec] = std::from_chars(begin, end, out.num);
        if (ec != std::errc{}) return false;
        m_i += static_cast<size_t>(ptr - begin);
        return true;
    }

    const std::string& m_t;
    size_t m_i = 0;
};

// ---- 직렬화 헬퍼 (필요한 만큼만) ----
inline std::string Esc(const std::string& s) {
    std::string o;
    for (const char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

} // namespace proto
