# JM Engine 아키텍처 설계 초안

> 목표: **기능의 천장을 낮추지 않고, 진입 장벽을 낮춘다.**
>
> 이 문서는 JM Engine의 장기 목표를 구현 가능한 경계로 나누고, 현재 v0.04 샌드박스에서 다음 단계로 이어지는 기본 설계 결정을 정한다. 지금의 SDL3/OpenGL/ImGui 샌드박스는 렌더링과 편집 UX를 확인하는 프로토타입이다. 완성된 엔진 아키텍처로 간주하지 않는다.

## 결정 요약

| 영역 | 초기 결정 | 확장 방향 |
|---|---|---|
| 제품 범위 | 먼저 Windows용 2D 게임 제작 흐름을 검증한다. 현재 3D 샌드박스는 별도 렌더러 실험으로 보존한다. | 렌더러·프로젝트 모드를 추가해 3D, Animation, Web, App, Simulation으로 확장 |
| 프로그램 표현 | 한글 구조화 텍스트와 코드 텍스트는 같은 버전 지정 AST를 표현한다. | 같은 AST에서 디버거, 그래프 뷰, 코드 생성 추가 |
| 첫 실행 전략 | AST → 검증된 JM IR → register 기반 바이트코드 VM | 측정 결과가 필요한 함수만 LLVM ORC JIT 또는 AOT 백엔드로 컴파일 |
| 게임 오브젝트 | 안정적인 ID와 컴포넌트를 가진 Entity/Component/Scene 모델 | 핫 경로는 sparse-set 풀과 연속 메모리 시스템으로 최적화 |
| 2D 물리 | Box2D를 어댑터 뒤에 두고 고정 timestep으로 호출 | 엔진이 소유한 Physics API는 유지하고 공급자 교체 가능성 확보 |
| 저장 | 사람이 읽고 Git으로 비교할 수 있는 JSON 기반 프로젝트·씬 파일 | 대형 에셋은 별도 바이너리 캐시, 원본은 프로젝트 폴더에 유지 |
| 최적화 | 프로필은 먼저 측정·진단 힌트로 사용한다. 자동으로 의미를 바꾸지 않는다. | 안전성이 증명되고 동등성 검증이 가능한 패스만 자동 적용 |
| 보안 | Samat VM에는 명시적 기능(capability)만 노출한다. | 플러그인은 out-of-process/권한 분리 선택지를 추가 |

## 1. 제품 경계와 원칙

JM Engine은 하나의 데이터 모델, 에디터, 런타임, 빌드 파이프라인 위에 여러 제작 분야를 얹는다. Unity의 화면이나 기능 목록을 복제하는 대신 다음을 제품 규칙으로 삼는다.

1. **표현 계층과 실행 계층을 분리한다.** 블록·한글·코드 UI를 바꿔도 실행 의미는 AST/IR에 남는다.
2. **초보자와 고급자는 같은 프로젝트를 연다.** 초보 모드는 편집 가능한 항목과 설명을 간소화하지만 프로젝트 데이터를 낮은 기능의 전용 포맷으로 바꾸지 않는다.
3. **단순 경로는 짧게, 깊은 경로는 열어 둔다.** 처음에는 버튼·기본 속성만 보이고, 고급 패널에서 충돌 필터, 물리 재질, 스크립트 API, 빌드 옵션을 확인한다.
4. **성능은 프로필과 측정으로 다룬다.** 최적화는 관측 가능한 경고, 비교 가능한 결과, 되돌리기 가능한 변경으로 제공한다.
5. **플랫폼별 전략을 강요하지 않는다.** Project Mode와 Optimization Profile은 서로 다른 입력이며 백엔드가 함께 해석한다.

현재 프로토타입과의 차이는 명시한다. 2026-10 기준 샌드박스에는 SDL3 창/이벤트 루프, OpenGL 3.3 렌더링, ImGui 계층·인스펙터, 큐브/스프라이트 데모가 있다. Box2D, 저장, 스크립트 AST/VM, 타일맵, 프로젝트 시스템은 아직 설계 또는 예정 단계다. 따라서 현재 실행기를 사용자가 만든 프로젝트 런타임이나 완성된 3D 엔진으로 표현하지 않는다.

## 2. 전체 계층

