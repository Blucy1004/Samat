import jm.game
struct PlayerState:
    health: Int
    velocity: Vector2
let state = PlayerState(health: 100, velocity: Vector2(8.0, 0.0))
on start:
    player.position += Vector2(1.0, 0.0)
on update:
    if input.keyHeld(1):
        player.position += state.velocity * time.delta
    if input.keyHeld(0):
        player.position -= state.velocity * time.delta
on key.space.pressed:
    if player.grounded:
        player.jumpFloat(force: 12.0)
