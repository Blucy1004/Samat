// Haerye declares the playfield and visible objects; Samat supplies behavior.
scene "Samat Pong" {
    background: "#101014"

    object "player" {
        shape: rectangle
        position: (-7.2, 0)
        size: (0.35, 2.2)
        rotation: 0
        color: "#40B8FF"
    }

    object "enemy" {
        shape: rectangle
        position: (7.2, 0)
        size: (0.35, 2.2)
        rotation: 0
        color: "#FF6B4D"
    }

    object "ball" {
        shape: circle
        position: (0, 0)
        size: (0.38, 0.38)
        color: "#FFFFFF"
    }
}
