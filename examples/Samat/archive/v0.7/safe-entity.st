import jm.game
on start:
    let enemy: Entity? = scene.find("Player")
    if enemy != null:
        enemy.position += Vector2(1.0, 0.0)
