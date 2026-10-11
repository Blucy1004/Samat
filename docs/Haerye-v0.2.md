# 해례 v0.2

해례(.hy)는 장면의 이름과 시각 요소를 선언하는 텍스트 형식입니다. Samat(.st)은 프로그램의 동작을 표현합니다. 두 형식은 각각 독립적으로 파싱할 수 있습니다.

## 장면 문법

파일에는 scene 선언 하나, 선택적인 background, 이름이 지정된 object 블록을 작성할 수 있습니다. 객체에는 shape(rectangle 또는 circle), position, 양수 크기의 size, color가 필요합니다. rotation은 생략하면 0도입니다. 색상은 #RRGGBB, 위치와 크기는 숫자 (x, y) 형식입니다. // 주석을 지원합니다.

## 플랫폼 게임

해례 v0.2는 간단한 2D 플랫폼 장면을 표현합니다. 장면 안에 game: platformer를 선언하고 오브젝트에 role을 지정합니다.

    scene "Samat Parkour" {
        game: platformer
        background: "#171421"

        object "player" {
            shape: rectangle
            position: (-6, -1)
            size: (0.42, 0.8)
            color: "#F3C879"
            role: player
        }

        object "ledge" {
            shape: rectangle
            position: (-6, -2)
            size: (3, 0.34)
            color: "#79D7B5"
            role: platform
        }

        object "goal" {
            shape: rectangle
            position: (7, 2)
            size: (0.3, 1)
            color: "#F3C879"
            role: goal
        }
    }

게임 장면에는 플레이어 하나, 하나 이상의 발판, 목표 하나 이상이 필요합니다. hazard 역할은 플레이어를 시작 위치로 되돌립니다. role이 없는 객체는 장식으로 그려집니다.

Windows에서는 SamatRunner.exe를 실행하고 .hy 파일을 선택하면 별도 게임 창이 열립니다. A/D 또는 방향키로 움직이고 Space로 점프합니다. 발판 아래로 떨어지면 시작 위치로 돌아오며, 목표에 닿으면 완주 화면이 표시됩니다. PowerShell이나 Samat CLI가 필요하지 않습니다.

## 범위

v0.2는 사각형과 원 도형, 위치·크기·회전·색상과 기본 플랫폼 게임 역할을 다룹니다. 에셋 가져오기, 텍스처, 오디오, 모델, 프리팹, 시각 편집기와의 동기화, 핫 리로드는 포함하지 않습니다.
