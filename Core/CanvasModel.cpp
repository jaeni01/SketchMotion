#include "CanvasModel.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace sm {

void CanvasModel::AddStroke(Stroke s) {
    m_lastId = std::max(m_lastId, s.Id());
    m_strokes.push_back(std::move(s));
}

bool CanvasModel::RemoveStroke(uint64_t id) {
    const auto it = std::find_if(m_strokes.begin(), m_strokes.end(),
                                 [id](const Stroke& s) { return s.Id() == id; });
    if (it == m_strokes.end())
        return false;
    m_strokes.erase(it);
    return true;
}

const Stroke* CanvasModel::FindStroke(uint64_t id) const {
    const auto it = std::find_if(m_strokes.begin(), m_strokes.end(),
                                 [id](const Stroke& s) { return s.Id() == id; });
    return it == m_strokes.end() ? nullptr : &*it;
}

void CanvasModel::Clear() {
    m_strokes.clear();
    m_lastId = 0;
}

std::optional<uint64_t> CanvasModel::TopmostHit(const PointF& p, float tolerance) const {
    for (auto it = m_strokes.rbegin(); it != m_strokes.rend(); ++it)
        if (it->HitTest(p, tolerance))
            return it->Id();
    return std::nullopt;
}

std::vector<std::vector<PointF>> CanvasModel::AllPolylines() const {
    std::vector<std::vector<PointF>> out;
    for (const auto& s : m_strokes)
        for (auto& line : s.ToPolylines())
            if (line.size() >= 2)
                out.push_back(std::move(line));
    return out;
}

// ---------------------------------------------------------------------------
// JSON 직렬화. 포맷은 우리가 완전히 통제하므로 파서도 이 스키마만 감당하면 된다.
// ---------------------------------------------------------------------------

std::string CanvasModel::ToJson() const {
    std::ostringstream os;
    os << "{\n";
    os << "  \"format\": \"skm\",\n";
    os << "  \"version\": " << kFormatVersion << ",\n";
    os << "  \"canvas\": { \"width\": " << m_width << ", \"height\": " << m_height << " },\n";
    os << "  \"strokes\": [\n";
    for (size_t i = 0; i < m_strokes.size(); ++i) {
        const Stroke& s = m_strokes[i];
        const ColorRGBA c = s.Color();
        os << "    { \"id\": " << s.Id()
           << ", \"kind\": " << static_cast<int>(s.Kind())
           << ", \"color\": [" << int(c.r) << "," << int(c.g) << "," << int(c.b) << "," << int(c.a) << "]"
           << ", \"width\": " << s.Width()
           << ", \"points\": [";
        const auto& pts = s.Points();
        for (size_t j = 0; j < pts.size(); ++j) {
            if (j) os << ",";
            os << pts[j].x << "," << pts[j].y;
        }
        os << "] }";
        if (i + 1 < m_strokes.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}\n";
    return os.str();
}

namespace {

// .skm 스키마 전용 미니 JSON 파서 (객체/배열/숫자/문자열)
class JsonCursor {
public:
    explicit JsonCursor(const std::string& text) : m_text(text) {}

    void SkipWs() {
        while (m_pos < m_text.size() &&
               std::isspace(static_cast<unsigned char>(m_text[m_pos])))
            ++m_pos;
    }

    bool Consume(char c) {
        SkipWs();
        if (m_pos < m_text.size() && m_text[m_pos] == c) { ++m_pos; return true; }
        return false;
    }

    bool Peek(char c) {
        SkipWs();
        return m_pos < m_text.size() && m_text[m_pos] == c;
    }

    bool ReadString(std::string& out) {
        SkipWs();
        if (!Consume('"')) return false;
        out.clear();
        while (m_pos < m_text.size() && m_text[m_pos] != '"') {
            if (m_text[m_pos] == '\\' && m_pos + 1 < m_text.size()) ++m_pos;
            out.push_back(m_text[m_pos++]);
        }
        return Consume('"');
    }

    bool ReadNumber(double& out) {
        SkipWs();
        const char* begin = m_text.data() + m_pos;
        const char* end = m_text.data() + m_text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, out);
        if (ec != std::errc{}) return false;
        m_pos += static_cast<size_t>(ptr - begin);
        return true;
    }

    // 현재 위치의 값(객체/배열/스칼라)을 통째로 건너뛴다 - 미지 키 무시용
    bool SkipValue() {
        SkipWs();
        if (m_pos >= m_text.size()) return false;
        const char c = m_text[m_pos];
        if (c == '"') { std::string tmp; return ReadString(tmp); }
        if (c == '{' || c == '[') {
            const char open = c, close = (c == '{') ? '}' : ']';
            int depth = 0;
            bool inString = false;
            while (m_pos < m_text.size()) {
                const char ch = m_text[m_pos];
                if (inString) {
                    if (ch == '\\') ++m_pos;
                    else if (ch == '"') inString = false;
                } else if (ch == '"') {
                    inString = true;
                } else if (ch == open) {
                    ++depth;
                } else if (ch == close) {
                    if (--depth == 0) { ++m_pos; return true; }
                }
                ++m_pos;
            }
            return false;
        }
        // 숫자/true/false/null
        while (m_pos < m_text.size() &&
               m_text[m_pos] != ',' && m_text[m_pos] != '}' && m_text[m_pos] != ']')
            ++m_pos;
        return true;
    }

private:
    const std::string& m_text;
    size_t m_pos = 0;
};

bool ParseNumberArray(JsonCursor& cur, std::vector<double>& out) {
    out.clear();
    if (!cur.Consume('[')) return false;
    if (cur.Consume(']')) return true;
    do {
        double v = 0;
        if (!cur.ReadNumber(v)) return false;
        out.push_back(v);
    } while (cur.Consume(','));
    return cur.Consume(']');
}

bool ParseStroke(JsonCursor& cur, Stroke& out) {
    if (!cur.Consume('{')) return false;

    uint64_t id = 0;
    int kind = 0;
    ColorRGBA color{};
    float width = 2.0f;
    std::vector<PointF> points;

    if (!cur.Peek('}')) {
        do {
            std::string key;
            if (!cur.ReadString(key) || !cur.Consume(':')) return false;
            if (key == "id") {
                double v; if (!cur.ReadNumber(v)) return false;
                id = static_cast<uint64_t>(v);
            } else if (key == "kind") {
                double v; if (!cur.ReadNumber(v)) return false;
                kind = static_cast<int>(v);
                if (kind < 0 || kind > static_cast<int>(StrokeKind::Ellipse)) return false;
            } else if (key == "color") {
                std::vector<double> c;
                if (!ParseNumberArray(cur, c) || c.size() != 4) return false;
                color = { static_cast<uint8_t>(c[0]), static_cast<uint8_t>(c[1]),
                          static_cast<uint8_t>(c[2]), static_cast<uint8_t>(c[3]) };
            } else if (key == "width") {
                double v; if (!cur.ReadNumber(v)) return false;
                width = static_cast<float>(v);
            } else if (key == "points") {
                std::vector<double> flat;
                if (!ParseNumberArray(cur, flat) || flat.size() % 2 != 0) return false;
                points.reserve(flat.size() / 2);
                for (size_t i = 0; i + 1 < flat.size(); i += 2)
                    points.push_back({ static_cast<float>(flat[i]),
                                       static_cast<float>(flat[i + 1]) });
            } else {
                if (!cur.SkipValue()) return false;
            }
        } while (cur.Consume(','));
    }
    if (!cur.Consume('}')) return false;

    out = Stroke(id, static_cast<StrokeKind>(kind), color, width);
    out.SetPoints(std::move(points));
    return true;
}

} // namespace

