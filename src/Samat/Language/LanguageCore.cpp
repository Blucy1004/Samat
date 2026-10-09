#include "Samat/Language/LanguageCore.hpp"
#include "Samat/Language/RuntimeABI.h"
#include "Samat/Language/StandardLibrary.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace jm::script {
namespace {

struct Token {
    enum class Kind {
        End,
        Newline,
        Indent,
        Dedent,
        Identifier,
        Number,
        String,
        LeftParen,
        RightParen,
        LeftBracket,
        RightBracket,
        LeftBrace,
        RightBrace,
        Comma,
        Colon,
        Dot,
        Range,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Power,
        Bang,
        Equal,
        EqualEqual,
        BangEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        PlusEqual,
        MinusEqual,
        StarEqual,
        SlashEqual,
        PercentEqual,
        BitAnd,
        BitOr,
        ShiftLeft,
        ShiftRight,
        Tilde,
        And,
        Or,
        Arrow,
        Question,
        Coalesce,
        OptionalDot
    };
    Kind kind{Kind::End};
    std::string text;
    double number{0.0};
    std::size_t line{1};
};

bool digitForBase(char value, int base) {
    if (value >= '0' && value <= '9')
        return value - '0' < base;
    if (base == 16 && value >= 'a' && value <= 'f')
        return true;
    return base == 16 && value >= 'A' && value <= 'F';
}

std::string withoutDigitSeparators(std::string_view value, int base = 10) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '_') {
            const bool decimalSyntax = base == 10 &&
                                       (value[index] == '.' || value[index] == 'e' || value[index] == 'E' ||
                                        value[index] == '+' || value[index] == '-');
            if (!digitForBase(value[index], base) && !decimalSyntax)
                throw std::runtime_error("Invalid digit in numeric literal.");
            result.push_back(value[index]);
            continue;
        }
        if (index == 0 || index + 1 == value.size() || !digitForBase(value[index - 1], base) ||
            !digitForBase(value[index + 1], base))
            throw std::runtime_error("Numeric separators must appear between digits.");
    }
    return result;
}

bool isIdentifierStart(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}
bool isIdentifierPart(unsigned char ch) { return isIdentifierStart(ch) || (ch >= '0' && ch <= '9'); }

std::vector<Token> lex(const std::string &source) {
    std::vector<Token> tokens;
    std::vector<std::size_t> indentation{0};
    std::istringstream input(source);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        try {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::size_t position = 0;
            std::size_t indent = 0;
            while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) {
                indent += line[position] == '\t' ? 4U : 1U;
                ++position;
            }
            if (position == line.size() || line[position] == '#')
                continue;
            if (indent > indentation.back()) {
                indentation.push_back(indent);
                tokens.push_back({Token::Kind::Indent, {}, 0.0, lineNumber});
            } else if (indent < indentation.back()) {
                while (indent < indentation.back()) {
                    indentation.pop_back();
                    tokens.push_back({Token::Kind::Dedent, {}, 0.0, lineNumber});
                }
                if (indent != indentation.back())
                    throw std::runtime_error("Indentation must return to a previous block level.");
            }
            while (position < line.size()) {
                const unsigned char ch = static_cast<unsigned char>(line[position]);
                if (ch == ' ' || ch == '\t') {
                    ++position;
                    continue;
                }
                const std::size_t start = position;
                if (ch == '.' && position + 1 < line.size() &&
                    std::isdigit(static_cast<unsigned char>(line[position + 1])) && !tokens.empty() &&
                    (tokens.back().kind == Token::Kind::Identifier ||
                     tokens.back().kind == Token::Kind::RightParen ||
                     tokens.back().kind == Token::Kind::RightBracket)) {
                    tokens.push_back({Token::Kind::Dot, ".", 0.0, lineNumber});
                    ++position;
                    continue;
                }
                if (isIdentifierStart(ch) || ch >= 0x80) {
                    ++position;
                    while (position < line.size() &&
                           (isIdentifierPart(static_cast<unsigned char>(line[position])) ||
                            static_cast<unsigned char>(line[position]) >= 0x80))
                        ++position;
                    const std::string word = line.substr(start, position - start);
                    Token::Kind kind = word == "and"  ? Token::Kind::And
                                       : word == "or" ? Token::Kind::Or
                                                      : Token::Kind::Identifier;
                    tokens.push_back({kind, word, 0.0, lineNumber});
                    continue;
                }
                if ((ch >= '0' && ch <= '9') || (ch == '.' && position + 1 < line.size() &&
                                                 line[position + 1] >= '0' && line[position + 1] <= '9')) {
                    if (ch == '0' && position + 1 < line.size() &&
                        (line[position + 1] == 'x' || line[position + 1] == 'X' ||
                         line[position + 1] == 'b' || line[position + 1] == 'B')) {
                        const int base = line[position + 1] == 'x' || line[position + 1] == 'X' ? 16 : 2;
                        position += 2;
                        while (position < line.size() &&
                               (std::isalnum(static_cast<unsigned char>(line[position])) || line[position] == '_'))
                            ++position;
                        const std::string digits = withoutDigitSeparators(
                            std::string_view(line).substr(start + 2, position - start - 2), base);
                        if (digits.empty())
                            throw std::runtime_error("Base-prefixed integer needs at least one digit.");
                        tokens.push_back({Token::Kind::Number, line.substr(start, position - start), 0.0,
                                          lineNumber});
                        continue;
                    }
                    if (!tokens.empty() && tokens.back().kind == Token::Kind::Dot) {
                        while (position < line.size() &&
                               (std::isdigit(static_cast<unsigned char>(line[position])) || line[position] == '_'))
                            ++position;
                        const auto text = line.substr(start, position - start);
                        const auto normalized = withoutDigitSeparators(text);
                        tokens.push_back({Token::Kind::Number, text, std::strtod(normalized.c_str(), nullptr),
                                          lineNumber});
                        continue;
                    }
                    const std::size_t range = line.find("..", position);
                    if (range != std::string::npos && range > position &&
                        line.substr(position, range - position).find_first_of(" \t+-*/%()[],") ==
                            std::string::npos) {
                        const std::string integerPart = line.substr(position, range - position);
                        const std::string normalized = withoutDigitSeparators(integerPart);
                        char *rangeEnd = nullptr;
                        const double rangeValue = std::strtod(normalized.c_str(), &rangeEnd);
                        if (rangeEnd == normalized.c_str() + normalized.size() &&
                            std::isfinite(rangeValue)) {
                            tokens.push_back({Token::Kind::Number, integerPart, rangeValue, lineNumber});
                            position = range;
                            continue;
                        }
                    }
                    std::string normalized;
                    bool decimalPoint = false;
                    while (position < line.size()) {
                        const char current = line[position];
                        if (std::isdigit(static_cast<unsigned char>(current))) {
                            normalized.push_back(current);
                            ++position;
                        } else if (current == '_') {
                            if (normalized.empty() || !std::isdigit(static_cast<unsigned char>(normalized.back())) ||
                                position + 1 >= line.size() ||
                                !std::isdigit(static_cast<unsigned char>(line[position + 1])))
                                throw std::runtime_error("Numeric separators must appear between digits.");
                            ++position;
                        } else if (current == '.' && !decimalPoint &&
                                   !(position + 1 < line.size() && line[position + 1] == '.')) {
                            decimalPoint = true;
                            normalized.push_back(current);
                            ++position;
                        } else if ((current == 'e' || current == 'E') &&
                                   normalized.find_first_of("eE") == std::string::npos) {
                            normalized.push_back(current);
                            ++position;
                            if (position < line.size() && (line[position] == '+' || line[position] == '-'))
                                normalized.push_back(line[position++]);
                        } else {
                            break;
                        }
                    }
                    char *end = nullptr;
                    const double number = std::strtod(normalized.c_str(), &end);
                    if (normalized.empty() || end != normalized.c_str() + normalized.size() || !std::isfinite(number))
                        throw std::runtime_error("Invalid number literal.");
                    tokens.push_back(
                        {Token::Kind::Number, line.substr(start, position - start), number, lineNumber});
                    continue;
                }
                if (ch == '"' || ch == '\'') {
                    const char quote = static_cast<char>(ch);
                    ++position;
                    std::string value;
                    bool closed = false;
                    while (position < line.size()) {
                        const char current = line[position++];
                        if (current == quote) {
                            closed = true;
                            break;
                        }
                        if (current == '\\' && position < line.size()) {
                            const char escaped = line[position++];
                            if (escaped == 'n')
                                value.push_back('\n');
                            else if (escaped == 't')
                                value.push_back('\t');
                            else if (escaped == 'r')
                                value.push_back('\r');
                            else
                                value.push_back(escaped);
                        } else
                            value.push_back(current);
                    }
                    if (!closed)
                        throw std::runtime_error("Unclosed string literal.");
                    tokens.push_back({Token::Kind::String, std::move(value), 0.0, lineNumber});
                    continue;
                }
                auto push = [&](Token::Kind kind, std::size_t length = 1) {
                    tokens.push_back({kind, line.substr(position, length), 0.0, lineNumber});
                    position += length;
                };
                const char next = position + 1 < line.size() ? line[position + 1] : '\0';
                if (ch == '?' && next == '?')
                    push(Token::Kind::Coalesce, 2);
                else if (ch == '?' && next == '.')
                    push(Token::Kind::OptionalDot, 2);
                else if (ch == '-' && next == '>')
                    push(Token::Kind::Arrow, 2);
                else if (ch == '.' && next == '.')
                    push(Token::Kind::Range, position + 2 < line.size() && line[position + 2] == '<' ? 3 : 2);
                else if (ch == '+' && next == '=')
                    push(Token::Kind::PlusEqual, 2);
                else if (ch == '-' && next == '=')
                    push(Token::Kind::MinusEqual, 2);
                else if (ch == '*' && next == '=')
                    push(Token::Kind::StarEqual, 2);
                else if (ch == '/' && next == '=')
                    push(Token::Kind::SlashEqual, 2);
                else if (ch == '%' && next == '=')
                    push(Token::Kind::PercentEqual, 2);
                else if (ch == '*' && next == '*')
                    push(Token::Kind::Power, 2);
                else if (ch == '<' && next == '<')
                    push(Token::Kind::ShiftLeft, 2);
                else if (ch == '>' && next == '>')
                    push(Token::Kind::ShiftRight, 2);
                else if (ch == '=' && next == '=')
                    push(Token::Kind::EqualEqual, 2);
                else if (ch == '!' && next == '=')
                    push(Token::Kind::BangEqual, 2);
                else if (ch == '<' && next == '=')
                    push(Token::Kind::LessEqual, 2);
                else if (ch == '>' && next == '=')
                    push(Token::Kind::GreaterEqual, 2);
                else if (ch == '&' && next == '&')
                    push(Token::Kind::And, 2);
                else if (ch == '|' && next == '|')
                    push(Token::Kind::Or, 2);
                else
                    switch (ch) {
                    case '?':
                        push(Token::Kind::Question);
                        break;
                    case '(':
                        push(Token::Kind::LeftParen);
                        break;
                    case ')':
                        push(Token::Kind::RightParen);
                        break;
                    case '[':
                        push(Token::Kind::LeftBracket);
                        break;
                    case ']':
                        push(Token::Kind::RightBracket);
                        break;
                    case '{':
                        push(Token::Kind::LeftBrace);
                        break;
                    case '}':
                        push(Token::Kind::RightBrace);
                        break;
                    case ',':
                        push(Token::Kind::Comma);
                        break;
                    case ':':
                        push(Token::Kind::Colon);
                        break;
                    case '.':
                        push(Token::Kind::Dot);
                        break;
                    case '+':
                        push(Token::Kind::Plus);
                        break;
                    case '-':
                        push(Token::Kind::Minus);
                        break;
                    case '*':
                        push(Token::Kind::Star);
                        break;
                    case '/':
                        push(Token::Kind::Slash);
                        break;
                    case '%':
                        push(Token::Kind::Percent);
                        break;
                    case '&':
                        push(Token::Kind::BitAnd);
                        break;
                    case '|':
                        push(Token::Kind::BitOr);
                        break;
                    case '~':
                        push(Token::Kind::Tilde);
                        break;
                    case '^':
                        push(Token::Kind::Power);
                        break;
                    case '!':
                        push(Token::Kind::Bang);
                        break;
                    case '=':
                        push(Token::Kind::Equal);
                        break;
                    case '<':
                        push(Token::Kind::Less);
                        break;
                    case '>':
                        push(Token::Kind::Greater);
                        break;
                    default:
                        throw std::runtime_error("Unexpected character in code expression.");
                    }
            }
            tokens.push_back({Token::Kind::Newline, {}, 0.0, lineNumber});
        } catch (const std::exception &error) {
            throw std::runtime_error("line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    while (indentation.size() > 1) {
        indentation.pop_back();
        tokens.push_back({Token::Kind::Dedent, {}, 0.0, lineNumber + 1});
    }
    tokens.push_back({Token::Kind::End, {}, 0.0, lineNumber + 1});
    return tokens;
}

ExpressionPtr node(Expression::Kind kind, std::string text = {}) {
    auto result = std::make_shared<Expression>();
    result->kind = kind;
    result->text = std::move(text);
    return result;
}

class ExpressionParser {
  public:
    explicit ExpressionParser(const std::vector<Token> &tokens, std::size_t &position)
        : tokens_(tokens), position_(position) {}
    ExpressionPtr parse() { return parseCoalesce(); }

