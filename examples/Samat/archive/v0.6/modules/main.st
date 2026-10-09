import "utils.st"
fn main() -> Int:
    let movement = scaleVelocity(Vector2(8.0, 0.0), 0.5)
    return int(movement.x) + 38
