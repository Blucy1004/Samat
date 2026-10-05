#include "JMEngine/Script/LanguageCore.hpp"
#include "JMEngine/Script/StandardLibrary.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <utility>
#include <unordered_set>

namespace jm::script {
namespace {

struct Token {
    enum class Kind { End, Newline, Indent, Dedent, Identifier, Number, String,
        LeftParen, RightParen, LeftBracket, RightBracket, LeftBrace, RightBrace,
        Comma, Colon, Dot, Range, Plus, Minus, Star, Slash, Percent, Power,
        Bang, Equal, EqualEqual, BangEqual, Less, LessEqual, Greater, GreaterEqual,
        PlusEqual, MinusEqual, StarEqual, SlashEqual, And, Or, Arrow };
    Kind kind{Kind::End};
    std::string text;
    double number{0.0};
    std::size_t line{1};
};

bool isIdentifierStart(unsigned char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_'; }
bool isIdentifierPart(unsigned char ch) { return isIdentifierStart(ch) || (ch >= '0' && ch <= '9'); }

std::vector<Token> lex(const std::string& source) {
    std::vector<Token> tokens;
    std::vector<std::size_t> indentation{0};
    std::istringstream input(source);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        try {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::size_t position = 0;
        std::size_t indent = 0;
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) {
            indent += line[position] == '\t' ? 4U : 1U;
            ++position;
        }
        if (position == line.size() || line[position] == '#') continue;
        if (indent > indentation.back()) { indentation.push_back(indent); tokens.push_back({Token::Kind::Indent, {}, 0.0, lineNumber}); }
        else if (indent < indentation.back()) {
            while (indent < indentation.back()) { indentation.pop_back(); tokens.push_back({Token::Kind::Dedent, {}, 0.0, lineNumber}); }
            if (indent != indentation.back()) throw std::runtime_error("Indentation must return to a previous block level.");
        }
        while (position < line.size()) {
            const unsigned char ch = static_cast<unsigned char>(line[position]);
            if (ch == ' ' || ch == '\t') { ++position; continue; }
            const std::size_t start = position;
            if (isIdentifierStart(ch) || ch >= 0x80) {
                ++position;
                while (position < line.size() && (isIdentifierPart(static_cast<unsigned char>(line[position])) || static_cast<unsigned char>(line[position]) >= 0x80)) ++position;
                const std::string word = line.substr(start, position - start);
                Token::Kind kind = word == "and" ? Token::Kind::And : word == "or" ? Token::Kind::Or : Token::Kind::Identifier;
                tokens.push_back({kind, word, 0.0, lineNumber});
                continue;
            }
            if ((ch >= '0' && ch <= '9') || (ch == '.' && position + 1 < line.size() && line[position + 1] >= '0' && line[position + 1] <= '9')) {
                const std::size_t range = line.find("..", position);
                if (range != std::string::npos && range > position && line.substr(position, range - position).find_first_of(" \t+-*/%()[],") == std::string::npos) {
                    const std::string integerPart = line.substr(position, range - position);
                    char* rangeEnd = nullptr;
                    const double rangeValue = std::strtod(integerPart.c_str(), &rangeEnd);
                    if (rangeEnd == integerPart.c_str() + integerPart.size() && std::isfinite(rangeValue)) {
                        tokens.push_back({Token::Kind::Number, integerPart, rangeValue, lineNumber});
                        position = range;
                        continue;
                    }
                }
                char* end = nullptr;
                const double number = std::strtod(line.c_str() + position, &end);
                if (end == line.c_str() + position || !std::isfinite(number)) throw std::runtime_error("Invalid number literal.");
                position = static_cast<std::size_t>(end - line.c_str());
                tokens.push_back({Token::Kind::Number, line.substr(start, position - start), number, lineNumber});
                continue;
            }
            if (ch == '"' || ch == '\'') {
                const char quote = static_cast<char>(ch);
                ++position;
                std::string value;
                bool closed = false;
                while (position < line.size()) {
                    const char current = line[position++];
                    if (current == quote) { closed = true; break; }
                    if (current == '\\' && position < line.size()) {
                        const char escaped = line[position++];
                        if (escaped == 'n') value.push_back('\n');
                        else if (escaped == 't') value.push_back('\t');
                        else if (escaped == 'r') value.push_back('\r');
                        else value.push_back(escaped);
                    } else value.push_back(current);
                }
                if (!closed) throw std::runtime_error("Unclosed string literal.");
                tokens.push_back({Token::Kind::String, std::move(value), 0.0, lineNumber});
                continue;
            }
            auto push = [&](Token::Kind kind, std::size_t length = 1) {
                tokens.push_back({kind, line.substr(position, length), 0.0, lineNumber});
                position += length;
            };
            const char next = position + 1 < line.size() ? line[position + 1] : '\0';
            if (ch == '-' && next == '>') push(Token::Kind::Arrow, 2);
            else if (ch == '.' && next == '.') push(Token::Kind::Range, 2);
            else if (ch == '+' && next == '=') push(Token::Kind::PlusEqual, 2);
            else if (ch == '-' && next == '=') push(Token::Kind::MinusEqual, 2);
            else if (ch == '*' && next == '=') push(Token::Kind::StarEqual, 2);
            else if (ch == '/' && next == '=') push(Token::Kind::SlashEqual, 2);
            else if (ch == '=' && next == '=') push(Token::Kind::EqualEqual, 2);
            else if (ch == '!' && next == '=') push(Token::Kind::BangEqual, 2);
            else if (ch == '<' && next == '=') push(Token::Kind::LessEqual, 2);
            else if (ch == '>' && next == '=') push(Token::Kind::GreaterEqual, 2);
            else if (ch == '&' && next == '&') push(Token::Kind::And, 2);
            else if (ch == '|' && next == '|') push(Token::Kind::Or, 2);
            else switch (ch) {
                case '(': push(Token::Kind::LeftParen); break; case ')': push(Token::Kind::RightParen); break;
                case '[': push(Token::Kind::LeftBracket); break; case ']': push(Token::Kind::RightBracket); break;
                case '{': push(Token::Kind::LeftBrace); break; case '}': push(Token::Kind::RightBrace); break;
                case ',': push(Token::Kind::Comma); break; case ':': push(Token::Kind::Colon); break;
                case '.': push(Token::Kind::Dot); break; case '+': push(Token::Kind::Plus); break;
                case '-': push(Token::Kind::Minus); break; case '*': push(Token::Kind::Star); break;
                case '/': push(Token::Kind::Slash); break; case '%': push(Token::Kind::Percent); break;
                case '^': push(Token::Kind::Power); break; case '!': push(Token::Kind::Bang); break;
                case '=': push(Token::Kind::Equal); break; case '<': push(Token::Kind::Less); break;
                case '>': push(Token::Kind::Greater); break;
                default: throw std::runtime_error("Unexpected character in code expression.");
            }
        }
        tokens.push_back({Token::Kind::Newline, {}, 0.0, lineNumber});
        }catch(const std::exception& error) { throw std::runtime_error("line "+std::to_string(lineNumber)+": "+error.what()); }
    }
    while (indentation.size() > 1) { indentation.pop_back(); tokens.push_back({Token::Kind::Dedent, {}, 0.0, lineNumber + 1}); }
    tokens.push_back({Token::Kind::End, {}, 0.0, lineNumber + 1});
    return tokens;
}

ExpressionPtr node(Expression::Kind kind, std::string text = {}) {
    auto result = std::make_shared<Expression>(); result->kind = kind; result->text = std::move(text); return result;
}

class ExpressionParser {
public:
    explicit ExpressionParser(const std::vector<Token>& tokens, std::size_t& position) : tokens_(tokens), position_(position) {}
    ExpressionPtr parse() { return parseOr(); }
private:
    const Token& peek() const { return tokens_.at(position_); }
    bool take(Token::Kind kind) { if (peek().kind != kind) return false; ++position_; return true; }
    Token expect(Token::Kind kind, const char* message) { if (!take(kind)) throw std::runtime_error(message); return tokens_.at(position_ - 1); }
    ExpressionPtr binary(ExpressionPtr left, const Token& op, ExpressionPtr right) {
        auto result = node(Expression::Kind::Binary, op.text); result->left = std::move(left); result->right = std::move(right); return result;
    }
    ExpressionPtr parseOr() { auto value = parseAnd(); while (peek().kind == Token::Kind::Or) { Token op = tokens_[position_++]; value = binary(value, op, parseAnd()); } return value; }
    ExpressionPtr parseAnd() { auto value = parseEquality(); while (peek().kind == Token::Kind::And) { Token op = tokens_[position_++]; value = binary(value, op, parseEquality()); } return value; }
    ExpressionPtr parseEquality() {
        auto value = parseComparison();
        while (peek().kind == Token::Kind::EqualEqual || peek().kind == Token::Kind::BangEqual) { Token op = tokens_[position_++]; value = binary(value, op, parseComparison()); }
        return value;
    }
    ExpressionPtr parseComparison() {
        auto value = parseAdd();
        while (peek().kind == Token::Kind::Less || peek().kind == Token::Kind::LessEqual || peek().kind == Token::Kind::Greater || peek().kind == Token::Kind::GreaterEqual) {
            Token op = tokens_[position_++]; value = binary(value, op, parseAdd());
        }
        return value;
    }
    ExpressionPtr parseAdd() {
        auto value = parseMultiply();
        while (peek().kind == Token::Kind::Plus || peek().kind == Token::Kind::Minus) { Token op = tokens_[position_++]; value = binary(value, op, parseMultiply()); }
        return value;
    }
    ExpressionPtr parseMultiply() {
        auto value = parsePower();
        while (peek().kind == Token::Kind::Star || peek().kind == Token::Kind::Slash || peek().kind == Token::Kind::Percent) { Token op = tokens_[position_++]; value = binary(value, op, parsePower()); }
        return value;
    }
    ExpressionPtr parsePower() {
        auto value = parseUnary();
        if (peek().kind == Token::Kind::Power) { Token op = tokens_[position_++]; value = binary(value, op, parsePower()); }
        return value;
    }
    ExpressionPtr parseUnary() {
        if(peek().kind==Token::Kind::Minus && tokens_.at(position_+1).kind==Token::Kind::Number && tokens_.at(position_+1).text=="9223372036854775808") {
            position_+=2;auto value=node(Expression::Kind::Literal);value->literal=Value(std::numeric_limits<std::int64_t>::min());return value;
        }
        if (peek().kind == Token::Kind::Bang || peek().kind == Token::Kind::Minus || peek().kind == Token::Kind::Plus) {
            Token op = tokens_[position_++]; auto value = node(Expression::Kind::Unary, op.text); value->right = parseUnary(); return value;
        }
        if (peek().kind == Token::Kind::Identifier && peek().text == "not") { Token op = tokens_[position_++]; auto value = node(Expression::Kind::Unary, op.text); value->right = parseUnary(); return value; }
        return parsePostfix();
    }
    ExpressionPtr parsePostfix() {
        auto value = parsePrimary();
        for (;;) {
            if (take(Token::Kind::LeftParen)) {
                auto call = node(Expression::Kind::Call); call->left = value;
                if (value->kind == Expression::Kind::Member && value->left && value->left->kind == Expression::Kind::Identifier) {
                    call->builtinSymbolName = "builtin." + value->left->text + "." + value->text;
                    call->builtinSymbolId = stableBuiltinSymbolId(call->builtinSymbolName);
                }
                if (!take(Token::Kind::RightParen)) {
                    do {
                        Expression::NamedArgument argument;
                        if (peek().kind == Token::Kind::Identifier && tokens_.at(position_ + 1).kind == Token::Kind::Colon) {
                            argument.name = tokens_[position_++].text; ++position_;
                        }
                        argument.value = parseOr(); call->arguments.push_back(std::move(argument));
                    } while (take(Token::Kind::Comma));
                    expect(Token::Kind::RightParen, "Expected ')' after function arguments.");
                }
                value = std::move(call);
            } else if (take(Token::Kind::LeftBracket)) {
                auto access = node(Expression::Kind::Index); access->left = value; access->right = parseOr();
                expect(Token::Kind::RightBracket, "Expected ']' after index."); value = std::move(access);
            } else if (take(Token::Kind::Dot)) {
                Token member = expect(Token::Kind::Identifier, "Expected a property name after '.'.");
                auto access = node(Expression::Kind::Member, member.text);access->left=value;
                if(value->kind==Expression::Kind::Identifier) { access->builtinSymbolName="builtin."+value->text+"."+member.text;access->builtinSymbolId=stableBuiltinSymbolId(access->builtinSymbolName); }
                value=std::move(access);
            } else break;
        }
        return value;
    }
    ExpressionPtr parsePrimary() {
        if (take(Token::Kind::Number)) {
            auto value = node(Expression::Kind::Literal); const auto& token = tokens_[position_ - 1];
            if (token.text.find_first_of(".eE") == std::string::npos) {
                std::int64_t integer{}; auto parsed = std::from_chars(token.text.data(), token.text.data()+token.text.size(), integer);
                if (parsed.ec != std::errc{} || parsed.ptr != token.text.data()+token.text.size()) throw std::runtime_error("Integer literal is outside i64 range.");
                value->literal = Value(integer);
            } else value->literal = Value(token.number);
            return value;
        }
        if (take(Token::Kind::String)) { auto value = node(Expression::Kind::Literal); value->literal = Value(tokens_[position_ - 1].text); return value; }
        if (peek().kind == Token::Kind::Identifier) {
            const std::string name = tokens_[position_++].text;
            if (name == "true" || name == "false" || name == "null" || name == "참" || name == "거짓") {
                auto value = node(Expression::Kind::Literal);
                if (name == "true" || name == "참") value->literal = Value(true); else if (name == "false" || name == "거짓") value->literal = Value(false);
                return value;
            }
            auto value = node(Expression::Kind::Identifier, name);
            return value;
        }
        if (take(Token::Kind::LeftParen)) { auto value = parseOr(); expect(Token::Kind::RightParen, "Expected ')' after expression."); return value; }
        if (take(Token::Kind::LeftBracket)) {
            auto value = node(Expression::Kind::Array);
            if (!take(Token::Kind::RightBracket)) { do { value->elements.push_back(parseOr()); } while (take(Token::Kind::Comma)); expect(Token::Kind::RightBracket, "Expected ']' after list."); }
            return value;
        }
        if (take(Token::Kind::LeftBrace)) {
            auto value = node(Expression::Kind::Map);
            if (!take(Token::Kind::RightBrace)) {
                do {
                    std::string key;
                    if (peek().kind == Token::Kind::String || peek().kind == Token::Kind::Identifier) key = tokens_[position_++].text;
                    else throw std::runtime_error("Map keys must be strings or names.");
                    expect(Token::Kind::Colon, "Expected ':' after map key.");
                    value->entries.emplace_back(std::move(key), parseOr());
                } while (take(Token::Kind::Comma));
                expect(Token::Kind::RightBrace, "Expected '}' after map.");
            }
            return value;
        }
        throw std::runtime_error("Expected a value or expression.");
    }
    const std::vector<Token>& tokens_;
    std::size_t& position_;
};

double number(const Value& value) {
    if (const auto* result = std::get_if<double>(&value.data)) return *result;
    if (const auto* result = std::get_if<std::int64_t>(&value.data)) return static_cast<double>(*result);
    throw std::runtime_error("Expected Number but got " + value.typeName() + ".");
}
std::int64_t checkedIndex(const Value& value) {
    if (auto integer=std::get_if<std::int64_t>(&value.data)) return *integer;
    throw std::runtime_error("JM3003: Index must have type Int.");
}
bool truth(const Value& value) {
    if (value.isNull()) return false;
    if (const auto* result = std::get_if<std::int64_t>(&value.data)) return *result != 0;
    if (const auto* result = std::get_if<bool>(&value.data)) return *result;
    if (const auto* result = std::get_if<double>(&value.data)) return *result != 0.0;
    if (const auto* result = std::get_if<std::string>(&value.data)) return !result->empty();
    return true;
}
bool equal(const Value& left, const Value& right) {
    if ((left.type()==Type::Int && right.type()==Type::Float) || (left.type()==Type::Float && right.type()==Type::Int)) return number(left)==number(right);
    if (left.data.index() != right.data.index()) return false;
    if (auto a = std::get_if<std::int64_t>(&left.data)) return *a == std::get<std::int64_t>(right.data);
    if (left.isNull()) return true;
    if (auto a = std::get_if<bool>(&left.data)) return *a == std::get<bool>(right.data);
    if (auto a = std::get_if<double>(&left.data)) return *a == std::get<double>(right.data);
    if (auto a = std::get_if<std::string>(&left.data)) return *a == std::get<std::string>(right.data);
    if (auto a = std::get_if<Value::ArrayPtr>(&left.data)) {
        const auto& b = *std::get<Value::ArrayPtr>(right.data);
        if ((*a)->size() != b.size()) return false;
        for (std::size_t i = 0; i < b.size(); ++i) if (!equal((**a)[i], b[i])) return false;
        return true;
    }
    if (auto a = std::get_if<Value::MapPtr>(&left.data)) {
        const auto& b = *std::get<Value::MapPtr>(right.data);
        if ((*a)->size() != b.size()) return false;
        for (const auto& [key, value] : **a) { const auto found = b.find(key); if (found == b.end() || !equal(value, found->second)) return false; }
        return true;
    }
    if (auto a = std::get_if<Vector2Value>(&left.data)) { auto b = std::get<Vector2Value>(right.data); return a->x == b.x && a->y == b.y; }
    if (auto a = std::get_if<ColorValue>(&left.data)) { auto b = std::get<ColorValue>(right.data); return a->r == b.r && a->g == b.g && a->b == b.b && a->a == b.a; }
    return std::get<EntityReference>(left.data).id == std::get<EntityReference>(right.data).id;
}

void rejectCycle(const Value& candidate,const void* target) {
    std::unordered_set<const void*> visited;
    std::function<bool(const Value&)> contains=[&](const Value& value) {
        if(auto array=std::get_if<Value::ArrayPtr>(&value.data)) {
            if(array->get()==target)return true;if(!visited.insert(array->get()).second)return false;
            for(const auto& item:**array)if(contains(item))return true;
        }
        if(auto map=std::get_if<Value::MapPtr>(&value.data)) {
            if(map->get()==target)return true;if(!visited.insert(map->get()).second)return false;
            for(const auto& item:**map)if(contains(item.second))return true;
        }
        return false;
    };
    if(contains(candidate))throw std::runtime_error("JM3010: Cyclic collection ownership is not supported.");
}
Value evaluate(const ExpressionPtr& expression, const Environment& environment, const HostFunction& host);

Value invokeBuiltin(const std::string& name, const std::vector<Value>& arguments,
                    const std::vector<std::string>& names, const HostFunction& host) {
    if (host) {
        try { return host(name, arguments, names); }
        catch (const std::out_of_range&) { /* try built-ins */ }
    }
    if(name.rfind("builtin.math.",0)==0)return invokeBuiltin(name.substr(std::string("builtin.math.").size()),arguments,names,{});
    const std::unordered_map<std::string,std::pair<std::size_t,std::size_t>> arities{
        {"abs",{1,1}},{"sqrt",{1,1}},{"pow",{2,2}},{"clamp",{3,3}},{"sin",{1,1}},{"cos",{1,1}},{"tan",{1,1}},
        {"round",{1,1}},{"floor",{1,1}},{"ceil",{1,1}},{"len",{1,1}},{"length",{1,1}},{"assert",{1,1}},{"vector2",{2,2}},{"color",{3,4}}};
    if(auto expected=arities.find(name);expected!=arities.end() && (arguments.size()<expected->second.first || arguments.size()>expected->second.second))throw std::runtime_error("JM2003: Function '"+name+"' argument count mismatch.");
    auto arg = [&](std::size_t index) -> const Value& { if (index >= arguments.size()) throw std::runtime_error(name + " expects more arguments."); return arguments[index]; };
    if (name == "print" || name == "println") return Value{};
    if (name == "clamp") { const auto low=number(arg(1)),high=number(arg(2));if(low>high)throw std::runtime_error("clamp minimum exceeds maximum.");return Value(std::clamp(number(arg(0)),low,high)); }
    if (name == "assert") { if (!truth(arg(0))) throw std::runtime_error("Assertion failed."); return Value(true); }
    if (name == "len" || name == "length") {
        if (auto array = std::get_if<Value::ArrayPtr>(&arg(0).data)) return Value(static_cast<std::int64_t>((*array)->size()));
        if (auto map = std::get_if<Value::MapPtr>(&arg(0).data)) return Value(static_cast<std::int64_t>((*map)->size()));
        if (auto text = std::get_if<std::string>(&arg(0).data)) return Value(static_cast<std::int64_t>(text->size()));
        throw std::runtime_error("len expects a list, map, or string.");
    }
    if (name == "abs") {
        if (auto integer=std::get_if<std::int64_t>(&arg(0).data)) {
            if(*integer==std::numeric_limits<std::int64_t>::min()) throw std::runtime_error("abs overflow.");
            return Value(*integer<0 ? -*integer : *integer);
        }
        return Value(std::abs(number(arg(0))));
    }
    if (name == "min" || name == "max") {
        if (arguments.empty()) throw std::runtime_error(name + " expects at least one argument.");
        bool integers=true;for(const auto& value:arguments)integers&=value.type()==Type::Int;
        if(integers) { auto result=std::get<std::int64_t>(arguments.front().data);for(std::size_t i=1;i<arguments.size();++i)result=name=="min"?std::min(result,std::get<std::int64_t>(arguments[i].data)):std::max(result,std::get<std::int64_t>(arguments[i].data));return Value(result); }
        double result = number(arguments.front());
        for (std::size_t i = 1; i < arguments.size(); ++i) result = name == "min" ? std::min(result, number(arguments[i])) : std::max(result, number(arguments[i]));
        return Value(result);
    }
    if (name == "round") return Value(std::round(number(arg(0))));
    if (name == "floor") return Value(std::floor(number(arg(0))));
    if (name == "ceil") return Value(std::ceil(number(arg(0))));
    if (name == "sqrt") return Value(std::sqrt(number(arg(0))));
    if (name == "pow") return Value(std::pow(number(arg(0)), number(arg(1))));
    if (name == "sin") return Value(std::sin(number(arg(0))));
    if (name == "cos") return Value(std::cos(number(arg(0))));
    if (name == "tan") return Value(std::tan(number(arg(0))));
    if (name == "vector2") return Value(Vector2Value{number(arg(0)), number(arg(1))});
    if (name == "color") return Value(ColorValue{number(arg(0)), number(arg(1)), number(arg(2)), arguments.size() > 3 ? number(arguments[3]) : 1.0});
    throw std::runtime_error("Unknown function: " + name);
}

Value evaluate(const ExpressionPtr& expression, const Environment& environment, const HostFunction& host) {
    if (!expression) return Value{};
    switch (expression->kind) {
    case Expression::Kind::Literal: return expression->literal;
    case Expression::Kind::Identifier: return environment.get(expression->text);
    case Expression::Kind::Array: { Value::Array result; for (const auto& item : expression->elements) result.push_back(evaluate(item, environment, host)); return Value::array(std::move(result)); }
    case Expression::Kind::Map: { Value::Map result; for (const auto& [key, item] : expression->entries) result.emplace(key, evaluate(item, environment, host)); return Value::map(std::move(result)); }
    case Expression::Kind::Unary: {
        const Value value = evaluate(expression->right, environment, host);
        if (auto integer = std::get_if<std::int64_t>(&value.data)) {
            if (expression->text == "-") return Value(std::bit_cast<std::int64_t>(std::uint64_t{0}-static_cast<std::uint64_t>(*integer)));
            if (expression->text == "+") return value;
        }
        if (expression->text == "!" || expression->text == "not") return Value(!truth(value));
        if (expression->text == "-") return Value(-number(value));
        if (expression->text == "+") return Value(number(value));
        throw std::runtime_error("Unknown unary operator.");
    }
    case Expression::Kind::Binary: {
        const std::string& op = expression->text;
        const Value left = evaluate(expression->left, environment, host);
        if (op == "and" || op == "&&") return truth(left) ? Value(truth(evaluate(expression->right, environment, host))) : Value(false);
        if (op == "or" || op == "||") return truth(left) ? Value(true) : Value(truth(evaluate(expression->right, environment, host)));
        const Value right = evaluate(expression->right, environment, host);
        if (op == "==") return Value(equal(left, right));
        if (op == "!=") return Value(!equal(left, right));
        if (left.type()==Type::Int && right.type()==Type::Int) {
            const auto a=std::get<std::int64_t>(left.data), b=std::get<std::int64_t>(right.data);
            const auto ua=static_cast<std::uint64_t>(a), ub=static_cast<std::uint64_t>(b);
            if(op=="+") return Value(std::bit_cast<std::int64_t>(ua+ub));
            if(op=="-") return Value(std::bit_cast<std::int64_t>(ua-ub));
            if(op=="*") return Value(std::bit_cast<std::int64_t>(ua*ub));
            if(op=="/" || op=="%") {
                if(b==0) throw std::runtime_error("JM3001: Cannot divide by zero.");
                if(a==std::numeric_limits<std::int64_t>::min() && b==-1) throw std::runtime_error("JM3002: Integer division overflow.");
                return Value(op=="/" ? a/b : a%b);
            }
            if(op=="<")return Value(a<b); if(op=="<=")return Value(a<=b); if(op==">")return Value(a>b); if(op==">=")return Value(a>=b);
        }
        if (op == "+") {
            if (auto a = std::get_if<std::string>(&left.data)) {
                auto b=std::get_if<std::string>(&right.data); if(!b) throw std::runtime_error("String concatenation requires String operands."); return Value(*a+*b);
            }
            if (auto a = std::get_if<Value::ArrayPtr>(&left.data)) { auto result = **a; if (auto b = std::get_if<Value::ArrayPtr>(&right.data)) result.insert(result.end(), (*b)->begin(), (*b)->end()); else result.push_back(right); return Value::array(std::move(result)); }
            return Value(number(left) + number(right));
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (auto a = std::get_if<std::string>(&left.data)) {
                const auto* b = std::get_if<std::string>(&right.data); if (!b) throw std::runtime_error("String comparison needs another string.");
                if (op == "<") return Value(*a < *b); if (op == "<=") return Value(*a <= *b); if (op == ">") return Value(*a > *b); return Value(*a >= *b);
            }
            const double a = number(left), b = number(right);
            if (op == "<") return Value(a < b); if (op == "<=") return Value(a <= b); if (op == ">") return Value(a > b); return Value(a >= b);
        }
        const double a = number(left), b = number(right);
        if (op == "-") return Value(a - b); if (op == "*") return Value(a * b);
        if (op == "/") { if (b == 0.0) throw std::runtime_error("Cannot divide by zero."); return Value(a / b); }
        if (op == "%") { if (b == 0.0) throw std::runtime_error("Cannot divide by zero."); return Value(std::fmod(a, b)); }
        if (op == "^" || op == "**") return Value(std::pow(a, b));
        throw std::runtime_error("Unknown binary operator: " + op);
    }
    case Expression::Kind::Index: {
        const Value object = evaluate(expression->left, environment, host);
        const Value index = evaluate(expression->right, environment, host);
        if (auto list = std::get_if<Value::ArrayPtr>(&object.data)) {
            const auto at = checkedIndex(index); if (at < 0 || static_cast<std::size_t>(at) >= (*list)->size()) throw std::runtime_error("List index is outside the list."); return (**list)[static_cast<std::size_t>(at)];
        }
        if (auto map = std::get_if<Value::MapPtr>(&object.data)) { const auto found = (*map)->find(index.toString()); if (found == (*map)->end()) return Value{}; return found->second; }
        if (auto text = std::get_if<std::string>(&object.data)) { const auto at = checkedIndex(index); if (at < 0 || static_cast<std::size_t>(at) >= text->size()) throw std::runtime_error("String index is outside the string."); return Value(std::string(1, (*text)[static_cast<std::size_t>(at)])); }
        throw std::runtime_error("Only a list, map, or string can be indexed.");
    }
    case Expression::Kind::Member: {
        if(host && expression->builtinSymbolId) { try { return host(expression->builtinSymbolName,{},{}); }catch(const std::out_of_range&) {} }
        const Value object = evaluate(expression->left, environment, host);
        if (auto list = std::get_if<Value::ArrayPtr>(&object.data); list && expression->text == "length") return Value(static_cast<std::int64_t>((*list)->size()));
        if (auto map = std::get_if<Value::MapPtr>(&object.data)) { const auto found = (*map)->find(expression->text); return found == (*map)->end() ? Value{} : found->second; }
        if (auto text = std::get_if<std::string>(&object.data); text && expression->text == "length") return Value(static_cast<std::int64_t>(text->size()));
        if (auto vector = std::get_if<Vector2Value>(&object.data)) { if (expression->text == "x") return Value(vector->x); if (expression->text == "y") return Value(vector->y); }
        throw std::runtime_error("Unknown property '" + expression->text + "' on " + object.typeName() + ".");
    }
    case Expression::Kind::Call: {
        std::vector<Value> args; std::vector<std::string> names;
        for (const auto& argument : expression->arguments) { names.push_back(argument.name); args.push_back(evaluate(argument.value, environment, host)); }
        if (expression->left->kind == Expression::Kind::Identifier) return invokeBuiltin(expression->left->text, args, names, host);
        if (expression->left->kind == Expression::Kind::Member) {
            if(expression->builtinSymbolName.rfind("builtin.math.",0)==0)return invokeBuiltin(expression->builtinSymbolName,args,names,{});
            if(host && !expression->builtinSymbolName.empty()) { try { return host(expression->builtinSymbolName,args,names); }catch(const std::out_of_range&) {} }
            const Value receiver = evaluate(expression->left->left, environment, host);
            if (auto list = std::get_if<Value::ArrayPtr>(&receiver.data)) {
                const std::string& method = expression->left->text;
                if (method == "append" || method == "push") { if (args.size() != 1) throw std::runtime_error(method + " expects one value."); rejectCycle(args[0],list->get()); (*list)->push_back(args[0]); return Value{}; }
                if (method == "pop") { if(!args.empty())throw std::runtime_error("pop expects no arguments."); if ((*list)->empty()) throw std::runtime_error("JM3004: Cannot pop an empty list."); Value last = list->get()->back(); list->get()->pop_back(); return last; }
                if (method == "clear") { list->get()->clear(); return Value{}; }
            }
        }
        throw std::runtime_error("This expression call requires a runtime function binding.");
    }
    }
    throw std::runtime_error("Invalid expression node.");
}

struct SourceLine { std::size_t indent; std::string text; std::size_t number; };
std::vector<SourceLine> sourceLines(const std::string& source) {
    std::vector<SourceLine> result; std::istringstream stream(source); std::string line; std::size_t number = 0;
    while (std::getline(stream, line)) {
        ++number; if (!line.empty() && line.back() == '\r') line.pop_back();
        std::size_t start = 0, indent = 0; while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) { indent += line[start] == '\t' ? 4U : 1U; ++start; }
        std::string content = line.substr(start);
        if (content.empty() || content[0] == '#') continue;
        result.push_back({indent, std::move(content), number});
    }
    return result;
}

