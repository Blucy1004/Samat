#include "RuntimeABI.h"
#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
struct Object {
    uint64_t type;
    std::string text;
    std::vector<uint64_t> values;
    uint64_t element{};
};
struct Context {
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<uint64_t> free;
    std::unordered_map<uint64_t, uint64_t> roots, retained;
    bool aot{};
    uint64_t randomState{0};
    std::chrono::steady_clock::time_point born = std::chrono::steady_clock::now();
};
thread_local Context fallback;
thread_local Context *current = &fallback;
[[noreturn]] void fail(const char *message) {
    if (current->aot) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
    throw std::runtime_error(message);
}
Object &object(uint64_t id, uint64_t expected) {
    if (!id || id > current->objects.size() || !current->objects[id - 1] ||
        current->objects[id - 1]->type != expected)
        fail("JM6001: Invalid runtime handle/type.");
    return *current->objects[id - 1];
}
uint64_t add(Object value) {
    auto object = std::make_unique<Object>(std::move(value));
    if (!current->free.empty()) {
        auto id = current->free.back();
        current->free.pop_back();
        current->objects[id - 1] = std::move(object);
        return id;
    }
    current->objects.push_back(std::move(object));
    return current->objects.size();
}
uint64_t str(std::string value) { return add({JM_RT_STRING, std::move(value), {}, 0}); }
std::string &text(uint64_t id) { return object(id, JM_RT_STRING).text; }
Object &list(uint64_t id) { return object(id, JM_RT_LIST); }
size_t index(uint64_t bits, size_t size, bool end = false) {
    auto value = std::bit_cast<int64_t>(bits);
    if (value < 0 || static_cast<uint64_t>(value) > size || (!end && static_cast<uint64_t>(value) == size))
        fail("JM3003: Index is outside the collection.");
    return static_cast<size_t>(value);
}
bool eq(uint64_t a, uint64_t b, uint64_t type) {
    if (type == JM_RT_STRING)
        return text(a) == text(b);
    if (type == JM_RT_FLOAT)
        return std::bit_cast<double>(a) == std::bit_cast<double>(b);
    return a == b;
}
std::string stringify(uint64_t value, uint64_t type) {
    if (type == JM_RT_VOID)
        return "null";
    if (type == JM_RT_STRING)
        return text(value);
    if (type == JM_RT_INT)
        return std::to_string(std::bit_cast<int64_t>(value));
    if (type == JM_RT_BOOL)
        return value ? "true" : "false";
    if (type == JM_RT_FLOAT) {
        char buffer[128];
        auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), std::bit_cast<double>(value));
        if (error != std::errc{})
            fail("JM6001: Float formatting failed.");
        return {buffer, end};
    }
    if (type == JM_RT_LIST) {
        auto &valueList = list(value);
        std::string result = "[";
        for (size_t i = 0; i < valueList.values.size(); ++i) {
            if (i)
                result += ", ";
            result += stringify(valueList.values[i], valueList.element);
        }
        return result + "]";
    }
    fail("JM6002: Unsupported runtime conversion type.");
}
uint64_t randomBits() {
    auto z = (current->randomState += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
double randomUnit() { return static_cast<double>(randomBits() >> 11) * 0x1.0p-53; }
void element(Object &value, uint64_t type) {
    if (type != value.element)
        fail("JM6002: Native List element type mismatch.");
}
} // namespace
extern "C" {
void *jm_runtime_create_context() { return new Context; }
void jm_runtime_destroy_context(void *context) { delete static_cast<Context *>(context); }
void *jm_runtime_activate(void *context) {
    auto *previous = current;
    current = context ? static_cast<Context *>(context) : &fallback;
    return previous;
}
void jm_runtime_root(uint64_t slot, JMHandle value) {
    if (!value || value > current->objects.size() || !current->objects[value - 1])
        fail("JM6001: Invalid root handle.");
    current->roots[slot] = value;
}
void jm_runtime_retain(JMHandle value) {
    if (!value || value > current->objects.size() || !current->objects[value - 1])
        fail("JM6001: Invalid retained handle.");
    ++current->retained[value];
}
void jm_runtime_release(JMHandle value) {
    auto found = current->retained.find(value);
    if (found == current->retained.end())
        fail("JM6001: Release without retain.");
    if (!--found->second)
        current->retained.erase(found);
}
uint64_t jm_runtime_live_objects() {
    uint64_t count = 0;
    for (const auto &object : current->objects)
        count += object != nullptr;
    return count;
}
void jm_runtime_collect() {
    std::unordered_set<uint64_t> live;
    std::vector<uint64_t> work;
    for (const auto &[slot, value] : current->roots)
        work.push_back(value);
    for (const auto &[value, count] : current->retained)
        work.push_back(value);
    while (!work.empty()) {
        auto id = work.back();
        work.pop_back();
        if (!live.insert(id).second)
            continue;
        if (!id || id > current->objects.size() || !current->objects[id - 1])
            fail("JM6001: Invalid managed root.");
        auto &value = *current->objects[id - 1];
        if (value.type == JM_RT_LIST && value.element == JM_RT_STRING)
            for (auto child : value.values)
                work.push_back(child);
    }
    for (size_t i = 0; i < current->objects.size(); ++i)
        if (current->objects[i] && !live.contains(i + 1)) {
            current->objects[i].reset();
            current->free.push_back(i + 1);
        }
}
void jm_runtime_set_aot(uint64_t enabled) { current->aot = enabled != 0; }
JMHandle jm_string_create(const char *bytes, uint64_t length) {
    if (!bytes && length)
        fail("JM6001: Null string bytes.");
    return str(std::string(bytes ? bytes : "", length));
}
uint64_t jm_list_element_type(JMHandle value) { return list(value).element; }
const char *jm_string_bytes(JMHandle value, uint64_t *length) {
    auto &result = text(value);
    if (length)
        *length = result.size();
    return result.data();
}
uint64_t jm_runtime_call(uint64_t operation, uint64_t a, uint64_t b, uint64_t c) {
    if (operation >= JM_RT_TRUNC && operation <= JM_RT_SMOOTHSTEP) {
        auto x = std::bit_cast<double>(a), y = std::bit_cast<double>(b), z = std::bit_cast<double>(c);
        double result{};
        switch (operation) {
        case JM_RT_TRUNC:
            result = std::trunc(x);
            break;
        case JM_RT_TAN:
            result = std::tan(x);
            break;
        case JM_RT_ASIN:
            result = std::asin(x);
            break;
        case JM_RT_ACOS:
            result = std::acos(x);
            break;
        case JM_RT_ATAN:
            result = std::atan(x);
            break;
        case JM_RT_ATAN2:
            result = std::atan2(x, y);
            break;
        case JM_RT_LOG:
            result = std::log(x);
            break;
        case JM_RT_LOG10:
            result = std::log10(x);
            break;
        case JM_RT_EXP:
            result = std::exp(x);
            break;
        case JM_RT_LERP:
            result = x + (y - x) * z;
            break;
        case JM_RT_DEG_TO_RAD:
            result = x * (3.14159265358979323846 / 180.0);
            break;
        case JM_RT_RAD_TO_DEG:
            result = x * (180.0 / 3.14159265358979323846);
            break;
        case JM_RT_SIGN:
            result = std::isnan(x) ? x : x > 0 ? 1 : x < 0 ? -1 : 0;
            break;
        case JM_RT_FRACT:
            result = x - std::floor(x);
            break;
        case JM_RT_SMOOTHSTEP:
            if (!(x < y))
                fail("JM3005: smoothstep edges must be ordered.");
            result = std::clamp((z - x) / (y - x), 0.0, 1.0);
            result = result * result * (3 - 2 * result);
            break;
        }
        return std::bit_cast<uint64_t>(result);
    }
    switch (operation) {
    case JM_RT_SEED:
        current->randomState = a;
        return 0;
    case JM_RT_RANDOM:
        return std::bit_cast<uint64_t>(randomUnit());
    case JM_RT_RANDOM_INT: {
        auto low = std::bit_cast<int64_t>(a), high = std::bit_cast<int64_t>(b);
        if (low >= high)
            fail("JM3005: randomInt requires minimum < maximum.");
        auto width = b - a, threshold = (uint64_t{0} - width) % width;
        uint64_t value;
        do {
            value = randomBits();
        } while (value < threshold);
        return a + value % width;
    }
    case JM_RT_RANDOM_FLOAT: {
        auto low = std::bit_cast<double>(a), high = std::bit_cast<double>(b);
        if (!std::isfinite(low) || !std::isfinite(high) || !(low < high) || !std::isfinite(high - low))
            fail("JM3005: randomFloat requires finite ordered bounds.");
        return std::bit_cast<uint64_t>(low + (high - low) * randomUnit());
    }
    case JM_RT_TIME_NOW:
        return std::bit_cast<uint64_t>(
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());
    case JM_RT_TIME_ELAPSED:
        return std::bit_cast<uint64_t>(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - current->born).count());
    case JM_RT_CONCAT:
        return str(text(a) + text(b));
    case JM_RT_EQUAL:
        return text(a) == text(b);
    case JM_RT_COMPARE:
        return std::bit_cast<uint64_t>(static_cast<int64_t>(text(a).compare(text(b))));
    case JM_RT_LENGTH:
        return c == JM_RT_LIST ? list(a).values.size() : text(a).size();
    case JM_RT_INDEX: {
        auto &value = text(a);
        return str(value.substr(index(b, value.size()), 1));
    }
    case JM_RT_SUBSTRING: {
        auto &value = text(a);
        auto start = index(b, value.size(), true);
        auto count = std::bit_cast<int64_t>(c);
        if (count < 0 || static_cast<uint64_t>(count) > value.size() - start)
            fail("JM3003: Substring range is outside the string.");
        return str(value.substr(start, count));
    }
    case JM_RT_CONTAINS:
        return text(a).find(text(b)) != std::string::npos;
    case JM_RT_STARTS_WITH:
        return text(a).starts_with(text(b));
    case JM_RT_ENDS_WITH:
        return text(a).ends_with(text(b));
    case JM_RT_FIND: {
        auto at = text(a).find(text(b));
        return at == std::string::npos ? uint64_t(-1) : at;
    }
    case JM_RT_REPLACE: {
        auto value = text(a), from = text(b), to = text(c);
        if (from.empty())
            fail("JM3005: Replace pattern must not be empty.");
        size_t at = 0;
        while ((at = value.find(from, at)) != std::string::npos) {
            value.replace(at, from.size(), to);
            at += to.size();
        }
        return str(std::move(value));
    }
    case JM_RT_SPLIT: {
        auto value = text(a), delimiter = text(b);
        if (delimiter.empty())
            fail("JM3005: Split delimiter must not be empty.");
        Object result{JM_RT_LIST, {}, {}, JM_RT_STRING};
        size_t start = 0, at;
        while ((at = value.find(delimiter, start)) != std::string::npos) {
            result.values.push_back(str(value.substr(start, at - start)));
            start = at + delimiter.size();
        }
        result.values.push_back(str(value.substr(start)));
        return add(std::move(result));
    }
    case JM_RT_TRIM: {
        auto value = text(a);
        auto first = value.find_first_not_of(" \t\r\n"), last = value.find_last_not_of(" \t\r\n");
        return str(first == std::string::npos ? "" : value.substr(first, last - first + 1));
    }
    case JM_RT_UPPER:
    case JM_RT_LOWER: {
        auto value = text(a);
        for (auto &ch : value) {
            auto byte = static_cast<unsigned char>(ch);
            if (byte < 128)
                ch = operation == JM_RT_UPPER ? static_cast<char>(std::toupper(byte))
                                              : static_cast<char>(std::tolower(byte));
        }
        return str(std::move(value));
    }
    case JM_RT_CODEPOINT_LENGTH: {
        uint64_t length = 0;
        auto &value = text(a);
        for (size_t i = 0; i < value.size();) {
            auto ch = static_cast<unsigned char>(value[i]);
            size_t n = ch < 128                     ? 1
                       : (ch >= 0xc2 && ch <= 0xdf) ? 2
                       : (ch >= 0xe0 && ch <= 0xef) ? 3
                       : (ch >= 0xf0 && ch <= 0xf4) ? 4
                                                    : 0;
            if (!n || i + n > value.size())
                fail("JM3005: Invalid UTF-8 string.");
            for (size_t j = 1; j < n; ++j)
                if ((static_cast<unsigned char>(value[i + j]) & 0xc0) != 0x80)
                    fail("JM3005: Invalid UTF-8 continuation.");
            if (n >= 3) {
                auto second = static_cast<unsigned char>(value[i + 1]);
                if ((ch == 0xe0 && second < 0xa0) || (ch == 0xed && second >= 0xa0) ||
                    (ch == 0xf0 && second < 0x90) || (ch == 0xf4 && second >= 0x90))
                    fail("JM3005: Invalid UTF-8 scalar.");
            }
            i += n;
            ++length;
        }
        return length;
    }
    case JM_RT_LIST_CREATE:
        if (a < JM_RT_INT || a > JM_RT_STRING)
            fail("JM6002: Native List supports Int/Float/Bool/String elements.");
        return add({JM_RT_LIST, {}, {}, a});
    case JM_RT_LIST_GET: {
        auto &value = list(a);
        element(value, c);
        return value.values.at(index(b, value.values.size()));
    }
    case JM_RT_LIST_SET: {
        auto &value = list(a);
        auto at = index(b, value.values.size());
        if (value.element == JM_RT_STRING)
            (void)text(c);
        value.values[at] = c;
        return 0;
    }
    case JM_RT_LIST_PUSH: {
        auto &value = list(a);
        element(value, c);
        if (c == JM_RT_STRING)
            (void)text(b);
        value.values.push_back(b);
        return 0;
    }
    case JM_RT_LIST_POP: {
        auto &value = list(a);
        element(value, c);
        if (value.values.empty())
            fail("JM3004: Cannot pop an empty list.");
        auto result = value.values.back();
        value.values.pop_back();
        return result;
    }
    case JM_RT_LIST_CLEAR:
        list(a).values.clear();
        return 0;
    case JM_RT_LIST_REVERSE: {
        auto &value = list(a).values;
        std::reverse(value.begin(), value.end());
        return 0;
    }
    case JM_RT_LIST_SORT: {
        auto &value = list(a);
        std::stable_sort(value.values.begin(), value.values.end(), [&](auto left, auto right) {
            if (value.element == JM_RT_STRING)
                return text(left) < text(right);
            if (value.element == JM_RT_FLOAT) {
                auto x = std::bit_cast<double>(left), y = std::bit_cast<double>(right);
                return std::isnan(y) ? !std::isnan(x) : !std::isnan(x) && x < y;
            }
            return std::bit_cast<int64_t>(left) < std::bit_cast<int64_t>(right);
        });
        return 0;
    }
    case JM_RT_LIST_CONTAINS:
    case JM_RT_LIST_INDEX_OF: {
        auto &value = list(a);
        element(value, c);
        for (size_t i = 0; i < value.values.size(); ++i)
            if (eq(value.values[i], b, c))
                return operation == JM_RT_LIST_CONTAINS ? 1 : i;
        return operation == JM_RT_LIST_CONTAINS ? 0 : uint64_t(-1);
    }
    case JM_RT_LIST_INSERT: {
        auto &value = list(a);
        auto at = index(b, value.values.size(), true);
        if (value.element == JM_RT_STRING)
            (void)text(c);
        value.values.insert(value.values.begin() + at, c);
        return 0;
    }
    case JM_RT_LIST_REMOVE_AT: {
        auto &value = list(a);
        auto at = index(b, value.values.size());
        auto result = value.values[at];
        value.values.erase(value.values.begin() + at);
        return result;
    }
    case JM_RT_LIST_EQUAL: {
        auto &left = list(a);
        auto &right = list(b);
        if (left.values.size() != right.values.size())
            return 0;
        for (size_t i = 0; i < left.values.size(); ++i) {
            if (left.element == right.element) {
                if (!eq(left.values[i], right.values[i], left.element))
                    return 0;
            } else if ((left.element == JM_RT_INT && right.element == JM_RT_FLOAT) ||
                       (left.element == JM_RT_FLOAT && right.element == JM_RT_INT)) {
                auto x = left.element == JM_RT_FLOAT
                             ? std::bit_cast<double>(left.values[i])
                             : static_cast<double>(std::bit_cast<int64_t>(left.values[i]));
                auto y = right.element == JM_RT_FLOAT
                             ? std::bit_cast<double>(right.values[i])
                             : static_cast<double>(std::bit_cast<int64_t>(right.values[i]));
                if (x != y)
                    return 0;
            } else
                return 0;
        }
        return 1;
    }
    case JM_RT_LIST_CLONE:
        return add(list(a));
    case JM_RT_TO_STRING:
        return str(stringify(a, b));
    case JM_RT_PARSE_INT: {
        auto &value = text(a);
        int64_t result{};
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc{} || end != value.data() + value.size())
            fail("JM3005: Invalid Int conversion.");
        return std::bit_cast<uint64_t>(result);
    }
    case JM_RT_PARSE_FLOAT: {
        auto &value = text(a);
        double result{};
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc{} || end != value.data() + value.size())
            fail("JM3005: Invalid Float conversion.");
        return std::bit_cast<uint64_t>(result);
    }
    case JM_RT_PRINT:
    case JM_RT_PRINTLN: {
        auto value = stringify(a, b);
        std::fwrite(value.data(), 1, value.size(), stdout);
        std::fputc('\n', stdout);
        return 0;
    }
    default:
        fail("JM6002: Unknown runtime operation.");
    }
}
}
