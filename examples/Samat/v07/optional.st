fn main() -> Int:
    let nickname: String? = null
    let displayName = nickname ?? "Player"
    let position: Vector2? = Vector2(41.0, 0.0)
    let x: Float = position?.x ?? 0.0
    let value: Int? = null
    if displayName == "Player":
        return int(x) + (value ?? 1)
    return 0