class CodeParser {
public:
    explicit CodeParser(const std::string& source) : tokens_(lex(source)) {}
    Program parse() {
        try { Program result; result.statements = block(false); expect(Token::Kind::End, "Unexpected end of code."); return result; }
        catch(const std::exception& error) { throw std::runtime_error("line "+std::to_string(peek().line)+": "+error.what()); }
    }
private:
    const Token& peek() const { return tokens_.at(position_); }
    bool take(Token::Kind kind) { if (peek().kind != kind) return false; ++position_; return true; }
    Token expect(Token::Kind kind, const char* message) { if (!take(kind)) throw std::runtime_error(message); return tokens_.at(position_ - 1); }
    bool keyword(const char* word) const { return peek().kind == Token::Kind::Identifier && peek().text == word; }
    void lineEnd() { if (!take(Token::Kind::Newline) && peek().kind != Token::Kind::Dedent && peek().kind != Token::Kind::End) throw std::runtime_error("Expected the end of a line."); }
    ExpressionPtr expression() { ExpressionParser parser(tokens_, position_); return parser.parse(); }
    StatementList block(bool indented) {
        StatementList result;
        if (indented) expect(Token::Kind::Indent, "Expected an indented block.");
        while (peek().kind != Token::Kind::End && (!indented || peek().kind != Token::Kind::Dedent)) {
            if (take(Token::Kind::Newline)) continue;
            const auto sourceLine=peek().line; auto item=statement(); item.line=sourceLine; result.push_back(std::move(item));
        }
        if (indented) expect(Token::Kind::Dedent, "Expected the end of the block.");
        return result;
    }
    Statement statement() {
        if(keyword("on")) {
            ++position_;Statement result;result.kind=Statement::Kind::Event;
            result.name=expect(Token::Kind::Identifier,"Expected a stable event name.").text;
            while(take(Token::Kind::Dot))result.name+="."+expect(Token::Kind::Identifier,"Expected event component.").text;
            expect(Token::Kind::Colon,"Expected ':' after event.");lineEnd();result.body=block(true);return result;
        }
        if(keyword("import")) {
            ++position_; Statement result; result.kind=Statement::Kind::Import;
            result.name=expect(Token::Kind::Identifier,"Expected a module name.").text;
            while(take(Token::Kind::Dot)) result.name+="."+expect(Token::Kind::Identifier,"Expected module component.").text;
            lineEnd(); return result;
        }
        if (keyword("let") || keyword("const")) {
            Statement result; result.kind = Statement::Kind::Variable; result.constant = keyword("const"); ++position_;
            Token name = expect(Token::Kind::Identifier, "Expected a variable name."); result.name = name.text;
            if (take(Token::Kind::Colon)) result.declaredType=parseType(expect(Token::Kind::Identifier, "Expected a type name.").text);
            if (take(Token::Kind::Equal)) result.expression = expression();
            else if (result.constant) throw std::runtime_error("A constant needs an initial value.");
            lineEnd(); return result;
        }
        if (keyword("fn")) {
            ++position_; Statement result; result.kind = Statement::Kind::Function;
            result.name = expect(Token::Kind::Identifier, "Expected a function name.").text;
            expect(Token::Kind::LeftParen, "Expected '(' after function name.");
            if (!take(Token::Kind::RightParen)) { do { result.parameters.push_back(expect(Token::Kind::Identifier, "Expected a parameter name.").text); result.parameterTypes.push_back(take(Token::Kind::Colon) ? parseType(expect(Token::Kind::Identifier, "Expected a parameter type.").text) : Type::Any); } while (take(Token::Kind::Comma)); expect(Token::Kind::RightParen, "Expected ')' after parameters."); }
            if (take(Token::Kind::Arrow)) result.returnType=parseType(expect(Token::Kind::Identifier, "Expected a return type.").text);
            expect(Token::Kind::Colon, "Expected ':' after function header."); lineEnd(); result.body = block(true); return result;
        }
        if (keyword("if")) {
            ++position_; Statement result; result.kind = Statement::Kind::If; result.expression = expression();
            expect(Token::Kind::Colon, "Expected ':' after condition."); lineEnd(); result.body = block(true);
            if (keyword("else")) { ++position_; if(keyword("if")) result.alternative.push_back(statement()); else { expect(Token::Kind::Colon, "Expected ':' after else."); lineEnd(); result.alternative = block(true); } }
            return result;
        }
        if (keyword("while")) {
            ++position_; Statement result; result.kind = Statement::Kind::While; result.expression = expression();
            expect(Token::Kind::Colon, "Expected ':' after while condition."); lineEnd(); result.body = block(true); return result;
        }
        if (keyword("for")) {
            ++position_; Statement result; result.kind = Statement::Kind::ForRange;
            result.name = expect(Token::Kind::Identifier, "Expected a loop variable.").text;
            if (!keyword("in")) throw std::runtime_error("Expected 'in' after loop variable."); ++position_;
            result.expression = expression();
            if (take(Token::Kind::Range)) { result.kind = Statement::Kind::ForRange; result.rangeEnd = expression(); }
            else result.kind = Statement::Kind::ForEach;
            expect(Token::Kind::Colon, "Expected ':' after for expression."); lineEnd(); result.body = block(true); return result;
        }
        if (keyword("return")) { ++position_; Statement result; result.kind = Statement::Kind::Return; if (peek().kind != Token::Kind::Newline) result.expression = expression(); lineEnd(); return result; }
        if (keyword("break") || keyword("continue")) { Statement result; result.kind = keyword("break") ? Statement::Kind::Break : Statement::Kind::Continue; ++position_; lineEnd(); return result; }
        const std::size_t saved = position_;
        ExpressionPtr left = expression();
        Statement result;
        if (peek().kind == Token::Kind::Equal || peek().kind == Token::Kind::PlusEqual || peek().kind == Token::Kind::MinusEqual ||
            peek().kind == Token::Kind::StarEqual || peek().kind == Token::Kind::SlashEqual) {
            Token op = tokens_[position_++]; result.kind = Statement::Kind::Assignment; result.target = std::move(left); result.operation = op.text; result.expression = expression();
        } else { position_ = saved; result.kind = Statement::Kind::Expression; result.expression = expression(); }
        lineEnd(); return result;
    }
    std::vector<Token> tokens_;
    std::size_t position_{0};
};