std::optional<CanvasModel> CanvasModel::FromJson(const std::string& json) {
    JsonCursor cur(json);
    if (!cur.Consume('{')) return std::nullopt;

    CanvasModel model;
    bool formatOk = false;

    if (!cur.Peek('}')) {
        do {
            std::string key;
            if (!cur.ReadString(key) || !cur.Consume(':')) return std::nullopt;
            if (key == "format") {
                std::string fmt;
                if (!cur.ReadString(fmt) || fmt != "skm") return std::nullopt;
                formatOk = true;
            } else if (key == "version") {
                double v; if (!cur.ReadNumber(v)) return std::nullopt;
                if (static_cast<int>(v) > kFormatVersion) return std::nullopt;
            } else if (key == "canvas") {
                if (!cur.Consume('{')) return std::nullopt;
                do {
                    std::string ck;
                    if (!cur.ReadString(ck) || !cur.Consume(':')) return std::nullopt;
                    double v; if (!cur.ReadNumber(v)) return std::nullopt;
                    if (ck == "width")  model.m_width = static_cast<float>(v);
                    if (ck == "height") model.m_height = static_cast<float>(v);
                } while (cur.Consume(','));
                if (!cur.Consume('}')) return std::nullopt;
            } else if (key == "strokes") {
                if (!cur.Consume('[')) return std::nullopt;
                if (!cur.Peek(']')) {
                    do {
                        Stroke s;
                        if (!ParseStroke(cur, s)) return std::nullopt;
                        model.AddStroke(std::move(s));
                    } while (cur.Consume(','));
                }
                if (!cur.Consume(']')) return std::nullopt;
            } else {
                if (!cur.SkipValue()) return std::nullopt;
            }
        } while (cur.Consume(','));
    }
    if (!cur.Consume('}') || !formatOk) return std::nullopt;
    return model;
}

} // namespace sm
