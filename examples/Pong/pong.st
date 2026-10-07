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
    serve()
    println("Pong: W/S and Up/Down move paddles; Space resets the score.")

on update:
    let delta = time.delta
    let player = scene.find("player")
    if player != null:
        let playerY = player.position.y
        if input.isHeld("w"):
            playerY += paddleSpeed * delta
        if input.isHeld("s"):
            playerY -= paddleSpeed * delta
        if playerY > 3.55:
            playerY = 3.55
        if playerY < -3.55:
            playerY = -3.55
        player.setPosition(Vector2(-7.2, playerY))

    let enemy = scene.find("enemy")
    if enemy != null:
        let enemyY = enemy.position.y
        if input.isHeld("up"):
            enemyY += paddleSpeed * delta
        if input.isHeld("down"):
            enemyY -= paddleSpeed * delta
        if enemyY > 3.55:
            enemyY = 3.55
        if enemyY < -3.55:
            enemyY = -3.55
        enemy.setPosition(Vector2(7.2, enemyY))

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

    if player != null:
        if ballVelocityX < 0.0 and ballX < -6.85 and ballX > -7.8:
            let difference = ballY - player.position.y
            if abs(difference) < 1.45:
                ballVelocityX = abs(ballVelocityX)
                ballVelocityY += difference * 0.8
    if enemy != null:
        if ballVelocityX > 0.0 and ballX > 6.85 and ballX < 7.8:
            let difference = ballY - enemy.position.y
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

    let ball = scene.find("ball")
    if ball != null:
        ball.setPosition(Vector2(ballX, ballY))