```text
JM Project
├─ Project Settings ── Project Mode + Optimization Profile + Target
├─ Assets / Scenes / Objects / Scripts (사람이 읽을 수 있는 원본)
└─ Editor
   ├─ 장면/계층/속성/파일/코드/실행 패널
   ├─ 한글 구조화 편집기 ─┐
   └─ 코드 텍스트 편집기 ─┴→ Versioned JM AST
                               ↓
                         Name/Type Resolver
                               ↓
                       Typed JM IR + Debug Map
                               ↓
                  Profile-aware Optimization Passes
                               ↓
                   Bytecode VM (초기 기본 경로)
                    ├─ Engine API / Event Queue
                    └─ 이후 LLVM ORC / Wasm / AOT Backend

Runtime
├─ Core: lifecycle, scheduler, logging, jobs, memory
├─ Scene: entity handles, components, transforms, serialization
├─ Input / Event / Time
├─ 2D Physics adapter (Box2D 초기 공급자)
├─ Renderer interface → OpenGL 초기 구현 / 이후 다른 backend
├─ Asset database/cache/streaming
└─ Audio, UI, animation, platform services
```

런타임은 ImGui나 편집기 위젯 타입을 include하지 않는다. 에디터가 프로젝트를 읽고 수정한 다음 동일한 직렬화 모델을 런타임에 전달한다. 렌더러·물리·파일시스템도 작은 인터페이스 경계로 분리하되, 초기에는 구현체 하나만 둔다. 검증되지 않은 범용 추상화를 여러 겹 만들지 않는다.

## 3. Samat: AST가 정본

Samat 프로그램의 정본은 문자열도 블록 배치도 아닌 **버전이 지정된 AST 문서**다. 텍스트 모드는 AST를 문법으로 직렬화하고 파싱한 결과를 AST에 적용한다. 한글 모드는 AST 노드를 구조화 문장으로 렌더링하고, 입력 컨트롤은 노드 필드와 Symbol Reference를 수정한다.

```text
Korean Structured Editor ─┐
                          ├─> JM AST vN ─> Semantic Analysis ─> JM IR
Code Parser/Formatter ────┘
```

### 기본 구문 범위

첫 언어 버전은 “게임 전체를 표현하는 언어”가 아니다. 이벤트 핸들러 안의 대입, 산술/비교, 조건, 제한된 횟수 반복, 함수 호출, 변수 선언을 지원한다.

```text
시작했을 때
    플레이어의 이동 속도를 8로 설정

오른쪽 키를 누르고 있는 동안
    플레이어를 오른쪽으로 이동
```

```text
on start:
    player.speed = 8

on key.right.held:
    player.move(x: 1)
```

이 예시에서 `player`, `speed`, `key.right.held`, `move`는 화면 문자열로 저장하지 않는다. 각 식별자는 `SymbolId`와 선택적 `MemberId`를 참조한다. 표시 언어를 바꿔도 동일 심볼을 가리킨다.

### 제안 AST

```text
ScriptDocument {
  schemaVersion, languageVersion, scriptId,
  nodes: [Node], symbols: [SymbolDeclaration],
  editorMetadata, sourceMapMetadata
}

Node = EventHandler | Block | Let | Assign | If | ForEach | Repeat |
       Call | Return | Literal | SymbolRef | MemberRef | Unary | Binary
```

모든 노드는 안정적인 `NodeId`, 타입 판정 후의 `TypeId`, 원본 범위(source span), 필요한 경우 UI 주석을 가진다. 편집기 위치·접기 상태는 `editorMetadata`에 둬 실행 의미와 분리한다. 이벤트 종류는 안정된 enum/심볼 ID(예: `Input.KeyHeld`)이며, “오른쪽 키를 누르고 있을 때”는 로컬라이즈 가능한 표시다.

### 한글/코드 왕복 규칙

- 한글 편집기는 자유 문장 해석기가 아니다. 필드별 타입, 자동완성, 유효하지 않은 조합 안내를 제공한다.
- Code mode 문법은 모호하지 않은 정식 문법으로 정의한다. formatter는 정규 공백·들여쓰기를 출력한다.
- 지원되는 문법은 parse → AST → pretty-print → parse가 동일한 정규 AST를 만들어야 한다. 노드 ID, 심볼 ID, 값, 제어 흐름이 보존되는지 비교한다.
- 텍스트 편집 중 문법이 깨진 상태는 임시 버퍼로 유지하고, 마지막 유효 AST를 조용히 덮어쓰지 않는다. 전환 전에 오류 위치와 “텍스트 초안을 유지/마지막 유효 상태로 돌아가기”를 보여준다.
- 알 수 없는 노드/필드가 있는 최신 문서는 원본 JSON 조각을 보존하는 `UnknownNode`로 읽는다. 구버전은 임의로 지우지 않고 “지원되지 않는 기능이 있어 편집 제한” 상태로 연다.
- 실행 전에 parse, resolve, typecheck, capability check를 통과해야 한다. 블록 편집 화면을 매 프레임 순회해 실행하지 않는다.

## 4. Symbol·타입·언어 의미