std::string trimLanguageText(std::string text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

// Korean Syntax constructs the shared JM AST directly. Expressions keep their common
// operator grammar and are parsed as expression nodes; no translated source program is made.
class KoreanAstParser {
public:
    explicit KoreanAstParser(const std::string& source) : lines_(sourceLines(source)) {}
    Program parse() { Program result; result.statements = block(0); if (position_ != lines_.size()) fail("Unexpected indentation."); return result; }
private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("line " + std::to_string(position_ < lines_.size() ? lines_[position_].number : 0) + ": " + message);
    }
    ExpressionPtr expr(const std::string& source) {
        std::string error; auto parsed = parseExpression(trimLanguageText(source), &error);
        if (!parsed) fail("식 표현을 확인해 주세요: " + error);
        return parsed;
    }
    ExpressionPtr binary(ExpressionPtr left, std::string op, ExpressionPtr right) {
        auto result=std::make_shared<Expression>(); result->kind=Expression::Kind::Binary; result->text=std::move(op);
        result->left=std::move(left); result->right=std::move(right); return result;
    }
    ExpressionPtr condition(std::string text) {
        text=trimLanguageText(text);
        for (const std::string conjunction : {"그리고","또는"}) {
            const auto split=text.find(" "+conjunction+" ");
            if(split!=std::string::npos) return binary(condition(text.substr(0,split)), conjunction=="그리고"?"and":"or", condition(text.substr(split+conjunction.size()+2)));
        }
        if(text=="참" || text=="거짓") { auto value=std::make_shared<Expression>(); value->kind=Expression::Kind::Literal; value->literal=Value(text=="참"); return value; }
        static const std::regex ordered(R"(^\s*(.+?)(?:이|가)\s+(.+?)보다\s*(작거나\s*같|작|크거나\s*같|크)(?:다|은|으면|다면)?\s*$)");
        std::smatch match;
        if(std::regex_match(text,match,ordered)) {
            std::string op=match[3].str(); op.erase(std::remove_if(op.begin(),op.end(),[](unsigned char c){return std::isspace(c);}),op.end());
            if(op=="작거나같")op="<=";else if(op=="작")op="<";else if(op=="크거나같")op=">=";else op=">";
            return binary(expr(match[1].str()),op,expr(match[2].str()));
        }
        static const std::regex equality(R"(^\s*(.+?)(?:이|가)\s+(.+?)(?:와|과)\s*(같|다르)(?:다|다면|으면)?\s*$)");
        if(std::regex_match(text,match,equality)) return binary(expr(match[1].str()),match[3].str()=="같"?"==":"!=",expr(match[2].str()));
        return expr(text);
    }
    StatementList block(std::size_t indent) {
        StatementList result;
        while(position_<lines_.size() && lines_[position_].indent==indent) {
            std::string text=lines_[position_].text; const auto line=lines_[position_++];
            if(text=="아니라면:" || text=="그렇지 않으면:") { --position_; break; }
            Statement statement; statement.line=line.number;
            const std::unordered_map<std::string,std::string> events{{"시작할 때:","start"},{"오른쪽 키를 누르는 동안:","key.right.held"},{"왼쪽 키를 누르는 동안:","key.left.held"},{"스페이스 키를 눌렀을 때:","key.space.pressed"}};
            if(auto event=events.find(text);event!=events.end()) { statement.kind=Statement::Kind::Event;statement.name=event->second;statement.body=nestedBlock(indent,line.number);result.push_back(statement);continue; }
            if(text.rfind("이벤트 ",0)==0 && text.back()==':') { statement.kind=Statement::Kind::Event;statement.name=text.substr(std::string("이벤트 ").size());statement.name.pop_back();statement.body=nestedBlock(indent,line.number);result.push_back(statement);continue; }
            std::smatch match;
            static const std::regex function(R"(^함수\s+([^\s(]+)\s*\(([^)]*)\)\s*(?:->\s*([^\s:]+))?\s*:$)");
            static const std::regex variable(R"(^(숫자|정수|실수|문자열|논리|목록|지도|자동|Vector2)\s+(변수|상수)\s+(.+?)\s*(?:을|를)\s+(.+?)\s*(?:으로|로)\s*정한다\.?$)");
            if(std::regex_match(text,match,function)) {
                statement.kind=Statement::Kind::Function; statement.name=match[1].str();
                std::string params=trimLanguageText(match[2].str());
                if(!params.empty()) { std::istringstream stream(params); std::string item; while(std::getline(stream,item,',')) { item=trimLanguageText(item); if(item.empty())fail("함수 매개변수 구문을 확인해 주세요."); const auto colon=item.find(':'); statement.parameters.push_back(trimLanguageText(item.substr(0,colon))); statement.parameterTypes.push_back(colon==std::string::npos ? Type::Any : parseType(trimLanguageText(item.substr(colon+1)))); } }
                if(match[3].matched) statement.returnType=parseType(match[3].str());
                statement.body=nestedBlock(indent,line.number); result.push_back(std::move(statement)); continue;
            }
            static const std::regex declaration(R"(^(숫자|정수|실수|문자열|논리|목록|지도|Vector2) 변수 (.+?)(?:을|를) 선언한다\.$)");
            if(std::regex_match(text,match,declaration)) { statement.kind=Statement::Kind::Variable;statement.name=match[2].str();statement.declaredType=parseType(match[1].str());result.push_back(statement);continue; }
            if(std::regex_match(text,match,variable)) {
                statement.kind=Statement::Kind::Variable; statement.constant=match[2].str()=="상수"; statement.name=match[3].str(); statement.declaredType=parseType(match[1].str()); statement.expression=expr(match[4].str()); result.push_back(std::move(statement)); continue;
            }
            if(text=="반복을 멈춘다." || text=="다음 반복을 진행한다.") { statement.kind=text=="반복을 멈춘다."?Statement::Kind::Break:Statement::Kind::Continue; result.push_back(statement); continue; }
            if(text.rfind("가져온다 ",0)==0) { statement.kind=Statement::Kind::Import; statement.name=trimLanguageText(text.substr(std::string("가져온다 ").size())); result.push_back(statement); continue; }
            static const std::regex rangeLoop(R"(^(.+?)부터 (.+?)까지 (.+?)(?:을|를) 반복하며:$)");
            if(std::regex_match(text,match,rangeLoop)) { statement.kind=Statement::Kind::ForRange; statement.name=match[3].str(); statement.expression=expr(match[1].str()); statement.rangeEnd=expr(match[2].str()); statement.body=nestedBlock(indent,line.number); result.push_back(statement); continue; }
            static const std::regex eachLoop(R"(^(.+?)의 각 (.+?)(?:을|를) 반복하며:$)");
            if(std::regex_match(text,match,eachLoop)) { statement.kind=Statement::Kind::ForEach; statement.name=match[2].str(); statement.expression=expr(match[1].str()); statement.body=nestedBlock(indent,line.number); result.push_back(statement); continue; }
            static const std::regex setValue(R"(^(.+?)(?:을|를) (.+?)(?:으로|로) 정한다\.?$)");
            static const std::regex setIndex(R"(^(.+?)의 (.+?)번째 값을 (.+?)(?:으로|로) 정한다\.?$)");
            if(std::regex_match(text,match,setIndex)) { statement.kind=Statement::Kind::Assignment; statement.operation="="; statement.target=node(Expression::Kind::Index); statement.target->left=expr(match[1].str()); statement.target->right=expr(match[2].str()); statement.expression=expr(match[3].str()); result.push_back(statement); continue; }
            if(std::regex_match(text,match,setValue)) { statement.kind=Statement::Kind::Assignment; statement.operation="="; statement.target=expr(match[1].str()); statement.expression=expr(match[2].str()); result.push_back(statement); continue; }
            if(text.rfind("실행한다 ",0)==0) { statement.kind=Statement::Kind::Expression; statement.expression=expr(text.substr(std::string("실행한다 ").size())); result.push_back(statement); continue; }
            if(text=="반환한다.") { statement.kind=Statement::Kind::Return; result.push_back(statement); continue; }
            if(text.rfind("함수 ",0)==0 && !std::regex_match(text,match,function)) fail("함수 선언을 확인해 주세요.");
            if(text.size()>=3 && text.back()==':') {
                std::string header=trimLanguageText(text.substr(0,text.size()-1));
                if(header.size()>=3 && (header.ends_with("라면")||header.ends_with("다면"))) {
                    const std::string ending=header.ends_with("라면")?"라면":"다면";
                    header.erase(header.size()-ending.size()); statement.kind=Statement::Kind::If; statement.expression=condition(header);
                    statement.body=nestedBlock(indent,line.number);
                    if(position_<lines_.size() && lines_[position_].indent==indent && (lines_[position_].text=="아니라면:"||lines_[position_].text=="그렇지 않으면:")) { ++position_; statement.alternative=nestedBlock(indent,line.number); }
                    result.push_back(std::move(statement)); continue;
                }
                if(header.ends_with("동안")) { header.erase(header.size()-std::string("동안").size()); statement.kind=Statement::Kind::While; statement.expression=condition(header); statement.body=nestedBlock(indent,line.number); result.push_back(std::move(statement)); continue; }
                fail("조건 또는 반복 문장 구문을 확인해 주세요.");
            }
            const std::string returnEnding=" 반환한다";
            if(text.ends_with(returnEnding+".")) text.resize(text.size()-returnEnding.size()-1);
            else if(text.ends_with(returnEnding)) text.resize(text.size()-returnEnding.size());
            else text.clear();
            if(!text.empty()) {
                if(text.size()>=3 && (text.ends_with("을")||text.ends_with("를"))) text.resize(text.size()-3);
                statement.kind=Statement::Kind::Return; statement.expression=expr(text); result.push_back(std::move(statement)); continue;
            }
            static const std::regex mutation(R"(^(.+?)\s*(?:을|를)\s+(.+?)만큼\s*(늘린다|줄인다|곱한다|나눈다)\.?$)");
            if(std::regex_match(lines_[position_-1].text,match,mutation)) {
                statement.kind=Statement::Kind::Assignment;statement.target=expr(match[1].str()); statement.operation=match[3].str()=="늘린다"?"+=":match[3].str()=="줄인다"?"-=":match[3].str()=="곱한다"?"*=":"/="; statement.expression=expr(match[2].str()); result.push_back(std::move(statement)); continue;
            }
            std::string raw=lines_[position_-1].text;
            if(raw.ends_with(" 출력한다.")) raw.resize(raw.size()-std::string(" 출력한다.").size());
            else if(raw.ends_with(" 출력한다")) raw.resize(raw.size()-std::string(" 출력한다").size());
            else raw.clear();
            if(!raw.empty()) { auto call=std::make_shared<Expression>(); call->kind=Expression::Kind::Call; auto callee=std::make_shared<Expression>(); callee->kind=Expression::Kind::Identifier; callee->text="print"; call->left=callee; call->arguments.push_back({{},expr(raw)}); statement.kind=Statement::Kind::Expression; statement.expression=call; result.push_back(std::move(statement)); continue; }
            fail("지원하지 않는 訓機正音 문장입니다.");
        }
        if(position_<lines_.size() && lines_[position_].indent>indent) fail("들여쓰기는 블록 안에서만 사용할 수 있습니다.");
        return result;
    }
    StatementList nestedBlock(std::size_t parentIndent,std::size_t parentLine) {
        if(position_>=lines_.size() || lines_[position_].indent<=parentIndent) { (void)parentLine; fail("블록 본문을 들여써 주세요."); }
        return block(lines_[position_].indent);
    }
    std::vector<SourceLine> lines_;
    std::size_t position_{};
};

