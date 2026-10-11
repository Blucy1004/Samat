모듈 jm.math를 가져온다.

함수 clampScore(score: Int) -> Int:
    score가 0보다 작다면:
        0을 반환한다.
    score가 100보다 크다면:
        100을 반환한다.
    score를 반환한다.

함수 sumEven(limit: Int) -> Int:
    정수 변수 total을 0으로 정한다.
    1부터 limit까지 number를 반복하며:
        number % 2 == 0 라면:
            total를 total + number로 정한다.
    total를 반환한다.

함수 report(title: String, score: Int, evenTotal: Int, root: Float, passed: Bool):
    함수 print(f"{title}: 점수 {score}, 짝수 합 {evenTotal}, 제곱근 {root}, 통과 {passed}")를 실행한다.

문자열 상수 title을 "Samat 문법 둘러보기"로 정한다.
정수 변수 score를 clampScore(87)로 정한다.
정수 변수 evenTotal을 sumEven(10)으로 정한다.
실수 변수 root를 sqrt(81.0)으로 정한다.
논리 변수 passed를 score >= 60으로 정한다.

함수 report(title, score, evenTotal, root, passed)를 실행한다.
passed라면:
    "조건문: 통과"를 출력한다.
아니라면:
    "조건문: 다시 도전"를 출력한다.
