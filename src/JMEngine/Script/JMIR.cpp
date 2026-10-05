#include "JMEngine/Script/JMIR.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace jm::script::ir {
namespace {

struct Lowerer {
    Function function;
    std::vector<std::unordered_map<std::string, std::uint32_t>> scopes;
    BlockId current{};
    std::uint32_t nextValue{};
    std::uint32_t nextBlock{};

    BlockId block(const std::string& name) {
        const BlockId id = nextBlock++;
        function.blocks.push_back({id, name, {}, {}});
        return id;
    }
    BasicBlock& at(BlockId id) { return function.blocks.at(id); }
    void select(BlockId id) { current = id; }
    ValueId emit(Instruction instruction) {
        instruction.result = nextValue++;
        at(current).instructions.push_back(std::move(instruction));
        return at(current).instructions.back().result;
    }
    std::uint32_t declare(const std::string& name) {
        if (scopes.back().contains(name)) throw std::runtime_error("Duplicate local variable: " + name);
        const auto slot = function.localCount++;
        scopes.back()[name] = slot;
        return slot;
    }
    std::uint32_t local(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        throw std::runtime_error("Native lowering cannot resolve variable '" + name + "'.");
    }
    ValueId expression(const ExpressionPtr& value) {
        if (!value) return emit({Op::Constant, 0, 0, 0, 0});
        switch (value->kind) {
        case Expression::Kind::Literal:
            if (const auto* number = std::get_if<double>(&value->literal.data)) {
                if (!std::isfinite(*number) || std::trunc(*number) != *number || *number < -9.22e18 || *number > 9.22e18)
                    throw std::runtime_error("Native x64 currently accepts signed 64-bit integer literals only.");
                return emit({Op::Constant, 0, 0, 0, static_cast<std::int64_t>(*number)});
            }
            if (const auto* boolean = std::get_if<bool>(&value->literal.data))
                return emit({Op::Constant, 0, 0, 0, *boolean ? 1 : 0});
            throw std::runtime_error("Native x64 currently supports integer and boolean literals only.");
        case Expression::Kind::Identifier: {
            Instruction instruction; instruction.op = Op::Load; instruction.local = local(value->text); return emit(std::move(instruction));
        }
        case Expression::Kind::Unary: {
            const ValueId operand = expression(value->right);
            Instruction instruction; instruction.op = value->text == "-" ? Op::Negate : Op::LogicalNot; instruction.left = operand;
            if (value->text == "+") return operand;
            if (value->text != "-" && value->text != "!" && value->text != "not") throw std::runtime_error("Unsupported unary operator in native x64.");
            return emit(std::move(instruction));
        }
        case Expression::Kind::Binary: {
            const ValueId left = expression(value->left);
            if (value->text == "and" || value->text == "&&" || value->text == "or" || value->text == "||") {
                const bool isAnd=value->text=="and"||value->text=="&&";
                const auto resultSlot=function.localCount++;
                const auto rhsBlock=block("logic.rhs"), shortBlock=block("logic.short"), mergeBlock=block("logic.end");
                at(current).terminator={Terminator::Kind::ConditionalBranch,left,isAnd?rhsBlock:shortBlock,isAnd?shortBlock:rhsBlock};
                select(shortBlock); const auto shortValue=emit({Op::Constant,0,0,0,isAnd?0:1});
                Instruction shortStore; shortStore.op=Op::Store; shortStore.local=resultSlot; shortStore.left=shortValue; at(current).instructions.push_back(std::move(shortStore));
                at(current).terminator={Terminator::Kind::Branch,0,mergeBlock,0};
                select(rhsBlock); const auto rawRight=expression(value->right); Instruction truthValue; truthValue.op=Op::ToBoolean; truthValue.left=rawRight;
                const auto rightValue=emit(std::move(truthValue)); Instruction rightStore; rightStore.op=Op::Store; rightStore.local=resultSlot; rightStore.left=rightValue; at(current).instructions.push_back(std::move(rightStore));
                at(current).terminator={Terminator::Kind::Branch,0,mergeBlock,0}; select(mergeBlock);
                Instruction result; result.op=Op::Load; result.local=resultSlot; return emit(std::move(result));
            }
            const ValueId right = expression(value->right);
            static const std::unordered_map<std::string, Op> operations{
                {"+",Op::Add},{"-",Op::Subtract},{"*",Op::Multiply},{"/",Op::Divide},{"%",Op::Modulo},
                {"==",Op::Equal},{"!=",Op::NotEqual},{"<",Op::Less},{"<=",Op::LessEqual},{">",Op::Greater},{">=",Op::GreaterEqual}};
            const auto found = operations.find(value->text);
            if (found == operations.end()) throw std::runtime_error("Unsupported native binary operator '" + value->text + "'.");
            Instruction instruction; instruction.op = found->second; instruction.left = left; instruction.right = right; return emit(std::move(instruction));
        }
        case Expression::Kind::Call: {
            if (!value->left || (value->left->kind != Expression::Kind::Identifier && value->left->kind != Expression::Kind::Member))
                throw std::runtime_error("Native calls currently require a named function or registered member function.");
            Instruction instruction; instruction.op = Op::Call;
            if (value->left->kind == Expression::Kind::Member) {
                if(value->builtinSymbolId==0 || value->builtinSymbolName.empty()) throw std::runtime_error("Native member call has no stable builtin symbol.");
                instruction.symbol = value->builtinSymbolName; instruction.symbolId=value->builtinSymbolId;
            } else instruction.symbol = value->left->text;
            for (const auto& argument : value->arguments) {
                if (!argument.name.empty()) throw std::runtime_error("Named arguments are not supported by native x64 yet.");
                instruction.arguments.push_back(expression(argument.value));
            }
            return emit(std::move(instruction));
        }
        default: throw std::runtime_error("Native x64 does not support list, map, index, or member expressions yet.");
        }
    }
    void statements(const StatementList& list) {
        for (const Statement& statement : list) {
            if (at(current).terminator.kind != Terminator::Kind::None) return;
            switch (statement.kind) {
            case Statement::Kind::Variable: {
                if (statement.constant) throw std::runtime_error("Native lowering currently treats locals as mutable; const is not supported.");
                const auto slot = declare(statement.name);
                if (statement.expression) { Instruction store; store.op = Op::Store; store.local = slot; store.left = expression(statement.expression); at(current).instructions.push_back(std::move(store)); }
                break;
            }
            case Statement::Kind::Assignment: {
                if (!statement.target || statement.target->kind != Expression::Kind::Identifier)
                    throw std::runtime_error("Native assignment currently supports local variables only.");
                const auto slot = local(statement.target->text);
                ValueId result = expression(statement.expression);
                if (statement.operation != "=") {
                    const ValueId old = emit({Op::Load, 0, 0, 0, 0, slot});
                    Instruction operation; operation.left = old; operation.right = result;
                    const auto op = statement.operation.substr(0, 1);
                    operation.op = op == "+" ? Op::Add : op == "-" ? Op::Subtract : op == "*" ? Op::Multiply : Op::Divide;
                    result = emit(std::move(operation));
                }
                Instruction store; store.op = Op::Store; store.local = slot; store.left = result; at(current).instructions.push_back(std::move(store));
                break;
            }
            case Statement::Kind::Expression: (void)expression(statement.expression); break;
            case Statement::Kind::Return: {
                const ValueId result = expression(statement.expression);
                at(current).terminator = {Terminator::Kind::Return, result, 0, 0};
                break;
            }
            case Statement::Kind::If: {
                const ValueId condition = expression(statement.expression);
                const auto thenBlock = block("if.then"), elseBlock = block("if.else"), mergeBlock = block("if.end");
                at(current).terminator = {Terminator::Kind::ConditionalBranch, condition, thenBlock, elseBlock};
                scopes.emplace_back(); select(thenBlock); statements(statement.body); if (at(current).terminator.kind == Terminator::Kind::None) at(current).terminator = {Terminator::Kind::Branch, 0, mergeBlock, 0}; scopes.pop_back();
                scopes.emplace_back(); select(elseBlock); statements(statement.alternative); if (at(current).terminator.kind == Terminator::Kind::None) at(current).terminator = {Terminator::Kind::Branch, 0, mergeBlock, 0}; scopes.pop_back();
                select(mergeBlock); break;
            }
            case Statement::Kind::While: {
                const auto conditionBlock = block("while.cond"), bodyBlock = block("while.body"), endBlock = block("while.end");
                at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0};
                select(conditionBlock); const auto condition = expression(statement.expression); at(current).terminator = {Terminator::Kind::ConditionalBranch, condition, bodyBlock, endBlock};
                scopes.emplace_back(); select(bodyBlock); statements(statement.body); if (at(current).terminator.kind == Terminator::Kind::None) at(current).terminator = {Terminator::Kind::Branch, 0, conditionBlock, 0}; scopes.pop_back();
                select(endBlock); break;
            }
            case Statement::Kind::Function: throw std::runtime_error("Nested function declarations are not supported by native x64.");
            case Statement::Kind::ForRange: case Statement::Kind::ForEach:
                throw std::runtime_error("Native x64 currently lowers while loops; for/list iteration is not supported yet.");
            case Statement::Kind::Break: case Statement::Kind::Continue:
                throw std::runtime_error("break/continue lowering is not supported by native x64 yet.");
            }
        }
    }
    Function build(const Statement& declaration) {
        function.name = declaration.name;
        function.parameterCount = static_cast<std::uint32_t>(declaration.parameters.size());
        scopes.emplace_back();
        std::function<std::size_t(const StatementList&)> countBlocks = [&](const StatementList& list) {
            std::size_t count = 1;
            for (const Statement& item : list) {
                if (item.kind == Statement::Kind::If || item.kind == Statement::Kind::While) count += 3;
                count += countBlocks(item.body);
                count += countBlocks(item.alternative);
            }
            return count;
        };
        function.blocks.reserve(countBlocks(declaration.body));
        for (const std::string& parameter : declaration.parameters) declare(parameter);
        const auto entry = block("entry"); select(entry);
        statements(declaration.body);
        if (at(current).terminator.kind == Terminator::Kind::None) {
            const auto zero = emit({Op::Constant, 0, 0, 0, 0}); at(current).terminator = {Terminator::Kind::Return, zero, 0, 0};
        }
        function.valueCount = nextValue;
        return std::move(function);
    }
};

} // namespace

