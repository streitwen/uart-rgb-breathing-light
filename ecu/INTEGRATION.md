# 电控端接入步骤

本目录提供 STM32 HAL 参考代码。它默认：

- 串口：`huart1`
- 红色 PWM：`htim2` 的 `TIM_CHANNEL_1`
- 绿色 PWM：`htim2` 的 `TIM_CHANNEL_2`
- 蓝色 PWM：`htim2` 的 `TIM_CHANNEL_3`
- PWM 最大比较值：`1000`

## 1. CubeMX 配置

1. UART 配置为 `115200`、8 数据位、无校验、1 停止位、无流控。
2. 打开 UART 全局中断。
3. 三个 LED PWM 通道均开启。
4. PWM 定时器 `ARR` 建议设为 `999` 或代码中的 `BREATH_PWM_MAX` 对应值。
5. PWM 频率建议至少 1 kHz。

## 2. 加入工程

把 `breath_receiver.c` 和 `breath_receiver.h` 加入电控工程，并确保：

```c
BreathReceiver_Init(&huart1);
```

在 `main()` 的外设初始化完成后调用一次。

在主循环中调用：

```c
BreathReceiver_Tick(HAL_GetTick());
```

## 3. 修改硬件映射

打开 `breath_receiver.c`，根据实际板子修改：

```c
extern TIM_HandleTypeDef htim2;

#define LED_TIMER htim2
#define LED_R_CHANNEL TIM_CHANNEL_1
#define LED_G_CHANNEL TIM_CHANNEL_2
#define LED_B_CHANNEL TIM_CHANNEL_3
```

## 4. 处理已有回调

如果工程中已经有 `HAL_UART_RxCpltCallback`，不要再定义第二份。
把下面逻辑合并进已有回调：

```c
if (uart == s_uart) {
    BreathReceiver_OnByte(s_rx_byte);
    HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1u);
}
```

更简单的做法是删除参考文件中的回调，在电控已有回调里调用公共函数。

## 5. WS2812

本参考代码是三路 PWM 版本。如果灯珠是 WS2812：

1. 保留串口解析和 CRC 校验。
2. 把 `write_rgb()` 改成 WS2812 发送函数。
3. 常见 WS2812 写入顺序是 `G,R,B`，不要把串口协议改成 GRB。

## 6. 自测顺序

1. 用串口助手以 HEX 模式发送纯红帧 `A5 FF 00 00 90 1D`。
2. 灯应为纯红，一直保持。
3. 依次测试纯绿、纯蓝、白色、关闭。
4. 最后运行视觉发送程序，观察 2 秒一次的呼吸效果。