struct Flow { enum class Kind { Normal, Return, Break, Continue }; Kind kind{Kind::Normal}; Value value; };

class Interpreter {
public:
    Interpreter(const Program& program, RunOptions options, HostFunction host)
        : program_(program), options_(std::move(options)), host_(std::move(host)), globals_(std::make_shared<Environment>()) {
        for (const auto& [name, value] : options_.initialValues) globals_->declare(name, value);
        for (const Statement& statement : program_.statements) if (statement.kind == Statement::Kind::Function) functions_.emplace(statement.name, &statement);
    }
    ExecutionResult run() {
        const Flow flow = runBlock(program_.statements, globals_, false);
        if (flow.kind != Flow::Kind::Normal) throw std::runtime_error("break, continue, or return is not valid at the top level.");
        if(!options_.eventName.empty())for(const auto& statement:program_.statements)if(statement.kind==Statement::Kind::Event && statement.name==options_.eventName)(void)call(statement,{});
        Value returnValue;
        if(!options_.entryFunction.empty()) {
            const auto found=functions_.find(options_.entryFunction);
            if(found==functions_.end()) throw std::runtime_error("Entry function not found: "+options_.entryFunction);
            returnValue=call(*found->second,{});
        }
        return {globals_, output_, executed_, returnValue};
    }
private:
    void step() {
        if (options_.shouldStop && options_.shouldStop()) throw std::runtime_error("Execution stopped by the editor.");
        if (++executed_ > options_.instructionBudget) throw std::runtime_error("Script stopped after reaching its instruction budget.");
    }
    Value eval(const ExpressionPtr& expression, const std::shared_ptr<Environment>& env) {
        step();
        if (expression && expression->kind == Expression::Kind::Call && expression->left->kind == Expression::Kind::Identifier) {
            const std::string& name = expression->left->text;
            std::vector<Value> args; std::vector<std::string> names;
            for (const auto& item : expression->arguments) { names.push_back(item.name); args.push_back(eval(item.value, env)); }
            const auto function = functions_.find(name);
            if (function != functions_.end()) return call(*function->second, args);
            if (name == "print" || name == "println") { if (args.empty()) output_.push_back(""); else output_.push_back(args.front().toString()); return Value{}; }
            return invokeBuiltin(name, args, names, host_);
        }
        HostFunction nestedHost = [&](const std::string& name, const std::vector<Value>& args, const std::vector<std::string>& names) {
            if(name.rfind("builtin.",0)==0) { if(host_)return host_(name,args,names);throw std::out_of_range("Unbound stable symbol"); }
            if(name=="print" || name=="println") { output_.push_back(args.empty()?"":args.front().toString()); return Value{}; }
            const auto function = functions_.find(name); if (function != functions_.end()) return call(*function->second, args);
            return invokeBuiltin(name, args, names, host_);
        };
        return evaluateExpression(expression, *env, nestedHost);
    }
    Value readTarget(const ExpressionPtr& target, const std::shared_ptr<Environment>& env) {
        return eval(target, env);
    }
    void assignTarget(const ExpressionPtr& target, Value value, const std::shared_ptr<Environment>& env) {
        step();
        if (!target) throw std::runtime_error("Missing assignment target.");
        if (target->kind == Expression::Kind::Identifier) {
            const auto current=env->get(target->text);
            if(current.type()==Type::Float && value.type()==Type::Int)value=Value(number(value));
            if(current.type()!=Type::Void && current.type()!=value.type())throw std::runtime_error("JM2001: Assignment changes variable type: "+target->text);
            env->assign(target->text, std::move(value)); return;
        }
        if (target->kind == Expression::Kind::Index) {
            const Value container = eval(target->left, env); const Value index = eval(target->right, env);
            if (auto list = std::get_if<Value::ArrayPtr>(&container.data)) {
                const auto at = checkedIndex(index); if (at < 0) throw std::runtime_error("List index cannot be negative.");
                if (static_cast<std::size_t>(at) >= list->get()->size()) throw std::runtime_error("List assignment index must already exist.");
                rejectCycle(value,list->get());list->get()->at(static_cast<std::size_t>(at)) = std::move(value); return;
            }
            if (auto map = std::get_if<Value::MapPtr>(&container.data)) { rejectCycle(value,map->get());map->get()->insert_or_assign(index.toString(), std::move(value)); return; }
            throw std::runtime_error("Only a list or map item can be assigned.");
        }
        if (target->kind == Expression::Kind::Member) {
            const Value container = eval(target->left, env);
            if (auto map = std::get_if<Value::MapPtr>(&container.data)) { rejectCycle(value,map->get());map->get()->insert_or_assign(target->text, std::move(value)); return; }
        }
        throw std::runtime_error("This expression cannot be assigned to.");
    }
    Flow runBlock(const StatementList& statements, const std::shared_ptr<Environment>& parent, bool childScope) {
        auto env = childScope ? parent->child() : parent;
        for (const Statement& statement : statements) {
            step();
            Flow flow = runStatement(statement, env);
            if (flow.kind != Flow::Kind::Normal) return flow;
        }
        return {};
    }
    Flow runStatement(const Statement& statement, const std::shared_ptr<Environment>& env) {
        switch (statement.kind) {
        case Statement::Kind::Variable: {
            Value value=statement.expression ? eval(statement.expression,env) : Value{};
            if(!statement.expression)switch(statement.declaredType) {
                case Type::Int:value=Value(std::int64_t{0});break;case Type::Float:value=Value(0.0);break;case Type::Bool:value=Value(false);break;
                case Type::String:value=Value("");break;case Type::List:value=Value::array({});break;case Type::Map:value=Value::map({});break;case Type::Vector2:value=Value(Vector2Value{});break;default:break;
            }
            if(statement.declaredType==Type::Float && value.type()==Type::Int) value=Value(number(value));
            if(statement.declaredType!=Type::Any && value.type()!=statement.declaredType) throw std::runtime_error("JM2001: Variable '"+statement.name+"' requires "+typeName(statement.declaredType)+", got "+value.typeName());
            env->declare(statement.name,std::move(value),statement.constant); return {};
        }
        case Statement::Kind::Import:
            if(!standardModule(statement.name) && (!options_.moduleResolver || !options_.moduleResolver(statement.name))) throw std::runtime_error("JM2008: Unknown module: "+statement.name);
            return {};
        case Statement::Kind::Assignment: {
            Value value = eval(statement.expression, env);
            if (statement.operation != "=") {
                Expression compound; compound.kind = Expression::Kind::Binary; compound.text = statement.operation.substr(0, 1);
                compound.left = std::make_shared<Expression>(); compound.left->kind = Expression::Kind::Literal; compound.left->literal = readTarget(statement.target, env);
                compound.right = std::make_shared<Expression>(); compound.right->kind = Expression::Kind::Literal; compound.right->literal = value;
                value = evaluateExpression(std::make_shared<Expression>(compound), *env);
            }
            assignTarget(statement.target, std::move(value), env); return {};
        }
        case Statement::Kind::Expression: (void)eval(statement.expression, env); return {};
        case Statement::Kind::If: return runBlock(truth(eval(statement.expression, env)) ? statement.body : statement.alternative, env, true);
        case Statement::Kind::While:
            while (truth(eval(statement.expression, env))) {
                Flow flow = runBlock(statement.body, env, true);
                if (flow.kind == Flow::Kind::Return) return flow;
                if (flow.kind == Flow::Kind::Break) break;
                if (flow.kind == Flow::Kind::Continue) continue;
            }
            return {};
        case Statement::Kind::ForRange: {
            const auto start = checkedIndex(eval(statement.expression, env)); const auto end = checkedIndex(eval(statement.rangeEnd, env));
            auto loop = env->child(); loop->declare(statement.name, Value(start));
            for (std::int64_t value = start; value < end; ++value) {
                loop->assign(statement.name, Value(value)); Flow flow = runBlock(statement.body, loop, true);
                if (flow.kind == Flow::Kind::Return) return flow; if (flow.kind == Flow::Kind::Break) break; if (flow.kind == Flow::Kind::Continue) continue;
            }
            return {};
        }
        case Statement::Kind::ForEach: {
            const Value collection = eval(statement.expression, env); auto loop = env->child(); loop->declare(statement.name, Value{});
            auto runOne = [&](const Value& value) { loop->assign(statement.name, value); return runBlock(statement.body, loop, true); };
            if (auto list = std::get_if<Value::ArrayPtr>(&collection.data)) { const auto snapshot=**list; for (const Value& item : snapshot) { step();Flow flow = runOne(item); if (flow.kind == Flow::Kind::Return) return flow; if (flow.kind == Flow::Kind::Break) break; } return {}; }
            if (auto map = std::get_if<Value::MapPtr>(&collection.data)) { const auto snapshot=**map;for (const auto& [key, value] : snapshot) { step();(void)key; Flow flow = runOne(value); if (flow.kind == Flow::Kind::Return) return flow; if (flow.kind == Flow::Kind::Break) break; } return {}; }
            throw std::runtime_error("for-in needs a list or map.");
        }
        case Statement::Kind::Event:case Statement::Kind::Function: return {};
        case Statement::Kind::Return: return {Flow::Kind::Return, statement.expression ? eval(statement.expression, env) : Value{}};
        case Statement::Kind::Break: return {Flow::Kind::Break, {}};
        case Statement::Kind::Continue: return {Flow::Kind::Continue, {}};
        }
        return {};
    }
    Value call(const Statement& function, const std::vector<Value>& arguments) {
        if (++depth_ > options_.recursionLimit) { --depth_; throw std::runtime_error("Function recursion limit reached in '" + function.name + "'."); }
        if (arguments.size() != function.parameters.size()) { --depth_; throw std::runtime_error("Function '" + function.name + "' expects " + std::to_string(function.parameters.size()) + " arguments."); }
        auto local = globals_->child();
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            Value argument=arguments[i]; const auto type=i<function.parameterTypes.size()?function.parameterTypes[i]:Type::Any;
            if(type==Type::Float && argument.type()==Type::Int) argument=Value(number(argument));
            if(type!=Type::Any && type!=argument.type()) { --depth_; throw std::runtime_error("JM2003: Parameter '"+function.parameters[i]+"' requires "+typeName(type)); }
            local->declare(function.parameters[i],argument);
        }
        Flow flow;
        try { flow = runBlock(function.body, local, false); }
        catch (...) { --depth_; throw; }
        --depth_;
        if (flow.kind == Flow::Kind::Return) {
            if(function.returnType==Type::Float && flow.value.type()==Type::Int) return Value(number(flow.value));
            if(function.returnType!=Type::Any && function.returnType!=flow.value.type()) throw std::runtime_error("JM2002: Return type mismatch in '"+function.name+"'.");
            return flow.value;
        }
        if (flow.kind != Flow::Kind::Normal) throw std::runtime_error("break or continue cannot leave a function.");
        return {};
    }
    const Program& program_;
    RunOptions options_;
    HostFunction host_;
    std::shared_ptr<Environment> globals_;
    std::unordered_map<std::string, const Statement*> functions_;
    std::vector<std::string> output_;
    std::size_t executed_{0};
    std::size_t depth_{0};
};

} // namespace

