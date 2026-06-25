# SenseCAP Indicator HA — 任务追踪

分支：`refactor/vertical-slice`

---

## 已完成

- [x] Phase 2a — 事件 manifest（view_data.h）、ARCHITECTURE.md、ha/README.md
- [x] Phase 2c — ha.h 拆分为 4 个域头文件
- [x] Phase 2d — scripts/test_ha_switch_protocol.py（11 个 MQTT 协议测试）
- [x] Phase 3 — HA 域垂直切片（main/ha/）
- [x] Phase 3 — WiFi 域垂直切片（main/wifi/）
- [x] Phase 3 — Sensor 域垂直切片（main/sensor/）
- [x] Phase 3 — Display 域垂直切片（main/display/）
- [x] Phase 3 — RP2040 域垂直切片（main/rp2040/）
- [x] Phase 3 — btn/mqtt/storage/cmd 域垂直切片
- [x] Build 问题修复（shim guard 碰撞、missing wifi headers）
- [x] 架构扫描工具 architecture_scan.py（持续验证域边界违规）

---

- [x] legacy shim/header cleanup：调用方直接 include 域头文件（2026-05-27）

---

- [x] **Phase 4** — lv_tileview 导航重构（2026-05-28，build 通过）
  - nav 模块：3 tile 水平 tileview（ha_data / ha_ctrl / ha_mix）
  - wifi / display / broker → lv_layer_top() modal
  - generated UI runtime 删除，SquareLine 依赖清零
  - 资源迁移至 main/assets/

---

---

- [x] **Phase 1** — LVGL v9 + ESP Component Manager（2026-05-28，build 通过）
  - main/idf_component.yml：lvgl/lvgl >=9.0.0 + espressif/esp_lvgl_port >=2.0.0
  - 删除 components/lvgl/（本地 LVGL 8 组件）
  - 重写 main/lv_port.c 使用 esp_lvgl_port v2 API
  - 修复 LVGL 9 API 变更：lv_event_send→lv_obj_send_event，lvgl/lvgl.h→lvgl.h
  - 修复 assets/ 图像描述符：always_zero→magic，LV_IMG_CF_TRUE_COLOR_ALPHA→LV_COLOR_FORMAT_NATIVE_WITH_ALPHA，新增 stride 字段
  - bsp_lcd: 新增 get_panel_handle / get_io_handle 供 esp_lvgl_port 注册 display

---

## 待办

- [ ] **合并到 main**
  - 完整 build 验证通过后 PR merge

---

## 架构原则（供 Agent 参考）

1. **垂直切片**：每个域自包含，互不依赖实现细节
2. **LVGL 边界**：只有 `lv_port`、`nav`、`*_view.c`、`*_screen.c` 拥有 LVGL 控件
3. **事件总线**：域间通信只通过 `view_event_handle`（VIEW_EVENT_BASE）
4. **architecture_scan.py**：每次改动后必须通过，CI 门槛
5. **Agent 友好**：每个文件的职责单一，改动范围可预测

## 文件布局

```
main/
  ha/       ← HA 域（mqtt/sensor/switch/config）
  wifi/     ← WiFi 域（model/view/list_screen/connect_screen）
  sensor/   ← Sensor 域（model/view）
  display/  ← Display 域（model/view）
  rp2040/   ← RP2040 UART/COBS 域
  btn/      ← 按钮 GPIO 域
  mqtt/     ← MQTT 总线域
  storage/  ← NVS 存储域
  cmd/      ← UART 控制台域
  nav/      ← lv_tileview 导航域
  assets/   ← LVGL 9 图像/字体资源
  util/     ← 工具函数
```