초기 타입은 `Bool`, `Int`, `Float`, `String`, `Vec2`, `Color`, `Duration`, `EntityRef<T>`, `Array<T>`, `Option<T>`로 제한한다. 엔진 오브젝트 필드는 일반 문자열 경로 대신 typed `ComponentFieldId` 또는 명시적 API 호출로 연결한다. `Float`에는 UI에서 단위와 범위 힌트를 붙이고 내부 저장 단위는 문서화한다. 2D 위치와 물리는 월드 단위(픽셀 환산은 렌더링 경계)로 유지한다.

이름 해석은 scope를 따라간다: script locals → script parameters → scene/project symbols → built-in APIs. 표시 이름(한글/영문)은 locale별 테이블에 있고 영속 심볼 ID는 locale과 독립이다. rename/refactor는 모든 참조 ID를 찾아 안전하게 갱신한다. 컴파일 오류는 사용자 문장으로 설명하되 NodeId와 소스 범위를 통해 정확히 강조한다.

`Any`, 임의 포인터, 사용자 정의 native 호출은 초기 언어에서 제외한다. 타입 시스템은 에디터 자동완성, 저장 검증, VM 검증, 보안 경계를 공유한다. 타입을 우회하는 escape hatch는 “고급 네이티브 플러그인”처럼 별도 권한·빌드 설정 아래 둔다.

## 5. JM IR과 실행 백엔드

### 권장 JM IR

AST는 편집 친화적 트리이고, IR은 검증 및 실행 친화적인 명령 흐름이다. 초기 IR은 basic block과 명시적인 control flow를 갖는 typed, register 기반 3-address IR로 시작한다. 각 명령은 입력/출력 타입, NodeId, source span을 보유한다.

```text
event key.right.held:
  %player = scene.resolve_symbol @PlayerSymbol : EntityRef
  %delta  = input.axis @Horizontal : Float
  %pos    = transform.position %player : Vec2
  %next   = vec2.add %pos, ( %delta * 1.0 )
  transform.set_position %player, %next
  return
```

상수 접기, dead-code 제거, 불변값 전파 정도를 초기에 구현하고 각각 IR 검증기를 둔다. 프로필 특화 패스는 별도 pass group으로 선택하며 기본 의미를 바꾸지 않는다. IR 최적화 전후의 이벤트·입출력 결과 동등성 테스트를 유지한다.

### 초기 백엔드 비교 및 선택

| 전략 | 장점 | 비용/위험 | 판단 |
|---|---|---|---|
| AST 직접 해석 | 구현이 빠름, 진단과 디버깅 쉬움 | AST 방문 비용과 편집 구조 결합, 최적화 경계가 약함 | 언어 파서 프로토타입에서만 임시 사용 |
| register bytecode VM | 작고 이식성 높은 런타임, 명령 검증/실행 예산/핫 교체 관리 가능 | VM 구현·디버거 필요, native보다 느릴 수 있음 | **첫 사용자 스크립트 실행 백엔드** |
| WebAssembly | 여러 플랫폼의 배포 형식 후보, 별도 모듈 경계와 도구 사용 가능 | 호스트 API ABI, 메모리/빌드/디버깅 통합 작업 | Web 모드 또는 sandbox 모듈 후보 |
| LLVM ORC JIT | 기존 최적화·기계어 생성 경로와 lazy compilation 활용 가능 | 배포 크기, 빌드 복잡성, ABI·디버그 매핑·보안 유지비 | VM 계측에서 실제 CPU 병목이 확인된 뒤 선택 |
| 네이티브 AOT | 시작 중 컴파일 없이 높은 실행 성능, 플랫폼별 패키지 최적화 | 대상 플랫폼별 툴체인/서명/배포 복잡도, 동적 로딩 제약 | 정식 빌드/export 단계에서 추가 |