Value Value::array(Array value) { Value result; result.data = std::make_shared<Array>(std::move(value)); return result; }
std::uint64_t stableBuiltinSymbolId(std::string_view name) {
    std::uint64_t value=14695981039346656037ULL;
    for(const unsigned char byte:name) { value^=byte; value*=1099511628211ULL; }
    return value;
}
Value Value::map(Map value) { Value result; result.data = std::make_shared<Map>(std::move(value)); return result; }
bool Value::isNull() const { return std::holds_alternative<std::monostate>(data); }
std::string Value::typeName() const {
    switch (data.index()) { case 0: return "Null"; case 1: return "Boolean"; case 2: return "Float"; case 3: return "String"; case 4: return "List"; case 5: return "Map"; case 6: return "Vector2"; case 7: return "Color"; case 8: return "Entity"; case 9: return "Int"; default: return "Unknown"; }
}
std::string Value::toString() const {
    if (isNull()) return "null";
    if (auto value=std::get_if<std::int64_t>(&data)) return std::to_string(*value);
    if (auto value = std::get_if<bool>(&data)) return *value ? "true" : "false";
    if (auto value = std::get_if<double>(&data)) { std::ostringstream out; out << std::setprecision(12) << *value; return out.str(); }
    if (auto value = std::get_if<std::string>(&data)) return *value;
    if (auto value = std::get_if<ArrayPtr>(&data)) { std::string out = "["; for (std::size_t i = 0; i < (*value)->size(); ++i) { if (i) out += ", "; out += (**value)[i].toString(); } return out + "]"; }
    if (auto value = std::get_if<MapPtr>(&data)) { std::string out = "{"; bool first = true; for (const auto& [key, item] : **value) { if (!first) out += ", "; first = false; out += key + ": " + item.toString(); } return out + "}"; }
    if (auto value = std::get_if<Vector2Value>(&data)) return "(" + Value(value->x).toString() + ", " + Value(value->y).toString() + ")";
    if (auto value = std::get_if<ColorValue>(&data)) return "Color(" + Value(value->r).toString() + ", " + Value(value->g).toString() + ", " + Value(value->b).toString() + ", " + Value(value->a).toString() + ")";
    return "Entity(" + std::get<EntityReference>(data).id + ")";
}

