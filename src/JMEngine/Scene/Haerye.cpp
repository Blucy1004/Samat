#include "JMEngine/Scene/Haerye.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace jm {
namespace {

enum class TokenKind {
    End,
    Identifier,
    String,
    Number,
    Colon,
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    Comma,
};
struct Token {
    TokenKind kind{TokenKind::End};
    std::string text;
    float number{};
    std::size_t line{1};
    std::size_t column{1};
};
struct ParseFailure {
    HaeryeDiagnostic diagnostic;
};

class Lexer {
  public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Token next() {
        skipTrivia();
        Token token;
        token.line = line_;
        token.column = column_;
        if (atEnd())
            return token;
        const char ch = peek();
        if (isAlpha(ch) || ch == '_') {
            token.kind = TokenKind::Identifier;
            while (!atEnd() && (isAlpha(peek()) || isDigit(peek()) || peek() == '_'))
                token.text.push_back(advance());
            return token;
        }
        if (ch == '"') {
            token.kind = TokenKind::String;
            advance();
            while (!atEnd() && peek() != '"') {
                char value = advance();
                if (value == '\\') {
                    if (atEnd())
                        fail(token.line, token.column, "Incomplete string escape.");
                    value = advance();
                    if (value != '\\' && value != '"')
                        fail(line_, column_ - 1, "Unsupported string escape.");
                }
                if (value == '\n' || value == '\r')
                    fail(token.line, token.column, "Quoted strings cannot contain a newline.");
                token.text.push_back(value);
            }
            if (atEnd())
                fail(token.line, token.column, "Unterminated quoted string.");
            advance();
            return token;
        }
        if (isDigit(ch) || (ch == '-' && isDigit(peek(1)))) {
            token.kind = TokenKind::Number;
            const std::size_t start = offset_;
            if (peek() == '-')
                advance();
            while (!atEnd() && isDigit(peek()))
                advance();
            if (!atEnd() && peek() == '.') {
                advance();
                if (atEnd() || !isDigit(peek()))
                    fail(token.line, token.column, "A decimal point must be followed by digits.");
                while (!atEnd() && isDigit(peek()))
                    advance();
            }
            if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
                advance();
                if (!atEnd() && (peek() == '+' || peek() == '-'))
                    advance();
                if (atEnd() || !isDigit(peek()))
                    fail(token.line, token.column, "An exponent must contain digits.");
                while (!atEnd() && isDigit(peek()))
                    advance();
            }
            token.text = std::string(source_.substr(start, offset_ - start));
            try {
                std::size_t used = 0;
                token.number = std::stof(token.text, &used);
                if (used != token.text.size() || !std::isfinite(token.number))
                    fail(token.line, token.column, "Number must be finite.");
            } catch (const ParseFailure &) {
                throw;
            } catch (...) {
                fail(token.line, token.column, "Number is outside the supported range.");
            }
            return token;
        }
        advance();
        switch (ch) {
        case ':': token.kind = TokenKind::Colon; return token;
        case '{': token.kind = TokenKind::LeftBrace; return token;
        case '}': token.kind = TokenKind::RightBrace; return token;
        case '(': token.kind = TokenKind::LeftParen; return token;
        case ')': token.kind = TokenKind::RightParen; return token;
        case ',': token.kind = TokenKind::Comma; return token;
        default: fail(token.line, token.column, std::string("Unexpected character '") + ch + "'.");
        }
    }

