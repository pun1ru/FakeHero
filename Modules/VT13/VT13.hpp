#pragma once

#include <memory>

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: VT13 链路遥控解析模块：从 UART 接收 21 字节协议帧，向 CMD 输入控制量并发出事件 / VT13 link remote controller Module that receives 21-byte frames over UART, feeds control data to CMD and emits events
depends:
- 3SE-xrobot-dev/CMD
=== END MANIFEST === */
// clang-format on

#include <cstdint>

#include "CMD.hpp"
#include "crc.hpp"
#include "thread.hpp"
#include "uart.hpp"

/// 帧头第 1 字节
/// First byte of the frame header
#define VT13_FRAME_HEAD_0 (0xA9u)
/// 帧头第 2 字节
/// Second byte of the frame header
#define VT13_FRAME_HEAD_1 (0x53u)

/// 通道最小值
/// Minimum channel value
#define VT13_CH_VALUE_MIN (364u)
/// 通道中间值
/// Middle channel value
#define VT13_CH_VALUE_MID (1024u)
/// 通道最大值
/// Maximum channel value
#define VT13_CH_VALUE_MAX (1684u)

/**
 * @brief VT13 链路遥控解析模块。
 *        VT13 link remote controller Module.
 *
 * @details 从 UART 接收 21 字节协议帧，向 CMD 输入控制量，并把挡位开关、自定义按键、
 *          拨轮和键鼠变化作为事件发出。
 *          Receives 21-byte protocol frames over UART, feeds control data to CMD and
 *          emits switch, custom key, dial and keyboard and mouse changes as events.
 */
class VT13
{
 public:
  static constexpr std::size_t VT13_FRAME_SIZE = 21;  ///< 帧长度，单位字节
  ///< Frame length in bytes
  static constexpr std::size_t VT13_PAYLOAD_SIZE_FOR_CRC = 19;  ///< CRC 覆盖的字节数
  ///< Number of bytes covered by the CRC

  /**
   * @brief 控制源。
   *        Control source.
   */
  enum class ControlSource : uint8_t
  {
    VT13_CTRL_SOURCE_SW = 0x00,     ///< 遥控器模式 Remote controller mode
    VT13_CTRL_SOURCE_MOUSE = 0x01,  ///< 键鼠模式 Keyboard and mouse mode
  };

  /**
   * @brief 挡位开关位置与扩展事件 ID。
   *        Switch positions and extended event IDs.
   */
  enum class SwitchPos : uint16_t
  {
    VT13_KEY_RELEASE_L = 0x100,  ///< 自定义左键松开 Custom left key released
    VT13_KEY_PRESSED_L,          ///< 自定义左键按下 Custom left key pressed
    VT13_KEY_RELEASE_R,          ///< 自定义右键松开 Custom right key released
    VT13_KEY_PRESSED_R,          ///< 自定义右键按下 Custom right key pressed
    VT13_KEY_RELEASE_PAUSED,     ///< 暂停键松开 Pause key released
    VT13_KEY_PRESSED_PAUSED,     ///< 暂停键按下 Pause key pressed
    VT13_KEY_RELEASE_TRIG,       ///< 扳机松开 Trigger released
    VT13_KEY_PRESSED_TRIG,       ///< 扳机按下 Trigger pressed

    VT13_SW_POS_C = 0x00,  ///< C 档（上） Position C (up)
    VT13_SW_POS_N = 0x01,  ///< N 档（中） Position N (middle)
    VT13_SW_POS_S = 0x02,  ///< S 档（下） Position S (down)
    VT13_SW_POS_NUM = 3,   ///< 档位数量 Number of positions

    VT13_DIAL_UP_SHORT = 0x120,  ///< 拨轮上拨短触 Dial pushed up, short touch
    VT13_DIAL_UP_LONG,           ///< 拨轮上拨长触 Dial pushed up, long touch
    VT13_DIAL_DOWN_TOUCH,        ///< 拨轮下拨 Dial pushed down