Environment::Environment(std::shared_ptr<Environment> parent) : parent_(std::move(parent)) {}
void Environment::declare(const std::string& name, Value value, bool constant) {
    if (name.empty()) throw std::runtime_error("Variable name cannot be empty.");
    if (!bindings_.emplace(name, Binding{std::move(value), constant}).second) throw std::runtime_error("Variable '" + name + "' is already declared in this scope.");
}
void Environment::assign(const std::string& name, Value value) {
    const auto found = bindings_.find(name);
    if (found != bindings_.end()) { if (found->second.constant) throw std::runtime_error("Cannot change constant '" + name + "'."); found->second.value = std::move(value); return; }
    if (parent_) { parent_->assign(name, std::move(value)); return; }
    throw std::runtime_error("Variable '" + name + "' is not defined.");
}
Value Environment::get(const std::string& name) const {
    const auto found = bindings_.find(name); if (found != bindings_.end()) return found->second.value;
    if (parent_) return parent_->get(name);
    throw std::runtime_error("Variable '" + name + "' is not defined.");
}
bool Environment::containsLocal(const std::string& name) const { return bindings_.contains(name); }
std::shared_ptr<Environment> Environment::child() { return std::make_shared<Environment>(shared_from_this()); }

ExpressionPtr parseExpression(const std::string& source, std::string* error) {
    try { std::vector<Token> tokens = lex(source); std::size_t position = 0; ExpressionParser parser(tokens, position); auto result = parser.parse(); if (tokens[position].kind != Token::Kind::Newline && tokens[position].kind != Token::Kind::End) throw std::runtime_error("Unexpected text after expression."); if (error) error->clear(); return result; }
    catch (const std::exception& exception) { if (error) *error = exception.what(); return {}; }
}
Value evaluateExpression(const ExpressionPtr& expression, const Environment& environment, const HostFunction& hostFunction) { return evaluate(expression, environment, hostFunction); }
Value evaluateExpression(const std::string& source, const Environment& environment, const HostFunction& hostFunction) {
    std::string error; auto expression = parseExpression(source, &error); if (!expression) throw std::runtime_error(error); return evaluate(expression, environment, hostFunction);
}

