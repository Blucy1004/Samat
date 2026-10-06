import jm.game
let moveSpeed: Float = 8.0
let health: Int = 100
fn jumpPlayer() -> Void:
    player.jump(force: 12)
on start:
    health = 37
on key.right.held:
    player.position += Vector2(moveSpeed * time.delta, 0.0)
on key.left.held:
    player.position -= Vector2(moveSpeed * time.delta, 0.0)
on key.space.pressed:
    jumpPlayer()