  private:
    const Token &peek() const { return tokens_.at(position_); }
    bool take(Token::Kind kind) {
        if (peek().kind != kind)
            return false;
        ++position_;
        return true;
    }
    Token expect(Token::Kind kind, const char *message) {
        if (!take(kind))
            throw std::runtime_error(message);
        return tokens_.at(position_ - 1);
    }
    ExpressionPtr binary(ExpressionPtr left, const Token &op, ExpressionPtr right) {
        auto result = node(Expression::Kind::Binary, op.text);
        result->left = std::move(left);
        result->right = std::move(right);
        return result;
    }
    ExpressionPtr parseCoalesce() {
        auto value = parseOr();
        if (peek().kind == Token::Kind::Coalesce) {
            auto op = tokens_[position_++];
            return binary(value, op, parseCoalesce());
        }
        return value;
    }
    ExpressionPtr parseOr() {
        auto value = parseAnd();
        while (peek().kind == Token::Kind::Or) {
            Token op = tokens_[position_++];
            value = binary(value, op, parseAnd());
        }
        return value;
    }
    ExpressionPtr parseAnd() {
        auto value = parseBitOr();
        while (peek().kind == Token::Kind::And) {
            Token op = tokens_[position_++];
            value = binary(value, op, parseBitOr());
        }
        return value;
    }
    ExpressionPtr parseBitOr() {
        auto value = parseBitAnd();
        while (peek().kind == Token::Kind::BitOr) {
            auto op = tokens_[position_++];
            value = binary(value, op, parseBitAnd());
        }
        return value;
    }
    ExpressionPtr parseBitAnd() {
        auto value = parseEquality();
        while (peek().kind == Token::Kind::BitAnd) {
            auto op = tokens_[position_++];
            value = binary(value, op, parseEquality());
        }
        return value;
    }
    ExpressionPtr parseShift() {
        auto value = parseAdd();
        while (peek().kind == Token::Kind::ShiftLeft || peek().kind == Token::Kind::ShiftRight) {
            auto op = tokens_[position_++];
            value = binary(value, op, parseAdd());
        }
        return value;
    }
    ExpressionPtr parseEquality() {
        auto value = parseComparison();
        while (peek().kind == Token::Kind::EqualEqual || peek().kind == Token::Kind::BangEqual) {
            Token op = tokens_[position_++];
            value = binary(value, op, parseComparison());
        }
        return value;
    }
    ExpressionPtr parseComparison() {
        auto value = parseShift();
        while (peek().kind == Token::Kind::Less || peek().kind == Token::Kind::LessEqual ||
               peek().kind == Token::Kind::Greater || peek().kind == Token::Kind::GreaterEqual) {
            Token op = tokens_[position_++];
            value = binary(value, op, parseShift());
        }
        return value;
    }
    ExpressionPtr parseAdd() {
        auto value = parseMultiply();
        while (peek().kind == Token::Kind::Plus || peek().kind == Token::Kind::Minus) {
            Token op = tokens_[position_++];
            value = binary(value, op, parseMultiply());
        }
        return value;
    }
    ExpressionPtr parseMultiply() {
        auto value = parsePower();
        while (peek().kind == Token::Kind::Star || peek().kind == Token::Kind::Slash ||
               peek().kind == Token::Kind::Percent) {
            Token op = tokens_[position_++];
            value = binary(value, op, parsePower());
        }
        return value;
    }
    ExpressionPtr parsePower() {
        auto value = parseUnary();
        if (peek().kind == Token::Kind::Power) {
            Token op = tokens_[position_++];
            value = binary(value, op, parsePower());
        }
        return value;
    }
    ExpressionPtr parseUnary() {
        if (peek().kind == Token::Kind::Minus && tokens_.at(position_ + 1).kind == Token::Kind::Number &&
            tokens_.at(position_ + 1).text == "9223372036854775808") {
            position_ += 2;
            auto value = node(Expression::Kind::Literal);
            value->literal = Value(std::numeric_limits<std::int64_t>::min());
            return value;
        }
        if (peek().kind == Token::Kind::Tilde || peek().kind == Token::Kind::Bang ||
            peek().kind == Token::Kind::Minus || peek().kind == Token::Kind::Plus) {
            Token op = tokens_[position_++];
            auto value = node(Expression::Kind::Unary, op.text);
            value->right = parseUnary();
            return value;
        }
        if (peek().kind == Token::Kind::Identifier && peek().text == "not") {
            Token op = tokens_[position_++];
            auto value = node(Expression::Kind::Unary, op.text);
            value->right = parseUnary();
            return value;
        }
        return parsePostfix();
    }
    ExpressionPtr parsePostfix() {
        auto value = parsePrimary();
        for (;;) {
            if (take(Token::Kind::LeftParen)) {
                auto call = node(Expression::Kind::Call);
                call->left = value;
                if (value->kind == Expression::Kind::Member && value->left &&
                    value->left->kind == Expression::Kind::Identifier) {
                    call->builtinSymbolName = "builtin." + value->left->text + "." + value->text;
                    call->builtinSymbolId = stableBuiltinSymbolId(call->builtinSymbolName);
                }
                if (!take(Token::Kind::RightParen)) {
                    do {
                        Expression::NamedArgument argument;
                        if (peek().kind == Token::Kind::Identifier &&
                            tokens_.at(position_ + 1).kind == Token::Kind::Colon) {
                            argument.name = tokens_[position_++].text;
                            ++position_;
                        }
                        argument.value = parseCoalesce();
                        call->arguments.push_back(std::move(argument));
                    } while (take(Token::Kind::Comma));
                    expect(Token::Kind::RightParen, "Expected ')' after function arguments.");
                }
                value = std::move(call);
            } else if (take(Token::Kind::LeftBracket)) {
                auto access = node(Expression::Kind::Index);
                access->left = value;
                access->right = parseCoalesce();
                expect(Token::Kind::RightBracket, "Expected ']' after index.");
                value = std::move(access);
            } else if (peek().kind == Token::Kind::Dot || peek().kind == Token::Kind::OptionalDot) {
                const bool optional = tokens_[position_++].kind == Token::Kind::OptionalDot;
                Token member = peek().kind == Token::Kind::Number
                                   ? tokens_[position_++]
                                   : expect(Token::Kind::Identifier, "Expected a property name after '.'.");
                auto access = node(Expression::Kind::Member, optional ? "?" + member.text : member.text);
                access->left = value;
                if (value->kind == Expression::Kind::Identifier) {
                    access->builtinSymbolName = "builtin." + value->text + "." + member.text;
                    access->builtinSymbolId = stableBuiltinSymbolId(access->builtinSymbolName);
                }
                value = std::move(access);
            } else
                break;
        }
        return value;
    }
    ExpressionPtr parsePrimary() {
        if (take(Token::Kind::Number)) {
            auto value = node(Expression::Kind::Literal);
            const auto &token = tokens_[position_ - 1];
            const bool hex = token.text.starts_with("0x") || token.text.starts_with("0X");
            const bool binary = token.text.starts_with("0b") || token.text.starts_with("0B");
            if (hex || binary) {
                const int base = hex ? 16 : 2;
                const std::string digits = withoutDigitSeparators(std::string_view(token.text).substr(2), base);
                std::int64_t integer{};
                auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), integer, base);
                if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
                    throw std::runtime_error("Integer literal is outside i64 range.");
                value->literal = Value(integer);
            } else if (token.text.find_first_of(".eE") == std::string::npos) {
                const std::string digits = withoutDigitSeparators(token.text);
                std::int64_t integer{};
                auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), integer);
                if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
                    throw std::runtime_error("Integer literal is outside i64 range.");
                value->literal = Value(integer);
            } else {
                value->literal = Value(token.number);
            }
            return value;
        }
        if (take(Token::Kind::String)) {
            auto value = node(Expression::Kind::Literal);
            value->literal = Value(tokens_[position_ - 1].text);
            return value;
        }
        if (peek().kind == Token::Kind::Identifier) {
            const std::string name = tokens_[position_++].text;
            if (name == "true" || name == "false" || name == "null" || name == "참" || name == "거짓") {
                auto value = node(Expression::Kind::Literal);
                if (name == "true" || name == "참")
                    value->literal = Value(true);
                else if (name == "false" || name == "거짓")
                    value->literal = Value(false);
                return value;
            }
            auto value = node(Expression::Kind::Identifier, name);
            return value;
        }
        if (take(Token::Kind::LeftParen)) {
            if (take(Token::Kind::RightParen))
                return node(Expression::Kind::Tuple);
            auto value = parseCoalesce();
            if (take(Token::Kind::Comma)) {
                auto tuple = node(Expression::Kind::Tuple);
                tuple->elements.push_back(value);
                if (!take(Token::Kind::RightParen)) {
                    do {
                        tuple->elements.push_back(parseOr());
                    } while (take(Token::Kind::Comma) && peek().kind != Token::Kind::RightParen);
                    expect(Token::Kind::RightParen, "Expected ')' after tuple.");
                }
                return tuple;
            }
            expect(Token::Kind::RightParen, "Expected ')' after expression.");
            return value;
        }
        if (take(Token::Kind::LeftBracket)) {
            auto value = node(Expression::Kind::Array);
            if (!take(Token::Kind::RightBracket)) {
                do {
                    value->elements.push_back(parseOr());
                } while (take(Token::Kind::Comma));
                expect(Token::Kind::RightBracket, "Expected ']' after list.");
            }
            return value;
        }
        if (take(Token::Kind::LeftBrace)) {
            auto value = node(Expression::Kind::Map);
            if (!take(Token::Kind::RightBrace)) {
                do {
                    std::string key;
                    if (peek().kind == Token::Kind::String || peek().kind == Token::Kind::Identifier)
                        key = tokens_[position_++].text;
                    else
                        throw std::runtime_error("Map keys must be strings or names.");
                    expect(Token::Kind::Colon, "Expected ':' after map key.");
                    value->entries.emplace_back(std::move(key), parseOr());
                } while (take(Token::Kind::Comma));
                expect(Token::Kind::RightBrace, "Expected '}' after map.");
            }
            return value;
        }
        throw std::runtime_error("Expected a value or expression.");
    }
    const std::vector<Token> &tokens_;
    std::size_t &position_;
};

double number(const Value &value) {
    if (const auto *result = std::get_if<double>(&value.data))
        return *result;
    if (const auto *result = std::get_if<std::int64_t>(&value.data))
        return static_cast<double>(*result);
    throw std::runtime_error("Expected Number but got " + value.typeName() + ".");
}
std::int64_t checkedIndex(const Value &value) {
    if (auto integer = std::get_if<std::int64_t>(&value.data))
        return *integer;
    throw std::runtime_error("JM3003: Index must have type Int.");
}
std::string outputText(const std::vector<Value> &args) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i)
            result += ' ';
        result += args[i].toString();
    }
    return result;
}
bool truth(const Value &value) {
    if (value.isNull())
        return false;
    if (const auto *result = std::get_if<std::int64_t>(&value.data))
        return *result != 0;
    if (const auto *result = std::get_if<bool>(&value.data))
        return *result;
    if (const auto *result = std::get_if<double>(&value.data))
        return *result != 0.0;
    if (const auto *result = std::get_if<std::string>(&value.data))
        return !result->empty();
    return true;
}
bool equal(const Value &left, const Value &right) {
    if ((left.type() == Type::Int && right.type() == Type::Float) ||
        (left.type() == Type::Float && right.type() == Type::Int))
        return number(left) == number(right);
    if (left.data.index() != right.data.index())
        return false;
    if (auto a = std::get_if<std::int64_t>(&left.data))
        return *a == std::get<std::int64_t>(right.data);
    if (left.isNull())
        return true;
    if (auto a = std::get_if<bool>(&left.data))
        return *a == std::get<bool>(right.data);
    if (auto a = std::get_if<double>(&left.data))
        return *a == std::get<double>(right.data);
    if (auto a = std::get_if<std::string>(&left.data))
        return *a == std::get<std::string>(right.data);
    if (auto a = std::get_if<Value::ArrayPtr>(&left.data)) {
        const auto &b = *std::get<Value::ArrayPtr>(right.data);
        if ((*a)->size() != b.size())
            return false;
        for (std::size_t i = 0; i < b.size(); ++i)
            if (!equal((**a)[i], b[i]))
                return false;
        return true;
    }
    if (auto a = std::get_if<Value::MapPtr>(&left.data)) {
        const auto &b = *std::get<Value::MapPtr>(right.data);
        if ((*a)->size() != b.size())
            return false;
        for (const auto &[key, value] : **a) {
            const auto found = b.find(key);
            if (found == b.end() || !equal(value, found->second))
                return false;
        }
        return true;
    }
    if (auto a = std::get_if<Value::TuplePtr>(&left.data)) {
        auto &b = std::get<Value::TuplePtr>(right.data)->values;
        if ((*a)->values.size() != b.size())
            return false;
        for (size_t i = 0; i < b.size(); ++i)
            if (!equal((*a)->values[i], b[i]))
                return false;
        return true;
    }
    if (auto a = std::get_if<RangeValue>(&left.data)) {
        auto b = std::get<RangeValue>(right.data);
        return a->start == b.start && a->end == b.end && a->step == b.step;
    }
    if (auto a = std::get_if<EnumValue>(&left.data)) {
        auto &b = std::get<EnumValue>(right.data);
        return a->type == b.type && a->ordinal == b.ordinal;
    }
    if (auto a = std::get_if<Value::StructPtr>(&left.data))
        return a->get() == std::get<Value::StructPtr>(right.data).get();
    if (auto a = std::get_if<Vector2Value>(&left.data)) {
        auto b = std::get<Vector2Value>(right.data);
        return a->x == b.x && a->y == b.y;
    }
    if (auto a = std::get_if<Vector3Value>(&left.data)) {
        auto b = std::get<Vector3Value>(right.data);
        return a->x == b.x && a->y == b.y && a->z == b.z;
    }
    if (auto a = std::get_if<ColorValue>(&left.data)) {
        auto b = std::get<ColorValue>(right.data);
        return a->r == b.r && a->g == b.g && a->b == b.b && a->a == b.a;
    }
    auto a = std::get<EntityReference>(left.data), b = std::get<EntityReference>(right.data);
    return a.id == b.id && a.sceneIdentity == b.sceneIdentity && a.generation == b.generation;
}

Value coerceElement(Value value, Type type, Type element = Type::Any, std::string nominal = {}) {
    if (type == Type::Optional)
        return Value::optional(element, std::move(value), std::move(nominal));
    if (type == Type::Any)
        return value;
    if (type == Type::Float && value.type() == Type::Int)
        return Value(number(value));
    if (type != value.type())
        throw std::runtime_error("JM2001: Typed List element requires " + typeName(type));
    return value;
}
void typeList(Value &value, Type type) {
    if (type == Type::Any)
        return;
    auto *list = std::get_if<Value::ArrayPtr>(&value.data);
    if (!list)
        throw std::runtime_error("JM2001: Typed List required.");
    if ((*list)->elementType != Type::Any && (*list)->elementType != type)
        throw std::runtime_error("JM2001: List alias has conflicting element metadata.");
    auto items = **list;
    for (auto &item : items)
        item = coerceElement(item, type);
    for (size_t i = 0; i < items.size(); ++i)
        (**list)[i] = std::move(items[i]);
    (*list)->elementType = type;
}
void typeMap(Value &value, Type type) {
    if (type == Type::Any)
        return;
    auto map = std::get<Value::MapPtr>(value.data);
    if (map->elementType != Type::Any && map->elementType != type)
        throw std::runtime_error("JM2001: Conflicting Map value metadata.");
    auto items = *map;
    for (auto &[key, item] : items)
        item = coerceElement(item, type);
    for (auto &[key, item] : items)
        map->at(key) = std::move(item);
    map->elementType = type;
}
void rejectCycle(const Value &candidate, const void *target) {
    std::unordered_set<const void *> visited;
    std::function<bool(const Value &)> contains = [&](const Value &value) {
        if (auto optional = std::get_if<std::shared_ptr<OptionalValue>>(&value.data))
            return contains((*optional)->value);
        if (auto array = std::get_if<Value::ArrayPtr>(&value.data)) {
            if (array->get() == target)
                return true;
            if (!visited.insert(array->get()).second)
                return false;
            for (const auto &item : **array)
                if (contains(item))
                    return true;
        }
        if (auto map = std::get_if<Value::MapPtr>(&value.data)) {
            if (map->get() == target)
                return true;
            if (!visited.insert(map->get()).second)
                return false;
            for (const auto &item : **map)
                if (contains(item.second))
                    return true;
        }
        if (auto tuple = std::get_if<Value::TuplePtr>(&value.data)) {
            if (tuple->get() == target)
                return true;
            if (!visited.insert(tuple->get()).second)
                return false;
            for (const auto &item : (*tuple)->values)
                if (contains(item))
                    return true;
        }
        if (auto structure = std::get_if<Value::StructPtr>(&value.data)) {
            if (structure->get() == target)
                return true;
            Value fields;
            fields.data = (*structure)->fields;
            return contains(fields);
        }
        return false;
    };
    if (contains(candidate))
        throw std::runtime_error("JM3010: Cyclic collection ownership is not supported.");
}
Value evaluate(const ExpressionPtr &expression, const Environment &environment, const HostFunction &host);

Value invokeBuiltin(const std::string &name, const std::vector<Value> &arguments,
                    const std::vector<std::string> &names, const HostFunction &host) {
    if (name == "range") {
        if (arguments.size() != 2 && arguments.size() != 3)
            throw std::runtime_error("range requires start, end, optional step.");
        auto step = arguments.size() == 3 ? checkedIndex(arguments[2]) : 1;
        if (!step)
            throw std::runtime_error("JM3005: Range step cannot be zero.");
        return Value(RangeValue{checkedIndex(arguments[0]), checkedIndex(arguments[1]), step});
    }
    if (name == "Vector2")
        return invokeBuiltin("vector2", arguments, names, host);
    if (name == "Vector3")
        return invokeBuiltin("vector3", arguments, names, host);
    if (name == "Color")
        return invokeBuiltin("color", arguments, names, host);
    if (host) {
        try {
            return host(name, arguments, names);
        } catch (const std::out_of_range &) { /* try built-ins */
        }
    }
    if (auto operation = runtimeStandardOperation(name)) {
        auto descriptor = standardFunction(name);
        if (arguments.size() != descriptor->minimumArity)
            throw std::runtime_error("JM2003: Standard function argument count mismatch.");
        std::uint64_t bits[3]{};
        for (size_t i = 0; i < arguments.size(); ++i)
            bits[i] = descriptor->numericArguments ? std::bit_cast<std::uint64_t>(number(arguments[i]))
                                                   : static_cast<std::uint64_t>(checkedIndex(arguments[i]));
        auto result = jm_runtime_call(operation, bits[0], bits[1], bits[2]);
        return descriptor->returnType == Type::Void  ? Value{}
               : descriptor->returnType == Type::Int ? Value(std::bit_cast<std::int64_t>(result))
                                                     : Value(std::bit_cast<double>(result));
    }
    if (name.rfind("builtin.math.", 0) == 0)
        return invokeBuiltin(name.substr(std::string("builtin.math.").size()), arguments, names, {});
    const std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> arities{
        {"int", {1, 1}},  {"float", {1, 1}},  {"string", {1, 1}}, {"bool", {1, 1}},    {"bitXor", {2, 2}},
        {"abs", {1, 1}},  {"sqrt", {1, 1}},   {"pow", {2, 2}},    {"clamp", {3, 3}},   {"sin", {1, 1}},
        {"cos", {1, 1}},  {"tan", {1, 1}},    {"round", {1, 1}},  {"floor", {1, 1}},   {"ceil", {1, 1}},
        {"len", {1, 1}},  {"length", {1, 1}}, {"assert", {1, 1}}, {"vector2", {2, 2}}, {"vector3", {3, 3}},
        {"color", {3, 4}}};
    if (auto expected = arities.find(name);
        expected != arities.end() &&
        (arguments.size() < expected->second.first || arguments.size() > expected->second.second))
        throw std::runtime_error("JM2003: Function '" + name + "' argument count mismatch.");
    auto arg = [&](std::size_t index) -> const Value & {
        if (index >= arguments.size())
            throw std::runtime_error(name + " expects more arguments.");
        return arguments[index];
    };
    if (name == "int") {
        if (arg(0).type() == Type::Int)
            return arg(0);
        if (auto text = std::get_if<std::string>(&arg(0).data)) {
            std::int64_t result{};
            auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
            if (error != std::errc{} || end != text->data() + text->size())
                throw std::runtime_error("JM3005: Invalid Int conversion.");
            return Value(result);
        }
        auto value = number(arg(0));
        if (!std::isfinite(value) || value < -9223372036854775808.0 || value >= 9223372036854775808.0)
            throw std::runtime_error("JM3005: Float is outside Int conversion range.");
        return Value(static_cast<std::int64_t>(value));
    }
    if (name == "float") {
        if (auto text = std::get_if<std::string>(&arg(0).data)) {
            double result{};
            auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), result);
            if (error != std::errc{} || end != text->data() + text->size())
                throw std::runtime_error("JM3005: Invalid Float conversion.");
            return Value(result);
        }
        return Value(number(arg(0)));
    }
    if (name == "string")
        return Value(arg(0).toString());
    if (name == "bool") {
        if (arg(0).type() != Type::Bool && arg(0).type() != Type::Int && arg(0).type() != Type::Float &&
            arg(0).type() != Type::String)
            throw std::runtime_error("JM3005: bool conversion accepts Bool, Int, Float, or String.");
        return Value(truth(arg(0)));
    }
    if (name == "bitXor")
        return Value(std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(checkedIndex(arg(0))) ^
                                                 static_cast<std::uint64_t>(checkedIndex(arg(1)))));
    if (name == "print" || name == "println")
        return Value{};
    if (name == "clamp") {
        const auto low = number(arg(1)), high = number(arg(2));
        if (low > high)
            throw std::runtime_error("clamp minimum exceeds maximum.");
        return Value(std::clamp(number(arg(0)), low, high));
    }
    if (name == "assert") {
        if (!truth(arg(0)))
            throw std::runtime_error("Assertion failed.");
        return Value(true);
    }
    if (name == "len" || name == "length") {
        if (auto array = std::get_if<Value::ArrayPtr>(&arg(0).data))
            return Value(static_cast<std::int64_t>((*array)->size()));
        if (auto map = std::get_if<Value::MapPtr>(&arg(0).data))
            return Value(static_cast<std::int64_t>((*map)->size()));
        if (auto text = std::get_if<std::string>(&arg(0).data))
            return Value(static_cast<std::int64_t>(text->size()));
        throw std::runtime_error("len expects a list, map, or string.");
    }
    if (name == "abs") {
        if (auto integer = std::get_if<std::int64_t>(&arg(0).data)) {
            if (*integer == std::numeric_limits<std::int64_t>::min())
                throw std::runtime_error("abs overflow.");
            return Value(*integer < 0 ? -*integer : *integer);
        }
        return Value(std::abs(number(arg(0))));
    }
    if (name == "min" || name == "max") {
        if (arguments.empty())
            throw std::runtime_error(name + " expects at least one argument.");
        bool integers = true;
        for (const auto &value : arguments)
            integers &= value.type() == Type::Int;
        if (integers) {
            auto result = std::get<std::int64_t>(arguments.front().data);
            for (std::size_t i = 1; i < arguments.size(); ++i)
                result = name == "min" ? std::min(result, std::get<std::int64_t>(arguments[i].data))
                                       : std::max(result, std::get<std::int64_t>(arguments[i].data));
            return Value(result);
        }
        double result = number(arguments.front());
        for (std::size_t i = 1; i < arguments.size(); ++i)
            result = name == "min" ? std::min(result, number(arguments[i]))
                                   : std::max(result, number(arguments[i]));
        return Value(result);
    }
    if (name == "round")
        return Value(std::round(number(arg(0))));
    if (name == "floor")
        return Value(std::floor(number(arg(0))));
    if (name == "ceil")
        return Value(std::ceil(number(arg(0))));
    if (name == "sqrt")
        return Value(std::sqrt(number(arg(0))));
    if (name == "pow")
        return Value(std::pow(number(arg(0)), number(arg(1))));
    if (name == "sin")
        return Value(std::sin(number(arg(0))));
    if (name == "cos")
        return Value(std::cos(number(arg(0))));
    if (name == "tan")
        return Value(std::tan(number(arg(0))));
    if (name == "vector3")
        return Value(Vector3Value{number(arg(0)), number(arg(1)), number(arg(2))});
    if (name == "vector2")
        return Value(Vector2Value{number(arg(0)), number(arg(1))});
    if (name == "color")
        return Value(ColorValue{number(arg(0)), number(arg(1)), number(arg(2)),
                                arguments.size() > 3 ? number(arguments[3]) : 1.0});
    throw std::runtime_error("Unknown function: " + name);
}