  private:
    static bool isAlpha(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }
    static bool isDigit(char ch) { return ch >= '0' && ch <= '9'; }
    bool atEnd() const { return offset_ >= source_.size(); }
    char peek(std::size_t ahead = 0) const {
        return offset_ + ahead < source_.size() ? source_[offset_ + ahead] : '\0';
    }
    char advance() {
        const char ch = source_[offset_++];
        if (ch == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return ch;
    }
    void skipTrivia() {
        while (!atEnd()) {
            if (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') {
                advance();
                continue;
            }
            if (peek() == '/' && peek(1) == '/') {
                while (!atEnd() && peek() != '\n')
                    advance();
                continue;
            }
            break;
        }
    }
    [[noreturn]] static void fail(std::size_t line, std::size_t column, std::string message) {
        throw ParseFailure{{line, column, std::move(message)}};
    }
    std::string_view source_;
    std::size_t offset_{};
    std::size_t line_{1};
    std::size_t column_{1};
};

Vec3 parseColor(const Token &token) {
    if (token.text.size() != 7 || token.text[0] != '#')
        throw ParseFailure{{token.line, token.column, "Color must use the #RRGGBB form."}};
    auto byte = [&](std::size_t index) {
        unsigned value = 0;
        for (std::size_t i = index; i < index + 2; ++i) {
            const char ch = token.text[i];
            const int digit = ch >= '0' && ch <= '9' ? ch - '0'
                              : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                              : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                                      : -1;
            if (digit < 0)
                throw ParseFailure{{token.line, token.column, "Color contains a non-hexadecimal digit."}};
            value = value * 16U + static_cast<unsigned>(digit);
        }
        return static_cast<float>(value) / 255.0F;
    };
    return {byte(1), byte(3), byte(5)};
}

class Parser {
  public:
    explicit Parser(std::string_view source) : lexer_(source), current_(lexer_.next()) {}

    HaeryeDocument parse() {
        expectIdentifier("scene", "A Haerye document must start with scene.");
        HaeryeDocument document;
        document.name = expect(TokenKind::String, "Expected a quoted scene name.").text;
        expect(TokenKind::LeftBrace, "Expected '{' after the scene name.");
        bool hasBackground = false;
        while (current_.kind != TokenKind::RightBrace) {
            if (current_.kind == TokenKind::End)
                fail(current_, "Expected '}' to close the scene.");
            if (isIdentifier("object")) {
                document.objects.push_back(parseObject());
                continue;
            }
            if (!isIdentifier("background"))
                fail(current_, current_.kind == TokenKind::Identifier
                                   ? "Unknown scene property '" + current_.text + "'."
                                   : "Expected background or object inside the scene.");
            const auto property = current_;
            advance();
            if (hasBackground)
                fail(property, "Duplicate scene property 'background'.");
            expect(TokenKind::Colon, "Expected ':' after background.");
            const Token &value = expect(TokenKind::String, "Expected a quoted #RRGGBB color.");
            document.backgroundColor = parseColor(value);
            hasBackground = true;
        }
        advance();
        if (current_.kind != TokenKind::End) {
            if (isIdentifier("scene"))
                fail(current_, "A Haerye document can contain only one scene.");
            fail(current_, "Unexpected content after the scene declaration.");
        }
        return document;
    }

  private:
    HaeryeObject parseObject() {
        const Token objectToken = current_;
        advance();
        HaeryeObject object;
        object.name = expect(TokenKind::String, "Expected a quoted object name.").text;
        object.sourceLine = objectToken.line;
        object.sourceColumn = objectToken.column;
        if (object.name.empty())
            fail(objectToken, "Object name cannot be empty.");
        expect(TokenKind::LeftBrace, "Expected '{' after the object name.");
        std::vector<std::string> properties;
        while (current_.kind != TokenKind::RightBrace) {
            if (current_.kind == TokenKind::End)
                fail(current_, "Expected '}' to close the object.");
            const Token property = expect(TokenKind::Identifier, "Expected an object property name.");
            if (std::find(properties.begin(), properties.end(), property.text) != properties.end())
                fail(property, "Duplicate object property '" + property.text + "'.");
            properties.push_back(property.text);
            expect(TokenKind::Colon, "Expected ':' after object property.");
            if (property.text == "shape") {
                const Token &value = expect(TokenKind::Identifier, "Expected rectangle or circle.");
                object.shape = value.text == "rectangle" ? HaeryeShape::Rectangle
                               : value.text == "circle" ? HaeryeShape::Circle
                                                         : HaeryeShape::Unknown;
            } else if (property.text == "position") {
                object.position = vector2("position");
            } else if (property.text == "size") {
                object.size = vector2("size");
            } else if (property.text == "rotation") {
                object.rotation = expect(TokenKind::Number, "Expected a numeric rotation in degrees.").number;
            } else if (property.text == "color") {
                object.color = parseColor(expect(TokenKind::String, "Expected a quoted #RRGGBB color."));
            } else if (property.text == "sprite" || property.text == "texture" || property.text == "asset") {
                const Token &asset = expect(TokenKind::String, "Expected a quoted asset path.");
                fail(property, "Asset references are unsupported in Haerye v0.1: '" + asset.text + "'.");
            } else {
                fail(property, "Unknown object property '" + property.text + "'.");
            }
        }
        advance();
        return object;
    }