    VT13_KEY_PAUSE_TOGGLE_ON = 0x130,  ///< 暂停键切换为 ON Pause key toggled ON
    VT13_KEY_PAUSE_TOGGLE_OFF,         ///< 暂停键切换为 OFF Pause key toggled OFF
    VT13_KEY_CUSTOM_L_TOGGLE_ON,       ///< 自定义左键切换为 ON Custom left key toggled ON
    VT13_KEY_CUSTOM_L_TOGGLE_OFF,  ///< 自定义左键切换为 OFF Custom left key toggled OFF
    VT13_KEY_CUSTOM_R_TOGGLE_ON,   ///< 自定义右键切换为 ON Custom right key toggled ON
    VT13_KEY_CUSTOM_R_TOGGLE_OFF,  ///< 自定义右键切换为 OFF Custom right key toggled OFF
  };

  /**
   * @brief 键盘与鼠标事件编码，`KEY_W` 到 `KEY_B` 与键盘位图的位序一致。
   *        Keyboard and mouse event codes; `KEY_W` to `KEY_B` follow the bit order of the
   *        keyboard bitmap.
   */
  enum class Key : uint8_t
  {
    KEY_W = static_cast<uint8_t>(SwitchPos::VT13_SW_POS_NUM),
    KEY_S,
    KEY_A,
    KEY_D,
    KEY_SHIFT,
    KEY_CTRL,
    KEY_Q,
    KEY_E,
    KEY_R,
    KEY_F,
    KEY_G,
    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,

    KEY_L_PRESS,
    KEY_R_PRESS,
    KEY_M_PRESS,
    KEY_L_RELEASE,
    KEY_R_RELEASE,
    KEY_M_RELEASE,
    KEY_NUM,
  };

  /**
   * @brief VT13 协议解包后的原始数据。
   *        Raw data unpacked from the VT13 protocol.
   */
  typedef struct
  {
    uint16_t ch_r_x;  ///< 右摇杆 X，364 到 1684
    ///< Right stick X, 364 to 1684
    uint16_t ch_r_y;  ///< 右摇杆 Y，364 到 1684
    ///< Right stick Y, 364 to 1684
    uint16_t ch_l_x;  ///< 左摇杆 X，364 到 1684
    ///< Left stick X, 364 to 1684
    uint16_t ch_l_y;  ///< 左摇杆 Y，364 到 1684
    ///< Left stick Y, 364 to 1684
    uint8_t sw;  ///< 挡位开关位置，0 到 2
    ///< Switch position, 0 to 2
    uint8_t pause;  ///< 暂停键状态
    ///< Pause key state
    uint8_t key_l;  ///< 自定义左键状态
    ///< Custom left key state
    uint8_t key_r;  ///< 自定义右键状态
    ///< Custom right key state
    uint16_t dial;  ///< 拨轮值，364 到 1684
    ///< Dial value, 364 to 1684
    uint8_t trig;  ///< 扳机状态
    ///< Trigger state
    int16_t x;  ///< 鼠标 X 方向移动量
    ///< Mouse movement along X
    int16_t y;  ///< 鼠标 Y 方向移动量
    ///< Mouse movement along Y
    int16_t z;  ///< 鼠标滚轮移动量
    ///< Mouse wheel movement
    uint8_t press_l;  ///< 鼠标左键状态
    ///< Left mouse button state
    uint8_t press_r;  ///< 鼠标右键状态
    ///< Right mouse button state
    uint8_t press_m;  ///< 鼠标中键状态
    ///< Middle mouse button state
    uint16_t key;  ///< 键盘按键位图，位序与 `Key::KEY_W` 到 `Key::KEY_B` 一致
    ///< Keyboard bitmap, bit order follows `Key::KEY_W` to `Key::KEY_B`
  } Data;