Value evaluate(const ExpressionPtr &expression, const Environment &environment, const HostFunction &host) {
    if (!expression)
        return Value{};
    switch (expression->kind) {
    case Expression::Kind::Literal:
        return expression->literal;
    case Expression::Kind::Identifier:
        try {
            return environment.get(expression->text);
        } catch (const std::runtime_error &) {
            if (expression->text == "PI")
                return Value(3.14159265358979323846);
            if (expression->text == "E")
                return Value(2.71828182845904523536);
            throw;
        }
    case Expression::Kind::Tuple: {
        auto tuple = std::make_shared<Value::Tuple>();
        for (const auto &item : expression->elements)
            tuple->values.push_back(evaluate(item, environment, host));
        return Value(tuple);
    }
    case Expression::Kind::Array: {
        Value::Array result;
        for (const auto &item : expression->elements)
            result.push_back(evaluate(item, environment, host));
        return Value::array(std::move(result));
    }
    case Expression::Kind::Map: {
        Value::Map result;
        for (const auto &[key, item] : expression->entries)
            result.emplace(key, evaluate(item, environment, host));
        return Value::map(std::move(result));
    }
    case Expression::Kind::Unary: {
        const Value value = evaluate(expression->right, environment, host).unwrap();
        if (auto integer = std::get_if<std::int64_t>(&value.data)) {
            if (expression->text == "-")
                return Value(
                    std::bit_cast<std::int64_t>(std::uint64_t{0} - static_cast<std::uint64_t>(*integer)));
            if (expression->text == "+")
                return value;
            if (expression->text == "~")
                return Value(std::bit_cast<std::int64_t>(~static_cast<std::uint64_t>(*integer)));
        }
        if (expression->text == "!" || expression->text == "not")
            return Value(!truth(value));
        if (expression->text == "-")
            return Value(-number(value));
        if (expression->text == "+")
            return Value(number(value));
        throw std::runtime_error("Unknown unary operator.");
    }
    case Expression::Kind::Binary: {
        const std::string &op = expression->text;
        const Value rawLeft = evaluate(expression->left, environment, host);
        if (op == "??")
            return rawLeft.isNull() ? evaluate(expression->right, environment, host) : rawLeft.unwrap();
        const Value left = (op == "==" || op == "!=") && rawLeft.isNull() ? Value{} : rawLeft.unwrap();
        if (op == "and" || op == "&&")
            return truth(left) ? Value(truth(evaluate(expression->right, environment, host))) : Value(false);
        if (op == "or" || op == "||")
            return truth(left) ? Value(true) : Value(truth(evaluate(expression->right, environment, host)));
        const Value rawRight = evaluate(expression->right, environment, host);
        const Value right = (op == "==" || op == "!=") && rawRight.isNull() ? Value{} : rawRight.unwrap();
        if (op == "==")
            return Value(equal(left, right));
        if (op == "!=")
            return Value(!equal(left, right));
        if (op == "*" && (right.type() == Type::Vector2 || right.type() == Type::Vector3) &&
            (left.type() == Type::Int || left.type() == Type::Float)) {
            auto scale = number(left);
            if (auto v = std::get_if<Vector2Value>(&right.data))
                return Value(Vector2Value{scale * v->x, scale * v->y});
            auto v = std::get<Vector3Value>(right.data);
            return Value(Vector3Value{scale * v.x, scale * v.y, scale * v.z});
        }
        if (auto a = std::get_if<Vector2Value>(&left.data)) {
            if (auto b = std::get_if<Vector2Value>(&right.data)) {
                if (op == "+")
                    return Value(Vector2Value{a->x + b->x, a->y + b->y});
                if (op == "-")
                    return Value(Vector2Value{a->x - b->x, a->y - b->y});
            }
            if (op == "*" || op == "/") {
                auto scalar = number(right);
                if (op == "/" && scalar == 0)
                    throw std::runtime_error("JM3001: Vector division by zero.");
                return Value(Vector2Value{op == "*" ? a->x * scalar : a->x / scalar,
                                          op == "*" ? a->y * scalar : a->y / scalar});
            }
            throw std::runtime_error("Invalid Vector2 operator.");
        }
        if (auto a = std::get_if<Vector3Value>(&left.data)) {
            if (auto b = std::get_if<Vector3Value>(&right.data)) {
                if (op == "+")
                    return Value(Vector3Value{a->x + b->x, a->y + b->y, a->z + b->z});
                if (op == "-")
                    return Value(Vector3Value{a->x - b->x, a->y - b->y, a->z - b->z});
            }
            if (op == "*" || op == "/") {
                auto scalar = number(right);
                if (op == "/" && scalar == 0)
                    throw std::runtime_error("JM3001: Vector division by zero.");
                return Value(Vector3Value{op == "*" ? a->x * scalar : a->x / scalar,
                                          op == "*" ? a->y * scalar : a->y / scalar,
                                          op == "*" ? a->z * scalar : a->z / scalar});
            }
            throw std::runtime_error("Invalid Vector3 operator.");
        }
        if (left.type() == Type::Int && right.type() == Type::Int) {
            const auto a = std::get<std::int64_t>(left.data), b = std::get<std::int64_t>(right.data);
            const auto ua = static_cast<std::uint64_t>(a), ub = static_cast<std::uint64_t>(b);
            if (op == "&")
                return Value(std::bit_cast<std::int64_t>(ua & ub));
            if (op == "|")
                return Value(std::bit_cast<std::int64_t>(ua | ub));
            if (op == "<<" || op == ">>") {
                if (b < 0 || b >= 64)
                    throw std::runtime_error("JM3004: Shift count must be in 0..63.");
                return Value(op == "<<"
                                 ? std::bit_cast<std::int64_t>(ua << b)
                                 : std::bit_cast<std::int64_t>(
                                       (ua >> b) | (a < 0 && b ? (~std::uint64_t{0} << (64 - b)) : 0)));
            }
            if (op == "+")
                return Value(std::bit_cast<std::int64_t>(ua + ub));
            if (op == "-")
                return Value(std::bit_cast<std::int64_t>(ua - ub));
            if (op == "*")
                return Value(std::bit_cast<std::int64_t>(ua * ub));
            if (op == "/" || op == "%") {
                if (b == 0)
                    throw std::runtime_error("JM3001: Cannot divide by zero.");
                if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
                    throw std::runtime_error("JM3002: Integer division overflow.");
                return Value(op == "/" ? a / b : a % b);
            }
            if (op == "<")
                return Value(a < b);
            if (op == "<=")
                return Value(a <= b);
            if (op == ">")
                return Value(a > b);
            if (op == ">=")
                return Value(a >= b);
        }
        if (op == "+") {
            if (auto a = std::get_if<std::string>(&left.data)) {
                auto b = std::get_if<std::string>(&right.data);
                if (!b)
                    throw std::runtime_error("String concatenation requires String operands.");
                return Value(*a + *b);
            }
            if (auto a = std::get_if<Value::ArrayPtr>(&left.data)) {
                auto result = **a;
                if (auto b = std::get_if<Value::ArrayPtr>(&right.data))
                    result.insert(result.end(), (*b)->begin(), (*b)->end());
                else
                    result.push_back(right);
                return Value::array(std::move(result));
            }
            return Value(number(left) + number(right));
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (auto a = std::get_if<std::string>(&left.data)) {
                const auto *b = std::get_if<std::string>(&right.data);
                if (!b)
                    throw std::runtime_error("String comparison needs another string.");
                if (op == "<")
                    return Value(*a < *b);
                if (op == "<=")
                    return Value(*a <= *b);
                if (op == ">")
                    return Value(*a > *b);
                return Value(*a >= *b);
            }
            const double a = number(left), b = number(right);
            if (op == "<")
                return Value(a < b);
            if (op == "<=")
                return Value(a <= b);
            if (op == ">")
                return Value(a > b);
            return Value(a >= b);
        }
        const double a = number(left), b = number(right);
        if (op == "-")
            return Value(a - b);
        if (op == "*")
            return Value(a * b);
        if (op == "/") {
            if (b == 0.0)
                throw std::runtime_error("Cannot divide by zero.");
            return Value(a / b);
        }
        if (op == "%") {
            if (b == 0.0)
                throw std::runtime_error("Cannot divide by zero.");
            return Value(std::fmod(a, b));
        }
        if (op == "^" || op == "**")
            return Value(std::pow(a, b));
        throw std::runtime_error("Unknown binary operator: " + op);
    }
    case Expression::Kind::Index: {
        const Value object = evaluate(expression->left, environment, host).unwrap();
        const Value index = evaluate(expression->right, environment, host);
        if (auto tuple = std::get_if<Value::TuplePtr>(&object.data)) {
            auto at = checkedIndex(index);
            if (at < 0 || static_cast<std::uint64_t>(at) >= (*tuple)->values.size())
                throw std::runtime_error("Tuple index is outside the tuple.");
            return (*tuple)->values[at];
        }
        if (auto list = std::get_if<Value::ArrayPtr>(&object.data)) {
            const auto at = checkedIndex(index);
            if (at < 0 || static_cast<std::size_t>(at) >= (*list)->size())
                throw std::runtime_error("List index is outside the list.");
            return (**list)[static_cast<std::size_t>(at)];
        }
        if (auto map = std::get_if<Value::MapPtr>(&object.data)) {
            const auto found = (*map)->find(index.toString());
            if ((*map)->elementType != Type::Any && index.type() != Type::String)
                throw std::runtime_error("JM2005: Typed Map key must be String.");
            if (found == (*map)->end()) {
                if ((*map)->elementType != Type::Any)
                    throw std::runtime_error("JM3003: Map key does not exist.");
                return Value{};
            }
            return found->second;
        }
        if (auto text = std::get_if<std::string>(&object.data)) {
            const auto at = checkedIndex(index);
            if (at < 0 || static_cast<std::size_t>(at) >= text->size())
                throw std::runtime_error("String index is outside the string.");
            return Value(std::string(1, (*text)[static_cast<std::size_t>(at)]));
        }
        throw std::runtime_error("Only a list, map, or string can be indexed.");
    }
    case Expression::Kind::Member: {
        if (expression->text.starts_with('?')) {
            auto raw = evaluate(expression->left, environment, host);
            if (raw.type() != Type::Optional)
                throw std::runtime_error("JM2010: ?. requires Optional.");
            auto element = std::get<std::shared_ptr<OptionalValue>>(raw.data)->element;
            auto resultType =
                element == Type::Entity ? expression->text == "?position" ? Type::Vector2 : Type::String
                : element == Type::Vector2 || element == Type::Vector3 || element == Type::Color ? Type::Float
                                                                                                 : Type::Int;
            if (element == Type::Struct) {
                auto &schema = std::get<std::shared_ptr<OptionalValue>>(raw.data)->schema;
                if (!schema.contains(expression->text.substr(1)))
                    throw std::runtime_error("JM2010: Unknown Optional Struct field.");
                resultType = schema.at(expression->text.substr(1)).base;
            }
            if (raw.isNull())
                return Value::optional(resultType);
            auto member = std::make_shared<Expression>(*expression);
            member->text.erase(0, 1);
            member->left = node(Expression::Kind::Literal);
            member->left->literal = raw.unwrap();
            member->builtinSymbolId = 0;
            member->builtinSymbolName.clear();
            auto value = evaluate(member, environment, host);
            return Value::optional(value.type(), value);
        }
        if (expression->left && expression->left->kind == Expression::Kind::Identifier &&
            expression->left->text == "math" && !environment.contains("math") &&
            (expression->text == "PI" || expression->text == "E"))
            return Value(expression->text == "PI" ? 3.14159265358979323846 : 2.71828182845904523536);
        if (expression->left && expression->left->kind == Expression::Kind::Identifier &&
            expression->left->text == "Color" && !environment.contains("Color")) {
            if (expression->text == "white")
                return Value(ColorValue{1, 1, 1, 1});
            if (expression->text == "black")
                return Value(ColorValue{0, 0, 0, 1});
            if (expression->text == "red")
                return Value(ColorValue{1, 0, 0, 1});
        }
        if (host && expression->builtinSymbolId &&
            !(expression->left && expression->left->kind == Expression::Kind::Identifier &&
              environment.contains(expression->left->text))) {
            try {
                return host(expression->builtinSymbolName, {}, {});
            } catch (const std::out_of_range &) {
            }
        }
        const Value object = evaluate(expression->left, environment, host).unwrap();
        if (object.type() == Type::Entity && host)
            return host("builtin.entity." + expression->text, {object}, {});
        if (auto list = std::get_if<Value::ArrayPtr>(&object.data); list && expression->text == "length")
            return Value(static_cast<std::int64_t>((*list)->size()));
        if (auto map = std::get_if<Value::MapPtr>(&object.data)) {
            if (expression->text == "length")
                return Value(static_cast<std::int64_t>((*map)->size()));
            const auto found = (*map)->find(expression->text);
            return found == (*map)->end() ? Value{} : found->second;
        }
        if (auto text = std::get_if<std::string>(&object.data); text && expression->text == "length")
            return Value(static_cast<std::int64_t>(text->size()));
        if (auto tuple = std::get_if<Value::TuplePtr>(&object.data)) {
            if (expression->text == "length")
                return Value(static_cast<std::int64_t>((*tuple)->values.size()));
            std::uint64_t index{};
            auto [end, error] = std::from_chars(expression->text.data(),
                                                expression->text.data() + expression->text.size(), index);
            if (error != std::errc{} || end != expression->text.data() + expression->text.size() ||
                index >= (*tuple)->values.size())
                throw std::runtime_error("Invalid Tuple member.");
            return (*tuple)->values[index];
        }
        if (auto range = std::get_if<RangeValue>(&object.data)) {
            if (expression->text == "start")
                return Value(range->start);
            if (expression->text == "end")
                return Value(range->end);
            if (expression->text == "step")
                return Value(range->step);
        }
        if (auto structure = std::get_if<Value::StructPtr>(&object.data)) {
            auto found = (*structure)->fields->find(expression->text);
            if (found == (*structure)->fields->end())
                throw std::runtime_error("Unknown struct field.");
            return found->second;
        }
        if (auto vector = std::get_if<Vector2Value>(&object.data)) {
            auto length = std::hypot(vector->x, vector->y);
            if (expression->text == "length")
                return Value(length);
            if (expression->text == "lengthSquared")
                return Value(vector->x * vector->x + vector->y * vector->y);
            if (expression->text == "normalized")
                return Value(length == 0 ? Vector2Value{}
                                         : Vector2Value{vector->x / length, vector->y / length});
        }
        if (auto vector = std::get_if<Vector3Value>(&object.data)) {
            auto length = std::hypot(vector->x, vector->y, vector->z);
            if (expression->text == "length")
                return Value(length);
            if (expression->text == "lengthSquared")
                return Value(vector->x * vector->x + vector->y * vector->y + vector->z * vector->z);
            if (expression->text == "normalized")
                return Value(length == 0
                                 ? Vector3Value{}
                                 : Vector3Value{vector->x / length, vector->y / length, vector->z / length});
        }
        if (auto vector = std::get_if<Vector2Value>(&object.data)) {
            if (expression->text == "x")
                return Value(vector->x);
            if (expression->text == "y")
                return Value(vector->y);
        }
        if (auto vector = std::get_if<Vector3Value>(&object.data)) {
            if (expression->text == "x")
                return Value(vector->x);
            if (expression->text == "y")
                return Value(vector->y);
            if (expression->text == "z")
                return Value(vector->z);
        }
        if (auto color = std::get_if<ColorValue>(&object.data)) {
            if (expression->text == "r")
                return Value(color->r);
            if (expression->text == "g")
                return Value(color->g);
            if (expression->text == "b")
                return Value(color->b);
            if (expression->text == "a")
                return Value(color->a);
        }
        throw std::runtime_error("Unknown property '" + expression->text + "' on " + object.typeName() + ".");
    }
    case Expression::Kind::Call: {
        if (expression->left->kind == Expression::Kind::Member && expression->left->text.starts_with('?')) {
            auto raw = evaluate(expression->left->left, environment, host);
            if (raw.type() != Type::Optional)
                throw std::runtime_error("JM2010: ?. requires Optional.");
            if (raw.isNull())
                return Value{};
            auto call = std::make_shared<Expression>(*expression);
            call->left = std::make_shared<Expression>(*expression->left);
            call->left->text.erase(0, 1);
            call->left->left = node(Expression::Kind::Literal);
            call->left->left->literal = raw.unwrap();
            call->builtinSymbolId = 0;
            call->builtinSymbolName.clear();
            return evaluate(call, environment, host);
        }
        std::vector<Value> args;
        std::vector<std::string> names;
        for (const auto &argument : expression->arguments) {
            names.push_back(argument.name);
            args.push_back(evaluate(argument.value, environment, host));
        }
        if (expression->left->kind == Expression::Kind::Identifier)
            return invokeBuiltin(expression->left->text, args, names, host);
        if (expression->left->kind == Expression::Kind::Member) {
            if (expression->builtinSymbolName.rfind("builtin.math.", 0) == 0 &&
                !(expression->left->left && expression->left->left->kind == Expression::Kind::Identifier &&
                  environment.contains(expression->left->left->text)))
                return invokeBuiltin(expression->builtinSymbolName, args, names, {});
            if (host && !expression->builtinSymbolName.empty() &&
                !(expression->left->left && expression->left->left->kind == Expression::Kind::Identifier &&
                  environment.contains(expression->left->left->text))) {
                try {
                    return host(expression->builtinSymbolName, args, names);
                } catch (const std::out_of_range &) {
                }
            }
            const Value receiver = evaluate(expression->left->left, environment, host).unwrap();
            if (receiver.type() == Type::Entity && host) {
                args.insert(args.begin(), receiver);
                names.insert(names.begin(), "entity");
                return host("builtin.entity." + expression->left->text, args, names);
            }
            if (receiver.type() == Type::Vector2 || receiver.type() == Type::Vector3) {
                auto vec = [](const Value &value) {
                    if (auto v = std::get_if<Vector2Value>(&value.data))
                        return Vector3Value{v->x, v->y, 0};
                    if (auto v = std::get_if<Vector3Value>(&value.data))
                        return *v;
                    throw std::runtime_error("Vector method requires a vector argument.");
                };
                auto v = vec(receiver);
                auto result = [&](Vector3Value value) {
                    return receiver.type() == Type::Vector2 ? Value(Vector2Value{value.x, value.y})
                                                            : Value(value);
                };
                auto magnitude = [](Vector3Value value) { return std::hypot(value.x, value.y, value.z); };
                auto method = expression->left->text;
                if (method == "length" || method == "normalized") {
                    if (!args.empty())
                        throw std::runtime_error("Vector length/normalized expects no arguments.");
                    auto length = magnitude(v);
                    return method == "length"
                               ? Value(length)
                               : result(length == 0 ? Vector3Value{}
                                                    : Vector3Value{v.x / length, v.y / length, v.z / length});
                }
                if (args.size() != (method == "lerp" ? 2u : 1u) || args[0].type() != receiver.type())
                    throw std::runtime_error("Vector method signature mismatch.");
                auto w = vec(args[0]);
                if (method == "dot")
                    return Value(v.x * w.x + v.y * w.y + v.z * w.z);
                if (method == "distance")
                    return Value(magnitude({v.x - w.x, v.y - w.y, v.z - w.z}));
                if (method == "lerp") {
                    auto t = number(args[1]);
                    return result({v.x + (w.x - v.x) * t, v.y + (w.y - v.y) * t, v.z + (w.z - v.z) * t});
                }
                if (method == "cross" && receiver.type() == Type::Vector3)
                    return result({v.y * w.z - v.z * w.y, v.z * w.x - v.x * w.z, v.x * w.y - v.y * w.x});
                throw std::runtime_error("Unknown Vector method.");
            }
            if (auto string = std::get_if<std::string>(&receiver.data)) {
                if (expression->left->text == "isEmpty") {
                    if (!args.empty())
                        throw std::runtime_error("String isEmpty expects no arguments.");
                    return Value(string->empty());
                }
                const std::unordered_map<std::string, int> methods{
                    {"substring", JM_RT_SUBSTRING}, {"slice", JM_RT_SUBSTRING},
                    {"contains", JM_RT_CONTAINS},   {"startsWith", JM_RT_STARTS_WITH},
                    {"endsWith", JM_RT_ENDS_WITH},  {"find", JM_RT_FIND},
                    {"replace", JM_RT_REPLACE},     {"split", JM_RT_SPLIT},
                    {"trim", JM_RT_TRIM},           {"upper", JM_RT_UPPER},
                    {"lower", JM_RT_LOWER},         {"codepointLength", JM_RT_CODEPOINT_LENGTH}};
                auto found = methods.find(expression->left->text);
                if (found == methods.end())
                    throw std::runtime_error("Unknown String method.");
                auto op = found->second;
                size_t count = op == JM_RT_SUBSTRING || op == JM_RT_REPLACE ? 2
                               : op == JM_RT_TRIM || op == JM_RT_UPPER || op == JM_RT_LOWER ||
                                       op == JM_RT_CODEPOINT_LENGTH
                                   ? 0
                                   : 1;
                if (args.size() != count)
                    throw std::runtime_error("String method argument count mismatch.");
                struct Scope {
                    void *context = jm_runtime_create_context();
                    void *previous = jm_runtime_activate(context);
                    ~Scope() {
                        jm_runtime_activate(previous);
                        jm_runtime_destroy_context(context);
                    }
                } scope;
                std::uint64_t bits[3]{jm_string_create(string->data(), string->size()), 0, 0};
                for (size_t i = 0; i < args.size(); ++i) {
                    if (op == JM_RT_SUBSTRING)
                        bits[i + 1] = static_cast<std::uint64_t>(checkedIndex(args[i]));
                    else {
                        auto *text = std::get_if<std::string>(&args[i].data);
                        if (!text)
                            throw std::runtime_error("String method requires String arguments.");
                        bits[i + 1] = jm_string_create(text->data(), text->size());
                    }
                }
                auto result = jm_runtime_call(op, bits[0], bits[1], bits[2]);
                if (op == JM_RT_CONTAINS || op == JM_RT_STARTS_WITH || op == JM_RT_ENDS_WITH)
                    return Value(result != 0);
                if (op == JM_RT_FIND || op == JM_RT_CODEPOINT_LENGTH)
                    return Value(std::bit_cast<std::int64_t>(result));
                auto decode = [](std::uint64_t handle) {
                    std::uint64_t length;
                    auto *bytes = jm_string_bytes(handle, &length);
                    return Value(std::string(bytes, length));
                };
                if (op == JM_RT_SPLIT) {
                    Value::Array values;
                    auto size = jm_runtime_call(JM_RT_LENGTH, result, 0, JM_RT_LIST);
                    for (std::uint64_t i = 0; i < size; ++i)
                        values.push_back(decode(jm_runtime_call(JM_RT_LIST_GET, result, i, JM_RT_STRING)));
                    return Value::array(std::move(values));
                }
                return decode(result);
            }
            if (auto list = std::get_if<Value::ArrayPtr>(&receiver.data)) {
                const std::string &method = expression->left->text;
                if (method == "isEmpty") {
                    if (!args.empty())
                        throw std::runtime_error("List isEmpty expects no arguments.");
                    return Value((*list)->empty());
                }
                if (method == "append" || method == "push") {
                    if (args.size() != 1)
                        throw std::runtime_error(method + " expects one value.");
                    rejectCycle(args[0], list->get());
                    (*list)->push_back(coerceElement(args[0], (*list)->elementType));
                    return Value{};
                }
                if (method == "pop") {
                    if (!args.empty())
                        throw std::runtime_error("pop expects no arguments.");
                    if ((*list)->empty())
                        throw std::runtime_error("JM3004: Cannot pop an empty list.");
                    Value last = list->get()->back();
                    list->get()->pop_back();
                    return last;
                }
                if (method == "clear") {
                    if (!args.empty())
                        throw std::runtime_error("clear expects no arguments.");
                    list->get()->clear();
                    return Value{};
                }
                if (method == "reverse" || method == "sort") {
                    if (!args.empty())
                        throw std::runtime_error("reverse/sort expects no arguments.");
                    if (method == "reverse")
                        std::reverse((*list)->begin(), (*list)->end());
                    else {
                        if (!(*list)->empty()) {
                            auto type = (*list)->front().type();
                            for (const auto &value : **list)
                                if (value.type() != type || (type != Type::String && type != Type::Int &&
                                                             type != Type::Float && type != Type::Bool))
                                    throw std::runtime_error("sort requires a homogeneous scalar list.");
                        }
                        std::stable_sort(
                            (*list)->begin(), (*list)->end(), [](const Value &a, const Value &b) {
                                if (a.type() == Type::String)
                                    return std::get<std::string>(a.data) < std::get<std::string>(b.data);
                                if (a.type() == Type::Int)
                                    return std::get<std::int64_t>(a.data) < std::get<std::int64_t>(b.data);
                                if (a.type() == Type::Bool)
                                    return std::get<bool>(a.data) < std::get<bool>(b.data);
                                auto x = number(a), y = number(b);
                                return std::isnan(y) ? !std::isnan(x) : !std::isnan(x) && x < y;
                            });
                    }
                    return Value{};
                }
                if (method == "contains" || method == "indexOf" || method == "remove") {
                    if (args.size() != 1)
                        throw std::runtime_error("List method requires one value.");
                    auto at = std::find_if((*list)->begin(), (*list)->end(),
                                           [&](const Value &value) { return equal(value, args[0]); });
                    if (method == "contains")
                        return Value(at != (*list)->end());
                    if (method == "indexOf")
                        return Value(at == (*list)->end() ? std::int64_t{-1}
                                                          : static_cast<std::int64_t>(at - (*list)->begin()));
                    bool removed = at != (*list)->end();
                    if (removed)
                        (*list)->erase(at);
                    return Value(removed);
                }
                if (method == "insert" || method == "removeAt") {
                    if (args.size() != (method == "insert" ? 2u : 1u))
                        throw std::runtime_error("List method argument count mismatch.");
                    auto at = checkedIndex(args[0]);
                    if (at < 0 || static_cast<std::uint64_t>(at) > (*list)->size() ||
                        (method == "removeAt" && static_cast<std::uint64_t>(at) == (*list)->size()))
                        throw std::runtime_error("List index is outside the list.");
                    if (method == "insert") {
                        rejectCycle(args[1], list->get());
                        (*list)->insert((*list)->begin() + at, coerceElement(args[1], (*list)->elementType));
                        return Value{};
                    }
                    auto result = (**list)[at];
                    (*list)->erase((*list)->begin() + at);
                    return result;
                }
            }
            if (auto map = std::get_if<Value::MapPtr>(&receiver.data)) {
                auto method = expression->left->text;
                if (method == "remove" || method == "clear")
                    if ((*map)->immutable)
                        throw std::runtime_error("Enum namespace is immutable.");
                if (method == "containsKey" || method == "remove") {
                    if (args.size() != 1)
                        throw std::runtime_error("Map method requires one key.");
                    if (method == "containsKey")
                        return Value((*map)->contains(args[0].toString()));
                    return Value((*map)->erase(args[0].toString()) != 0);
                }
                if (method == "keys" || method == "values") {
                    if (!args.empty())
                        throw std::runtime_error("Map keys/values expects no arguments.");
                    Value::Array result;
                    for (const auto &[key, value] : **map)
                        result.push_back(method == "keys" ? Value(key) : value);
                    return Value::array(std::move(result));
                }
                if (method == "clear") {
                    if (!args.empty())
                        throw std::runtime_error("Map clear expects no arguments.");
                    (*map)->clear();
                    return Value{};
                }
            }
        }
        throw std::runtime_error("This expression call requires a runtime function binding.");
    }
    }
    throw std::runtime_error("Invalid expression node.");
}

