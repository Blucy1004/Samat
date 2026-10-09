let velocity = Vector2(8.0, 0.0)
let position = Vector2(0.0, 0.0)
fn update(delta: Float):
    position += velocity * delta
fn main() -> Int:
    for frame in 0..<10:
        update(0.5)
    let direction = Vector2(3.0, 4.0)
    println(direction.length)
    println(position)
    return int(position.x) + 2