그러므로 `Samat → C` 경로를 기본으로 두지 않으며 자체 x86-64 생성기를 만들지 않는다. LLVM ORC는 성능 경로 후보지만 초기 필수 의존성으로 넣지 않는다. LLVM 공식 ORC 문서는 LLJIT와 지연 컴파일 구성을 제공하므로, 향후 실측 요구가 생겼을 때 검증할 수 있다 ([LLVM ORC](https://llvm.org/docs/ORCv2.html)). WebAssembly는 이식 가능한 컴파일 목표 및 비웹 임베딩도 목표로 설명하지만, 호스트 기능은 import로 제공되므로 JM API 어댑터가 필요하다 ([WebAssembly 목표](https://webassembly.org/docs/high-level-goals/), [보안 모델](https://webassembly.org/docs/security/)).

### VM 안전과 성능

- VM은 검증된 bytecode만 실행한다. 스택/레지스터 인덱스, 함수 인자, 타입, branch 대상, API 권한을 로드 시점에 검사한다.
- 이벤트당 instruction budget과 call-depth limit를 둬 무한 반복을 멈추고 문제 노드/호출 스택을 보여준다.
- 엔진 API 호출은 호스트가 제공한 함수 테이블로만 가능하게 한다. 임의 파일·네트워크·프로세스 실행은 기본 미허용이다.
- 스크립트는 매 프레임 컴파일되지 않는다. 편집 변경 시 해당 ScriptDocument만 재컴파일하고, 성공한 버전을 tick 경계에서 교체한다.
- profiling은 함수/노드별 누적 시간, 호출 횟수, instruction count를 기록한다. 최적화나 native 승격은 profiler 결과와 사용자가 승인한 기준으로 결정한다.

## 6. Scene과 Entity/Component

### 데이터 모델

오브젝트는 `Entity` 핸들과 조합된 컴포넌트로 표현한다. ID는 프로젝트 내 영속 UUID(파일 참조용)와 런타임 `(index, generation)` 핸들을 구분한다. 이름은 중복 가능하며 참조 키가 아니다.

초기 컴포넌트 후보: `Transform2D`, `SpriteRenderer`, `Camera2D`, `RigidBody2D`, `Collider2D`, `AudioSource`, `ScriptAttachment`, `TilemapRenderer`, `CanvasItem`. v0에서는 실제 필요한 것만 만든다. 기존 `GameObject` 데모 타입은 `Entity + Transform + DemoRenderable`로 옮길 수 있는 마이그레이션 단계를 둔다.

### 저장/런타임 구조

초기에는 sparse-set component pool을 사용한다. 타입별 배열과 entity-to-dense index map으로 iteration이 연속적이고 삭제는 swap-remove 할 수 있다. 구조 변경은 Scene 명령 큐를 통해 처리해 순회 중 메모리를 무효화하지 않는다. 숫자가 커지고 시스템 간 locality가 중요해지면 선택된 컴포넌트 그룹부터 archetype/chunk 저장을 비교한다. 처음부터 복잡한 archetype ECS를 강제하지 않는다.

Entity API는 `EntityHandle` 기반으로 하고 raw pointer를 스크립트/플러그인에 장기간 노출하지 않는다. 시스템은 `query<Transform2D, SpriteRenderer>()` 같은 선언형 query를 쓰고, 작업 실행 전에 필요 component를 확인한다. Editor undo/redo는 Scene mutation command와 stable Entity UUID를 사용한다.

## 7. Game Loop, Input, Events, Physics

### 시간과 프레임 순서

권장 순서는 다음과 같다.

```text
OS events → raw input snapshot → editor/game routing
          → fixed simulation ticks (scripts/events, physics)
          → transform propagation → render extraction → GPU submit → present
```

물리·게임 규칙은 기본 60Hz 고정 timestep을 accumulator로 실행한다. 렌더링은 가변 프레임 주기로 돌리고, physics transform의 이전/현재 값을 보간한다. 한 프레임의 누적 시간을 제한해 탭 전환 후 수십 개의 밀린 tick을 한꺼번에 실행하지 않게 한다. 최소 성능 기준은 평균 FPS가 아니라 p50/p95/p99 frame time, simulation tick time, input-to-visible latency로 측정한다.

Box2D는 2D physics provider 뒤에 두고 엔진의 월드 단위, Entity 매핑, contact event, 디버그 렌더링을 어댑터가 담당한다. Box2D 문서는 고정 timestep을 권장하고 일반 게임에서 1/60초가 높은 품질의 흔한 선택이라고 안내한다 ([Box2D 시뮬레이션](https://box2d.org/documentation/md_simulation.html), [Hello Box2D](https://box2d.org/documentation/hello.html)). Box2D의 결정성 보장이 전체 앱의 결정성까지 뜻하는 것은 아니므로, 이벤트 순서·난수 seed·스크립트 API·부동소수점 옵션도 별도로 관리한다.

### 입력 모델

`InputSnapshot`은 tick마다 키/마우스의 held, pressed, released 상태와 timestamp를 가진다. OS 입력 이벤트는 한 번 정규화한 뒤 에디터 UI와 게임 포커스 정책으로 분배한다. 액션 맵(`move_left`, `jump`)을 키보드/마우스/패드 매핑과 분리한다. 에디터가 포커스를 가진 동안 게임 입력이 소비되는지 명확한 실행 모드 표시로 알린다. 리플레이를 위해 raw event 대신 tick에 적용된 action snapshot을 기록할 수 있게 설계한다.

엔진 이벤트 큐에는 입력, 물리 접촉, 씬 로드, 사용자 정의 이벤트를 순서와 tick 번호를 포함해 넣는다. 한 이벤트 처리 중 같은 큐를 재귀적으로 호출하지 않는다. 이벤트 발생 및 스크립트 핸들러 실행의 고정 순서를 문서화한다.

## 8. 렌더링·카메라·2D/3D

초기 렌더링은 현재 OpenGL 3.3 경로로 2D 씬을 실용화한다. `Renderer` 인터페이스의 scene extraction 단계에서 컴포넌트를 GPU draw data로 변환하고, renderer backend는 배치·상태 정렬·리소스 업로드를 담당한다. Scene이나 VM이 GL 호출을 직접 하지 않는다.

2D 렌더러 초기 우선순위: atlas 기반 Sprite, camera/viewport, alpha blending, draw batching, tilemap chunk, scissor UI, 기본 shader/material. 현재 3D 큐브 샘플은 별도 `JMEngine3DPreview` capability/demo로 유지할 수 있지만, 제작용 3D 목표는 2D 편집·저장·실행 흐름이 완성된 뒤 별도 milestone로 시작한다. 이는 사용자의 Unity급/3D 관심을 폐기하는 뜻이 아니라, 문서의 초기 2D 검증 목표와 맞춰 제작 범위를 쪼개는 결정이다.

렌더러 API는 향후 WebGL/WebGPU나 Vulkan/Direct3D 구현이 가능하도록 플랫폼 시스템과 분리하되, 당장은 OpenGL 한 backend만 지원한다. backend portability를 이유로 초기부터 가장 낮은 공통분모 기능만 제한하지 않는다.

## 9. Animation, Audio, UI, Assets

- Animation은 `AnimationClip`(시간축·채널), `Animator`(상태/파라미터), renderer가 소비하는 최종 pose로 나눈다. 먼저 sprite sheet/frame animation부터 구현한다.
- Audio는 asset handle과 AudioSource component 중심으로 설계한다. callback thread에서는 allocation/scene access를 금지하고 lock-free command buffer 또는 제한된 오디오 큐를 사용한다.
- 게임 UI는 에디터 ImGui와 분리된 retained-mode 또는 즉시모드 runtime UI API로 제공한다. 같은 컴포넌트 포맷을 쓰더라도 편집기 widget이 게임에서 실행되지 않게 한다.
- Asset database는 UUID, type, source URI, import settings, content hash, dependency graph, importer version을 관리한다. 원본 파일과 읽을 수 있는 설정은 저장하고 GPU texture/mesh cache는 재생성 가능한 파생 데이터로 둔다.
- 로딩은 먼저 동기 경로로 정확성을 확보하고, 이후 작업 큐, background decode, streaming으로 확장한다. GPU resource 생성은 renderer가 소유한 thread 정책을 따른다.

## 10. Project Mode와 Optimization Profile

`ProjectMode`는 제품 도메인/기능 세트/내보내기 타깃을 정의한다. `OptimizationProfile`은 품질 목표와 workload 가중치다. 별도 객체이며 서로 덮어쓰지 않는다.

```json
{
  "projectMode": "game",
  "target": { "platform": "windows", "architecture": "x64" },
  "profile": {
    "workload": "2d-platformer",
    "priorities": ["input-latency", "stable-frametime"],
    "targetFps": 60,
    "memoryBudgetMb": 1024
  }
}
```

모드는 입력 액션, 런타임 서비스, export pipeline, 기본 프로젝트 템플릿을 선택한다. 프로필은 분석·batch 크기·asset 압축·quality preset의 후보를 조정한다. 설정을 생략했을 때는 `Game + balanced + 60 FPS`처럼 예측 가능한 기본값을 제공한다. 모드별 코드가 런타임 곳곳에 흩어지는 대신 capability registry와 빌드 preset에서 차이를 만든다.

첫 구현에서 Simulation/Web/Animation을 선언만 해두고 비어 있는 메뉴를 노출하지 않는다. `project.jm` schema에는 확장 가능한 mode ID를 허용하되, 실제 생성 UI는 지원 가능한 모드만 제공한다.

## 11. Adaptive Optimizer 정책

Adaptive Runtime은 자동으로 사용자 코드를 몰래 바꾸는 기능이 아니다. 세 단계로 구현한다.

1. **Observe:** 병목, 반복 수, allocation, overdraw, asset 크기를 계측한다.
2. **Explain:** “Sprite 400개가 개별 draw call을 만듭니다. atlas batching 후보를 적용하면 draw call이 줄 수 있습니다.”처럼 근거·영향·한계를 제시한다.
3. **Apply:** 자동 적용 가능한 변경은 새 branch/undoable transaction으로 만들고 결과를 동일 장면에서 비교한다.

최적화 제안은 `RuleId`, 대상 Node/Asset/Entity, profile 조건, 사전 조건, 예상 영향, 위험, 적용 diff, 취소 방법, 실제 전후 측정치를 가진다. runtime semantics에 영향을 주는 최적화는 동등성 검사 없이는 자동화하지 않는다. 프로필별 optimization pass는 컴파일 캐시에 프로필 hash를 포함해 다른 프로필의 결과를 재사용하지 않는다.

## 12. 에디터 구조 및 UX

### 화면 구조

초기 화면의 사용자용 이름은 전문 용어보다 행동 중심으로 한다.

```text
상단:  파일  실행 ▶ / 정지 ■  2D 장면  프로젝트
좌측:  장면 (오브젝트 목록, 추가)
중앙:  게임 화면 (선택/이동 gizmo, 실행 미리보기)
우측:  속성 (위치, 크기, 모양, 물리, 고급 설정)
하단:  코드 / 오류·출력 / 성능
```

내부 타입명은 Entity, Component, Rigidbody여도 사용자 기본 화면은 “오브젝트”, “모양”, “물리” 같은 용어로 시작한다. 고급 모드를 켜면 내부 설정과 실제 타입명이 드러난다. 패널 탭과 저장 포맷은 별개다.

현재 ImGui Hierarchy/Inspector는 이 UX 검토용 초기 쉘이다. 이후 Scene object 데이터에 연결된 panel 모델로 정리하고, UI 업데이트에서 렌더러/프로젝트 로직을 직접 소유하지 않게 한다. 실행 중에는 편집 씬과 play-world를 분리 복제하거나 undo journal snapshot을 사용한다. Stop 때 실행 중 데이터 변경을 자동으로 원본 씬에 합치지 않는다.

### 한글 구조화 코드 편집기

한글 모드는 가로로 읽히는 statement row editor로 만든다. 각 문장은 고정된 문법 템플릿, 타입 필드, Symbol picker, numeric input으로 나뉜다. 키보드 입력으로 검색/선택 가능하고, `플레...` 입력 시 symbol catalog에서 “플레이어”, “플레이어 위치”, “플레이어 속도” 같은 후보를 보여준다. suggestion은 심볼 종류와 타입을 함께 표시한다.

코드 모드로 바꾸면 같은 AST를 정식 JM syntax로 보여준다. 변수 이름의 영문 alias는 project locale map에서 제공하며, 선택한 display alias와 stable ID를 함께 표시할 수 있다. 두 모드 사이의 저장·undo 단위는 AST edit transaction이다.

### 자동완성·진단

자동완성은 언어 서버처럼 `ScriptSnapshot + cursor + symbol index`를 입력으로 받는다. AST 부분 파싱이 실패해도 버퍼를 보존하고 syntax recovery node로 범위를 표시한다. 진단은 parser, resolver/type checker, verifier, runtime으로 분류되고 각각 문장 원본 범위로 돌아간다.

## 13. Hot Reload와 디버깅

스크립트 편집은 컴파일 성공 시 실행 중 world의 고정 tick 경계에서 새 bytecode 버전을 설치한다. event handler 실행 중인 함수 frame은 이전 코드 버전에서 끝나고 이후 invocation부터 새 코드를 사용한다. 지역 변수 구조가 달라진 경우 frame을 강제 변환하지 않고 다음 invocation부터 변경된다.

Scene component 변경은 기본적으로 Play 상태와 편집 상태를 분리한다. Inspector의 “실행 중 변경”은 runtime override 표시로 나타내고, 적용/폐기/특정 속성만 저장 중 하나를 명시적으로 선택하게 한다. 디버거는 breakpoint(NodeId), step over, call stack, local value, watch expression, event trace를 제공한다. NodeId가 유지되지 않는 경우 source map의 structural match를 쓰되 breakpoint 이동을 알린다.

## 14. 프로젝트·씬 파일 포맷

프로젝트는 `project.jm` manifest와 사람이 읽을 수 있는 파일들로 구성한다.

```text
MyGame/
├─ project.jm
├─ assets/       # 원본과 import 설정
├─ scenes/*.scene
├─ objects/*.jmobject
├─ scripts/*.samat.json   # canonical AST
├─ settings/*.json
└─ .jm/cache/    # 무시 가능한 파생 데이터
```

manifest 예:

```json
{
  "formatVersion": 1,
  "engineVersion": "0.4.0-dev",
  "projectId": "uuid",
  "name": "My Game",
  "projectMode": "game",
  "profile": { "workload": "2d-platformer", "targetFps": 60 },
  "startupScene": "scenes/main.scene"
}
```

각 파일은 `formatVersion`, stable ID, 기본값 누락 처리 규칙을 가진다. deterministic key ordering과 UTF-8을 사용한다. Git diff에 불필요한 노이즈를 줄이기 위해 런타임 생성 ID를 저장 순서에 쓰지 않고 stable UUID를 쓴다. 대형 바이너리 에셋은 일반 텍스트가 아니어도 되지만 원본 경로와 import 설정은 보존한다.

마이그레이션은 `N → N+1` 순서로 자동 수행하며 원본 백업 또는 undo 가능한 임시 복사본을 만든다. 신버전 필드를 구버전 엔진이 발견하면 삭제하지 않고 읽기 전용 모드 또는 unknown field 보존으로 연다. 씬 저장은 전체 프로젝트를 매번 다시 쓰지 않고 수정된 문서만 atomic temp-file replace한다.

## 15. 확장·플러그인 구조

에디터 확장, asset importer, runtime native module을 구별한다. v0.x에서는 공식 public ABI가 아니라 C++ 소스 확장/내부 interface부터 사용한다. C++ ABI는 컴파일러/런타임 버전에 민감하므로 안정 ABI를 약속하지 않는다.

사용자 제작 plugin은 manifest에 ID, 버전, capability, entry point, engine API range를 선언한다. 다운로드 plugin은 기본 비활성·승인 후 설치, project 단위 allowlist, 서명/해시 확인, 사용자 데이터 백업을 지원한다. 격리된 script plugin은 Wasm 같은 sandbox 모듈 후보로 두되, capability import를 좁힌다. 네이티브 plugin은 신뢰 코드로 취급하고 프로세스 격리 옵션 또는 위험 표시를 제공한다.

## 16. 보안과 안정성

- Samat는 C++ 포인터, 임의 메모리, 임의 syscall에 접근하지 않는다.
- asset path는 프로젝트 root 밖으로 탈출하는 `..`와 symlink 정책을 검사한다.
- 압축 파일/이미지/모델 importer는 크기 제한, 메모리 예산, 취소, 버전 명시를 갖는다.
- 사용자 스크립트는 instruction budget, recursion limit, per-frame time budget을 적용한다.
- 저장 파일은 schema validator를 거치고, 손상 데이터 복구는 원본 백업과 함께 진행한다.
- 네트워크/멀티플레이 기능을 추가할 때 script capability, 연결 대상, 저장 데이터 권한을 별도 모델로 정의한다.
- 에디터 crash recovery를 위해 변경 journal/autosave를 사용하고, 안정화 후 crash dump와 최소 재현 프로젝트 수집은 명시적 동의를 받는다.

Wasm은 sandbox와 host import 경계를 제공하는 실행 형식 후보지만, 그것만으로 전체 애플리케이션 보안이 끝나는 것은 아니다. Wasm embedding은 제공하는 import 집합에 따라 권한이 정해진다 ([Wasm 보안](https://webassembly.org/docs/security/)). 초기 VM 역시 동일하게 capability API를 검증해야 한다.

## 17. 성능 병목과 관측

| 예상 병목 | 원인 | 초기 계측/대응 |
|---|---|---|
| 편집 UI | 큰 hierarchy 매 프레임 문자열/위젯 생성 | visible row clip, 변경분 갱신, stable IDs |
| 스크립트 VM | 객체별 동적 조회·박싱·호스트 호출 | typed registers, property ID binding, node/function profiler |
| ECS 순회 | pointer chasing, 잦은 구조 변경 | sparse-set 연속 풀, structural command buffer, query 시간 |
| 렌더링 | texture/state 변경과 개별 draw | atlas, material key sort, batch 수·GPU time 측정 |
| 물리 | 많은 contact와 과도한 solver substep | 고정 tick, contact count, step ms, profiler 경고 |
| asset import | main-thread decode 및 중복 변환 | hash cache, worker decode, import memory/time budget |
| 씬 저장 | 매 변경 전체 파일 직렬화 | 문서 단위 dirty tracking, debounce, atomic save |

모든 최적화는 “프레임이 빨라졌다” 대신 장면 규모, 하드웨어, build mode, p50/p95, 메모리 등을 함께 기록한다. 저사양 PC를 포함한 benchmark scene이 없으면 backend 선택과 성능 주장을 보류한다.

## 18. 비게임 제작 모드와 3D 확장

모드별 extension point는 다음과 같이 유지한다.

| Mode | 중심 서비스 | profile 예 |
|---|---|---|
| Game | fixed simulation, input, physics, real-time renderer | latency, frame pacing, object count |
| Animation | timeline, clip graph, preview cache, final render queue | preview responsiveness, final quality, cache size |
| Web | web export, bundle graph, asset compression, lazy loading | startup size/time, request count, cache policy |
| App | UI/event lifecycle, platform services | startup, response time, memory |
| Simulation | batch execution, deterministic data pipeline, worker scheduler | throughput, accuracy, parallelism |
| Custom | user-selected capability set | explicit target metrics |

공통 AST/Scene/Asset/Build manifest가 모든 모드를 만족시키려 무리하게 뭉개지 않는다. mode-specific component는 namespace와 schema module로 분리하고, 공유 가능한 renderer/resource API만 재사용한다. 3D는 공통 Transform/Entity/Asset/Renderer 추상화 위에 mesh, material, light, camera component를 추가한다. 2D 좌표가 Z=0인 3D 구현으로만 표현되지 않게 2D semantics도 보존한다.

## 19. 단계별 구현 계획

### v0.04 — 이미 있는 편집기 실험

현재 구현을 기준점으로 고정: 2D/3D 모드, 카메라 조작, 큐브/스프라이트, hierarchy/inspector, 실행 중 회전 데모. 이 코드는 제품 런타임/저장 시스템으로 승격하기 전 리팩터링 대상이다.

### v0.05 — 프로젝트/Scene 모델과 저장 (초기 구현됨)

`project.jm`, Scene object stable ID, Transform/Sprite component, 새 프로젝트/저장/열기, schema version을 구현했다. 현재 편집기는 `project.jm`, `scenes/main.scene`, `scripts/main.samat.json`을 기록한다. 정식 dirty indicator와 저장 후 재실행 end-to-end 흐름은 후속 작업이다.

### v0.06 — 입력·물리의 한 줄 수직 기능

Input action map, fixed timestep, Box2D adapter, collider gizmo, collision event trace를 추가한다. 플레이어 사각형 하나가 좌우 이동하고 충돌하는 샘플이 목표다.

### v0.07 — Samat AST와 한글 구조 편집기 (작동 범위의 첫 세로 기능 구현됨)

`on key.right.held → player.move(x: 1)` 한 이벤트의 AST, 한국어 템플릿, 코드 formatter/parser, 오브젝트 참조와 JSON 저장을 연결했다. 아직 undo/redo와 일반화된 SymbolId/Type System은 없다. 다음 단계에서 문법을 무리하게 늘리기보다 오류 진단·왕복 검증·stable symbol 모델을 강화한다.

### v0.08 — 코드 모드 왕복 검증

동일 AST를 정식 텍스트 문법으로 출력·파싱하고 structural equality를 검사한다. syntax error 중에도 버퍼와 마지막 유효 AST를 보존한다. 그다음 변수/조건/함수 순으로 문법을 확장한다.

### v0.09 — 일반화된 IR/VM 및 디버깅

현재 오른쪽 키 이동을 검증하는 단일 move opcode와 제한된 bytecode 실행기가 있다. v0.09에서는 typed register JM IR, 포괄적인 bytecode verifier, 함수/분기/조건, instruction budget 오류 진단, breakpoint/source map, 안전한 hot reload로 이를 일반화한다.

### v0.10+ — 타일맵/애니메이션/성능/배포

Sprite atlas, tilemap chunk, animation, audio, export, profiler, profile 기반 보고를 순서대로 구현한다. LLVM/Wasm/JIT/native, 3D 제작, plugin, 기타 project mode는 계측 결과와 사용자 시나리오에 따라 별도 milestone으로 승인한다.

## 20. 프로토타입 수용 기준

최초 Samat vertical slice는 다음 기준이 모두 성립해야 완료로 본다.

1. 빈 프로젝트를 만들고 사각형 플레이어를 씬에 둔다.
2. 한글 구조화 모드에서 “오른쪽 키를 누르고 있는 동안 → 플레이어를 오른쪽으로 이동” 노드를 만들 수 있다.
3. 선택한 Code mode는 `on key.right.held: player.move(x: 1)`에 해당하는 문법을 보여준다.
4. AST structural hash가 한글→코드→한글 왕복 전후 동일하다.
5. 프로젝트 저장 후 재실행/재열기에도 동일한 script와 symbol 참조가 유지된다.
6. 실행 버튼으로 플레이어가 이동하고, 정지하면 편집 상태와 실행 상태가 명확히 구분된다.
7. 잘못된 텍스트는 마지막 유효 프로그램을 잃지 않고 오류 위치를 표시한다.
8. 이벤트 핸들러 runaway는 실행 예산 안에서 중단되고 오류 노드를 알려준다.

## 미결정 사항

이 문서에서 일부러 확정하지 않은 항목은 다음과 같다: OpenGL 이후 graphics backend, native plugin ABI, 네트워크 동기화 규칙, deterministic rollback 보장, Android/macOS 지원 시점, timeline/3D tool UX, 사용자 프로젝트 telemetry 기본값, LLVM 또는 Wasm의 실제 도입 시점. 이 결정들은 샘플 프로젝트, 배포 요구, 성능 측정 또는 보안 모델이 준비된 뒤 내린다.