struct SourceLine {
    std::size_t indent;
    std::string text;
    std::size_t number;
};
std::vector<SourceLine> sourceLines(const std::string &source) {
    std::vector<SourceLine> result;
    std::istringstream stream(source);
    std::string line;
    std::size_t number = 0;
    while (std::getline(stream, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::size_t start = 0, indent = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
            indent += line[start] == '\t' ? 4U : 1U;
            ++start;
        }
        std::string content = line.substr(start);
        if (content.empty() || content[0] == '#')
            continue;
        result.push_back({indent, std::move(content), number});
    }
    return result;
}

class CodeParser {
  public:
    explicit CodeParser(const std::string &source) : tokens_(lex(source)) {}
    Program parse() {
        try {
            Program result;
            result.statements = block(false);
            expect(Token::Kind::End, "Unexpected end of code.");
            return result;
        } catch (const std::exception &error) {
            throw std::runtime_error("line " + std::to_string(peek().line) + ": " + error.what());
        }
    }

  private:
    const Token &peek() const { return tokens_.at(position_); }
    bool take(Token::Kind kind) {
        if (peek().kind != kind)
            return false;
        ++position_;
        return true;
    }
    Token expect(Token::Kind kind, const char *message) {
        if (!take(kind))
            throw std::runtime_error(message);
        return tokens_.at(position_ - 1);
    }
    bool keyword(const char *word) const {
        return peek().kind == Token::Kind::Identifier && peek().text == word;
    }
    void lineEnd() {
        if (!take(Token::Kind::Newline) && peek().kind != Token::Kind::Dedent &&
            peek().kind != Token::Kind::End)
            throw std::runtime_error("Expected the end of a line.");
    }
    ExpressionPtr expression() {
        ExpressionParser parser(tokens_, position_);
        return parser.parse();
    }
    StatementList block(bool indented) {
        StatementList result;
        if (indented)
            expect(Token::Kind::Indent, "Expected an indented block.");
        while (peek().kind != Token::Kind::End && (!indented || peek().kind != Token::Kind::Dedent)) {
            if (take(Token::Kind::Newline))
                continue;
            const auto sourceLine = peek().line;
            auto item = statement();
            item.line = sourceLine;
            result.push_back(std::move(item));
        }
        if (indented)
            expect(Token::Kind::Dedent, "Expected the end of the block.");
        return result;
    }
    Statement dataDeclaration(bool enumeration) {
        Statement result;
        result.kind = enumeration ? Statement::Kind::Enum : Statement::Kind::Struct;
        ++position_;
        result.name = expect(Token::Kind::Identifier, "Expected a data type name.").text;
        expect(Token::Kind::Colon, "Expected ':' after data declaration.");
        lineEnd();
        expect(Token::Kind::Indent, "Indent data fields.");
        std::int64_t ordinal = 0;
        while (peek().kind != Token::Kind::Dedent && peek().kind != Token::Kind::End) {
            Statement field;
            field.kind = Statement::Kind::Variable;
            field.name = expect(Token::Kind::Identifier, "Expected a field/member name.").text;
            field.line = peek().line;
            if (enumeration) {
                field.expression = node(Expression::Kind::Literal);
                field.expression->literal = Value(ordinal);
                if (take(Token::Kind::Equal))
                    field.expression = expression();
                if (field.expression->kind != Expression::Kind::Literal ||
                    field.expression->literal.type() != Type::Int)
                    throw std::runtime_error("Enum values must be Int literals.");
                auto current = std::get<std::int64_t>(field.expression->literal.data);
                if (current == std::numeric_limits<std::int64_t>::max())
                    ordinal = current;
                else
                    ordinal = current + 1;
            } else {
                expect(Token::Kind::Colon, "Struct fields need type annotations.");
                auto type = annotation();
                field.declaredType = type.base;
                field.declaredTypeName = type.nominal;
                field.elementType = type.element;
                if (take(Token::Kind::Equal))
                    field.expression = expression();
            }
            lineEnd();
            result.body.push_back(std::move(field));
        }
        expect(Token::Kind::Dedent, "Expected data declaration end.");
        return result;
    }
    TypeAnnotation annotation() {
        auto text = expect(Token::Kind::Identifier, "Expected a type name.").text;
        if (take(Token::Kind::Less)) {
            text += "<" + expect(Token::Kind::Identifier, "Expected a List element type.").text;
            if (take(Token::Kind::Comma))
                text += "," + expect(Token::Kind::Identifier, "Expected Map value type.").text;
            expect(Token::Kind::Greater, "Expected '>' after collection element types.");
            text += ">";
        }
        if (take(Token::Kind::Question))
            text += "?";
        return parseAnnotation(text);
    }
    Statement statement() {
        if (keyword("enum"))
            return dataDeclaration(true);
        if (keyword("struct"))
            return dataDeclaration(false);
        if (keyword("on")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::Event;
            result.name = expect(Token::Kind::Identifier, "Expected a stable event name.").text;
            while (take(Token::Kind::Dot))
                result.name += "." + expect(Token::Kind::Identifier, "Expected event component.").text;
            expect(Token::Kind::Colon, "Expected ':' after event.");
            lineEnd();
            result.body = block(true);
            return result;
        }
        if (keyword("import")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::Import;
            if (peek().kind == Token::Kind::String) {
                result.fileImport = true;
                result.name = tokens_[position_++].text;
                lineEnd();
                return result;
            }
            result.name = expect(Token::Kind::Identifier, "Expected a module name.").text;
            while (take(Token::Kind::Dot))
                result.name += "." + expect(Token::Kind::Identifier, "Expected module component.").text;
            lineEnd();
            return result;
        }
        if (keyword("let") || keyword("const")) {
            Statement result;
            result.kind = Statement::Kind::Variable;
            result.constant = keyword("const");
            ++position_;
            Token name = expect(Token::Kind::Identifier, "Expected a variable name.");
            result.name = name.text;
            if (take(Token::Kind::Colon)) {
                auto type = annotation();
                result.declaredType = type.base;
                result.declaredTypeName = type.nominal;
                result.elementType = type.element;
            }
            if (take(Token::Kind::Equal))
                result.expression = expression();
            else if (result.constant)
                throw std::runtime_error("A constant needs an initial value.");
            lineEnd();
            return result;
        }
        if (keyword("fn")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::Function;
            result.name = expect(Token::Kind::Identifier, "Expected a function name.").text;
            expect(Token::Kind::LeftParen, "Expected '(' after function name.");
            if (!take(Token::Kind::RightParen)) {
                do {
                    result.parameters.push_back(
                        expect(Token::Kind::Identifier, "Expected a parameter name.").text);
                    auto type = take(Token::Kind::Colon) ? annotation() : TypeAnnotation{};
                    result.parameterTypes.push_back(type.base);
                    result.parameterTypeNames.push_back(type.nominal);
                    result.parameterElementTypes.push_back(type.element);
                } while (take(Token::Kind::Comma));
                expect(Token::Kind::RightParen, "Expected ')' after parameters.");
            }
            if (take(Token::Kind::Arrow)) {
                auto type = annotation();
                result.returnType = type.base;
                result.returnTypeName = type.nominal;
                result.returnElementType = type.element;
            }
            expect(Token::Kind::Colon, "Expected ':' after function header.");
            lineEnd();
            result.body = block(true);
            return result;
        }
        if (keyword("if")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::If;
            result.expression = expression();
            expect(Token::Kind::Colon, "Expected ':' after condition.");
            lineEnd();
            result.body = block(true);
            if (keyword("else")) {
                ++position_;
                if (keyword("if"))
                    result.alternative.push_back(statement());
                else {
                    expect(Token::Kind::Colon, "Expected ':' after else.");
                    lineEnd();
                    result.alternative = block(true);
                }
            }
            return result;
        }
        if (keyword("while")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::While;
            result.expression = expression();
            expect(Token::Kind::Colon, "Expected ':' after while condition.");
            lineEnd();
            result.body = block(true);
            return result;
        }
        if (keyword("for")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::ForRange;
            result.name = expect(Token::Kind::Identifier, "Expected a loop variable.").text;
            if (!keyword("in"))
                throw std::runtime_error("Expected 'in' after loop variable.");
            ++position_;
            result.expression = expression();
            if (take(Token::Kind::Range)) {
                result.kind = Statement::Kind::ForRange;
                result.rangeEnd = expression();
            } else
                result.kind = Statement::Kind::ForEach;
            expect(Token::Kind::Colon, "Expected ':' after for expression.");
            lineEnd();
            result.body = block(true);
            return result;
        }
        if (keyword("return")) {
            ++position_;
            Statement result;
            result.kind = Statement::Kind::Return;
            if (peek().kind != Token::Kind::Newline)
                result.expression = expression();
            lineEnd();
            return result;
        }
        if (keyword("break") || keyword("continue")) {
            Statement result;
            result.kind = keyword("break") ? Statement::Kind::Break : Statement::Kind::Continue;
            ++position_;
            lineEnd();
            return result;
        }
        const std::size_t saved = position_;
        ExpressionPtr left = expression();
        Statement result;
        if (peek().kind == Token::Kind::Equal || peek().kind == Token::Kind::PlusEqual ||
            peek().kind == Token::Kind::MinusEqual || peek().kind == Token::Kind::PercentEqual ||
            peek().kind == Token::Kind::StarEqual || peek().kind == Token::Kind::SlashEqual) {
            Token op = tokens_[position_++];
            result.kind = Statement::Kind::Assignment;
            result.target = std::move(left);
            result.operation = op.text;
            result.expression = expression();
        } else {
            position_ = saved;
            result.kind = Statement::Kind::Expression;
            result.expression = expression();
        }
        lineEnd();
        return result;
    }
    std::vector<Token> tokens_;
    std::size_t position_{0};
};

