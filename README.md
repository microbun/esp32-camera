# GOOUUU ESP32-S3-CAM USB 摄像头

适用硬件：用户提供 IO 分布图中的 GOOUUU ESP32-S3-CAM，ESP32-S3 N16R8，OV2640。
摄像头通过 FPC 排线连接，传输协议为 DVP。

使用 ESP-IDF、乐鑫 `esp32-camera` 和 `usb_device_uvc`，电脑通过标准 UVC 接口接收 MJPEG。
新版使用 USB Bulk 传输，默认 **640×480、目标 30 fps**，无需配置 Wi-Fi。
电脑可以在下面四档 MJPEG 模式间切换，无需重新烧录：

| 分辨率 | 声明帧率 | 用途 |
| --- | --- | --- |
| 640×480（默认） | 30 fps | 清晰度与流畅度的折中 |
| 800×600 | 30 fps | 更多细节 |
| 1280×720 | 15 fps | 高清画面，接受较低帧率 |
| 320×240 | 30 fps | 优先流畅度 |

表内是 USB 模式的目标帧率，实际帧率取决于 JPEG 大小、光照和 USB 吞吐量。
S3 的原生 USB 是 Full-Speed，不能按普通 USB 2.0 高清摄像头的带宽估计。
VGA/SVGA 的 JPEG quality 从旧版的 15 调到 12（数值越小，压缩损失越少）；
帧缓冲扩为 192 KiB，并使用三个 PSRAM 摄像头缓冲。
OV2640 输入时钟使用数据手册给出的 24 MHz，本板实测 VGA 帧率从 20 MHz 时的
约 25 fps 提升至约 30 fps。

## USB 接口

按提供的图片，摄像头朝上、USB 接口朝下时：

- 左侧 **OTG**：连接电脑，输出 USB 摄像头视频；首次也可以用它烧录。
- 右侧 **TTL**：USB 转串口，用于烧录和查看 115200 波特率日志。

UVC 启动后，OTG 上原有的 `/dev/ttyACM0` 消失是正常现象，电脑会识别为
`GOOUUU OV2640 USB Camera`。视频设备编号由电脑分配。
运行日志通过 TTL 接口输出。

## 编译和烧录

在 VS Code 的 PlatformIO 中选择 `goouuu-s3-cam` 环境，点击 Build、Upload。
也可以在 PlatformIO 终端执行：

```bash
pio run -e goouuu-s3-cam
pio run -e goouuu-s3-cam -t upload --upload-port /dev/ttyACM0
```

上面的端口适用于首次通过 OTG 烧录；通过 TTL 烧录时，将端口替换为实际的
`/dev/ttyUSB*`。如果同时连接了其他开发板，请明确指定烧录端口。

TTL 日志：

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

启动成功时，日志包含 `PSRAM: 8388608 bytes`、`First JPEG captured` 和
`USB camera ready`。如果 PSRAM 初始化、传感器识别或首次采集失败，程序会输出
对应错误，不启动 UVC。

## 在电脑上查看画面

- Windows：打开“相机”，或在 OBS 的“视频采集设备”中选择此设备。
- Linux：在摄像头软件中选择此设备，先选择 MJPEG，640×480、30 fps。
- macOS：在支持外接 UVC 摄像头的软件中选择此设备。