  /**
   * @brief 计算 Shift 组合键的事件 ID。
   *        Compute the event ID of a key combined with Shift.
   *
   * @param key 基础按键。
   *            Base key.
   * @return 加上 1 倍 `KEY_NUM` 的事件 ID。
   *         Event ID increased by 1 times `KEY_NUM`.
   */
  constexpr uint32_t ShiftWith(Key key)
  {
    return static_cast<uint8_t>(key) + 1 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 计算 Ctrl 组合键的事件 ID。
   *        Compute the event ID of a key combined with Ctrl.
   *
   * @param key 基础按键。
   *            Base key.
   * @return 加上 2 倍 `KEY_NUM` 的事件 ID。
   *         Event ID increased by 2 times `KEY_NUM`.
   */
  constexpr uint32_t CtrlWith(Key key)
  {
    return static_cast<uint8_t>(key) + 2 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 计算 Shift+Ctrl 组合键的事件 ID。
   *        Compute the event ID of a key combined with Shift+Ctrl.
   *
   * @param key 基础按键。
   *            Base key.
   * @return 加上 3 倍 `KEY_NUM` 的事件 ID。
   *         Event ID increased by 3 times `KEY_NUM`.
   */
  constexpr uint32_t ShiftCtrlWith(Key key)
  {
    return static_cast<uint8_t>(key) + 3 * static_cast<uint8_t>(Key::KEY_NUM);
  }

  /**
   * @brief 获取按键在键盘位图中的位掩码。
   *        Get the bit mask of a key in the keyboard bitmap.
   *
   * @param key 按键。
   *            Key.
   * @return 对应的位掩码；`KEY_W` 到 `KEY_B` 之外为 0。
   *         The bit mask; 0 outside `KEY_W` to `KEY_B`.
   */
  constexpr uint32_t RawValue(Key key)
  {
    const auto KEY_U = static_cast<uint8_t>(key);
    const auto KEY_W_U = static_cast<uint8_t>(Key::KEY_W);
    const auto KEY_B_U = static_cast<uint8_t>(Key::KEY_B);
    if (KEY_U < KEY_W_U || KEY_U > KEY_B_U)
    {
      return 0;
    }
    return 1u << (KEY_U - KEY_W_U);
  }

  /**
   * @brief 构造参数。
   *        Construction parameters.
   */
  struct Param
  {
    uint32_t task_stack_depth_uart = 1536;  ///< 接收线程栈深
    ///< Receive thread stack depth
    LibXR::Thread::Priority thread_priority_uart = LibXR::Thread::Priority::HIGH;  ///< 接收线程优先级
    ///< Receive thread priority
    uint32_t baudrate = 100000;  ///< Hero UART5 baudrate
    LibXR::UART::Parity parity = LibXR::UART::Parity::EVEN;
    uint8_t stop_bits = 2;
  };

  /**
   * @brief 构造 VT13，配置串口并创建接收线程。
   *        Construct VT13, configure the UART and create the receive thread.
   *
   * @param uart 连接 VT13 链路的串口。
   *             UART connected to the VT13 link.
   * @param cmd 接收控制数据的 CMD 实例。
   *            CMD instance that receives the control data.
   * @param param 构造参数。
   *              Construction parameters.
   */
  VT13(LibXR::UART& uart, CMD& cmd) : VT13(uart, cmd, Param{}) {}

  VT13(LibXR::UART& uart, CMD& cmd, const Param& param)
      : cmd_(&cmd), uart_(std::addressof(uart)), sem_(0), op_(sem_, 64)
  {
    uart_->SetConfig({param.baudrate, param.parity, 8, param.stop_bits});
    thread_uart_.Create(this, ThreadVT13, "uart_vt13", param.task_stack_depth_uart,
                        param.thread_priority_uart);
  }

  /**
   * @brief 获取 VT13 的事件对象。
   *        Get the event object of VT13.
   *
   * @return 事件对象引用。
   *         Reference to the event object.
   */
  LibXR::Event& GetEvent() { return vt13_event_; }

  /**
   * @brief UART 接收线程：同步帧头、拼帧、解析并输入给 CMD，并检查离线。
   *        UART receive thread: synchronizes on the header, assembles frames, parses and
   *        feeds them to CMD, and checks for the offline state.
   *
   * @param vt13 VT13 实例。
   *             VT13 instance.
   */
  static void ThreadVT13(VT13* vt13)
  {
    constexpr std::size_t RX_BUFFER_SIZE = 1;
    uint8_t rx_buffer[RX_BUFFER_SIZE] = {0};
    uint8_t frame_buffer[VT13_FRAME_SIZE] = {0};
    std::size_t frame_pos = 0;
    CMD::Data rc_data;

    while (1)
    {
      if (vt13->uart_->Read({rx_buffer, RX_BUFFER_SIZE}, vt13->op_) ==
          LibXR::ErrorCode::OK)
      {
        for (std::size_t idx = 0; idx < RX_BUFFER_SIZE; ++idx)
        {
          uint8_t rx_byte = rx_buffer[idx];

          /* 字节流状态机：先同步2字节帧头，再累积到完整21字节帧 */
          if (frame_pos == 0)
          {
            if (rx_byte != VT13_FRAME_HEAD_0)
            {
              continue;
            }
            frame_buffer[frame_pos++] = rx_byte;
          }
          else if (frame_pos == 1)
          {
            if (rx_byte == VT13_FRAME_HEAD_1)
            {
              frame_buffer[frame_pos++] = rx_byte;
            }
            else
            {
              frame_pos = 0;
              /* 若当前字节本身也是首字节，则直接作为下一帧起点继续对齐 */
              if (rx_byte == VT13_FRAME_HEAD_0)
              {
                frame_buffer[frame_pos++] = rx_byte;
              }
            }
          }
          else
          {
            frame_buffer[frame_pos++] = rx_byte;
            if (frame_pos < VT13_FRAME_SIZE)
            {
              continue;
            }

            frame_pos = 0;
            if (vt13->ParseRC(frame_buffer, rc_data) == LibXR::ErrorCode::OK)
            {
              /* 仅在完整且通过校验的帧上更新时间戳并下发控制数据 */
              vt13->last_time_ = LibXR::Timebase::GetMilliseconds();
              vt13->cmd_->FeedRC(CMD::RCInputSource::RC_INPUT_VT13, rc_data);
            }
          }
        }
      }
      vt13->CheckoutOffline();
    }
  }

  /**
   * @brief 校验并解析一帧，生成 CMD 控制数据并发出事件。
   *        Validate and parse one frame, produce the CMD control data and emit events.
   *
   * @param raw_data 21 字节原始帧。
   *                 Raw 21-byte frame.
   * @param output_data 解析得到的 CMD 数据。
   *                    Parsed CMD data.
   * @return 成功为 `ErrorCode::OK`；指针为空为 `PTR_NULL`；帧头、CRC 或数值范围不符为
   *         `CHECK_ERR`。
   *         `ErrorCode::OK` on success, `PTR_NULL` for a null pointer, `CHECK_ERR` when
   *         the header, the CRC or a value range does not match.
   */
  LibXR::ErrorCode ParseRC(const uint8_t* raw_data, CMD::Data& output_data)
  {
    if (!raw_data)
    {
      return LibXR::ErrorCode::PTR_NULL;
    }

    /* 固定帧头校验 */
    if (raw_data[0] != VT13_FRAME_HEAD_0 || raw_data[1] != VT13_FRAME_HEAD_1)
    {
      return LibXR::ErrorCode::CHECK_ERR;
    }

    /* CRC覆盖前19字节，帧尾2字节为校验值 */
    const bool CRC_OK = VerifyCRC(raw_data);
    if (!CRC_OK)
    {
      return LibXR::ErrorCode::CHECK_ERR;
    }

    Data curr_rc{};

    curr_rc.ch_r_x = static_cast<uint16_t>(ExtractBits(raw_data, 16, 11));
    curr_rc.ch_r_y = static_cast<uint16_t>(ExtractBits(raw_data, 27, 11));
    curr_rc.ch_l_x = static_cast<uint16_t>(ExtractBits(raw_data, 38, 11));
    curr_rc.ch_l_y = static_cast<uint16_t>(ExtractBits(raw_data, 49, 11));
    curr_rc.sw = static_cast<uint8_t>(ExtractBits(raw_data, 60, 2));
    curr_rc.pause = static_cast<uint8_t>(ExtractBits(raw_data, 62, 1));
    curr_rc.key_l = static_cast<uint8_t>(ExtractBits(raw_data, 63, 1));
    curr_rc.key_r = static_cast<uint8_t>(ExtractBits(raw_data, 64, 1));
    curr_rc.dial = static_cast<uint16_t>(ExtractBits(raw_data, 65, 11));
    curr_rc.trig = static_cast<uint8_t>(ExtractBits(raw_data, 76, 1));
    curr_rc.x = static_cast<int16_t>(ExtractBits(raw_data, 80, 16));
    curr_rc.y = static_cast<int16_t>(ExtractBits(raw_data, 96, 16));
    curr_rc.z = static_cast<int16_t>(ExtractBits(raw_data, 112, 16));
    curr_rc.press_l = static_cast<uint8_t>(ExtractBits(raw_data, 128, 2));
    curr_rc.press_r = static_cast<uint8_t>(ExtractBits(raw_data, 130, 2));
    curr_rc.press_m = static_cast<uint8_t>(ExtractBits(raw_data, 132, 2));
    curr_rc.key = static_cast<uint16_t>(ExtractBits(raw_data, 136, 16));

    const bool RANGE_OK =
        !(curr_rc.ch_r_x < VT13_CH_VALUE_MIN || curr_rc.ch_r_x > VT13_CH_VALUE_MAX ||
          curr_rc.ch_r_y < VT13_CH_VALUE_MIN || curr_rc.ch_r_y > VT13_CH_VALUE_MAX ||
          curr_rc.ch_l_x < VT13_CH_VALUE_MIN || curr_rc.ch_l_x > VT13_CH_VALUE_MAX ||
          curr_rc.ch_l_y < VT13_CH_VALUE_MIN || curr_rc.ch_l_y > VT13_CH_VALUE_MAX ||
          curr_rc.dial < VT13_CH_VALUE_MIN || curr_rc.dial > VT13_CH_VALUE_MAX ||
          curr_rc.sw > static_cast<uint8_t>(SwitchPos::VT13_SW_POS_S));

    if (!RANGE_OK)
    {
      return LibXR::ErrorCode::CHECK_ERR;
    }

    output_data = CMD::Data();

    /* 断链恢复后的首帧仅用于建立边沿基线，避免误触发事件 */
    if (this->offline_latched_)
    {
      this->last_data_ = curr_rc;
      this->offline_latched_ = false;
    }

    /* 检测挡位切换开关 */
    if (curr_rc.sw != this->last_data_.sw)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_SW_POS_C) +
                               curr_rc.sw);
    }