    std::array<float, 2> vector2(std::string_view property) {
        expect(TokenKind::LeftParen, "Expected '(' before " + std::string(property) + " vector.");
        const float x = expect(TokenKind::Number, "Expected the x coordinate.").number;
        expect(TokenKind::Comma, "Expected ',' between vector coordinates.");
        const float y = expect(TokenKind::Number, "Expected the y coordinate.").number;
        expect(TokenKind::RightParen, "Expected ')' after vector coordinates.");
        return {x, y};
    }
    bool isIdentifier(std::string_view value) const {
        return current_.kind == TokenKind::Identifier && current_.text == value;
    }
    void expectIdentifier(std::string_view value, std::string message) {
        if (!isIdentifier(value))
            fail(current_, std::move(message));
        advance();
    }
    Token expect(TokenKind kind, std::string message) {
        if (current_.kind != kind)
            fail(current_, std::move(message));
        Token result = current_;
        advance();
        return result;
    }
    void advance() { current_ = lexer_.next(); }
    [[noreturn]] static void fail(const Token &token, std::string message) {
        throw ParseFailure{{token.line, token.column, std::move(message)}};
    }
    Lexer lexer_;
    Token current_;
};

std::string formatFloat(float value) {
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
    return output.str();
}
std::string formatColor(Vec3 color) {
    auto byte = [](float value) {
        return static_cast<unsigned>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    std::ostringstream output;
    output << '#' << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << byte(color.x)
           << std::setw(2) << byte(color.y) << std::setw(2) << byte(color.z);
    return output.str();
}
std::string quote(std::string_view value) {
    std::string result{"\""};
    for (char ch : value) {
        if (ch == '\\' || ch == '"')
            result.push_back('\\');
        result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

} // namespace

std::string HaeryeDiagnostic::toString() const {
    return std::to_string(line) + ":" + std::to_string(column) + ": " + message;
}

bool parseHaerye(std::string_view source, HaeryeDocument &document, HaeryeDiagnostic &diagnostic) {
    try {
        HaeryeDocument candidate = Parser(source).parse();
        document = std::move(candidate);
        diagnostic = {};
        return true;
    } catch (const ParseFailure &error) {
        diagnostic = error.diagnostic;
    } catch (const std::exception &error) {
        diagnostic = {1, 1, error.what()};
    }
    return false;
}

bool validateHaerye(const HaeryeDocument &document, HaeryeDiagnostic &diagnostic) {
    if (document.name.empty()) {
        diagnostic = {1, 1, "Scene name cannot be empty."};
        return false;
    }
    auto validColor = [](Vec3 color) {
        for (float channel : {color.x, color.y, color.z}) {
            if (!std::isfinite(channel) || channel < 0.0F || channel > 1.0F ||
                static_cast<float>(std::lround(channel * 255.0F)) / 255.0F != channel)
                return false;
        }
        return true;
    };
    if (!validColor(document.backgroundColor)) {
        diagnostic = {1, 1, "Background color must use exact 8-bit #RRGGBB channels."};
        return false;
    }
    std::vector<std::string> names;
    names.reserve(document.objects.size());
    for (const HaeryeObject &object : document.objects) {
        if (object.name.empty()) {
            diagnostic = {object.sourceLine, object.sourceColumn, "Object name cannot be empty."};
            return false;
        }
        if (std::find(names.begin(), names.end(), object.name) != names.end()) {
            diagnostic = {object.sourceLine, object.sourceColumn,
                          "Duplicate object name '" + object.name + "'."};
            return false;
        }
        names.push_back(object.name);
        if (!object.shape || *object.shape == HaeryeShape::Unknown) {
            diagnostic = {object.sourceLine, object.sourceColumn,
                          "Object '" + object.name + "' needs shape rectangle or circle."};
            return false;
        }
        if (!object.position || !object.size || !object.color) {
            diagnostic = {object.sourceLine, object.sourceColumn,
                          "Object '" + object.name + "' needs shape, position, size, and color."};
            return false;
        }
        for (float value : *object.position) {
            if (!std::isfinite(value)) {
                diagnostic = {object.sourceLine, object.sourceColumn, "Object position values must be finite."};
                return false;
            }
        }
        for (float value : *object.size) {
            if (!std::isfinite(value) || value <= 0.0F) {
                diagnostic = {object.sourceLine, object.sourceColumn,
                              "Object size values must be finite and greater than zero."};
                return false;
            }
        }
        if (object.rotation && !std::isfinite(*object.rotation)) {
            diagnostic = {object.sourceLine, object.sourceColumn, "Object rotation must be finite."};
            return false;
        }
        if (!validColor(*object.color)) {
            diagnostic = {object.sourceLine, object.sourceColumn,
                          "Object color must use exact 8-bit #RRGGBB channels."};
            return false;
        }
    }
    diagnostic = {};
    return true;
}

bool instantiateHaerye(const HaeryeDocument &document, Scene &scene, HaeryeDiagnostic &diagnostic) {
    if (!validateHaerye(document, diagnostic))
        return false;
    std::vector<GameObject> objects;
    objects.reserve(document.objects.size());
    for (const HaeryeObject &source : document.objects) {
        GameObject object;
        object.id = "haerye:" + source.name;
        object.kind = ObjectKind::Sprite2D;
        object.shape = *source.shape == HaeryeShape::Circle ? SpriteShape::Circle : SpriteShape::Rectangle;
        object.name = source.name;
        object.position = {(*source.position)[0], (*source.position)[1], 0.0F};
        object.scale = {(*source.size)[0], (*source.size)[1], 1.0F};
        object.rotationDegrees.z = source.rotation.value_or(0.0F);
        object.color = *source.color;
        objects.push_back(std::move(object));
    }
    scene.setName(document.name);
    scene.setBackgroundColor(document.backgroundColor);
    scene.replaceObjects(std::move(objects));
    diagnostic = {};
    return true;
}

std::string serializeHaerye(const HaeryeDocument &document) {
    HaeryeDiagnostic diagnostic;
    if (!validateHaerye(document, diagnostic))
        throw std::invalid_argument(diagnostic.toString());
    std::ostringstream output;
    output << "scene " << quote(document.name) << " {\n";
    constexpr Vec3 defaultBackground{14.0F / 255.0F, 18.0F / 255.0F, 27.0F / 255.0F};
    if (document.backgroundColor.x != defaultBackground.x ||
        document.backgroundColor.y != defaultBackground.y ||
        document.backgroundColor.z != defaultBackground.z)
        output << "    background: " << quote(formatColor(document.backgroundColor)) << "\n";
    for (const HaeryeObject &object : document.objects) {
        output << "\n    object " << quote(object.name) << " {\n"
               << "        shape: " << (*object.shape == HaeryeShape::Rectangle ? "rectangle" : "circle") << "\n"
               << "        position: (" << formatFloat((*object.position)[0]) << ", "
               << formatFloat((*object.position)[1]) << ")\n"
               << "        size: (" << formatFloat((*object.size)[0]) << ", " << formatFloat((*object.size)[1])
               << ")\n";
        if (object.rotation)
            output << "        rotation: " << formatFloat(*object.rotation) << "\n";
        output << "        color: " << quote(formatColor(*object.color)) << "\n"
               << "    }\n";
    }
    output << "}\n";
    return output.str();
}

} // namespace jm
