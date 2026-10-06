import jm.game
import jm.math
import jm.random
import jm.time
import console

let paddleSpeed: Float = 8.0
let ballSpeed: Float = 6.0
let leftScore: Int = 0
let rightScore: Int = 0
let ballX: Float = 0.0
let ballY: Float = 0.0
let ballVelocityX: Float = 0.0
let ballVelocityY: Float = 0.0

fn serve() -> Void:
    ballX = 0.0
    ballY = 0.0
    if randomInt(0, 2) == 0:
        ballVelocityX = -ballSpeed
    else:
        ballVelocityX = ballSpeed
    ballVelocityY = randomFloat(-0.65, 0.65)

on start:
    seed(2026)
    let left = scene.spawn("Left Paddle", Vector2(-7.2, 0.0), Vector2(0.35, 2.2), Color(0.25, 0.72, 1.0))
    let right = scene.spawn("Right Paddle", Vector2(7.2, 0.0), Vector2(0.35, 2.2), Color(1.0, 0.42, 0.3))
    let ball = scene.spawn("Ball", Vector2(0.0, 0.0), Vector2(0.38, 0.38), Color(1.0, 1.0, 1.0))
    if left != null:
        left.setScale(Vector2(0.35, 2.2))
        left.setRotation(0.0)
    if right != null:
        right.setScale(Vector2(0.35, 2.2))
        right.setRotation(0.0)
    if ball != null:
        ball.setScale(Vector2(0.38, 0.38))
        ball.setRotation(0.0)
    serve()
    println("Samat Pong: W/S and Up/Down move paddles; Space resets the score.")

on update:
    let delta = time.delta
    let left = scene.find("Left Paddle")
    if left != null:
        let leftY = left.position.y
        if input.isHeld("w"):
            leftY += paddleSpeed * delta
        if input.isHeld("s"):
            leftY -= paddleSpeed * delta
        if leftY > 3.55:
            leftY = 3.55
        if leftY < -3.55:
            leftY = -3.55
        left.setPosition(Vector2(-7.2, leftY))

    let right = scene.find("Right Paddle")
    if right != null:
        let rightY = right.position.y
        if input.isHeld("up"):
            rightY += paddleSpeed * delta
        if input.isHeld("down"):
            rightY -= paddleSpeed * delta
        if rightY > 3.55:
            rightY = 3.55
        if rightY < -3.55:
            rightY = -3.55
        right.setPosition(Vector2(7.2, rightY))

    if input.wasPressed("space"):
        leftScore = 0
        rightScore = 0
        serve()
        println("Score reset: " + string(leftScore) + " - " + string(rightScore))

    ballX += ballVelocityX * delta
    ballY += ballVelocityY * delta
    if ballY > 4.0:
        ballY = 4.0
        ballVelocityY = -abs(ballVelocityY)
    if ballY < -4.0:
        ballY = -4.0
        ballVelocityY = abs(ballVelocityY)

    if ballVelocityX < 0.0 and ballX < -6.85 and ballX > -7.8:
        let left = scene.find("Left Paddle")
        if left != null:
            let difference = ballY - left.position.y
            if abs(difference) < 1.45:
                ballVelocityX = abs(ballVelocityX)
                ballVelocityY += difference * 0.8
    if ballVelocityX > 0.0 and ballX > 6.85 and ballX < 7.8:
        let right = scene.find("Right Paddle")
        if right != null:
            let difference = ballY - right.position.y
            if abs(difference) < 1.45:
                ballVelocityX = -abs(ballVelocityX)
                ballVelocityY += difference * 0.8

    if ballX < -8.5:
        rightScore += 1
        println("Score: " + string(leftScore) + " - " + string(rightScore))
        serve()
    if ballX > 8.5:
        leftScore += 1
        println("Score: " + string(leftScore) + " - " + string(rightScore))
        serve()

    let ball = scene.find("Ball")
    if ball != null:
        ball.setPosition(Vector2(ballX, ballY))
