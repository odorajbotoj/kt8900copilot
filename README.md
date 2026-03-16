# kt8900copilot

ESP32-S3 remote controller for QYT-KT8900

Author: BG4QBF

服务器见仓库 [odorajbotoj/kt8900copilot-server](https://github.com/odorajbotoj/kt8900copilot-server)

## 简介

30 元硬件成本的简易语音远控.

使用 **ESP32S3 N16R8** , 带 TF 卡支持. 如需修改 / 关闭相关功能请自行编辑代码.

项目配置需要 **适当增加缓冲区** , 我个人将很多网络相关的 buf_size 都提高到了 16384 . 业务任务均放在 **SPIRAM** 上运行. ( 仓库中已包含 `sdkconfig` ).

音频的采集通过 **ADC** , 而播放依赖 **LEDC PWM** . 音频采样率为 **16000Hz** , ADC 配置为 12bit 左移四位填充到 **16bit** , PWM 配置为 **16bit** .

TF 卡中 `pcm/` 文件夹下可存储 **16kHz 16bit 单声道 原始数据** 音频, 可由服务器控制播发.

## KT-8900 RJ45 定义

从主机正面看, RJ45 八线从左到右依此为:

1. 数据: 手咪按键信号
2. 控制: 当电台接收到信号时, 该引脚被拉低. 否则维持 3.3V 左右电压
3. 麦克风+: 输入给电台的音频
4. 麦克风-: 与 GND 连通
5. PTT: 被拉低时, 电台发射
6. GND: 地
7. +8V: 电源
8. 音频输出: 0V 中心, Vpp 2V, 不受音量旋钮控制的音频输出, 可直接采集

以上端口若要引出, 请打开 43 号菜单 **中继转发模式**:

+ CARRI 接收到有效载波转发
+ CTDCS 接收到亚音信令转发
+ TONE 接收到单音音频信号转发
+ DTMF 接收到等于本机身份码的 DTMF 信令转发

## 外围电路

需要一些外围电路才能安全且正常地工作. 一些电容和电阻即可.

### CTRL

通过一个 **10Kohm** 电阻 *直接* 连到 **IO42** . 电阻的作用是限流防损坏.

```text
CTRL -- R:10K -- IO42
```

### PTT

*直接* 连接 **IO41** , 同时通过一个 **4.7Kohm** 电阻上拉到 3.3V . GPIO 需配置为 **开漏** 模式.

```text
PTT
|
+-- R:4K7 -- 3V3
|
IO41(OD)
```

### 单片机采集音频 ( ADC )

通过 **220Kohm** **100Kohm** 两个电阻进行直流加偏置, 一个 **5.1Kohm** 限流, 输入到 **IO1**

```text
3V3
|
R:220K
|
+-- IO1 -- R:5K1 -- AF
|
R:100K
|
GND
```

## 单片机输出音频 ( PWM )

本电路为重点. 需要滤除高频 PWM 噪声, 去直流偏置, 压缩到 Vpp 1V.

1. PWM 输出信号经过 **100Kohm** **220Kohm**分压
2. 分压之后信号经过 **0.1uF** **100Kohm** RC 高通滤波器 ( -3dB 截止频率约为 15Hz )
3. 一次滤波之后经过 *两组* **3.3Kohm** **2.2nF** (二阶) RC 低通滤波器 ( -3dB 截止频率约为 22kHz )
4. 过滤后的信号输出到电台

```text
PWM
|
R:100K
|
+-- R:220K -- GND
|
C:0.1uF
|
+-- R:100K -- GND
|
R:3K3
|
+ -- C:2.2nF -- GND
|
R:3K3
|
+ -- C:2.2nF -- GND
|
OUT
```

整体是一个分压 + 带通的结构.

## 配置文件

在 TF 卡内存有配置文件

```text
wifi_ssid test
wifi_password examplepass
ws_server wss://example.net:1234/test
ws_key examplekey
timezone CST-8
ntp_server ntp.aliyun.com
adc_offset -2300
tx_limit_ms 60000
```

| 字段 | 解释 |
| --- | --- |
| wifi_ssid | 要连接的 WiFi 名称 |
| wifi_password | 要连接的 WiFi 密码 |
| ws_server | 要连接的服务器地址 |
| ws_key | 要连接服务器的验证密钥 |
| timezone | 时区, 中国 CST-8 |
| ntp_server | 授时服务器 ( NTP ) |
| adc_offset | ADC 偏移校准 ( 一般不用修改, -2300 即可) |
| tx_limit_ms | 发射限时, 超时强制松开 PTT 防止损坏电台. 单位毫秒. 建议小于 1 分钟 |

## AIGC 内容告知

本仓库部分代码参考 *ChatGPT* 结果, 主要为架构优化等等. 代码都经人工校验.

## 免责声明

使用请遵守相关法律法规, 无线电相关内容请遵循无线电管理规定.

## License

MIT
