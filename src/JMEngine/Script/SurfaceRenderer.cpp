#include "JMEngine/Script/KoreanParticles.hpp"
#include "JMEngine/Script/LanguageCore.hpp"
#include <iomanip>
#include <sstream>

namespace jm::script {
namespace {
std::string quote(const std::string& value) {
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            out += '\\';
            out += ch;
        } else if (ch == '\r')
            out += "\\r";
        else if (ch == '\n')
            out += "\\n";
        else if (ch == '\t')
            out += "\\t";
        else
            out += ch;
    }
    return out + '"';
}
std::string expression(const ExpressionPtr& value) {
    if (!value)
        return "null";
    switch (value->kind) {
    case Expression::Kind::Literal: {
        if (auto text = std::get_if<std::string>(&value->literal.data))
            return quote(*text);
        if (auto number = std::get_if<double>(&value->literal.data)) {
            std::ostringstream out;
            out << std::setprecision(17) << *number;
            auto result = out.str();
            if (result.find_first_of(".eE") == std::string::npos)
                result += ".0";
            return result;
        }
        return value->literal.toString();
    }
    case Expression::Kind::Identifier:
        return value->text;
    case Expression::Kind::Unary:
        return "(" + value->text + " " + expression(value->right) + ")";
    case Expression::Kind::Binary:
        return "(" + expression(value->left) + " " + value->text + " " + expression(value->right) + ")";
    case Expression::Kind::Index:
        return expression(value->left) + "[" + expression(value->right) + "]";
    case Expression::Kind::Member:
        return expression(value->left) + "." + value->text;
    case Expression::Kind::Call: {
        std::string out = expression(value->left) + "(";
        for (std::size_t i = 0; i < value->arguments.size(); ++i) {
            if (i)
                out += ", ";
            const auto& item = value->arguments[i];
            if (!item.name.empty())
                out += item.name + ": ";
            out += expression(item.value);
        }
        return out + ")";
    }
    case Expression::Kind::Array: {
        std::string out = "[";
        for (std::size_t i = 0; i < value->elements.size(); ++i) {
            if (i)
                out += ", ";
            out += expression(value->elements[i]);
        }
        return out + "]";
    }
    case Expression::Kind::Map: {
        std::string out = "{";
        for (std::size_t i = 0; i < value->entries.size(); ++i) {
            if (i)
                out += ", ";
            out += quote(value->entries[i].first) + ": " + expression(value->entries[i].second);
        }
        return out + "}";
    }
    }
    throw std::runtime_error("Cannot render unknown expression.");
}
std::string koreanType(Type type) {
    switch (type) {
    case Type::Int:
        return "정수";
    case Type::Float:
        return "실수";
    case Type::Bool:
        return "논리";
    case Type::String:
        return "문자열";
    case Type::List:
        return "목록";
    case Type::Map:
        return "지도";
    case Type::Vector2:
        return "Vector2";
    case Type::Any:
        return "숫자";
    default:
        throw std::runtime_error("Korean declaration renderer does not support type " + typeName(type));
    }
}
void render(std::ostringstream& out, const StatementList& list, bool korean, std::size_t depth = 0) {
    const std::string indent(depth * 4, ' ');
    for (const auto& item : list) {
        out << indent;
        switch (item.kind) {
        case Statement::Kind::Variable:
            if (!item.expression) {
                if (korean)
                    out << koreanType(item.declaredType) << " 변수 "
                        << jm::attachKoreanParticle(item.name, jm::KoreanParticle::Object) << " 선언한다.\n";
                else {
                    out << "let " << item.name;
                    if (item.declaredType != Type::Any)
                        out << ": " << typeName(item.declaredType);
                    out << '\n';
                }
                break;
            }
            if (korean)
                out << koreanType(item.declaredType) << ' ' << (item.constant ? "상수 " : "변수 ")
                    << jm::attachKoreanParticle(item.name, jm::KoreanParticle::Object) << ' '
                    << expression(item.expression) << "로 정한다.\n";
            else {
                out << (item.constant ? "const " : "let ") << item.name;
                if (item.declaredType != Type::Any)
                    out << ": " << typeName(item.declaredType);
                out << " = " << expression(item.expression) << '\n';
            }
            break;
        case Statement::Kind::Assignment:
            if (korean && item.operation == "=")
                out << expression(item.target) << "를 " << expression(item.expression) << "로 정한다.\n";
            else if (korean)
                out << expression(item.target) << "를 " << expression(item.expression) << "만큼 "
                    << (item.operation == "+="   ? "늘린다."
                        : item.operation == "-=" ? "줄인다."
                        : item.operation == "*=" ? "곱한다."
                                                 : "나눈다.")
                    << '\n';
            else
                out << expression(item.target) << ' ' << item.operation << ' ' << expression(item.expression)
                    << '\n';
            break;
        case Statement::Kind::Expression:
            out << (korean ? "실행한다 " : "") << expression(item.expression) << '\n';
            break;
        case Statement::Kind::Function:
            out << (korean ? "함수 " : "fn ") << item.name << '(';
            for (std::size_t i = 0; i < item.parameters.size(); ++i) {
                if (i)
                    out << ", ";
                out << item.parameters[i];
                if (i < item.parameterTypes.size() && item.parameterTypes[i] != Type::Any)
                    out << ": " << typeName(item.parameterTypes[i]);
            }
            out << ')';
            if (item.returnType != Type::Any)
                out << " -> " << typeName(item.returnType);
            out << ":\n";
            render(out, item.body, korean, depth + 1);
            break;
        case Statement::Kind::Return:
            if (korean)
                out << (item.expression ? expression(item.expression) + "를 " : "") << "반환한다.\n";
            else
                out << "return" << (item.expression ? " " + expression(item.expression) : "") << '\n';
            break;
        case Statement::Kind::If:
            out << (korean ? "" : "if ") << expression(item.expression) << (korean ? "라면:\n" : ":\n");
            render(out, item.body, korean, depth + 1);
            if (!item.alternative.empty()) {
                out << indent << (korean ? "아니라면:\n" : "else:\n");
                render(out, item.alternative, korean, depth + 1);
            }
            break;
        case Statement::Kind::While:
            out << (korean ? "" : "while ") << expression(item.expression) << (korean ? " 동안:\n" : ":\n");
            render(out, item.body, korean, depth + 1);
            break;
        case Statement::Kind::ForRange:
            if (korean)
                out << expression(item.expression) << "부터 " << expression(item.rangeEnd) << "까지 "
                    << item.name << "를 반복하며:\n";
            else
                out << "for " << item.name << " in " << expression(item.expression) << ".."
                    << expression(item.rangeEnd) << ":\n";
            render(out, item.body, korean, depth + 1);
            break;
        case Statement::Kind::ForEach:
            if (korean)
                out << expression(item.expression) << "의 각 " << item.name << "를 반복하며:\n";
            else
                out << "for " << item.name << " in " << expression(item.expression) << ":\n";
            render(out, item.body, korean, depth + 1);
            break;
        case Statement::Kind::Break:
            out << (korean ? "반복을 멈춘다." : "break") << '\n';
            break;
        case Statement::Kind::Continue:
            out << (korean ? "다음 반복을 진행한다." : "continue") << '\n';
            break;
        case Statement::Kind::Event:
            if (korean) {
                if (item.name == "start")
                    out << "시작할 때:\n";
                else if (item.name == "key.right.held")
                    out << "오른쪽 키를 누르는 동안:\n";
                else if (item.name == "key.left.held")
                    out << "왼쪽 키를 누르는 동안:\n";
                else if (item.name == "key.space.pressed")
                    out << "스페이스 키를 눌렀을 때:\n";
                else
                    out << "이벤트 " << item.name << ":\n";
            } else
                out << "on " << item.name << ":\n";
            render(out, item.body, korean, depth + 1);
            break;
        case Statement::Kind::Import:
            out << (korean ? "가져온다 " : "import ") << item.name << '\n';
            break;
        }
    }
}
} // namespace
std::string renderCode(const Program& program) {
    std::ostringstream out;
    render(out, program.statements, false);
    return out.str();
}
std::string renderKorean(const Program& program) {
    std::ostringstream out;
    render(out, program.statements, true);
    return out.str();
}
} // namespace jm::script
