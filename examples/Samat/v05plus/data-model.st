enum Direction:
    left
    right
struct Stats:
    health: Int
    speed: Float
    name: String = "Player"
fn main() -> Int:
    let stats = Stats(health: 100, speed: 8.5)
    let direction = Direction.right
    let position: Tuple = (10.0, 20.0)
    let vector: Vector3 = Vector3(1, 0, 0).cross(Vector3(0, 1, 0))
    stats.health -= 58
    if direction == Direction.right && vector.z == 1.0:
        return stats.health
    return 1