namespace {
void validateIdentifiers(const StatementList& statements) {
    auto valid=[](const std::string& name) {
        static const std::unordered_set<std::string> reserved{"let","const","fn","if","else","while","for","in","return","break","continue","import","on","true","false","null","and","or","not","참","거짓"};
        if(name.empty() || reserved.contains(name))return false;
        const auto first=static_cast<unsigned char>(name.front());if(!isIdentifierStart(first) && first<0x80)return false;
        for(const unsigned char byte:name)if(!isIdentifierPart(byte) && byte<0x80)return false;
        return true;
    };
    for(const auto& item:statements) {
        if((item.kind==Statement::Kind::Variable || item.kind==Statement::Kind::Function || item.kind==Statement::Kind::ForRange || item.kind==Statement::Kind::ForEach) && !valid(item.name))throw std::runtime_error("line "+std::to_string(item.line)+": Invalid or reserved identifier '"+item.name+"'.");
        for(const auto& parameter:item.parameters)if(!valid(parameter))throw std::runtime_error("line "+std::to_string(item.line)+": Invalid parameter identifier '"+parameter+"'.");
        validateIdentifiers(item.body);validateIdentifiers(item.alternative);
    }
}
}

bool parseCode(const std::string& source, Program& output, Diagnostic& diagnostic) {
    try { Program parsed = CodeParser(source).parse();validateIdentifiers(parsed.statements); output = std::move(parsed); diagnostic = {}; return true; }
    catch (const std::exception& exception) {
        std::string message=exception.what(); static const std::regex prefix(R"(^line ([0-9]+): (.*)$)"); std::smatch match;
        if(std::regex_match(message,match,prefix)) diagnostic={match[2].str(),static_cast<std::size_t>(std::stoull(match[1].str()))}; else diagnostic={message,0};
        return false;
    }
}

