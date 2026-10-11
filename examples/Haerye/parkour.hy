// Static Haerye scene for the Samat Parkour course.
scene "Samat Parkour" {
    game: platformer
    background: "#171421"

    object "start platform" {
        shape: rectangle
        position: (-6, -2)
        size: (3, 0.34)
        color: "#79D7B5"
        role: platform
    }

    object "high platform one" {
        shape: rectangle
        position: (-1, -0.8)
        size: (3, 0.34)
        color: "#A991E8"
        role: platform
    }

    object "high platform two" {
        shape: rectangle
        position: (3, 0.4)
        size: (3, 0.34)
        color: "#A991E8"
        role: platform
    }

    object "goal platform" {
        shape: rectangle
        position: (6.5, 1.6)
        size: (2, 0.34)
        color: "#79D7B5"
        role: platform
    }

    object "runner" {
        shape: rectangle
        position: (-6, -1.35)
        size: (0.42, 0.8)
        color: "#F3C879"
        role: player
    }

    object "goal flag" {
        shape: rectangle
        position: (7, 2.35)
        size: (0.12, 1.2)
        color: "#F3C879"
        role: goal
    }

    object "pit" {
        shape: rectangle
        position: (0, -3.5)
        size: (15, 0.18)
        color: "#E97991"
        role: hazard
    }
}