Linux 安装了 `v4l-utils` 和 FFmpeg 后，可以先找出设备编号，再查看画面：

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
ffplay -fflags nobuffer -flags low_delay -f v4l2 -input_format mjpeg -video_size 800x600 -framerate 30 /dev/video0
```

将 `/dev/video0` 替换为本开发板对应的视频节点；部分系统还会为同一摄像头建立
元数据节点，选择具有 Video Capture 能力的节点。
预览窗口中按 Q 或关闭窗口退出；一个采集程序占用摄像头时，先关闭它再打开其他程序。

## 摄像头引脚

与提供的核心板 IO 图一致，定义在 `include/camera_pins.h`：

| OV2640 信号 | ESP32-S3 GPIO |
| --- | --- |
| SIOD / SCCB SDA | 4 |
| SIOC / SCCB SCL | 5 |
| XCLK | 15 |
| VSYNC | 6 |
| HREF | 7 |
| PCLK | 13 |
| Y2 / D0 | 11 |
| Y3 / D1 | 9 |
| Y4 / D2 | 8 |
| Y5 / D3 | 10 |
| Y6 / D4 | 12 |
| Y7 / D5 | 18 |
| Y8 / D6 | 17 |
| Y9 / D7 | 16 |
| PWDN / RESET | -1（未由 GPIO 控制） |

原生 USB 使用 GPIO19（D−）和 GPIO20（D+），与上述摄像头引脚不冲突。
图像方向可以用 `CAM_VFLIP`、`CAM_HMIRROR` 调整，再重新编译烧录。

## 常见问题

- **串口 Permission denied**：本机终端执行
  `sudo setfacl -m u:"$USER":rw /dev/ttyACM0`，端口按实际替换。
  临时权限在重新插拔或 USB 设备重新枚举后可能失效。
- **烧录后找不到原来的串口**：OTG 已切换为 UVC。可改用 TTL 接口烧录；
  或按住 BOOT，按下并松开 RST，再松开 BOOT，从 OTG 进入 ROM 下载模式重新烧录。
- **没有摄像头设备**：检查是否插入 OTG 口、数据线是否支持数据，随后检查 TTL 日志。
- **Camera init failed / Initial capture failed**：断电后检查摄像头排线方向和锁扣，
  确认传感器确实为 OV2640、板型与上述引脚一致。
- **修改 sdkconfig.defaults 未生效**：PlatformIO 的 `sdkconfig.goouuu-s3-cam`
  是生成的完整配置。可通过 `pio run -e goouuu-s3-cam -t menuconfig` 修改配置。
  摄像头模式与 USB 描述符共用 sdkconfig 参数，修改后重新编译即可同步生效。
  新分辨率必须是 OV2640 和驱动共同支持的尺寸。

## 增强版上板验证（2026-10-23，Bulk、24 MHz）

用户确认镜头无遮挡后，在同一块开发板、OV2640 和本机 Linux 上逐档采集约 8 秒。
FFmpeg 解码后以微秒时间戳
计算实际帧率，并为每个解码帧计算哈希；四档均正常退出，无解码错误，时间戳递增，
各档的帧哈希均不同。

| 分辨率 | 解码帧数 | 测得平均帧率 |
| --- | --- | --- |
| 640×480 | 251 | 31.25 fps |
| 800×600 | 251 | 31.25 fps |
| 1280×720 | 127 | 15.84 fps |
| 320×240 | 249 | 31.09 fps |

这些是包含切换启动阶段的短时均值，实际使用按 30/30/15/30 fps 四档理解。
当前场景以近处、低纹理画面为主，复杂画面产生更大的 JPEG，可能降低实际帧率。
结果和日志保存在 `.pio/validation/unobstructed/`，包括 `results.json`、各档的 `.framehash`
与 FFmpeg 日志、`serial.log`，以及同次采集中保存的各档 JPEG 样张。
同一最终配置的前一轮结果保存在 `.pio/validation/performance-final/`。
20 MHz 对照结果保存在 `.pio/validation/bulk/`，当时 VGA 实测 25.22 fps。

相对于初版，默认模式的像素数从 76,800 提高到 307,200，帧率从约 15 fps 提高到约 30 fps。
本机预览选用 800×600、30 fps，像素数为初版的 6.25 倍；固件默认模式仍为 VGA。

## 初版上板验证（2026-10-23，320×240/15 fps 等时传输）

- PlatformIO 编译通过，固件烧录完成并通过 Flash 哈希校验。
- Linux 识别为 `303a:8000 GOOUUU OV2640 USB Camera`，视频采集节点为 `/dev/video0`。
- FFmpeg 成功解码连续 150 帧 MJPEG（320×240），约 9.82 秒，约 15 fps。
  输出使用微秒时间刻度，验证完成时没有解码错误，重复/丢弃计数均为 0。
- 已验证关闭后重新打开采集、保存单帧 JPEG，并打开桌面实时预览窗口。
- 本机测试图：`.pio/validation/preview.jpg`；采集进度：`.pio/validation/capture-progress.txt`。
- 烧录前原始 16 MB Flash 已备份为 `.pio/backups/before-usb-camera.bin`。
  备份 SHA-256：`07cbd481665270d287500b2524b44304f0e0ba1abd9ba9032273141d69726400`。

初版可工作的 bootloader、分区表和应用固件另存于 `.pio/backups/qvga15/`。

`managed_components` 和完整 `sdkconfig` 是生成文件；依赖版本由 `src/idf_component.yml`
与 `dependencies.lock` 固定。根目录 CMake 处理了 UVC 1.3.1 的 PUBLIC 描述符源文件
在 PlatformIO 中重复编译的问题，未改动下载组件。

## 官方参考

- [USB UVC 组件](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/usb_device_uvc.html)
- [USB Webcam 示例](https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/device/usb_webcam)
- [ESP32 Camera 驱动](https://github.com/espressif/esp32-camera)
- [OV2640 数据手册，表 8 输入时钟](https://files.waveshare.com/wiki/common/OV2640DS_en.pdf)