bool lower(const Program& program, Module& output, LoweringDiagnostic& diagnostic) {
    try {
        Module result;
        for (const Statement& statement : program.statements) {
            if (statement.kind != Statement::Kind::Function) throw std::runtime_error("Native modules currently require top-level function declarations.");
            result.functions.push_back(Lowerer{}.build(statement));
        }
        if (result.functions.empty()) throw std::runtime_error("Native module has no functions.");
        output = std::move(result); diagnostic.message.clear(); return true;
    } catch (const std::exception& error) { diagnostic.message = error.what(); return false; }
}

std::string format(const Module& module) {
    std::ostringstream out;
    for (const Function& function : module.functions) {
        out << "func " << function.name << "(" << function.parameterCount << " x i64) -> i64 {\n";
        for (const BasicBlock& block : function.blocks) {
            out << block.name << ":\n";
            for (const Instruction& instruction : block.instructions) {
                out << "  "; if (instruction.op != Op::Store) out << '%' << instruction.result << " = ";
                switch (instruction.op) {
                case Op::Constant: out << "const.i64 " << instruction.immediate; break;
                case Op::Load: out << "load.local " << instruction.local; break;
                case Op::Store: out << "store.local " << instruction.local << ", %" << instruction.left; break;
                case Op::Call: out << "call @" << instruction.symbol << "("; for (std::size_t i=0;i<instruction.arguments.size();++i) { if(i) out << ", "; out << '%' << instruction.arguments[i]; } out << ')'; break;
                case Op::Add: out << "add.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Subtract: out << "sub.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Multiply: out << "mul.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Divide: out << "div.s.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Modulo: out << "rem.s.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Negate: out << "neg.i64 %" << instruction.left; break;
                case Op::LogicalNot: out << "not.bool %" << instruction.left; break;
                case Op::ToBoolean: out << "to.bool %" << instruction.left; break;
                case Op::Equal: out << "eq.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::NotEqual: out << "ne.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Less: out << "lt.s.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::LessEqual: out << "le.s.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::Greater: out << "gt.s.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::GreaterEqual: out << "ge.s.i64 %" << instruction.left << ", %" << instruction.right; break;
                case Op::BooleanAnd: out << "and.bool %" << instruction.left << ", %" << instruction.right; break;
                case Op::BooleanOr: out << "or.bool %" << instruction.left << ", %" << instruction.right; break;
                }
                out << '\n';
            }
            switch (block.terminator.kind) {
            case Terminator::Kind::Branch: out << "  br " << function.blocks.at(block.terminator.first).name << '\n'; break;
            case Terminator::Kind::ConditionalBranch: out << "  br_if %" << block.terminator.value << ", " << function.blocks.at(block.terminator.first).name << ", " << function.blocks.at(block.terminator.second).name << '\n'; break;
            case Terminator::Kind::Return: out << "  ret %" << block.terminator.value << '\n'; break;
            default: out << "  unreachable\n"; break;
            }
        }
        out << "}\n";
    }
    return out.str();
}

} // namespace jm::script::ir