std::string trimLanguageText(std::string text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

// Korean Syntax constructs the shared JM AST directly. Expressions keep their common
// operator grammar and are parsed as expression nodes; no translated source program is made.
class KoreanAstParser {
  public:
    explicit KoreanAstParser(const std::string &source) : lines_(sourceLines(source)) {}
    Program parse() {
        Program result;
        result.statements = block(0);
        if (position_ != lines_.size())
            fail("Unexpected indentation.");
        return result;
    }

  private:
    [[noreturn]] void fail(const std::string &message) const {
        throw std::runtime_error("line " +
                                 std::to_string(position_ < lines_.size() ? lines_[position_].number : 0) +
                                 ": " + message);
    }
    ExpressionPtr expr(const std::string &source) {
        std::string error;
        auto parsed = parseExpression(trimLanguageText(source), &error);
        if (!parsed)
            fail("식 표현을 확인해 주세요: " + error);
        return parsed;
    }
    ExpressionPtr binary(ExpressionPtr left, std::string op, ExpressionPtr right) {
        auto result = std::make_shared<Expression>();
        result->kind = Expression::Kind::Binary;
        result->text = std::move(op);
        result->left = std::move(left);
        result->right = std::move(right);
        return result;
    }
    ExpressionPtr condition(std::string text) {
        text = trimLanguageText(text);
        for (const std::string conjunction : {"그리고", "또는"}) {
            const auto split = text.find(" " + conjunction + " ");
            if (split != std::string::npos)
                return binary(condition(text.substr(0, split)), conjunction == "그리고" ? "and" : "or",
                              condition(text.substr(split + conjunction.size() + 2)));
        }
        if (text == "참" || text == "거짓") {
            auto value = std::make_shared<Expression>();
            value->kind = Expression::Kind::Literal;
            value->literal = Value(text == "참");
            return value;
        }
        static const std::regex ordered(
            R"(^\s*(.+?)(?:이|가)\s+(.+?)보다\s*(작거나\s*같|작|크거나\s*같|크)(?:다|은|으면|다면)?\s*$)");
        std::smatch match;
        if (std::regex_match(text, match, ordered)) {
            std::string op = match[3].str();
            op.erase(std::remove_if(op.begin(), op.end(), [](unsigned char c) { return std::isspace(c); }),
                     op.end());
            if (op == "작거나같")
                op = "<=";
            else if (op == "작")
                op = "<";
            else if (op == "크거나같")
                op = ">=";
            else
                op = ">";
            return binary(expr(match[1].str()), op, expr(match[2].str()));
        }
        static const std::regex equality(
            R"(^\s*(.+?)(?:이|가)\s+(.+?)(?:와|과)\s*(같|다르)(?:다|다면|으면)?\s*$)");
        if (std::regex_match(text, match, equality))
            return binary(expr(match[1].str()), match[3].str() == "같" ? "==" : "!=", expr(match[2].str()));
        return expr(text);
    }
    StatementList block(std::size_t indent) {
        StatementList result;
        while (position_ < lines_.size() && lines_[position_].indent == indent) {
            std::string text = lines_[position_].text;
            const auto line = lines_[position_++];
            if (text == "아니라면:" || text == "그렇지 않으면:") {
                --position_;
                break;
            }
            Statement statement;
            statement.line = line.number;
            const std::unordered_map<std::string, std::string> events{
                {"시작할 때:", "start"},
                {"오른쪽 키를 누르는 동안:", "key.right.held"},
                {"왼쪽 키를 누르는 동안:", "key.left.held"},
                {"스페이스 키를 눌렀을 때:", "key.space.pressed"}};
            if (auto event = events.find(text); event != events.end()) {
                statement.kind = Statement::Kind::Event;
                statement.name = event->second;
                statement.body = nestedBlock(indent, line.number);
                result.push_back(statement);
                continue;
            }
            if (text.rfind("이벤트 ", 0) == 0 && text.back() == ':') {
                statement.kind = Statement::Kind::Event;
                statement.name = text.substr(std::string("이벤트 ").size());
                statement.name.pop_back();
                statement.body = nestedBlock(indent, line.number);
                result.push_back(statement);
                continue;
            }
            std::smatch match;
            static const std::regex dataHeader(R"(^(열거형|자료형) ([^ ]+):$)");
            if (std::regex_match(text, match, dataHeader)) {
                bool enumeration = match[1].str() == "열거형";
                statement.kind = enumeration ? Statement::Kind::Enum : Statement::Kind::Struct;
                statement.name = match[2].str();
                if (position_ >= lines_.size() || lines_[position_].indent <= indent)
                    fail("자료형 본문을 들여써 주세요.");
                auto fieldIndent = lines_[position_].indent;
                std::int64_t ordinal = 0;
                while (position_ < lines_.size() && lines_[position_].indent == fieldIndent) {
                    auto raw = lines_[position_++];
                    Statement field;
                    field.kind = Statement::Kind::Variable;
                    field.line = raw.number;
                    auto equal = raw.text.find('=');
                    auto head = trimLanguageText(raw.text.substr(0, equal));
                    if (enumeration) {
                        field.name = head;
                        field.expression = equal == std::string::npos
                                               ? node(Expression::Kind::Literal)
                                               : expr(trimLanguageText(raw.text.substr(equal + 1)));
                        if (equal == std::string::npos)
                            field.expression->literal = Value(ordinal);
                        if (field.expression->kind != Expression::Kind::Literal ||
                            field.expression->literal.type() != Type::Int)
                            fail("열거형 값은 정수 리터럴이어야 합니다.");
                        auto value = std::get<std::int64_t>(field.expression->literal.data);
                        ordinal = value == std::numeric_limits<std::int64_t>::max() ? value : value + 1;
                    } else {
                        auto colon = head.find(':');
                        if (colon == std::string::npos)
                            fail("자료형 필드에는 타입이 필요합니다.");
                        field.name = trimLanguageText(head.substr(0, colon));
                        auto type = parseAnnotation(trimLanguageText(head.substr(colon + 1)));
                        field.declaredType = type.base;
                        field.declaredTypeName = type.nominal;
                        field.elementType = type.element;
                        if (equal != std::string::npos)
                            field.expression = expr(trimLanguageText(raw.text.substr(equal + 1)));
                    }
                    statement.body.push_back(std::move(field));
                }
                result.push_back(std::move(statement));
                continue;
            }
            static const std::regex function(R"(^함수\s+([^\s(]+)\s*\(([^)]*)\)\s*(?:->\s*([^:]+?))?\s*:$)");
            static const std::regex variable(
                R"(^(List<[^>]+>|Map<[^>]+>|숫자|정수|실수|문자열|논리|목록|지도|자동|Vector2|Vector3|Color|Tuple|Range|[^\s]+)\s+(변수|상수)\s+(.+?)\s*(?:을|를)\s+(.+?)\s*(?:으로|로)\s*정한다\.?$)");
            if (std::regex_match(text, match, function)) {
                statement.kind = Statement::Kind::Function;
                statement.name = match[1].str();
                std::string params = trimLanguageText(match[2].str());
                if (!params.empty()) {
                    std::vector<std::string> parameters;
                    size_t start = 0;
                    int depth = 0;
                    for (size_t i = 0; i < params.size(); ++i) {
                        if (params[i] == '<')
                            ++depth;
                        else if (params[i] == '>')
                            --depth;
                        else if (params[i] == ',' && depth == 0) {
                            parameters.push_back(params.substr(start, i - start));
                            start = i + 1;
                        }
                        if (depth < 0)
                            fail("함수 타입 괄호를 확인해 주세요.");
                    }
                    if (depth != 0)
                        fail("함수 타입 괄호를 확인해 주세요.");
                    parameters.push_back(params.substr(start));
                    for (auto item : parameters) {
                        item = trimLanguageText(item);
                        if (item.empty())
                            fail("함수 매개변수 구문을 확인해 주세요.");
                        const auto colon = item.find(':');
                        statement.parameters.push_back(trimLanguageText(item.substr(0, colon)));
                        auto type = colon == std::string::npos
                                        ? TypeAnnotation{}
                                        : parseAnnotation(trimLanguageText(item.substr(colon + 1)));
                        statement.parameterTypes.push_back(type.base);
                        statement.parameterTypeNames.push_back(type.nominal);
                        statement.parameterElementTypes.push_back(type.element);
                    }
                }
                if (match[3].matched) {
                    auto type = parseAnnotation(trimLanguageText(match[3].str()));
                    statement.returnType = type.base;
                    statement.returnTypeName = type.nominal;
                    statement.returnElementType = type.element;
                }
                statement.body = nestedBlock(indent, line.number);
                result.push_back(std::move(statement));
                continue;
            }
            static const std::regex declaration(
                R"(^(List<[^>]+>|Map<[^>]+>|숫자|정수|실수|문자열|논리|목록|지도|Vector2|Vector3|Color|Tuple|Range|[^\s]+) 변수 (.+?)(?:을|를) 선언한다\.$)");
            if (std::regex_match(text, match, declaration)) {
                statement.kind = Statement::Kind::Variable;
                statement.name = match[2].str();
                {
                    auto type = parseAnnotation(match[1].str());
                    statement.declaredType = type.base;
                    statement.declaredTypeName = type.nominal;
                    statement.elementType = type.element;
                }
                result.push_back(statement);
                continue;
            }
            if (std::regex_match(text, match, variable)) {
                statement.kind = Statement::Kind::Variable;
                statement.constant = match[2].str() == "상수";
                statement.name = match[3].str();
                {
                    auto type = parseAnnotation(match[1].str());
                    statement.declaredType = type.base;
                    statement.declaredTypeName = type.nominal;
                    statement.elementType = type.element;
                }
                statement.expression = expr(match[4].str());
                result.push_back(std::move(statement));
                continue;
            }
            if (text == "반복을 멈춘다." || text == "다음 반복을 진행한다.") {
                statement.kind =
                    text == "반복을 멈춘다." ? Statement::Kind::Break : Statement::Kind::Continue;
                result.push_back(statement);
                continue;
            }
            if (text.rfind("가져온다 ", 0) == 0) {
                statement.kind = Statement::Kind::Import;
                statement.name = trimLanguageText(text.substr(std::string("가져온다 ").size()));
                if (statement.name.starts_with("\"")) {
                    auto value = expr(statement.name);
                    if (value->kind != Expression::Kind::Literal || value->literal.type() != Type::String)
                        fail("파일 가져오기에는 문자열 경로가 필요합니다.");
                    statement.name = std::get<std::string>(value->literal.data);
                    statement.fileImport = true;
                }
                result.push_back(statement);
                continue;
            }
            static const std::regex rangeLoop(R"(^(.+?)부터 (.+?)까지 (.+?)(?:을|를) 반복하며:$)");
            if (std::regex_match(text, match, rangeLoop)) {
                statement.kind = Statement::Kind::ForRange;
                statement.name = match[3].str();
                statement.expression = expr(match[1].str());
                statement.rangeEnd = expr(match[2].str());
                statement.body = nestedBlock(indent, line.number);
                result.push_back(statement);
                continue;
            }
            static const std::regex eachLoop(R"(^(.+?)의 각 (.+?)(?:을|를) 반복하며:$)");
            if (std::regex_match(text, match, eachLoop)) {
                statement.kind = Statement::Kind::ForEach;
                statement.name = match[2].str();
                statement.expression = expr(match[1].str());
                statement.body = nestedBlock(indent, line.number);
                result.push_back(statement);
                continue;
            }
            static const std::regex setValue(R"(^(.+?)(?:을|를) (.+?)(?:으로|로) 정한다\.?$)");
            static const std::regex setIndex(R"(^(.+?)의 (.+?)번째 값을 (.+?)(?:으로|로) 정한다\.?$)");
            if (std::regex_match(text, match, setIndex)) {
                statement.kind = Statement::Kind::Assignment;
                statement.operation = "=";
                statement.target = node(Expression::Kind::Index);
                statement.target->left = expr(match[1].str());
                statement.target->right = expr(match[2].str());
                statement.expression = expr(match[3].str());
                result.push_back(statement);
                continue;
            }
            if (std::regex_match(text, match, setValue)) {
                statement.kind = Statement::Kind::Assignment;
                statement.operation = "=";
                statement.target = expr(match[1].str());
                statement.expression = expr(match[2].str());
                result.push_back(statement);
                continue;
            }
            if (text.rfind("실행한다 ", 0) == 0) {
                statement.kind = Statement::Kind::Expression;
                statement.expression = expr(text.substr(std::string("실행한다 ").size()));
                result.push_back(statement);
                continue;
            }
            if (text == "반환한다.") {
                statement.kind = Statement::Kind::Return;
                result.push_back(statement);
                continue;
            }
            if (text.rfind("함수 ", 0) == 0 && !std::regex_match(text, match, function))
                fail("함수 선언을 확인해 주세요.");
            if (text.size() >= 3 && text.back() == ':') {
                std::string header = trimLanguageText(text.substr(0, text.size() - 1));
                if (header.size() >= 3 && (header.ends_with("라면") || header.ends_with("다면"))) {
                    const std::string ending = header.ends_with("라면") ? "라면" : "다면";
                    header.erase(header.size() - ending.size());
                    statement.kind = Statement::Kind::If;
                    statement.expression = condition(header);
                    statement.body = nestedBlock(indent, line.number);
                    if (position_ < lines_.size() && lines_[position_].indent == indent &&
                        (lines_[position_].text == "아니라면:" ||
                         lines_[position_].text == "그렇지 않으면:")) {
                        ++position_;
                        statement.alternative = nestedBlock(indent, line.number);
                    }
                    result.push_back(std::move(statement));
                    continue;
                }
                if (header.ends_with("동안")) {
                    header.erase(header.size() - std::string("동안").size());
                    statement.kind = Statement::Kind::While;
                    statement.expression = condition(header);
                    statement.body = nestedBlock(indent, line.number);
                    result.push_back(std::move(statement));
                    continue;
                }
                fail("조건 또는 반복 문장 구문을 확인해 주세요.");
            }
            const std::string returnEnding = " 반환한다";
            if (text.ends_with(returnEnding + "."))
                text.resize(text.size() - returnEnding.size() - 1);
            else if (text.ends_with(returnEnding))
                text.resize(text.size() - returnEnding.size());
            else
                text.clear();
            if (!text.empty()) {
                if (text.size() >= 3 && (text.ends_with("을") || text.ends_with("를")))
                    text.resize(text.size() - 3);
                statement.kind = Statement::Kind::Return;
                statement.expression = expr(text);
                result.push_back(std::move(statement));
                continue;
            }
            static const std::regex mutation(
                R"(^(.+?)\s*(?:을|를)\s+(.+?)만큼\s*(늘린다|줄인다|곱한다|나눈다|나머지를 구한다)\.?$)");
            if (std::regex_match(lines_[position_ - 1].text, match, mutation)) {
                statement.kind = Statement::Kind::Assignment;
                statement.target = expr(match[1].str());
                statement.operation = match[3].str() == "늘린다"            ? "+="
                                      : match[3].str() == "줄인다"          ? "-="
                                      : match[3].str() == "곱한다"          ? "*="
                                      : match[3].str() == "나머지를 구한다" ? "%="
                                                                            : "/=";
                statement.expression = expr(match[2].str());
                result.push_back(std::move(statement));
                continue;
            }
            std::string raw = lines_[position_ - 1].text;
            if (raw.ends_with(" 출력한다."))
                raw.resize(raw.size() - std::string(" 출력한다.").size());
            else if (raw.ends_with(" 출력한다"))
                raw.resize(raw.size() - std::string(" 출력한다").size());
            else
                raw.clear();
            if (!raw.empty()) {
                if (raw.ends_with("을") || raw.ends_with("를"))
                    raw.resize(raw.size() - 3);
                auto call = std::make_shared<Expression>();
                call->kind = Expression::Kind::Call;
                auto callee = std::make_shared<Expression>();
                callee->kind = Expression::Kind::Identifier;
                callee->text = "print";
                call->left = callee;
                call->arguments.push_back({{}, expr(raw)});
                statement.kind = Statement::Kind::Expression;
                statement.expression = call;
                result.push_back(std::move(statement));
                continue;
            }
            fail("지원하지 않는 訓C正音 문장입니다.");
        }
        if (position_ < lines_.size() && lines_[position_].indent > indent)
            fail("들여쓰기는 블록 안에서만 사용할 수 있습니다.");
        return result;
    }
    StatementList nestedBlock(std::size_t parentIndent, std::size_t parentLine) {
        if (position_ >= lines_.size() || lines_[position_].indent <= parentIndent) {
            (void)parentLine;
            fail("블록 본문을 들여써 주세요.");
        }
        return block(lines_[position_].indent);
    }
    std::vector<SourceLine> lines_;
    std::size_t position_{};
};

struct Flow {
    enum class Kind { Normal, Return, Break, Continue };
    Kind kind{Kind::Normal};
    Value value;
};

class Interpreter {
  public:
    Interpreter(const Program &program, RunOptions options, HostFunction host)
        : program_(program), options_(std::move(options)), host_(std::move(host)),
          globals_(std::make_shared<Environment>()),
          runtime_(jm_runtime_create_context(), jm_runtime_destroy_context) {
        semanticHistory_ = program;
        for (const auto &[name, value] : options_.initialValues)
            globals_->declare(name, value);
        for (const Statement &statement : program_.statements) {
            if (statement.kind == Statement::Kind::Function)
                functions_.emplace(statement.name, &statement);
            if (statement.kind == Statement::Kind::Struct)
                structures_[statement.name] = &statement;
        }
    }
    ExecutionResult fragment(const std::string &source) {
        Program parsed;
        Diagnostic diagnostic;
        if (!parseCode(source, parsed, diagnostic))
            throw std::runtime_error(diagnostic.message);
        std::vector<Diagnostic> errors;
        auto checking = semanticHistory_;
        checking.statements.insert(checking.statements.end(), parsed.statements.begin(),
                                   parsed.statements.end());
        if (!check(checking, errors, options_.nativeMetadata.get()))
            throw std::runtime_error(errors.front().code + ": " + errors.front().message);
        if (!initialized_)
            select({}, {});
        struct Scope {
            void *previous;
            explicit Scope(void *context) : previous(jm_runtime_activate(context)) {}
            ~Scope() { jm_runtime_activate(previous); }
        } scope(runtime_.get());
        output_.clear();
        outputBytes_ = 0;
        executed_ = 0;
        for (const auto &item : parsed.statements)
            if (item.kind == Statement::Kind::Struct) {
                if (structures_.contains(item.name))
                    throw std::runtime_error("Duplicate REPL Struct: " + item.name);
                fragments_.push_back(item);
                structures_[item.name] = &fragments_.back();
            } else if (item.kind == Statement::Kind::Function) {
                if (functions_.contains(item.name))
                    throw std::runtime_error("Duplicate REPL function: " + item.name);
                fragments_.push_back(item);
                functions_[item.name] = &fragments_.back();
            }
        Value last;
        for (const auto &item : parsed.statements) {
            if (item.kind == Statement::Kind::Expression)
                last = eval(item.expression, globals_);
            else {
                auto flow = runStatement(item, globals_);
                if (flow.kind != Flow::Kind::Normal)
                    throw std::runtime_error("REPL statement cannot escape module scope.");
            }
        }
        semanticHistory_.statements.swap(checking.statements);
        return {globals_, output_, executed_, last};
    }
    bool replace(Program &running, Program candidate, Diagnostic &diagnostic) {
        if (depth_ != 0 || !fragments_.empty()) {
            diagnostic = {"JM7201: Hot swap requires an execution boundary."};
            return false;
        }
        if (!hotSwapCompatible(running, candidate, diagnostic, options_.nativeMetadata.get()))
            return false;
        try {
            auto state = std::make_shared<Environment>(*globals_);
            std::unordered_map<std::string, const Statement *> oldGlobals, functions, structures;
            for (auto &item : running.statements)
                if (item.kind == Statement::Kind::Variable)
                    oldGlobals[item.name] = &item;
            for (auto &item : candidate.statements) {
                if (item.kind == Statement::Kind::Function)
                    functions.emplace(item.name, &item);
                if (item.kind == Statement::Kind::Struct)
                    structures.emplace(item.name, &item);
                if (item.kind != Statement::Kind::Variable)
                    continue;
                auto old = oldGlobals.find(item.name);
                bool changed = old == oldGlobals.end();
                if (!changed) {
                    Program a{{*old->second}}, b{{item}};
                    changed = !structurallyEqual(a, b);
                }
                if (changed) {
                    if (!item.expression || item.expression->kind != Expression::Kind::Literal)
                        throw std::runtime_error("JM7202: Live initializer changes require literal values.");
                    auto value = item.expression->literal;
                    if (item.declaredType == Type::Optional)
                        value = Value::optional(item.elementType, value, item.declaredTypeName);
                    if (item.declaredType == Type::Float && value.type() == Type::Int)
                        value = Value(number(value));
                    if (old != oldGlobals.end()) {
                        auto previous = state->get(item.name);
                        if (previous.type() != value.type())
                            throw std::runtime_error("JM7202: Live edit changes global type.");
                        state->eraseLocal(item.name);
                    }
                    state->declare(item.name, value, item.constant);
                }
                oldGlobals.erase(item.name);
            }
            for (auto &[name, item] : oldGlobals)
                state->eraseLocal(name);
            auto history = candidate;
            // All allocations/checks precede these noexcept swaps at a call/event boundary.
            running.statements.swap(candidate.statements);
            semanticHistory_.statements.swap(history.statements);
            functions_.swap(functions);
            structures_.swap(structures);
            globals_.swap(state);
            diagnostic = {};
            return true;
        } catch (const std::exception &error) {
            diagnostic = {error.what()};
            diagnostic.code = "JM7202";
            return false;
        }
    }
    std::map<std::string, std::string> inspect() const { return globals_->inspect(); }
    ExecutionResult select(std::string event, std::string entry) {
        options_.eventName = std::move(event);
        options_.entryFunction = std::move(entry);
        return run();
    }
    ExecutionResult run() {
        struct Scope {
            void *previous;
            explicit Scope(void *context) : previous(jm_runtime_activate(context)) {}
            ~Scope() { jm_runtime_activate(previous); }
        } runtimeScope(runtime_.get());
        output_.clear();
        outputBytes_ = 0;
        executed_ = 0;
        const Flow flow = initialized_ ? Flow{} : runBlock(program_.statements, globals_, false);
        if (flow.kind != Flow::Kind::Normal)
            throw std::runtime_error("break, continue, or return is not valid at the top level.");
        initialized_ = true;
        if (!options_.eventName.empty())
            for (const auto &statement : program_.statements)
                if (statement.kind == Statement::Kind::Event && statement.name == options_.eventName)
                    (void)call(statement, {});
        Value returnValue;
        if (!options_.entryFunction.empty()) {
            const auto found = functions_.find(options_.entryFunction);
            if (found == functions_.end())
                throw std::runtime_error("Entry function not found: " + options_.entryFunction);
            returnValue = call(*found->second, {});
        }
        return {globals_, output_, executed_, returnValue};
    }

  private:
    void step() {
        if (options_.shouldStop && options_.shouldStop())
            throw std::runtime_error("Execution stopped by the editor.");
        if (++executed_ > options_.instructionBudget)
            throw std::runtime_error("Script stopped after reaching its instruction budget.");
    }
    void emitOutput(std::string text) {
        const std::size_t limit = options_.outputByteLimit;
        if (limit != 0 && text.size() > limit - std::min(limit, outputBytes_)) {
            const std::size_t remaining = outputBytes_ < limit ? limit - outputBytes_ : 0;
            if (remaining != 0) {
                text.resize(remaining);
                outputBytes_ += text.size();
                output_.push_back(text);
                if (options_.onOutput)
                    options_.onOutput(text);
            }
            throw std::runtime_error("JM7203: Interpreter output exceeded the configured byte limit.");
        }
        outputBytes_ += text.size();
        output_.push_back(text);
        if (options_.onOutput)
            options_.onOutput(text);
    }

    Value eval(const ExpressionPtr &expression, const std::shared_ptr<Environment> &env) {
        step();
        if (expression && expression->kind == Expression::Kind::Call &&
            expression->left->kind == Expression::Kind::Identifier) {
            const std::string &name = expression->left->text;
            std::vector<Value> args;
            std::vector<std::string> names;
            for (const auto &item : expression->arguments) {
                names.push_back(item.name);
                args.push_back(eval(item.value, env));
            }
            const auto function = functions_.find(name);
            if (function != functions_.end())
                return call(*function->second, args);
            if (auto structure = structures_.find(name); structure != structures_.end())
                return construct(*structure->second, args, names);
            if (name == "print" || name == "println") {
                emitOutput(outputText(args));
                return Value{};
            }
            return invokeBuiltin(name, args, names, host_);
        }
        HostFunction nestedHost = [&](const std::string &name, const std::vector<Value> &args,
                                      const std::vector<std::string> &names) {
            if (name.rfind("builtin.", 0) == 0) {
                if (host_)
                    return host_(name, args, names);
                throw std::out_of_range("Unbound stable symbol");
            }
            const auto function = functions_.find(name);
            if (function != functions_.end())
                return call(*function->second, args);
            if (auto structure = structures_.find(name); structure != structures_.end())
                return construct(*structure->second, args, names);
            if (name == "print" || name == "println") {
                emitOutput(outputText(args));
                return Value{};
            }
            return invokeBuiltin(name, args, names, host_);
        };
        return evaluateExpression(expression, *env, nestedHost);
    }
    Value construct(const Statement &declaration, const std::vector<Value> &args,
                    const std::vector<std::string> &names) {
        if (args.size() > declaration.body.size())
            throw std::runtime_error("Struct constructor has too many arguments.");
        auto result = std::make_shared<StructObject>();
        result->name = declaration.name;
        result->fields = std::make_shared<Value::Map>();
        std::unordered_set<size_t> supplied;
        for (size_t i = 0; i < declaration.body.size(); ++i) {
            const auto &field = declaration.body[i];
            result->schema[field.name] = {field.declaredType, field.elementType, field.declaredTypeName};
        }
        for (size_t i = 0; i < args.size(); ++i) {
            size_t field = i;
            if (!names[i].empty()) {
                field = declaration.body.size();
                for (size_t j = 0; j < declaration.body.size(); ++j)
                    if (declaration.body[j].name == names[i])
                        field = j;
            }
            if (field >= declaration.body.size() || !supplied.insert(field).second)
                throw std::runtime_error("Unknown or duplicate struct constructor field.");
            const auto &spec = declaration.body[field];
            auto value = coerceElement(args[i], spec.declaredType, spec.elementType, spec.declaredTypeName);
            if (spec.declaredType == Type::List)
                typeList(value, spec.elementType);
            if (spec.declaredType == Type::Map)
                typeMap(value, spec.elementType);
            result->fields->emplace(spec.name, std::move(value));
        }
        for (size_t i = 0; i < declaration.body.size(); ++i)
            if (!supplied.contains(i)) {
                const auto &spec = declaration.body[i];
                if (!spec.expression)
                    throw std::runtime_error("Missing struct constructor field: " + spec.name);
                auto value = coerceElement(eval(spec.expression, globals_), spec.declaredType,
                                           spec.elementType, spec.declaredTypeName);
                if (spec.declaredType == Type::List)
                    typeList(value, spec.elementType);
                if (spec.declaredType == Type::Map)
                    typeMap(value, spec.elementType);
                result->fields->emplace(spec.name, std::move(value));
            }
        return Value(result);
    }
    Value readTarget(const ExpressionPtr &target, const std::shared_ptr<Environment> &env) {
        return eval(target, env);
    }
    void assignTarget(const ExpressionPtr &target, Value value, const std::shared_ptr<Environment> &env) {
        step();
        if (!target)
            throw std::runtime_error("Missing assignment target.");
        if (target->kind == Expression::Kind::Identifier) {
            const auto current = env->get(target->text);
            if (current.type() == Type::Optional) {
                auto &old = *std::get<std::shared_ptr<OptionalValue>>(current.data);
                value = Value::optional(old.element, value, old.nominal);
                auto &box = *std::get<std::shared_ptr<OptionalValue>>(value.data);
                if (box.schema.empty())
                    box.schema = old.schema;
            }
            if (current.type() == Type::Float && value.type() == Type::Int)
                value = Value(number(value));
            if (current.type() != Type::Void && current.type() != value.type())
                throw std::runtime_error("JM2001: Assignment changes variable type: " + target->text);
            if (current.type() == Type::Enum &&
                std::get<EnumValue>(current.data).type != std::get<EnumValue>(value.data).type)
                throw std::runtime_error("Enum assignment requires the same enum type.");
            if (current.type() == Type::Struct && std::get<Value::StructPtr>(current.data)->name !=
                                                      std::get<Value::StructPtr>(value.data)->name)
                throw std::runtime_error("Struct assignment requires the same named type.");
            if (current.type() == Type::List)
                typeList(value, std::get<Value::ArrayPtr>(current.data)->elementType);
            if (current.type() == Type::Map)
                typeMap(value, std::get<Value::MapPtr>(current.data)->elementType);
            env->assign(target->text, std::move(value));
            return;
        }
        if (target->kind == Expression::Kind::Member && host_) {
            auto receiver = eval(target->left, env).unwrap();
            if (receiver.type() == Type::Entity) {
                auto property = target->text;
                property[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(property[0])));
                host_("builtin.entity.set" + property, {receiver, value}, {});
                return;
            }
        }
        if (target->kind == Expression::Kind::Index) {
            const Value container = eval(target->left, env).unwrap();
            const Value index = eval(target->right, env);
            if (auto list = std::get_if<Value::ArrayPtr>(&container.data)) {
                const auto at = checkedIndex(index);
                if (at < 0)
                    throw std::runtime_error("List index cannot be negative.");
                if (static_cast<std::size_t>(at) >= list->get()->size())
                    throw std::runtime_error("List assignment index must already exist.");
                rejectCycle(value, list->get());
                list->get()->at(static_cast<std::size_t>(at)) =
                    coerceElement(std::move(value), (*list)->elementType);
                return;
            }
            if (auto map = std::get_if<Value::MapPtr>(&container.data)) {
                if ((*map)->immutable)
                    throw std::runtime_error("Enum namespace is immutable.");
                rejectCycle(value, map->get());
                if ((*map)->elementType != Type::Any && index.type() != Type::String)
                    throw std::runtime_error("JM2005: Typed Map key must be String.");
                map->get()->insert_or_assign(index.toString(),
                                             coerceElement(std::move(value), (*map)->elementType));
                return;
            }
            throw std::runtime_error("Only a list or map item can be assigned.");
        }
        if (target->kind == Expression::Kind::Member) {
            const Value container = eval(target->left, env).unwrap();
            if (auto structure = std::get_if<Value::StructPtr>(&container.data)) {
                auto found = (*structure)->schema.find(target->text);
                if (found == (*structure)->schema.end())
                    throw std::runtime_error("Unknown struct field.");
                value =
                    coerceElement(value, found->second.base, found->second.element, found->second.nominal);
                if (found->second.base == Type::List)
                    typeList(value, found->second.element);
                rejectCycle(value, (*structure)->fields.get());
                (*structure)->fields->at(target->text) = std::move(value);
                return;
            }
            if (auto map = std::get_if<Value::MapPtr>(&container.data)) {
                if ((*map)->immutable)
                    throw std::runtime_error("Enum namespace is immutable.");
                rejectCycle(value, map->get());
                map->get()->insert_or_assign(target->text, std::move(value));
                return;
            }
        }
        throw std::runtime_error("This expression cannot be assigned to.");
    }
    Flow runBlock(const StatementList &statements, const std::shared_ptr<Environment> &parent,
                  bool childScope) {
        auto env = childScope ? parent->child() : parent;
        for (const Statement &statement : statements) {
            step();
            Flow flow = runStatement(statement, env);
            if (flow.kind != Flow::Kind::Normal)
                return flow;
        }
        return {};
    }
    Flow runStatement(const Statement &statement, const std::shared_ptr<Environment> &env) {
        switch (statement.kind) {
        case Statement::Kind::Struct:
            return {};
        case Statement::Kind::Enum: {
            Value::Map members;
            for (const auto &member : statement.body)
                members.emplace(member.name,
                                Value(EnumValue{statement.name, member.name,
                                                std::get<std::int64_t>(member.expression->literal.data)}));
            members.immutable = true;
            env->declare(statement.name, Value::map(std::move(members)), true);
            return {};
        }
        case Statement::Kind::Variable: {
            Value value = statement.expression ? eval(statement.expression, env) : Value{};
            if (!statement.expression)
                switch (statement.declaredType) {
                case Type::Int:
                    value = Value(std::int64_t{0});
                    break;
                case Type::Float:
                    value = Value(0.0);
                    break;
                case Type::Bool:
                    value = Value(false);
                    break;
                case Type::String:
                    value = Value("");
                    break;
                case Type::List:
                    value = Value::array({});
                    break;
                case Type::Map:
                    value = Value::map({});
                    break;
                case Type::Vector2:
                    value = Value(Vector2Value{});
                    break;
                default:
                    break;
                }
            if (statement.declaredType == Type::Optional) {
                value = Value::optional(statement.elementType, value, statement.declaredTypeName);
                if (statement.elementType == Type::Struct &&
                    structures_.contains(statement.declaredTypeName)) {
                    auto &box = *std::get<std::shared_ptr<OptionalValue>>(value.data);
                    for (const auto &field : structures_.at(statement.declaredTypeName)->body)
                        box.schema[field.name] = {field.declaredType, field.elementType,
                                                  field.declaredTypeName};
                }
            }
            if (statement.declaredType == Type::Float && value.type() == Type::Int)
                value = Value(number(value));
            if (statement.declaredType != Type::Any && value.type() != statement.declaredType)
                throw std::runtime_error("JM2001: Variable '" + statement.name + "' requires " +
                                         typeName(statement.declaredType) + ", got " + value.typeName());
            if (statement.declaredType == Type::List)
                typeList(value, statement.elementType);
            if (statement.declaredType == Type::Map)
                typeMap(value, statement.elementType);
            env->declare(statement.name, std::move(value), statement.constant);
            return {};
        }
        case Statement::Kind::Import:
            if (!standardModule(statement.name) &&
                (!options_.moduleResolver || !options_.moduleResolver(statement.name)))
                throw std::runtime_error("JM2008: Unknown module: " + statement.name);
            return {};
        case Statement::Kind::Assignment: {
            Value value = eval(statement.expression, env);
            if (statement.operation != "=") {
                Expression compound;
                compound.kind = Expression::Kind::Binary;
                compound.text = statement.operation.substr(0, 1);
                compound.left = std::make_shared<Expression>();
                compound.left->kind = Expression::Kind::Literal;
                compound.left->literal = readTarget(statement.target, env);
                compound.right = std::make_shared<Expression>();
                compound.right->kind = Expression::Kind::Literal;
                compound.right->literal = value;
                value = evaluateExpression(std::make_shared<Expression>(compound), *env);
            }
            if (host_ && statement.target && statement.target->kind == Expression::Kind::Member &&
                statement.target->left && statement.target->left->kind == Expression::Kind::Identifier &&
                !env->contains(statement.target->left->text)) {
                auto property = statement.target->text;
                if (property == "position" || property == "velocity" || property == "scale" ||
                    property == "rotation") {
                    auto setter = property;
                    setter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(setter[0])));
                    host_("builtin." + statement.target->left->text + ".set" + setter, {value}, {});
                    return {};
                }
            }
            assignTarget(statement.target, std::move(value), env);
            return {};
        }
        case Statement::Kind::Expression:
            (void)eval(statement.expression, env);
            return {};
        case Statement::Kind::If:
            return runBlock(truth(eval(statement.expression, env)) ? statement.body : statement.alternative,
                            env, true);
        case Statement::Kind::While:
            while (truth(eval(statement.expression, env))) {
                Flow flow = runBlock(statement.body, env, true);
                if (flow.kind == Flow::Kind::Return)
                    return flow;
                if (flow.kind == Flow::Kind::Break)
                    break;
                if (flow.kind == Flow::Kind::Continue)
                    continue;
            }
            return {};
        case Statement::Kind::ForRange: {
            const auto start = checkedIndex(eval(statement.expression, env));
            const auto end = checkedIndex(eval(statement.rangeEnd, env));
            auto loop = env->child();
            loop->declare(statement.name, Value(start));
            for (std::int64_t value = start; value < end; ++value) {
                loop->assign(statement.name, Value(value));
                Flow flow = runBlock(statement.body, loop, true);
                if (flow.kind == Flow::Kind::Return)
                    return flow;
                if (flow.kind == Flow::Kind::Break)
                    break;
                if (flow.kind == Flow::Kind::Continue)
                    continue;
            }
            return {};
        }
        case Statement::Kind::ForEach: {
            const Value collection = eval(statement.expression, env);
            auto loop = env->child();
            loop->declare(statement.name, Value{});
            auto runOne = [&](const Value &value) {
                loop->assign(statement.name, value);
                return runBlock(statement.body, loop, true);
            };
            if (auto list = std::get_if<Value::ArrayPtr>(&collection.data)) {
                const auto snapshot = **list;
                for (const Value &item : snapshot) {
                    step();
                    Flow flow = runOne(item);
                    if (flow.kind == Flow::Kind::Return)
                        return flow;
                    if (flow.kind == Flow::Kind::Break)
                        break;
                }
                return {};
            }
            if (auto map = std::get_if<Value::MapPtr>(&collection.data)) {
                const auto snapshot = **map;
                for (const auto &[key, value] : snapshot) {
                    step();
                    (void)key;
                    Flow flow = runOne(value);
                    if (flow.kind == Flow::Kind::Return)
                        return flow;
                    if (flow.kind == Flow::Kind::Break)
                        break;
                }
                return {};
            }
            if (auto range = std::get_if<RangeValue>(&collection.data)) {
                for (auto value = range->start; range->step > 0 ? value < range->end : value > range->end;) {
                    step();
                    auto flow = runOne(Value(value));
                    if (flow.kind == Flow::Kind::Return)
                        return flow;
                    if (flow.kind == Flow::Kind::Break)
                        break;
                    if ((range->step > 0 && value > std::numeric_limits<std::int64_t>::max() - range->step) ||
                        (range->step < 0 && value < std::numeric_limits<std::int64_t>::min() - range->step))
                        break;
                    value += range->step;
                }
                return {};
            }
            if (auto tuple = std::get_if<Value::TuplePtr>(&collection.data)) {
                for (const auto &item : (*tuple)->values) {
                    step();
                    auto flow = runOne(item);
                    if (flow.kind == Flow::Kind::Return)
                        return flow;
                    if (flow.kind == Flow::Kind::Break)
                        break;
                }
                return {};
            }
            throw std::runtime_error("for-in needs a List, Map, Tuple, or Range.");
        }
        case Statement::Kind::Event:
        case Statement::Kind::Function:
            return {};
        case Statement::Kind::Return:
            return {Flow::Kind::Return, statement.expression ? eval(statement.expression, env) : Value{}};
        case Statement::Kind::Break:
            return {Flow::Kind::Break, {}};
        case Statement::Kind::Continue:
            return {Flow::Kind::Continue, {}};
        }
        return {};
    }
    Value call(const Statement &function, const std::vector<Value> &arguments) {
        if (++depth_ > options_.recursionLimit) {
            --depth_;
            throw std::runtime_error("Function recursion limit reached in '" + function.name + "'.");
        }
        struct Depth {
            std::size_t &value;
            ~Depth() { --value; }
        } depthGuard{depth_};
        if (arguments.size() != function.parameters.size()) {
            throw std::runtime_error("Function '" + function.name + "' expects " +
                                     std::to_string(function.parameters.size()) + " arguments.");
        }
        auto local = globals_->child();
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            Value argument = arguments[i];
            const auto type = i < function.parameterTypes.size() ? function.parameterTypes[i] : Type::Any;
            if (type == Type::Optional)
                argument = Value::optional(
                    function.parameterElementTypes.at(i), argument,
                    i < function.parameterTypeNames.size() ? function.parameterTypeNames[i] : "");
            if (type != Type::Optional && argument.type() == Type::Optional)
                argument = argument.unwrap();
            if (type == Type::Float && argument.type() == Type::Int)
                argument = Value(number(argument));
            if (type != Type::Any && type != argument.type()) {
                throw std::runtime_error("JM2003: Parameter '" + function.parameters[i] + "' requires " +
                                         typeName(type));
            }
            if (type == Type::List)
                typeList(argument, i < function.parameterElementTypes.size()
                                       ? function.parameterElementTypes[i]
                                       : Type::Any);
            if (type == Type::Map)
                typeMap(argument, i < function.parameterElementTypes.size()
                                      ? function.parameterElementTypes[i]
                                      : Type::Any);
            local->declare(function.parameters[i], argument);
        }
        Flow flow = runBlock(function.body, local, false);
        if (flow.kind == Flow::Kind::Return) {
            if (function.returnType == Type::Optional)
                flow.value = Value::optional(function.returnElementType, flow.value, function.returnTypeName);
            if (function.returnType != Type::Optional && flow.value.type() == Type::Optional)
                flow.value = flow.value.unwrap();
            if (function.returnType == Type::Float && flow.value.type() == Type::Int)
                return Value(number(flow.value));
            if (function.returnType != Type::Any && function.returnType != flow.value.type())
                throw std::runtime_error("JM2002: Return type mismatch in '" + function.name + "'.");
            if (function.returnType == Type::List)
                typeList(flow.value, function.returnElementType);
            if (function.returnType == Type::Map)
                typeMap(flow.value, function.returnElementType);
            return flow.value;
        }
        if (flow.kind != Flow::Kind::Normal)
            throw std::runtime_error("break or continue cannot leave a function.");
        return {};
    }
    const Program &program_;
    Program semanticHistory_;
    RunOptions options_;
    HostFunction host_;
    std::shared_ptr<Environment> globals_;
    std::unordered_map<std::string, const Statement *> functions_, structures_;
    std::vector<std::string> output_;
    std::deque<Statement> fragments_;
    std::shared_ptr<void> runtime_;
    bool initialized_{false};
    std::size_t executed_{0};
    std::size_t depth_{0};
    std::size_t outputBytes_{0};
};

} // namespace

