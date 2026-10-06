import jm.game

fn jumpPlayer():
    if player.grounded:
        player.jump(force: 12)

on start:
    player.move(direction: 0, speed: 8)

on key.right.held:
    player.move(direction: 1, speed: 8)

on key.space.pressed:
    jumpPlayer()
