# Playable turn-based 2D parkour. Run with the Samat CLI in a terminal.
fn platformAt(x: Int, y: Int) -> Int:
    if y == 0 and x >= 0 and x <= 2:
        return 0
    if y == 1 and x >= 5 and x <= 7:
        return 1
    if y == 2 and x >= 9 and x <= 11:
        return 2
    if y == 3 and x >= 13 and x <= 14:
        return 3
    return -1

fn drawCourse(playerX: Int, playerY: Int):
    print("\nSamat Parkour  |  a/d 이동 · j 점프 · q 종료")
    for row in 0..4:
        let level = 3 - row
        let line = ""
        for x in 0..15:
            if x == playerX and level == playerY:
                line += "@"
            else if x == 14 and level == 3:
                line += "G"
            else if platformAt(x, level) >= 0:
                line += "="
            else:
                line += " "
        print(line)
    print("현재 발판: " + string(playerY + 1) + " / 4")

let playerX = 1
let playerY = 0
let facing = 1
let checkpointX = 1
let checkpointY = 0
let playing = true
let command = ""

print("Samat Parkour")
print("끊어진 발판을 건너 목표 지점까지 가세요.")
while playing:
    drawCourse(playerX, playerY)
    print("입력: ")
    command = readLine()

    if command == "q" or command == "":
        playing = false
    else if command == "a":
        facing = -1
        playerX -= 1
        if platformAt(playerX, playerY) < 0:
            print("발판에서 떨어졌습니다. 체크포인트로 돌아갑니다.")
            playerX = checkpointX
            playerY = checkpointY
    else if command == "d":
        facing = 1
        playerX += 1
        if platformAt(playerX, playerY) < 0:
            print("발판에서 떨어졌습니다. 체크포인트로 돌아갑니다.")
            playerX = checkpointX
            playerY = checkpointY
    else if command == "j":
        let landingX = playerX + facing * 3
        let landingY = playerY + facing
        let landingPlatform = platformAt(landingX, landingY)
        if landingPlatform >= 0:
            playerX = landingX
            playerY = landingY
            checkpointX = playerX
            checkpointY = playerY
            print("착지 성공! 다음 발판으로 이동했습니다.")
            if playerX == 14 and playerY == 3:
                drawCourse(playerX, playerY)
                print("완주! Samat Parkour 클리어!")
                playing = false
        else:
            print("점프가 닿지 않았습니다. 체크포인트로 돌아갑니다.")
            playerX = checkpointX
            playerY = checkpointY
    else:
        print("a, d, j, q 중 하나를 입력해 주세요.")

print("게임을 종료했습니다.")