Value Value::array(Array value) {
    Value result;
    result.data = std::make_shared<Array>(std::move(value));
    return result;
}
std::uint64_t stableBuiltinSymbolId(std::string_view name) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char byte : name) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}
Value Value::map(Map value) {
    Value result;
    result.data = std::make_shared<Map>(std::move(value));
    return result;
}
Value Value::optional(Type element, Value value, std::string nominal) {
    if (element == Type::Struct && value.type() == Type::Struct) {
        auto name = std::get<StructPtr>(value.data)->name;
        if (!nominal.empty() && nominal != name)
            throw std::runtime_error("JM2010: Named Optional Struct mismatch.");
        nominal = name;
    }
    if (auto box = std::get_if<std::shared_ptr<OptionalValue>>(&value.data)) {
        if ((*box)->element != element || (!nominal.empty() && (*box)->nominal != nominal))
            throw std::runtime_error("JM2010: Optional payload type mismatch.");
        return value;
    }
    if (element == Type::Float && value.type() == Type::Int)
        value = Value(static_cast<double>(std::get<int64_t>(value.data)));
    if (!value.isNull() && value.type() != element)
        throw std::runtime_error("JM2010: Optional payload type mismatch.");
    Value result;
    result.data =
        std::make_shared<OptionalValue>(OptionalValue{element, std::move(value), std::move(nominal), {}});
    auto &box = *std::get<std::shared_ptr<OptionalValue>>(result.data);
    if (box.value.type() == Type::Struct)
        box.schema = std::get<StructPtr>(box.value.data)->schema;
    return result;
}
Value Value::unwrap() const {
    if (auto box = std::get_if<std::shared_ptr<OptionalValue>>(&data)) {
        if ((*box)->value.isNull())
            throw std::runtime_error("JM2011: Optional is empty; check != null before use.");
        return (*box)->value;
    }
    return *this;
}
bool Value::isNull() const {
    if (auto box = std::get_if<std::shared_ptr<OptionalValue>>(&data))
        return (*box)->value.isNull();
    return std::holds_alternative<std::monostate>(data);
}
std::string Value::typeName() const {
    if (auto box = std::get_if<std::shared_ptr<OptionalValue>>(&data))
        return annotationName(Type::Optional, (*box)->element, (*box)->nominal);
    switch (data.index()) {
    case 0:
        return "Null";
    case 1:
        return "Boolean";
    case 2:
        return "Float";
    case 3:
        return "String";
    case 4:
        return "List";
    case 5:
        return "Map";
    case 6:
        return "Vector2";
    case 7:
        return "Color";
    case 8:
        return "Entity";
    case 9:
        return "Int";
    case 10:
        return "Vector3";
    case 11:
        return "Enum";
    case 12:
        return "Struct";
    case 13:
        return "Tuple";
    case 14:
        return "Range";
    default:
        return "Unknown";
    }
}
std::string Value::toString() const {
    if (auto box = std::get_if<std::shared_ptr<OptionalValue>>(&data))
        return (*box)->value.toString();
    if (isNull())
        return "null";
    if (auto value = std::get_if<std::int64_t>(&data))
        return std::to_string(*value);
    if (auto value = std::get_if<bool>(&data))
        return *value ? "true" : "false";
    if (auto value = std::get_if<double>(&data)) {
        char buffer[128];
        auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), *value);
        if (error != std::errc{})
            throw std::runtime_error("Float formatting failed.");
        return {buffer, end};
    }
    if (auto value = std::get_if<std::string>(&data))
        return *value;
    if (auto value = std::get_if<ArrayPtr>(&data)) {
        std::string out = "[";
        for (std::size_t i = 0; i < (*value)->size(); ++i) {
            if (i)
                out += ", ";
            out += (**value)[i].toString();
        }
        return out + "]";
    }
    if (auto value = std::get_if<MapPtr>(&data)) {
        std::string out = "{";
        bool first = true;
        for (const auto &[key, item] : **value) {
            if (!first)
                out += ", ";
            first = false;
            out += key + ": " + item.toString();
        }
        return out + "}";
    }
    if (auto value = std::get_if<Vector2Value>(&data))
        return "(" + Value(value->x).toString() + ", " + Value(value->y).toString() + ")";
    if (auto value = std::get_if<TuplePtr>(&data)) {
        std::string result = "(";
        for (size_t i = 0; i < (*value)->values.size(); ++i) {
            if (i)
                result += ", ";
            result += (*value)->values[i].toString();
        }
        if ((*value)->values.size() == 1)
            result += ",";
        return result + ")";
    }
    if (auto value = std::get_if<RangeValue>(&data))
        return "range(" + std::to_string(value->start) + ", " + std::to_string(value->end) + ", " +
               std::to_string(value->step) + ")";
    if (auto value = std::get_if<EnumValue>(&data))
        return value->type + "." + value->member;
    if (auto value = std::get_if<StructPtr>(&data)) {
        Value fields;
        fields.data = (*value)->fields;
        return (*value)->name + fields.toString();
    }
    if (auto value = std::get_if<Vector3Value>(&data))
        return "(" + Value(value->x).toString() + ", " + Value(value->y).toString() + ", " +
               Value(value->z).toString() + ")";
    if (auto value = std::get_if<ColorValue>(&data))
        return "Color(" + Value(value->r).toString() + ", " + Value(value->g).toString() + ", " +
               Value(value->b).toString() + ", " + Value(value->a).toString() + ")";
    return "Entity(" + std::get<EntityReference>(data).id + ")";
}

