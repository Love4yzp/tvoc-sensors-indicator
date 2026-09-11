# main/ AGENTS.md — ESP32-S3 应用层开发手册

本文件提供 `main/` 目录下的开发实操（新建界面、资源格式、常见编译错误速查）。
全局规则（构建命令、事件总线约定、LVGL 线程安全、禁区、验证清单）以
[根目录 AGENTS.md](../AGENTS.md) 为准。架构深入解读见
[ARCHITECTURE.md](ARCHITECTURE.md)。

技术栈：ESP-IDF v5.5.x，LVGL 9.5（ESP Component Manager 管理），FreeRTOS。

---

## 文件布局

```
main/
  sen5x/        SEN5x 域：MQTT 发布（topic/payload/status 状态机）
  sensor/       Sensor 域：RP2040 数据包解析 + 数值缓存 + 仪表盘 view
  ha/           HA 域：MQTT 客户端生命周期、broker 配置、legacy switch 协议
  wifi/         WiFi 域：扫描、连接、状态图标、AP 列表/连接 modal
  display/      Display 域：背光亮度、睡眠模式
  settings/     Settings 域：设置入口 modal（齿轮按钮）
  rp2040/       RP2040 UART/COBS 通信
  btn/          按钮 GPIO
  mqtt/         MQTT 总线（共享实例）
  storage/      NVS 读写
  cmd/          UART 控制台命令
  nav/          lv_tileview 导航（当前单 tile）
  ui/           UI 基础设施：ui_event（非阻塞事件）、ui_defer、ui_freeze_mon、ui_mem_pool
  assets/       图像和字体资源（LVGL 9 格式）
  util/         公共工具函数（cobs 等）

  main.c              入口
  indicator_model.c   全局模型初始化（各域 model_init 汇总）
  indicator_view.c    全局视图初始化（各域 view_init 汇总）
  indicator_enabler.h 功能开关（#include 各域头文件）
  lv_port.c           ⚠️ BSP 硬件边界，禁区见根 AGENTS.md
  view_data.h         事件 ID 清单
  view_data_types.h   事件数据结构体 + 生产者/消费者 manifest 注释
  home_assistant_config.h  MQTT broker 编译期默认值
  idf_component.yml   ESP Component Manager 依赖声明
```

域的文件命名因域而异，没有统一的 `<domain>_view.c` 规律。实际示例：

```
main/ha/        ha.h, ha_mqtt.c, ha_sensor.c, ha_switch.c, ha_config.c, ha_switch_screen.c
main/sen5x/     sen5x_mqtt.c, sen5x_mqtt.h
main/sensor/    sensor_model.h, sensor_model.c, sensor_view.h, sensor_view.c
main/wifi/      wifi.h, wifi_model.c, wifi_view.c, wifi_list_screen.c, wifi_connect_screen.c
main/display/   display.h, display_model.c, display_view.h, display_view.c
main/nav/       nav.h, nav.c
main/assets/    ui_img_*.c, ui_font_*.c（图像/字体资源）
```

共同规律：`*_view.c` / `*_screen.c` 只做 LVGL 控件操作；model/业务逻辑文件不持有 LVGL 对象。

---

## 导航系统（当前：单 tile + modal）

主屏是**单 tile**：SEN54 仪表盘是唯一 tile（`NAV_TILE_SEN5X`，`NAV_TILE_COUNT 1`，见 `nav/nav.h`）。
`NAV_TILE_HA_DATA / HA_CTRL / HA_MIX` 是历史别名，全部等于 `NAV_TILE_SEN5X`，仅为兼容旧引用而存在。

Settings、WiFi、Display、Broker 等界面都是建在 `lv_layer_top()` 上的 **modal**，
由屏幕上的按钮打开，没有横向滑动。

---

## 新建界面流程

### 类型 A：Modal 弹窗（默认选择）

绝大多数新界面（设置项、对话框、表单）都应该做成 modal，不占 tile：

```c
lv_obj_t *overlay = lv_obj_create(lv_layer_top());
lv_obj_set_size(overlay, 480, 480);
lv_obj_set_style_bg_color(overlay, lv_color_hex(0x1A1A1A), 0);
lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);   // 初始隐藏

// 显示：lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
// 隐藏：lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
```

