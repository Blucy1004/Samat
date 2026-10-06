import jm.game
let score: Int = 0
on start:
    transform.setPosition(x: 0.0, y: -2.2)
    score += 10
on update:
    score += 1
on key.right.held:
    player.moveFloat(direction: 1.0, speed: 8.5)
on key.space.pressed:
    player.jumpFloat(force: 12.5)
