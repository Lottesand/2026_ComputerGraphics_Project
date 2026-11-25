# 허브 열차 객차 모델

- `train_car.glb`: Unity AI(Nano Banana 2.1 → Tripo 3.1)로 생성한 AI 에셋. 객차 1칸.
- 크기: 7.0 x 3.0 x 2.6 (`CAR_LEN`, `CAR_H`, `CAR_W`와 같음). 중심이 원점, 앞면 +X, 위 +Y.
  `carWorld(k, i)` 행렬을 스케일 없이 곱하면 되도록 정점에 크기를 구워 두었다 (노드 변환 없음).
- 메시 1개, 8,257 삼각형, uint32 인덱스, POSITION / NORMAL / TEXCOORD_0 (탄젠트 없음).
- GLB에 내장된 2048 텍스처: 색(JPEG), ORM(JPEG, G=거칠기 B=금속), 노멀(PNG, OpenGL 방식).
- UV는 glTF 기준(왼쪽 위 원점). 이미지를 뒤집지 않고 올리면 그대로 맞는다.

## 적용할 때
- 현재 `Mesh`는 정점당 9 float이고 이미지/GLB 로더가 없다. UV 속성과 로더가 필요하다.
- 셰이더에서 PBR을 쓰지 않으면 색 텍스처가 어둡다 (평균 약 52/255). 밝기 보정이 필요하다.
- 창문 불빛과 발광 맵이 없다. `drawTrains()`의 발광 상자처럼 OpenGL에서 덧붙인다.
- 앞뒤가 모두 운전실 모양이라 중간 칸에서도 앞유리가 보인다.
- 허브 크기에 맞추느라 길이를 약 23% 줄였다.

## 적용
- `python tools/train/bake_train.py` 가 GLB에서 `train_car.mesh`와 1024 텍스처 3장을 만든다. 게임은 이 파일들만 읽는다.
- 그리기와 불빛(실내등, 전조등, 후미등, 행선판): `trainmodel.cpp`. 셰이더: `scene_fragment.glsl` 의 pattern 5, 6.
- 빨간 후미등이 +X 쪽에 있어 게임에서는 180도 돌려 그린다.
- Shift + 3: 모델 ↔ 예전 상자 열차 전환. 파일이 없으면 상자 열차로 그린다.