参考实现：`main/settings/settings_view.c`（首选）、`main/wifi/wifi_view.c`、`main/display/display_view.c`、`main/ha/ha_config.c`。

带输入框的表单必须处理键盘遮挡：键盘弹出时压缩可滚动表单并用
`lv_obj_scroll_to_view_recursive()` 滚动到聚焦输入框，键盘收起时恢复。
参考 `_set_keyboard_visible()`（`main/ha/ha_config.c`）。

新域目录还需要（两步缺一不可）：

1. `main/CMakeLists.txt` 的 `DIRECTORIES_TO_INCLUDE` 加入新目录 ——
   不加则 `.c` 文件被静默忽略，build 通过但代码不执行；之后 `touch main/CMakeLists.txt` 再构建。
2. 在 `indicator_view.c` / `indicator_model.c` 调用对应的 `*_view_init()` / `*_model_init()`。

### 类型 B：全屏 Tile（仅当产品确实需要分页滑动）

1. `nav/nav.h`：`#define NAV_TILE_MY_PAGE 1`，并把 `NAV_TILE_COUNT` 同步 +1（否则新 tile 空白）。
2. `nav/nav.c`：给该 tile 设置非 `LV_DIR_NONE` 的滑动方向。
3. 同类型 A 的两步（CMakeLists + init 调用）。
4. view 里用 `nav_get_tile(NAV_TILE_MY_PAGE)` 拿父容器再建控件。

---

## LVGL 使用规则速查

| 规则 | 说明 |
|------|------|
| 访问 LVGL 必须持锁 | `lv_port_sem_take()` / `lv_port_sem_give()`（view 里发事件用 `ui_event_post()`，见 `ui/README.md`） |
| 字体声明 | `LV_FONT_DECLARE(ui_font_font0)` |
| 图像声明 | `LV_IMAGE_DECLARE(ui_img_ic_temp_png)` |
| 发送 LVGL 事件 | `lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL)` |
| include 路径 | `#include "lvgl.h"`（不是 `lvgl/lvgl.h`） |

### assets/ 图像格式（LVGL 9）

数据为 RGB565 色彩平面 + A8 alpha 平面，合计 3 字节/像素。LVGL 9 描述符格式：

```c
const lv_image_dsc_t ui_img_xxx = {
    .header.magic  = LV_IMAGE_HEADER_MAGIC,
    .header.cf     = LV_COLOR_FORMAT_RGB565A8,
    .header.flags  = 0,
    .header.w      = W,
    .header.h      = H,
    .header.stride = W * 2,        // RGB565 plane stride; alpha plane follows data
    .data_size     = sizeof(ui_img_xxx_data),
    .data          = ui_img_xxx_data,
};
```

---

## 常见编译错误速查

| 错误信息 | 原因 | 修复 |
|----------|------|------|
| `lvgl/lvgl.h: No such file` | 旧 LVGL 8 路径 | 改为 `#include "lvgl.h"` |
| `lv_event_send` 类型不匹配 | LVGL 8 API | 改为 `lv_obj_send_event(obj, event, NULL)` |
| `always_zero` 无此成员 | 旧图像描述符格式 | 用上方 assets 格式重写 |
| `LV_IMG_CF_TRUE_COLOR_ALPHA` 未声明 | LVGL 8 常量 | 改为 `LV_COLOR_FORMAT_RGB565A8` 并设置 `header.stride` |
| 新 tile 页面空白 | `NAV_TILE_COUNT` 未同步 +1 | nav.h 中 COUNT +1 |
| 新目录代码不执行 | 未加入 `DIRECTORIES_TO_INCLUDE` | 改 `main/CMakeLists.txt` 并 `touch` 它 |
| `missing and no known rule to make it` | 增删源文件后 GLOB 缓存过期 | `touch main/CMakeLists.txt` 再构建 |
| LVGL assert / crash | 未持锁访问 LVGL | 加 `lv_port_sem_take/give` |

---

## 各域文档

- `ha/README.md` — HA 域文件职责与事件流
- `sensor/README.md` — Sensor 域解析/缓存/事件流
- `wifi/README.md` — WiFi 域状态机与 modal 结构
- `display/README.md` — Display 域背光/睡眠
- `ui/README.md` — UI 线程模型与 `ui_event_post` / `ui_defer` 规则（改 UI 代码前必读）