bool parseKorean(const std::string& source, Program& output, Diagnostic& diagnostic) {
    try { Program parsed=KoreanAstParser(source).parse();validateIdentifiers(parsed.statements); output=std::move(parsed); diagnostic={}; return true; }
    catch(const std::exception& exception) {
        const std::string message=exception.what();
        static const std::regex linePrefix(R"(^line ([0-9]+): (.*)$)"); std::smatch match;
        if(std::regex_match(message,match,linePrefix)) diagnostic={match[2].str(),static_cast<std::size_t>(std::stoull(match[1].str()))};
        else diagnostic={message,0};
        return false;
    }
}

bool structurallyEqual(const Program& left, const Program& right) {
    std::function<bool(const ExpressionPtr&, const ExpressionPtr&)> sameExpression;
    sameExpression = [&](const ExpressionPtr& a, const ExpressionPtr& b) {
        if (!a || !b) return !a && !b;
        if (a->kind != b->kind || a->text != b->text || a->builtinSymbolId != b->builtinSymbolId || a->builtinSymbolName != b->builtinSymbolName || !equal(a->literal,b->literal) || a->literal.type()!=b->literal.type() ||
            a->elements.size() != b->elements.size() || a->entries.size() != b->entries.size() || a->arguments.size() != b->arguments.size() ||
            !sameExpression(a->left, b->left) || !sameExpression(a->right, b->right)) return false;
        for (std::size_t i = 0; i < a->elements.size(); ++i) if (!sameExpression(a->elements[i], b->elements[i])) return false;
        for (std::size_t i = 0; i < a->entries.size(); ++i) if (a->entries[i].first != b->entries[i].first || !sameExpression(a->entries[i].second, b->entries[i].second)) return false;
        for (std::size_t i = 0; i < a->arguments.size(); ++i) if (a->arguments[i].name != b->arguments[i].name || !sameExpression(a->arguments[i].value, b->arguments[i].value)) return false;
        return true;
    };
    std::function<bool(const StatementList&, const StatementList&)> sameStatements;
    sameStatements = [&](const StatementList& a, const StatementList& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const Statement& x = a[i]; const Statement& y = b[i];
            if (x.kind != y.kind || x.name != y.name || x.operation != y.operation || x.constant != y.constant || x.parameters != y.parameters || x.declaredType!=y.declaredType || x.returnType!=y.returnType || x.parameterTypes!=y.parameterTypes ||
                !sameExpression(x.target, y.target) || !sameExpression(x.expression, y.expression) || !sameExpression(x.rangeEnd, y.rangeEnd) ||
                !sameStatements(x.body, y.body) || !sameStatements(x.alternative, y.alternative)) return false;
        }
        return true;
    };
    return sameStatements(left.statements, right.statements);
}

ExecutionResult execute(const Program& program, const RunOptions& options, const HostFunction& hostFunction) {
    std::vector<Diagnostic> diagnostics; if(!check(program,diagnostics)) throw std::runtime_error(diagnostics.front().code+": "+diagnostics.front().message);
    return Interpreter(program, options, hostFunction).run();
}

} // namespace jm::script
