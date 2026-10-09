# 해례 v0.1

해례(`.hy`)는 장면의 이름과 시각 요소를 선언하는 텍스트 형식입니다. Samat(`.st`)은 프로그램의 동작을 표현합니다. 두 형식은 각각 독립적으로 파싱할 수 있으며, 해례 파서는 게임 엔진이나 렌더러에 의존하지 않습니다.

## 예제

저장소의 [Pong 장면 예제](../examples/Haerye/pong.hy)를 확인할 수 있습니다.

```hy
scene "Pong" {
    background: "#101014"

    object "player" {
        shape: rectangle
        position: (-8, 0)
        size: (0.5, 3)
        rotation: 0
        color: "#FFFFFF"
    }

    object "ball" {
        shape: circle
        position: (0, 0)
        size: (0.5, 0.5)
        color: "#FFFFFF"
    }
}
```

## 지원 문법

파일에는 `scene` 선언 하나, 선택적인 `background`, 이름이 지정된 `object` 블록을 작성할 수 있습니다. 객체에는 `shape`(`rectangle` 또는 `circle`), `position`, 양수 크기의 `size`, `color`가 필요합니다. `rotation`은 생략하면 0도입니다. 색상은 `#RRGGBB`, 위치와 크기는 숫자 `(x, y)` 형식입니다. `//` 주석을 지원합니다.

파서는 문법 오류의 줄과 열을 알려줍니다. 중복 객체나 속성, 알 수 없는 속성, 잘못된 모양·좌표·색상, 필수 값 누락, 중첩 장면 선언을 거부합니다. 이미지·텍스처·모델 같은 에셋 참조는 아직 지원하지 않습니다.

`parseHaerye`는 문서를 읽고, `validateHaerye`는 값과 중복을 검사하며, `serializeHaerye`는 유효한 문서를 정규 형식으로 기록합니다. 공백과 주석은 직렬화 과정에서 보존되지 않습니다.

## 범위

v0.1은 사각형과 원 도형, 위치·크기·회전·색상만 다룹니다. 에셋 가져오기, 텍스처, 오디오, 모델, 프리팹, 시각 편집기와의 동기화, 핫 리로드는 포함하지 않습니다.
