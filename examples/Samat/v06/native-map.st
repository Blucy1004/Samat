fn main() -> Int:
    let scores: Map<String, Int>
    scores["JM"] = 42
    scores["Engine"] = 100
    println(scores.keys())
    scores.remove("Engine")
    return scores["JM"]