Environment::Environment(std::shared_ptr<Environment> parent) : parent_(std::move(parent)) {}
void Environment::declare(const std::string &name, Value value, bool constant) {
    if (name.empty())
        throw std::runtime_error("Variable name cannot be empty.");
    if (!bindings_.emplace(name, Binding{std::move(value), constant}).second)
        throw std::runtime_error("Variable '" + name + "' is already declared in this scope.");
}
void Environment::assign(const std::string &name, Value value) {
    const auto found = bindings_.find(name);
    if (found != bindings_.end()) {
        if (found->second.constant)
            throw std::runtime_error("Cannot change constant '" + name + "'.");
        found->second.value = std::move(value);
        return;
    }
    if (parent_) {
        parent_->assign(name, std::move(value));
        return;
    }
    throw std::runtime_error("Variable '" + name + "' is not defined.");
}
Value Environment::get(const std::string &name) const {
    const auto found = bindings_.find(name);
    if (found != bindings_.end())
        return found->second.value;
    if (parent_)
        return parent_->get(name);
    throw std::runtime_error("Variable '" + name + "' is not defined.");
}
bool Environment::contains(const std::string &name) const {
    return bindings_.contains(name) || (parent_ && parent_->contains(name));
}
bool Environment::containsLocal(const std::string &name) const { return bindings_.contains(name); }
std::shared_ptr<Environment> Environment::child() {
    return std::make_shared<Environment>(shared_from_this());
}

