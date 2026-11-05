// SPDX-License-Identifier: Apache-2.0
#include "decode.hpp"

#include <cctype>
#include <stdexcept>

namespace adari::detail {

// -------------------------------------------------------------------- XML

const std::string* XmlNode::attr(const std::string& key) const {
    for (const auto& [k, v] : attrs)
        if (k == key) return &v;
    return nullptr;
}

std::string XmlNode::attr_or(const std::string& key, const std::string& fallback) const {
    const std::string* v = attr(key);
    return v ? *v : fallback;
}

const XmlNode* XmlNode::child(const std::string& n) const {
    for (const XmlNode& c : children)
        if (c.name == n) return &c;
    return nullptr;
}

namespace {

class XmlParser {
public:
    explicit XmlParser(const std::string& s) : s_(s) {}

    XmlNode document() {
        skip_misc();
        if (!at('<')) fail("expected a root element");
        XmlNode root = element();
        skip_misc();
        return root;
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        std::size_t line = 1;
        for (std::size_t k = 0; k < i_ && k < s_.size(); ++k) line += s_[k] == '\n';
        throw std::runtime_error("XML line " + std::to_string(line) + ": " + what);
    }
    bool at(char c) const { return i_ < s_.size() && s_[i_] == c; }
    bool starts(const char* p) const { return s_.compare(i_, std::char_traits<char>::length(p), p) == 0; }
    void skip_ws() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    void skip_until(const char* end) {
        const std::size_t p = s_.find(end, i_);
        if (p == std::string::npos) fail(std::string("missing '") + end + "'");
        i_ = p + std::char_traits<char>::length(end);
    }
    // Skips whitespace, comments, processing instructions and doctypes.
    void skip_misc() {
        while (true) {
            skip_ws();
            if (starts("<?")) skip_until("?>");
            else if (starts("<!--")) skip_until("-->");
            else if (starts("<!DOCTYPE")) skip_until(">");
            else return;
        }
    }
    std::string name() {
        const std::size_t b = i_;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ':') ++i_;
            else break;
        }
        if (b == i_) fail("expected a name");
        return s_.substr(b, i_ - b);
    }
    static void put_utf8(std::string& out, unsigned long cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    std::string unescape(const std::string& raw) const {
        std::string out;
        for (std::size_t k = 0; k < raw.size(); ++k) {
            if (raw[k] != '&') {
                out += raw[k];
                continue;
            }
            const std::size_t semi = raw.find(';', k);
            if (semi == std::string::npos) {
                out += raw[k];
                continue;
            }
            const std::string ent = raw.substr(k + 1, semi - k - 1);
            if (ent == "amp") out += '&';
            else if (ent == "lt") out += '<';
            else if (ent == "gt") out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (!ent.empty() && ent[0] == '#') {
                try {
                    const unsigned long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                                                 ? std::stoul(ent.substr(2), nullptr, 16)
                                                 : std::stoul(ent.substr(1));
                    put_utf8(out, cp);
                } catch (...) {
                    out += "&" + ent + ";";
                }
            } else {
                out += "&" + ent + ";";
            }
            k = semi;
        }
        return out;
    }
    XmlNode element() {
        ++i_;  // '<'
        XmlNode n;
        n.name = name();
        while (true) {
            skip_ws();
            if (starts("/>")) {
                i_ += 2;
                return n;
            }
            if (at('>')) {
                ++i_;
                break;
            }
            std::string key = name();
            skip_ws();
            if (!at('=')) fail("expected '=' after attribute '" + key + "'");
            ++i_;
            skip_ws();
            if (!at('"') && !at('\'')) fail("expected a quoted value for '" + key + "'");
            const char q = s_[i_++];
            const std::size_t end = s_.find(q, i_);
            if (end == std::string::npos) fail("unterminated attribute value");
            n.attrs.emplace_back(std::move(key), unescape(s_.substr(i_, end - i_)));
            i_ = end + 1;
        }
        while (true) {
            if (i_ >= s_.size()) fail("unexpected end inside <" + n.name + ">");
            if (starts("</")) {
                i_ += 2;
                const std::string close = name();
                if (close != n.name) fail("</" + close + "> closes <" + n.name + ">");
                skip_ws();
                if (!at('>')) fail("expected '>'");
                ++i_;
                return n;
            }
            if (starts("<!--")) skip_until("-->");
            else if (starts("<![CDATA[")) {
                i_ += 9;
                const std::size_t end = s_.find("]]>", i_);
                if (end == std::string::npos) fail("unterminated CDATA");
                n.text += s_.substr(i_, end - i_);
                i_ = end + 3;
            } else if (starts("<?")) skip_until("?>");
            else if (at('<')) n.children.push_back(element());
            else {
                const std::size_t end = s_.find('<', i_);
                if (end == std::string::npos) fail("unexpected end of document");
                n.text += unescape(s_.substr(i_, end - i_));
                i_ = end;
            }
        }
    }
};

}  // namespace

XmlNode parse_xml(const std::string& text) { return XmlParser(text).document(); }

// ----------------------------------------------------------------- base64

std::vector<std::uint8_t> base64_decode(const std::string& text) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int v = val(c);
        if (v < 0) {
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            throw std::runtime_error("invalid base64 character");
        }
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFFu));
        }
    }
    return out;
}

