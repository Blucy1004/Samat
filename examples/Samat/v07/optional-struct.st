struct Stats:
    health: Int?
fn main() -> Int:
    let state: Stats? = Stats(health: 42)
    if state == null:
        return 0
    return state.health ?? 0
