#include "Samat/Language/LanguageCore.hpp"
#include "Samat/Language/RuntimeABI.h"
#include <iostream>

int main() {
    using namespace jm::script;
    auto run = [](const std::string &source) {
        Program program;
        Diagnostic diagnostic;
        if (!parseCode(source, program, diagnostic))
            throw std::runtime_error(diagnostic.message);
        RunOptions options;
        options.entryFunction = "main";
        return execute(program, options);
    };
    try {
        // Alias mutations remain valid after the creating function frame is destroyed.
        auto values = run("fn make():\n    return [1, 2, 3]\nfn main():\n    let first = make()\n    let "
                          "alias = first\n    first = [7]\n    alias[0] = 42\n    assert(alias == [42, 2, "
                          "3])\n    assert(first == [7])\n    return alias[0]\n");
        if (values.returnValue.toString() != "42")
            throw std::runtime_error("Escaping list ownership failed.");
        // Snapshot foreach must survive invalidating writes to the source vector.
        run("fn main():\n    let values = [1, 2, 3]\n    let sum = 0\n    for item in values:\n        sum "
            "+= item\n        values.clear()\n        values.push(100)\n    assert(sum == 6)\n    return "
            "sum\n");
        // Indirect cycles must be rejected; otherwise shared_ptr ownership leaks.
        bool rejected = false;
        try {
            run("fn main():\n    let first = []\n    let second = []\n    first.push(second)\n    "
                "second.push(first)\n    return 0\n");
        } catch (const std::exception &error) {
            rejected = std::string(error.what()).find("JM3010") != std::string::npos;
        }
        if (!rejected)
            throw std::runtime_error("Indirect list ownership cycle was accepted.");
        rejected = false;
        try {
            run("fn main():\n    let data = {}\n    data.self = data\n    return 0\n");
        } catch (const std::exception &error) {
            rejected = std::string(error.what()).find("JM3010") != std::string::npos;
        }
        if (!rejected)
            throw std::runtime_error("Map ownership cycle was accepted.");
        for (const auto &source : {std::string("struct Node:\n    child: Any\nfn main():\n    let node = "
                                               "Node(child: null)\n    node.child = node\n    return 0\n"),
                                   std::string("fn main():\n    let values = []\n    let tuple = (values,)\n "
                                               "   values.push(tuple)\n    return 0\n")}) {
            bool rejected = false;
            try {
                run(source);
            } catch (const std::exception &error) {
                rejected = std::string(error.what()).find("JM3010") != std::string::npos;
            }
            if (!rejected)
                throw std::runtime_error("Data ownership cycle accepted.");
        }
        for (int i = 0; i < 100; ++i) {
            auto *context = jm_runtime_create_context();
            auto *previous = jm_runtime_activate(context);
            auto text = jm_string_create("JM", 2);
            auto list = jm_runtime_call(JM_RT_LIST_CREATE, JM_RT_STRING, 0, 0);
            jm_runtime_call(JM_RT_LIST_PUSH, list, text, JM_RT_STRING);
            jm_runtime_retain(list);
            jm_runtime_collect();
            if (jm_runtime_live_objects() != 2)
                throw std::runtime_error("Managed roots lost.");
            jm_runtime_release(list);
            jm_runtime_collect();
            if (jm_runtime_live_objects() != 0)
                throw std::runtime_error("Managed objects leaked.");
            jm_runtime_activate(previous);
            jm_runtime_destroy_context(context);
        }
        // Destroy aliased strings/collections repeatedly and check surviving values.
        for (int i = 0; i < 100; ++i)
            run("fn main():\n    let text = \"Samat\"\n    let values = [text, [1, 2], {name: "
                "text}]\n    let alias = values\n    values.pop()\n    assert(alias.length == 2)\n    "
                "assert(alias[0] == \"Samat\")\n    return 0\n");
        std::cout << "Runtime memory: escaping aliases, foreach mutation, direct/indirect cycle rejection, "
                     "repeated destruction passed.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