// ---------------------------------------------------------------- inflate

namespace {

struct Huffman {
    short count[16] = {};
    short symbol[288] = {};
};

class Inflater {
public:
    Inflater(const std::uint8_t* data, std::size_t size) : in_(data), size_(size) {}

    std::vector<std::uint8_t> run() {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            const int type = bits(2);
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else fail("invalid block type");
        }
        return std::move(out_);
    }
    std::size_t consumed() const { return pos_; }

private:
    const std::uint8_t* in_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint32_t buf_ = 0;
    int cnt_ = 0;
    std::vector<std::uint8_t> out_;

    [[noreturn]] static void fail(const char* what) { throw std::runtime_error(std::string("inflate: ") + what); }

    int bits(int need) {
        std::uint32_t v = buf_;
        while (cnt_ < need) {
            if (pos_ >= size_) fail("unexpected end of data");
            v |= static_cast<std::uint32_t>(in_[pos_++]) << cnt_;
            cnt_ += 8;
        }
        buf_ = v >> need;
        cnt_ -= need;
        return static_cast<int>(v & ((1u << need) - 1u));
    }

    void stored() {
        buf_ = 0;
        cnt_ = 0;
        if (pos_ + 4 > size_) fail("truncated stored block");
        const unsigned len = in_[pos_] | (static_cast<unsigned>(in_[pos_ + 1]) << 8);
        const unsigned nlen = in_[pos_ + 2] | (static_cast<unsigned>(in_[pos_ + 3]) << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu)) fail("stored length mismatch");
        if (pos_ + len > size_) fail("truncated stored block");
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
    }

    static int build(Huffman& h, const short* lengths, int n) {
        for (short& c : h.count) c = 0;
        for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return -1;
        }
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (lengths[s] != 0) h.symbol[offs[lengths[s]]++] = static_cast<short>(s);
        return left;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1);
            const int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        fail("bad Huffman code");
    }

    void codes(const Huffman& lencode, const Huffman& distcode) {
        static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                       3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                        6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        while (true) {
            int sym = decode(lencode);
            if (sym < 256) {
                out_.push_back(static_cast<std::uint8_t>(sym));
            } else if (sym == 256) {
                return;
            } else {
                sym -= 257;
                if (sym >= 29) fail("bad length symbol");
                const int len = lbase[sym] + bits(lext[sym]);
                const int ds = decode(distcode);
                if (ds >= 30) fail("bad distance symbol");
                const std::size_t dist = static_cast<std::size_t>(dbase[ds] + bits(dext[ds]));
                if (dist > out_.size()) fail("distance too far back");
                const std::size_t from = out_.size() - dist;
                for (int k = 0; k < len; ++k) out_.push_back(out_[from + static_cast<std::size_t>(k)]);
            }
        }
    }

    void fixed() {
        static Huffman lencode, distcode;
        static bool ready = false;
        if (!ready) {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            build(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            build(distcode, lengths, 30);
            ready = true;
        }
        codes(lencode, distcode);
    }

    void dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = bits(5) + 257;
        const int ndist = bits(5) + 1;
        const int ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) fail("bad counts");
        short lengths[320] = {};
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = static_cast<short>(bits(3));
        for (; index < 19; ++index) lengths[order[index]] = 0;
        Huffman lencode, distcode;
        if (build(lencode, lengths, 19) != 0) fail("incomplete code-length code");
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 16) {
                lengths[index++] = static_cast<short>(sym);
            } else {
                short len = 0;
                if (sym == 16) {
                    if (index == 0) fail("repeat with no previous length");
                    len = lengths[index - 1];
                    sym = 3 + bits(2);
                } else if (sym == 17) {
                    sym = 3 + bits(3);
                } else {
                    sym = 11 + bits(7);
                }
                if (index + sym > nlen + ndist) fail("too many lengths");
                while (sym--) lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) fail("no end-of-block code");
        const int err = build(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) fail("bad literal/length code");
        const int derr = build(distcode, lengths + nlen, ndist);
        if (derr < 0 || (derr > 0 && ndist - distcode.count[0] != 1)) fail("bad distance code");
        codes(lencode, distcode);
    }
};

}  // namespace

std::vector<std::uint8_t> inflate(const std::vector<std::uint8_t>& data, bool gzip) {
    std::size_t start = 0;
    if (gzip) {
        if (data.size() < 18 || data[0] != 0x1F || data[1] != 0x8B || data[2] != 8)
            throw std::runtime_error("inflate: not a gzip stream");
        const std::uint8_t flags = data[3];
        start = 10;
        if (flags & 4) start += 2 + (data[start] | (static_cast<std::size_t>(data[start + 1]) << 8));
        if (flags & 8) while (start < data.size() && data[start++] != 0) {}
        if (flags & 16) while (start < data.size() && data[start++] != 0) {}
        if (flags & 2) start += 2;
    } else {
        if (data.size() < 6 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0)
            throw std::runtime_error("inflate: not a zlib stream");
        if (data[1] & 0x20) throw std::runtime_error("inflate: preset dictionaries are not supported");
        start = 2;
    }
    if (start >= data.size()) throw std::runtime_error("inflate: truncated header");
    Inflater inf(data.data() + start, data.size() - start);
    return inf.run();
}

}  // namespace adari::detail