ExpressionPtr parseExpression(const std::string &source, std::string *error) {
    try {
        std::vector<Token> tokens = lex(source);
        std::size_t position = 0;
        ExpressionParser parser(tokens, position);
        auto result = parser.parse();
        if (tokens[position].kind != Token::Kind::Newline && tokens[position].kind != Token::Kind::End)
            throw std::runtime_error("Unexpected text after expression.");
        if (error)
            error->clear();
        return result;
    } catch (const std::exception &exception) {
        if (error)
            *error = exception.what();
        return {};
    }
}
Value evaluateExpression(const ExpressionPtr &expression, const Environment &environment,
                         const HostFunction &hostFunction) {
    return evaluate(expression, environment, hostFunction);
}
Value evaluateExpression(const std::string &source, const Environment &environment,
                         const HostFunction &hostFunction) {
    std::string error;
    auto expression = parseExpression(source, &error);
    if (!expression)
        throw std::runtime_error(error);
    return evaluate(expression, environment, hostFunction);
}

namespace {
void validateIdentifiers(const StatementList &statements) {
    auto valid = [](const std::string &name) {
        static const std::unordered_set<std::string> reserved{
            "let",    "const", "fn",   "if",    "else", "while", "for", "in",  "return", "break", "continue",
            "import", "on",    "true", "false", "null", "and",   "or",  "not", "참",     "거짓"};
        if (name.empty() || reserved.contains(name))
            return false;
        const auto first = static_cast<unsigned char>(name.front());
        if (!isIdentifierStart(first) && first < 0x80)
            return false;
        for (const unsigned char byte : name)
            if (!isIdentifierPart(byte) && byte < 0x80)
                return false;
        return true;
    };
    for (const auto &item : statements) {
        if ((item.kind == Statement::Kind::Struct || item.kind == Statement::Kind::Enum ||
             item.kind == Statement::Kind::Variable || item.kind == Statement::Kind::Function ||
             item.kind == Statement::Kind::ForRange || item.kind == Statement::Kind::ForEach) &&
            !valid(item.name))
            throw std::runtime_error("line " + std::to_string(item.line) +
                                     ": Invalid or reserved identifier '" + item.name + "'.");
        for (const auto &parameter : item.parameters)
            if (!valid(parameter))
                throw std::runtime_error("line " + std::to_string(item.line) +
                                         ": Invalid parameter identifier '" + parameter + "'.");
        validateIdentifiers(item.body);
        validateIdentifiers(item.alternative);
    }
}
} // namespace

bool parseCode(const std::string &source, Program &output, Diagnostic &diagnostic) {
    try {
        Program parsed = CodeParser(source).parse();
        validateIdentifiers(parsed.statements);
        output = std::move(parsed);
        diagnostic = {};
        return true;
    } catch (const std::exception &exception) {
        std::string message = exception.what();
        static const std::regex prefix(R"(^line ([0-9]+): (.*)$)");
        std::smatch match;
        if (std::regex_match(message, match, prefix))
            diagnostic = {match[2].str(), static_cast<std::size_t>(std::stoull(match[1].str()))};
        else
            diagnostic = {message, 0};
        return false;
    }
}

bool parseKorean(const std::string &source, Program &output, Diagnostic &diagnostic) {
    try {
        Program parsed = KoreanAstParser(source).parse();
        validateIdentifiers(parsed.statements);
        output = std::move(parsed);
        diagnostic = {};
        return true;
    } catch (const std::exception &exception) {
        const std::string message = exception.what();
        static const std::regex linePrefix(R"(^line ([0-9]+): (.*)$)");
        std::smatch match;
        if (std::regex_match(message, match, linePrefix))
            diagnostic = {match[2].str(), static_cast<std::size_t>(std::stoull(match[1].str()))};
        else
            diagnostic = {message, 0};
        return false;
    }
}

bool structurallyEqual(const Program &left, const Program &right) {
    std::function<bool(const ExpressionPtr &, const ExpressionPtr &)> sameExpression;
    sameExpression = [&](const ExpressionPtr &a, const ExpressionPtr &b) {
        if (!a || !b)
            return !a && !b;
        if (a->kind != b->kind || a->text != b->text || a->builtinSymbolId != b->builtinSymbolId ||
            a->builtinSymbolName != b->builtinSymbolName || !equal(a->literal, b->literal) ||
            a->literal.type() != b->literal.type() || a->elements.size() != b->elements.size() ||
            a->entries.size() != b->entries.size() || a->arguments.size() != b->arguments.size() ||
            !sameExpression(a->left, b->left) || !sameExpression(a->right, b->right))
            return false;
        for (std::size_t i = 0; i < a->elements.size(); ++i)
            if (!sameExpression(a->elements[i], b->elements[i]))
                return false;
        for (std::size_t i = 0; i < a->entries.size(); ++i)
            if (a->entries[i].first != b->entries[i].first ||
                !sameExpression(a->entries[i].second, b->entries[i].second))
                return false;
        for (std::size_t i = 0; i < a->arguments.size(); ++i)
            if (a->arguments[i].name != b->arguments[i].name ||
                !sameExpression(a->arguments[i].value, b->arguments[i].value))
                return false;
        return true;
    };
    std::function<bool(const StatementList &, const StatementList &)> sameStatements;
    sameStatements = [&](const StatementList &a, const StatementList &b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const Statement &x = a[i];
            const Statement &y = b[i];
            if (x.kind != y.kind || x.name != y.name || x.operation != y.operation ||
                x.constant != y.constant || x.fileImport != y.fileImport || x.parameters != y.parameters ||
                x.declaredType != y.declaredType || x.returnType != y.returnType ||
                x.parameterTypes != y.parameterTypes || x.elementType != y.elementType ||
                x.returnElementType != y.returnElementType || x.declaredTypeName != y.declaredTypeName ||
                x.returnTypeName != y.returnTypeName || ([&] {
                    for (size_t j = 0; j < x.parameters.size(); ++j)
                        if ((j < x.parameterTypeNames.size() ? x.parameterTypeNames[j] : "") !=
                            (j < y.parameterTypeNames.size() ? y.parameterTypeNames[j] : ""))
                            return true;
                    return false;
                }()) ||
                ([&] {
                    for (size_t j = 0; j < x.parameters.size(); ++j)
                        if ((j < x.parameterElementTypes.size() ? x.parameterElementTypes[j] : Type::Any) !=
                            (j < y.parameterElementTypes.size() ? y.parameterElementTypes[j] : Type::Any))
                            return true;
                    return false;
                }()) ||
                !sameExpression(x.target, y.target) || !sameExpression(x.expression, y.expression) ||
                !sameExpression(x.rangeEnd, y.rangeEnd) || !sameStatements(x.body, y.body) ||
                !sameStatements(x.alternative, y.alternative))
                return false;
        }
        return true;
    };
    return sameStatements(left.statements, right.statements);
}

void Environment::eraseLocal(const std::string &name) { bindings_.erase(name); }
std::map<std::string, std::string> Environment::inspect(std::size_t limit) const {
    std::map<std::string, std::string> result;
    for (const auto &[name, binding] : bindings_) {
        if (result.size() >= limit)
            break;
        auto type = binding.value.type();
        // Avoid traversing arbitrarily large containers on the render thread.
        auto payloadType = type == Type::Optional
                               ? std::get<std::shared_ptr<OptionalValue>>(binding.value.data)->element
                               : type;
        const Value &observed = type == Type::Optional
                                    ? std::get<std::shared_ptr<OptionalValue>>(binding.value.data)->value
                                    : binding.value;
        auto text = payloadType == Type::List || payloadType == Type::Map || payloadType == Type::Struct ||
                            payloadType == Type::Tuple
                        ? "<aggregate>"
                    : observed.type() == Type::String ? std::get<std::string>(observed.data).substr(0, 256)
                                                      : observed.toString();
        if (text.size() > 256)
            text.resize(256);
        result.emplace(name, binding.value.typeName() + ": " + text);
    }
    return result;
}
bool hotSwapCompatible(const Program &running, const Program &candidate, Diagnostic &diagnostic,
                       const ir::NativeFunctionRegistry *registry) {
    std::vector<Diagnostic> errors;
    if (!check(candidate, errors, registry)) {
        diagnostic = errors.front();
        return false;
    }
    auto reject = [&](const std::string &message) {
        diagnostic = {message};
        diagnostic.code = "JM7202";
        return false;
    };
    std::unordered_map<std::string, const Statement *> oldFunctions, newFunctions, oldGlobals;
    Program oldDefinitions, newDefinitions;
    for (auto &item : running.statements) {
        if (item.kind == Statement::Kind::Function)
            oldFunctions[item.name] = &item;
        else if (item.kind == Statement::Kind::Variable)
            oldGlobals[item.name] = &item;
        else if (item.kind != Statement::Kind::Event)
            oldDefinitions.statements.push_back(item);
    }
    for (auto &item : candidate.statements) {
        if (item.kind == Statement::Kind::Function)
            newFunctions[item.name] = &item;
        else if (item.kind == Statement::Kind::Variable) {
            if (oldGlobals.contains(item.name)) {
                auto &old = *oldGlobals.at(item.name);
                if (old.declaredType != item.declaredType || old.elementType != item.elementType ||
                    old.declaredTypeName != item.declaredTypeName || old.constant != item.constant)
                    return reject("JM7202: Global declaration type/constness changed: " + item.name);
            }
        } else if (item.kind != Statement::Kind::Event)
            newDefinitions.statements.push_back(item);
    }
    if (!structurallyEqual(oldDefinitions, newDefinitions))
        return reject("JM7202: Imports/type schemas/top-level actions cannot hot-swap.");
    if (oldFunctions.size() != newFunctions.size())
        return reject("JM7202: Function set changes are unsupported.");
    for (auto &[name, old] : oldFunctions) {
        if (std::any_of(old->parameterTypes.begin(), old->parameterTypes.end(),
                        [](Type type) { return type == Type::Any; }))
            return reject("JM7202: Live functions need concrete parameter annotations.");
        if (!newFunctions.contains(name))
            return reject("JM7202: Removed function: " + name);
        auto a = *old, b = *newFunctions.at(name);
        a.body.clear();
        b.body.clear();
        if (!structurallyEqual(Program{{a}}, Program{{b}}))
            return reject("JM7202: Function signature changed: " + name);
    }
    diagnostic = {};
    return true;
}
struct ExecutionSession::Impl {
    Program program;
    std::uint64_t generation{1};
    std::thread::id thread{std::this_thread::get_id()};
    bool active{};
    struct Guard {
        bool &active;
        ~Guard() { active = false; }
    };
    bool boundary() const { return std::this_thread::get_id() == thread && !active; }
    Guard enter() {
        if (!boundary())
            throw std::runtime_error("JM7201: Session requires an execution boundary on its owner thread.");
        active = true;
        return Guard{active};
    }
    std::unique_ptr<Interpreter> interpreter;
    Impl(Program value, RunOptions options, HostFunction host) : program(std::move(value)) {
        std::vector<Diagnostic> errors;
        if (!check(program, errors, options.nativeMetadata.get()))
            throw std::runtime_error(errors.front().code + ": " + errors.front().message);
        interpreter = std::make_unique<Interpreter>(program, std::move(options), std::move(host));
    }
};
ExecutionSession::ExecutionSession(Program program, RunOptions options, HostFunction host)
    : impl_(std::make_unique<Impl>(std::move(program), std::move(options), std::move(host))) {}
ExecutionSession::~ExecutionSession() = default;
bool ExecutionSession::hotSwap(Program candidate, Diagnostic &diagnostic) {
    if (!impl_->boundary()) {
        diagnostic = {"JM7201: Hot swap requires an owner-thread execution boundary."};
        diagnostic.code = "JM7201";
        return false;
    }
    if (!impl_->interpreter->replace(impl_->program, std::move(candidate), diagnostic))
        return false;
    ++impl_->generation;
    return true;
}
std::uint64_t ExecutionSession::generation() const { return impl_->generation; }
std::map<std::string, std::string> ExecutionSession::inspect() const {
    if (!impl_->boundary())
        throw std::runtime_error("JM7201: Inspection requires an owner-thread execution boundary.");
    return impl_->interpreter->inspect();
}
ExecutionResult ExecutionSession::dispatch(const std::string &event) {
    auto guard = impl_->enter();
    return impl_->interpreter->select(event, {});
}
ExecutionResult ExecutionSession::evaluate(const std::string &source) {
    auto guard = impl_->enter();
    return impl_->interpreter->fragment(source);
}
ExecutionResult ExecutionSession::invoke(const std::string &entry) {
    auto guard = impl_->enter();
    return impl_->interpreter->select({}, entry);
}
ExecutionResult execute(const Program &program, const RunOptions &options, const HostFunction &hostFunction) {
    std::vector<Diagnostic> diagnostics;
    if (!check(program, diagnostics, options.nativeMetadata.get()))
        throw std::runtime_error(diagnostics.front().code + ": " + diagnostics.front().message);
    return Interpreter(program, options, hostFunction).run();
}

} // namespace jm::script