    /* 检测其余自定义按键 */
    if (curr_rc.key_l && !this->last_data_.key_l)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_PRESSED_L));

      this->fric_enable_ = !this->fric_enable_;
      if (this->fric_enable_)
      {
        this->vt13_event_.Active(
            static_cast<uint32_t>(SwitchPos::VT13_KEY_CUSTOM_L_TOGGLE_ON));
      }
      else
      {
        this->vt13_event_.Active(
            static_cast<uint32_t>(SwitchPos::VT13_KEY_CUSTOM_L_TOGGLE_OFF));
      }
    }

    if (curr_rc.key_r && !this->last_data_.key_r)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_PRESSED_R));

      this->ai_mode_enable_ = !this->ai_mode_enable_;
      if (this->ai_mode_enable_)
      {
        this->vt13_event_.Active(
            static_cast<uint32_t>(SwitchPos::VT13_KEY_CUSTOM_R_TOGGLE_ON));
      }
      else
      {
        this->vt13_event_.Active(
            static_cast<uint32_t>(SwitchPos::VT13_KEY_CUSTOM_R_TOGGLE_OFF));
      }
    }

    if (curr_rc.pause && !this->last_data_.pause)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_PRESSED_PAUSED));

      if (this->ai_mode_enable_)
      {
        this->gimbal_enable_ = false;
      }
      else
      {
        this->gimbal_enable_ = !this->gimbal_enable_;
      }

      if (this->gimbal_enable_)
      {
        this->vt13_event_.Active(
            static_cast<uint32_t>(SwitchPos::VT13_KEY_PAUSE_TOGGLE_ON));
      }
      else
      {
        this->vt13_event_.Active(
            static_cast<uint32_t>(SwitchPos::VT13_KEY_PAUSE_TOGGLE_OFF));
      }
    }

    if (curr_rc.trig && !this->last_data_.trig)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_PRESSED_TRIG));
    }

    if (!curr_rc.key_l && this->last_data_.key_l)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_RELEASE_L));
    }
    if (!curr_rc.key_r && this->last_data_.key_r)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_RELEASE_R));
    }
    if (!curr_rc.pause && this->last_data_.pause)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_RELEASE_PAUSED));
    }
    if (!curr_rc.trig && this->last_data_.trig)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_KEY_RELEASE_TRIG));
    }

    this->ActiveDialTouchEvent(curr_rc.dial);

    /* 检测Shift/Ctrl */
    uint32_t tmp = 0;
    if (curr_rc.key & RawValue(Key::KEY_SHIFT))
    {
      tmp += static_cast<uint32_t>(Key::KEY_NUM);
    }
    if (curr_rc.key & RawValue(Key::KEY_CTRL))
    {
      tmp += 2 * static_cast<uint32_t>(Key::KEY_NUM);
    }

    for (int i = 0; i < 16; i++)
    {
      if ((curr_rc.key & (1u << i)) && !(this->last_data_.key & (1u << i)))
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_W) + i + tmp);
      }
    }

    const uint16_t COMBO_SW =
        RawValue(Key::KEY_SHIFT) | RawValue(Key::KEY_CTRL) | RawValue(Key::KEY_Q);
    const uint16_t COMBO_MOUSE =
        RawValue(Key::KEY_SHIFT) | RawValue(Key::KEY_CTRL) | RawValue(Key::KEY_E);

    /* Shift+Ctrl+Q/E 控制源切换 */
    if ((curr_rc.key & COMBO_SW) == COMBO_SW)
    {
      this->ctrl_source_ = ControlSource::VT13_CTRL_SOURCE_SW;
    }
    if ((curr_rc.key & COMBO_MOUSE) == COMBO_MOUSE)
    {
      this->ctrl_source_ = ControlSource::VT13_CTRL_SOURCE_MOUSE;
    }

    constexpr float FULL_RANGE =
        static_cast<float>(VT13_CH_VALUE_MAX - VT13_CH_VALUE_MIN);
    constexpr float INV_FULL_RANGE = 1.0f / FULL_RANGE;
    constexpr float MOUSE_SCALER = 1000.0f / 32768.0f;

    if (this->ctrl_source_ == ControlSource::VT13_CTRL_SOURCE_MOUSE)
    {
      if (curr_rc.press_l && !this->last_data_.press_l)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_L_PRESS));
      }
      if (curr_rc.press_r && !this->last_data_.press_r)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_R_PRESS));
      }
      if (curr_rc.press_m && !this->last_data_.press_m)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_M_PRESS));
      }
      if (!curr_rc.press_l && this->last_data_.press_l)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_L_RELEASE));
      }
      if (!curr_rc.press_r && this->last_data_.press_r)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_R_RELEASE));
      }
      if (!curr_rc.press_m && this->last_data_.press_m)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(Key::KEY_M_RELEASE));
      }

      if (curr_rc.key & RawValue(Key::KEY_A))
      {
        output_data.chassis.x -= 1.0f;
      }
      if (curr_rc.key & RawValue(Key::KEY_D))
      {
        output_data.chassis.x += 1.0f;
      }
      if (curr_rc.key & RawValue(Key::KEY_S))
      {
        output_data.chassis.y -= 1.0f;
      }
      if (curr_rc.key & RawValue(Key::KEY_W))
      {
        output_data.chassis.y += 1.0f;
      }

      output_data.chassis.z = 0.0f;

      output_data.gimbal.pit = static_cast<float>(curr_rc.y) * MOUSE_SCALER;
      output_data.gimbal.yaw = static_cast<float>(curr_rc.x) * MOUSE_SCALER;
      output_data.gimbal.rol = 0.0f;

      output_data.stir.isfire = curr_rc.press_l != 0;
    }
    else
    {
      /* 遥控器模式 */
      output_data.chassis.x = 2.0f *
                              (static_cast<float>(curr_rc.ch_l_y) - VT13_CH_VALUE_MID) *
                              INV_FULL_RANGE;
      output_data.chassis.y = 2.0f *
                              (static_cast<float>(curr_rc.ch_l_x) - VT13_CH_VALUE_MID) *
                              INV_FULL_RANGE;
      output_data.chassis.z = 2.0f *
                              (static_cast<float>(curr_rc.ch_r_x) - VT13_CH_VALUE_MID) *
                              INV_FULL_RANGE;
      output_data.gimbal.yaw = 2.0f *
                               (static_cast<float>(curr_rc.ch_r_x) - VT13_CH_VALUE_MID) *
                               INV_FULL_RANGE;
      output_data.gimbal.pit = 2.0f *
                               (static_cast<float>(curr_rc.ch_r_y) - VT13_CH_VALUE_MID) *
                               INV_FULL_RANGE;
      output_data.gimbal.rol = 0.0f;

      output_data.stir.isfire = curr_rc.trig != 0;
    }

    output_data.shooter.isfric = this->fric_enable_;
    output_data.chassis_online = true;
    output_data.gimbal_online = true;
    output_data.shooter_online = true;
    output_data.stir_online = true;
    output_data.ctrl_source = CMD::ControlSource::CTRL_SOURCE_RC;

    this->last_data_ = curr_rc;

    return LibXR::ErrorCode::OK;
  }

  /**
   * @brief 离线输出：把控制量归零、标记离线、清除切换状态并输入给 CMD。
   *        Offline output: zero the control values, mark the link offline, clear the
   *        toggle states and feed the result to CMD.
   */
  void Offline()
  {
    this->cmd_data_.chassis.x = 0;
    this->cmd_data_.chassis.y = 0;
    this->cmd_data_.chassis.z = 0;
    this->cmd_data_.gimbal.yaw = 0;
    this->cmd_data_.gimbal.pit = 0;
    this->cmd_data_.gimbal.rol = 0;

    this->cmd_data_.shooter.isfric = false;
    this->cmd_data_.stir.isfire = false;

    this->cmd_data_.chassis_online = false;
    this->cmd_data_.gimbal_online = false;
    this->cmd_data_.shooter_online = false;
    this->cmd_data_.stir_online = false;

    this->gimbal_enable_ = false;
    this->fric_enable_ = false;
    this->ai_mode_enable_ = false;
    /* 清除拨轮触碰中间态，避免离线恢复后误触发短触/长触事件 */
    this->dial_up_active_ = false;
    this->dial_up_long_triggered_ = false;

    this->last_data_ = Data{};
    this->offline_latched_ = true;

    this->cmd_->FeedRC(CMD::RCInputSource::RC_INPUT_VT13, this->cmd_data_);
  }

 private:
  CMD* cmd_; /* CMD模块指针 */
  ControlSource ctrl_source_ = ControlSource::VT13_CTRL_SOURCE_SW;

  bool gimbal_enable_ = false;
  bool fric_enable_ = false;
  bool ai_mode_enable_ = false;

  Data last_data_{};     /* 上一帧数据 */
  CMD::Data cmd_data_{}; /* 命令数据 */

  /* 离线后只发送一次Offline，待下一次有效帧到来再解除 */
  bool offline_latched_ = false;

  bool dial_up_active_ = false;
  bool dial_up_long_triggered_ = false;
  LibXR::MillisecondTimestamp dial_up_touch_start_{};

  LibXR::UART* uart_;                       /* UART接口指针 */
  LibXR::Event vt13_event_;                 /* 事件处理器 */
  LibXR::Thread thread_uart_;               /* UART线程 */
  LibXR::Semaphore sem_;                    /* 读操作信号量 */
  LibXR::ReadOperation op_;                 /* 读操作（阻塞型） */
  LibXR::MillisecondTimestamp last_time_{}; /* 上次接收时间 */

  /**
   * @brief 从任意位偏移提取指定位宽的数据。
   *        Extract a field of the given bit width from an arbitrary bit offset.
   *
   * @param raw_data 原始字节流。
   *                 Raw byte stream.
   * @param bit_offset 起始位偏移。
   *                   Start bit offset.
   * @param bit_len 位宽。
   *                Bit width.
   * @return 提取的无符号值。
   *         The extracted unsigned value.
   */
  static uint32_t ExtractBits(const uint8_t* raw_data, uint16_t bit_offset,
                              uint8_t bit_len)
  {
    uint32_t value = 0;
    for (uint8_t i = 0; i < bit_len; ++i)
    {
      const uint16_t CURRENT_BIT = bit_offset + i;
      const uint8_t BIT =
          static_cast<uint8_t>((raw_data[CURRENT_BIT / 8] >> (CURRENT_BIT % 8)) & 0x01u);
      value |= static_cast<uint32_t>(BIT) << i;
    }
    return value;
  }

  /**
   * @brief 读取小端 16 位整数。
   *        Read a little-endian 16-bit integer.
   *
   * @param raw_data 原始字节流。
   *                 Raw byte stream.
   * @param offset 字节偏移。
   *               Byte offset.
   * @return 读取的值。
   *         The value read.
   */
  static uint16_t ReadLe16(const uint8_t* raw_data, std::size_t offset)
  {
    return static_cast<uint16_t>(raw_data[offset]) |
           (static_cast<uint16_t>(raw_data[offset + 1]) << 8);
  }

  /**
   * @brief 校验帧 CRC：前 19 字节的 CRC16 与帧尾 2 字节小端值比较。
   *        Verify the frame CRC: the CRC16 of the first 19 bytes is compared with the
   *        little-endian value in the last 2 bytes.
   *
   * @param raw_data 21 字节原始帧。
   *                 Raw 21-byte frame.
   * @return 校验通过为 true。
   *         True when the check passes.
   */
  bool VerifyCRC(const uint8_t* raw_data)
  {
    constexpr std::size_t CRC_OFFSET = VT13_PAYLOAD_SIZE_FOR_CRC;
    /* 按官方协议：CRC覆盖前19字节，帧尾2字节为小端CRC16 */
    const uint16_t calc_crc =
        LibXR::CRC16::Calculate(raw_data, VT13_PAYLOAD_SIZE_FOR_CRC);
    const uint16_t frame_crc_le = ReadLe16(raw_data, CRC_OFFSET);
    return calc_crc == frame_crc_le;
  }

  /**
   * @brief 按拨轮值发出上拨短触、上拨长触与下拨事件。
   *        Emit the dial up-short, up-long and down events from the dial value.
   *
   * @param curr_dial 当前拨轮值。
   *                  Current dial value.
   */
  void ActiveDialTouchEvent(uint16_t curr_dial)
  {
    constexpr uint16_t DIAL_UP_THRESHOLD = VT13_CH_VALUE_MID + 180u;
    constexpr uint16_t DIAL_DOWN_THRESHOLD = VT13_CH_VALUE_MID - 180u;
    constexpr uint32_t DIAL_LONG_TOUCH_MS = 500u;

    const bool DIAL_UP = curr_dial >= DIAL_UP_THRESHOLD;
    const bool LAST_DIAL_UP = this->last_data_.dial >= DIAL_UP_THRESHOLD;
    const bool DIAL_DOWN = curr_dial <= DIAL_DOWN_THRESHOLD;
    const bool LAST_DIAL_DOWN = this->last_data_.dial <= DIAL_DOWN_THRESHOLD;
    const auto CURRENT_TIME = LibXR::Timebase::GetMilliseconds();

    if (DIAL_UP && !LAST_DIAL_UP)
    {
      this->dial_up_active_ = true;
      this->dial_up_long_triggered_ = false;
      this->dial_up_touch_start_ = CURRENT_TIME;
    }

    if (this->dial_up_active_ && DIAL_UP && !this->dial_up_long_triggered_)
    {
      const uint32_t TOUCH_MS =
          (CURRENT_TIME - this->dial_up_touch_start_).ToMillisecond();
      if (TOUCH_MS >= DIAL_LONG_TOUCH_MS)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_DIAL_UP_LONG));
        this->dial_up_long_triggered_ = true;
      }
    }

    if (!DIAL_UP && LAST_DIAL_UP && this->dial_up_active_)
    {
      if (!this->dial_up_long_triggered_)
      {
        this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_DIAL_UP_SHORT));
      }
      this->dial_up_active_ = false;
      this->dial_up_long_triggered_ = false;
    }

    if (DIAL_DOWN && !LAST_DIAL_DOWN)
    {
      this->vt13_event_.Active(static_cast<uint32_t>(SwitchPos::VT13_DIAL_DOWN_TOUCH));
    }
  }

  /**
   * @brief 检查在线状态：超过 100 ms 没有有效帧时执行一次 `Offline()`。
   *        Check the online state: `Offline()` runs once when no valid frame arrived for
   *        more than 100 ms.
   */
  void CheckoutOffline()
  {
    auto current_time = LibXR::Timebase::GetMilliseconds();
    if ((current_time - this->last_time_).ToMillisecond() > 100)
    {
      /*离线窗口内只触发一次Offline，避免重复下发相同离线数据 */
      if (!this->offline_latched_)
      {
        this->Offline();
      }
    }
  }
};
