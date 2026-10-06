struct Player:
    health: Int
    velocity: Vector2
    name: String
    scores: List<Int>
let state = Player(health: 100, velocity: Vector2(8.0, 0.0), name: "Player", scores: [1, 2])
fn main() -> Int:
    let stats = Player(health: 100, velocity: Vector2(3.0, 4.0), name: "JM", scores: [1, 2])
    let alias = stats
    alias.health -= 58
    stats.scores.push(3)
    state.health += 1
    return stats.health
