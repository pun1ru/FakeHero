#pragma once

#include <memory>

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: RoboMaster 裁判系统（2025 协议）串口收发模块：解析数据并发布 Topic，发送客户端 UI 与哨兵、雷达决策 / RoboMaster referee system (2025 protocol) UART Module that parses data, publishes Topics and sends client UI and sentry and radar decisions
depends:
- 3SE-xrobot-dev/CMD
=== END MANIFEST === */
// clang-format on
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "CMD.hpp"
#include "RefereeTypes.hpp"
#include "crc.hpp"
#include "libxr_def.hpp"
#include "libxr_time.hpp"
#include "libxr_type.hpp"
#include "message.hpp"
#include "mutex.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "uart.hpp"

/**
 * @brief RoboMaster 裁判系统串口收发模块。
 *        RoboMaster referee system UART Module.
 *
 * @details 接收并解析裁判系统数据帧，发布底盘、发射与比赛摘要 Topic，并发送客户端 UI、
 *          哨兵与雷达决策等数据。紧凑结构体中的位域按 GCC / Clang 的位域布局解释。
 *          Receives and parses referee system frames, publishes the chassis, launcher and
 *          game summary Topics, and sends client UI, sentry and radar decisions.
 *          Bit-fields in the packed structures follow the GCC / Clang bit-field layout.
 */
class Referee
{
 public:
  /**
   * @brief 机器人 ID，用于 0x0104 裁判警告数据。
   *        Robot IDs, used in the 0x0104 referee warning data.
   */
  enum class RobotID : uint8_t
  {
    REF_BOT_RED_HERO = 1,
    REF_BOT_RED_ENGINEER = 2,
    REF_BOT_RED_INFANTRY_1 = 3,
    REF_BOT_RED_INFANTRY_2 = 4,
    REF_BOT_RED_INFANTRY_3 = 5,
    REF_BOT_RED_DRONE = 6,
    REF_BOT_RED_SENTRY = 7,
    REF_BOT_RED_DART = 8,
    REF_BOT_RED_RADAR = 9,
    REF_BOT_RED_OUTPOST = 10,
    REF_BOT_RED_BASE = 11,
    REF_BOT_BLU_HERO = 101,
    REF_BOT_BLU_ENGINEER = 102,
    REF_BOT_BLU_INFANTRY_1 = 103,
    REF_BOT_BLU_INFANTRY_2 = 104,
    REF_BOT_BLU_INFANTRY_3 = 105,
    REF_BOT_BLU_DRONE = 106,
    REF_BOT_BLU_SENTRY = 107,
    REF_BOT_BLU_DART = 108,
    REF_BOT_BLU_RADAR = 109,
    REF_BOT_BLU_OUTPOST = 110,
    REF_BOT_BLU_BASE = 111,
  };

  /**
   * @brief 选手端 ID，0x8080 为裁判系统服务器。
   *        Operator client IDs; 0x8080 is the referee system server.
   */
  enum class ClientID : uint16_t
  {
    REF_CL_RED_HERO = 0x0101,
    REF_CL_RED_ENGINEER = 0x0102,
    REF_CL_RED_INFANTRY_1 = 0x0103,
    REF_CL_RED_INFANTRY_2 = 0x0104,
    REF_CL_RED_INFANTRY_3 = 0x0105,
    REF_CL_RED_DRONE = 0x0106,
    REF_CL_BLU_HERO = 0x0165,
    REF_CL_BLU_ENGINEER = 0x0166,
    REF_CL_BLU_INFANTRY_1 = 0x0167,
    REF_CL_BLU_INFANTRY_2 = 0x0168,
    REF_CL_BLU_INFANTRY_3 = 0x0169,
    REF_CL_BLU_DRONE = 0x016A,
    REF_CL_REFEREE_SERVER = 0x8080, /* 裁判系统服务器，用于哨兵和雷达自主决策 */
  };

  /**
   * @brief 子内容 ID，来源于 0x0301 机器人交互数据。
   *        Sub-content IDs, from the 0x0301 robot interaction data.
   */
  enum class CMDID : uint16_t
  {
    REF_STDNT_CMD_ID_UI_DEL = 0x0100,         /*选手端删除图层*/
    REF_STDNT_CMD_ID_UI_DRAW1 = 0x0101,       /*选手端绘制一个图形*/
    REF_STDNT_CMD_ID_UI_DRAW2 = 0x0102,       /*选手端绘制两个图形*/
    REF_STDNT_CMD_ID_UI_DRAW5 = 0x0103,       /*选手端绘制五个图形*/
    REF_STDNT_CMD_ID_UI_DRAW7 = 0x0104,       /*选手端绘制七个图形*/
    REF_STDNT_CMD_ID_UI_STR = 0x0110,         /*选手端绘制字符图形*/
    REF_STDNT_CMD_ID_CUSTOM = 0x0200,         /*0x0200~0x02FF机器人之间通信*/
    REF_STDNT_CMD_ID_SENTRY_CMD = 0X0120,     /*哨兵自主决策指令*/
    REF_STDNT_CMD_ID_RADAR_CMD = 0X0121,      /*雷达自主决策指令*/
    REF_STDNT_CMD_ID_SEND_RADAR_DATA = 0X0279 /*发送给雷达位置数据*/
  };

  /**
   * @brief 裁判系统链路状态。
   *        Referee link status.
   */
  enum class Status : uint8_t
  {
    OFFLINE = 0, /* 离线 */
    RUNNING,     /* 在线 */
  };

  /**
   * @brief 裁判系统命令码。
   *        Referee system command IDs.
   */
  enum class CommandID : uint16_t
  {
    REF_CMD_ID_GAME_STATUS = 0x0001,           /* 比赛状态数据，固定以 1Hz 频率发送 */
    REF_CMD_ID_GAME_RESULT = 0x0002,           /* 比赛结果数据，比赛结束触发发送 */
    REF_CMD_ID_GAME_ROBOT_HP = 0x0003,         /* 机器人血量数据，固定以 3Hz 频率发送 */
    REF_CMD_ID_FIELD_EVENTS = 0x0101,          /* 场地事件数据，固定以 1Hz 频率发送 */
    REF_CMD_ID_WARNING = 0x0104,               /* 裁判警告数据，1Hz，触发时发送 */
    REF_CMD_ID_DART_COUNTDOWN = 0x0105,        /* 飞镖发射相关数据，固定1Hz发送 */
    REF_CMD_ID_ROBOT_STATUS = 0x0201,          /* 机器人性能体系数据，10Hz */
    REF_CMD_ID_POWER_HEAT_DATA = 0x0202,       /* 实时底盘缓冲能量和射击热量, 10Hz */
    REF_CMD_ID_ROBOT_POS = 0x0203,             /* 机器人位置数据, 1Hz */
    REF_CMD_ID_ROBOT_BUFF = 0x0204,            /* 机器人增益和底盘能量数据, 3Hz */
    REF_CMD_ID_DRONE_ENERGY = 0x0205,          /* 空中机器人能量状态数据 */
    REF_CMD_ID_ROBOT_DMG = 0x0206,             /* 伤害状态数据，伤害发生后发送 */
    REF_CMD_ID_LAUNCHER_DATA = 0x0207,         /* 实时射击数据，弹丸发射后发送 */
    REF_CMD_ID_BULLET_REMAINING = 0x0208,      /* 允许发弹量, 10Hz */
    REF_CMD_ID_RFID = 0x0209,                  /* 机器人RFID模块状态, 3Hz */
    REF_CMD_ID_DART_CLIENT = 0x020A,           /* 飞镖选手端指令数据, 3Hz */
    REF_CMD_ID_ROBOT_POS_TO_SENTRY = 0X020B,   /* 地面机器人位置数据, 1Hz */
    REF_CMD_ID_RADAR_MARK = 0X020C,            /* 雷达标记进度数据, 1Hz */
    REF_CMD_ID_SENTRY_DECISION = 0x020D,       /* 哨兵自主决策相关信息同步, 1Hz */
    REF_CMD_ID_RADAR_DECISION = 0x020E,        /* 雷达自主决策相关信息同步, 1Hz */
    REF_CMD_ID_INTER_STUDENT = 0x0301,         /* 机器人交互数据, 30Hz */
    REF_CMD_ID_INTER_STUDENT_CUSTOM = 0x0302,  /*自定义控制器和机器人,30Hz,图传*/
    REF_CMD_ID_CLIENT_MAP = 0x0303,            /* 选手端小地图交互数据, 选手触发发送 */
    REF_CMD_ID_KEYBOARD_MOUSE = 0x0304,        /* 键鼠遥控数据， 30Hz, 图传链路 */
    REF_CMD_ID_MAP_ROBOT_DATA = 0x0305,        /* 小地图接收雷达数据, 5HzMax */
    REF_CMD_ID_CUSTOM_KEYBOARD_MOUSE = 0X0306, /* 自定义控制器交互, 30Hz */
    REF_CMD_ID_SENTRY_POS_DATA = 0x0307,       /* 小地图接收路径数据, 1Hz */
    REF_CMD_ID_ROBOT_POS_DATA = 0x0308,        /* 选手端小地图接受机器人消息, 3Hz */

    REF_CMD_ID_CUSTOM_RECV_DATA = 0x0309,      /* 自定义控制器收包,10Hz,图传链路 */
    REF_CMD_ID_DATA_TO_CUSTOM_CLIENT = 0x0310, /*自定义客户端,50Hz,图传链路*/

    REF_CMD_ID_SET_VIDEO_TRANS_CH = 0x0F01, /* 设置图传出图信道,应答,图传链路 */
    REF_CMD_ID_GET_VIDEO_TRANS_CH = 0x0F02, /* 查询当前出图信道,应答,图传链路 */

    REF_CMD_ID_RADAR_ENEMY_POS = 0x0A01,    /*对方机器人的位置坐标,10Hz,雷达链路*/
    REF_CMD_ID_RADAR_ENEMY_HP = 0x0A02,     /*对方机器人的血量信息,10Hz,雷达链路*/
    REF_CMD_ID_RADAR_ENEMY_BULLET = 0x0A03, /* 对方剩余发弹量,10Hz,雷达链路 */
    REF_CMD_ID_RADAR_ENEMY_INFO = 0x0A04,   /*对方宏观状态信息,10Hz,雷达链路*/
    REF_CMD_ID_RADAR_ENEMY_BUFF = 0x0A05,   /*对方增益效果,10Hz,雷达链路*/
    REF_CMD_ID_RADAR_ENEMY_KEY = 0x0A06,    /*对方干扰波密钥,10Hz,雷达链路*/

  };

  /**
   * @brief 比赛类型，来源 0x0001。
   *        Game type, from 0x0001.
   */
  enum class GameType : uint8_t
  {
    REF_GAME_TYPE_RMUC = 1, /* 超抗 */
    REF_GAME_TYPE_RMUT,     /* 高校单项赛 */
    REF_GAME_TYPE_RMUA,     /* 人工智能挑战赛 */
    REF_GAME_TYPE_RMUL_3V3, /* 3V3 对抗*/
    REF_GAME_TYPE_RMUL_1V1, /* 步兵1v1 */
  };

  /**
   * @brief 当前比赛阶段，来源 0x0001。
   *        Current game stage, from 0x0001.
   */
  enum class GameProcess : uint8_t
  {
    REF_GAME_PROCESS_PENDING = 0,   /* 未开始 */
    REF_GAME_PROCESS_READY,         /* 准备阶段 */
    REF_GAME_PROCESS_SELF_TEST,     /* 裁判系统15s自检 */
    REF_GAME_PROCESS_COUNTING_DOWN, /* 5s倒计时 */
    REF_GAME_PROCESS_PROCESSING,    /* 正在进行 */
    REF_GAME_PROCESS_SCORING,       /* 结算中 */
  };

  /**
   * @brief 获胜方，来源 0x0002。
   *        Winner, from 0x0002.
   */
  enum class Winner : uint8_t
  {
    REF_GAME_WINNER_TIE = 0, /* 平局 */
    REF_GAME_WINNER_RED,     /* 红方胜利 */
    REF_GAME_WINNER_BLUE,    /* 蓝方胜利 */
  };

  /**
   * @brief 键盘按键位，来源 0x0304。
   *        Keyboard key bits, from 0x0304.
   */
  enum class KeyBoard : uint16_t
  {
    KEY_W = 1U << 0,
    KEY_S = 1U << 1,
    KEY_A = 1U << 2,
    KEY_D = 1U << 3,
    KEY_SHIFT = 1U << 4,
    KEY_CTRL = 1U << 5,
    KEY_Q = 1U << 6,
    KEY_E = 1U << 7,
    KEY_R = 1U << 8,
    KEY_F = 1U << 9,
    KEY_G = 1U << 10,
    KEY_Z = 1U << 11,
    KEY_X = 1U << 12,
    KEY_C = 1U << 13,
    KEY_V = 1U << 14,
    KEY_B = 1U << 15,
  };

  /**
   * @brief UI 删除操作类型，来源子内容 ID 0x0100。
   *        UI delete operation type, from sub-content ID 0x0100.
   */
  enum class UIDeleteType : uint8_t
  {
    UI_DELETE_NO_OP = 0,
    UI_DELETE_LAYER = 1,
    UI_DELETE_ALL = 2,
  };

  /**
   * @brief UI 图形操作类型，来源子内容 ID 0x0101。
   *        UI figure operation type, from sub-content ID 0x0101.
   */
  enum class UIFigureOp : uint8_t
  {
    UI_OP_NO_OP = 0,
    UI_OP_ADD = 1,
    UI_OP_MODIFY = 2,
    UI_OP_DELETE = 3,
  };

  /**
   * @brief UI 图形类型，来源子内容 ID 0x0101。
   *        UI figure type, from sub-content ID 0x0101.
   */
  enum class UIFigureType : uint8_t
  {
    UI_TYPE_LINE = 0,
    UI_TYPE_RECT = 1,
    UI_TYPE_CIRCLE = 2,
    UI_TYPE_ELLIPSE = 3,
    UI_TYPE_ARC = 4,
    UI_TYPE_FLOAT = 5,
    UI_TYPE_INT = 6,
    UI_TYPE_CHAR = 7,
  };

  /**
   * @brief UI 图形颜色，来源子内容 ID 0x0101。
   *        UI figure color, from sub-content ID 0x0101.
   */
  enum class UIColor : uint8_t
  {
    UI_COLOR_TEAM = 0, /* 红/蓝，己方颜色 */
    UI_COLOR_YELLOW = 1,
    UI_COLOR_GREEN = 2,
    UI_COLOR_ORANGE = 3,
    UI_COLOR_PURPLE_RED = 4,
    UI_COLOR_PINK = 5,
    UI_COLOR_CYAN = 6,
    UI_COLOR_BLACK = 7,
    UI_COLOR_WHITE = 8,
  };

  /**
   * @brief UI 图层删除包，来源子内容 ID 0x0100。
   *        UI layer deletion packet, from sub-content ID 0x0100.
   */
  struct [[gnu::packed]] UILayerDelete
  {
    uint8_t delete_type; /* @enum UIDeleteType */
    uint8_t layer;       /* 0~9 */
  };

  /**
   * @brief UI 单图形定义，来源子内容 ID 0x0101。
   *        UI single figure definition, from sub-content ID 0x0101.
   */
  struct [[gnu::packed]] UIFigure
  {
    uint8_t figure_name[3];    /* 图形名，作为索引 */
    uint32_t operate_type : 3; /* @enum UIFigureOp */
    uint32_t figure_type : 3;  /* @enum UIFigureType */
    uint32_t layer : 4;        /* 图层号 0~9 */
    uint32_t color : 4;        /* @enum UIColor */
    uint32_t details_a : 9;
    uint32_t details_b : 9;
    uint32_t width : 10;
    uint32_t start_x : 11;
    uint32_t start_y : 11;
    uint32_t details_c : 10;
    uint32_t details_d : 11;
    uint32_t details_e : 11;
  };

  /**
   * @brief UI 双图形，来源子内容 ID 0x0102。
   *        UI two figures, from sub-content ID 0x0102.
   */
  struct [[gnu::packed]] UIFigure2
  {
    UIFigure interaction_figure[2];
  };

  /**
   * @brief UI 五图形，来源子内容 ID 0x0103。
   *        UI five figures, from sub-content ID 0x0103.
   */
  struct [[gnu::packed]] UIFigure5
  {
    UIFigure interaction_figure[5];
  };

  /**
   * @brief UI 七图形，来源子内容 ID 0x0104。
   *        UI seven figures, from sub-content ID 0x0104.
   */
  struct [[gnu::packed]] UIFigure7
  {
    UIFigure interaction_figure[7];
  };

  /**
   * @brief UI 字符图形，来源子内容 ID 0x0110。
   *        UI character figure, from sub-content ID 0x0110.
   */
  struct [[gnu::packed]] UICharacter
  {
    UIFigure grapic_data_struct; /* 与图形定义一致，figure_type应为字符 */
    uint8_t data[30];            /* ASCII/UTF-8 文本数据 */
  };

  static_assert(sizeof(UILayerDelete) == 2, "UI layer delete size mismatch");
  static_assert(sizeof(UIFigure) == 15, "UI figure size mismatch");
  static_assert(sizeof(UIFigure2) == 30, "UI figure2 size mismatch");
  static_assert(sizeof(UIFigure5) == 75, "UI figure5 size mismatch");
  static_assert(sizeof(UIFigure7) == 105, "UI figure7 size mismatch");
  static_assert(sizeof(UICharacter) == 45, "UI character size mismatch");

  /**
   * @brief 裁判系统包头。
   *        Referee system frame header.
   */
  struct [[gnu::packed]] Header
  {
    uint8_t sof = 0xA5;   /* 数据帧起始字节，固定值为 0xA5 */
    uint16_t data_length; /* 数据帧中 data 的长度 */
    uint8_t seq;          /* 包序号 */
    uint8_t crc8;         /* 包头crc8校验 */
  };

  /**
   * @brief 0x0001 比赛状态数据，固定以 1 Hz 发送。
   *        0x0001 game status data, sent at a fixed 1 Hz.
   */
  using GameStatus = RefereeTypes::GameStatus;

  /**
   * @brief 0x0002 比赛结果数据，比赛结束时触发发送。
   *        0x0002 game result data, sent when the game ends.
   */
  struct [[gnu::packed]] GameResult
  {
    uint8_t winner; /* 获胜方 */
  };

  /**
   * @brief 0x0003 机器人血量数据，固定以 3 Hz 发送。
   *        0x0003 robot HP data, sent at a fixed 3 Hz.
   */
  struct [[gnu::packed]] RobotHP
  {
    uint16_t our_1_hero;     /* 己方1号英雄 */
    uint16_t our_2_engineer; /* 己方2号工程 */
    uint16_t our_3_infantry; /* 己方3号步兵 */
    uint16_t our_4_infantry; /* 己方4号步兵 */
    uint16_t res_1;          /* 保留位 */
    uint16_t our_7_sentry;   /* 己方7号哨兵 */
    uint16_t our_outpose;    /* 己方前哨站 */
    uint16_t red_base;       /* 己方基地 */
  };

  /**
   * @brief 0x0101 场地事件数据，固定以 1 Hz 发送。
   *        0x0101 field event data, sent at a fixed 1 Hz.
   */
  struct [[gnu::packed]] FieldEvents
  {
    uint32_t supply_outter_status : 1;     /*bit0己方与资源区不重叠的补给区占领*/
    uint32_t supply_inner_status : 1;      /*bit1己方与资源区重叠的补给区占领状态*/
    uint32_t supply_status_RMUL : 1;       /*bit2己方补给区的占领状态，仅UL*/
    uint32_t energy_mech_small_status : 2; /*bit3-4己方小能量机关的激活状态*/
    uint32_t energy_mech_big_status : 2;   /*bit5-6己方大能量机关的激活状态*/
    uint32_t highland_center : 2;          /*bit7-8己方中央高地的占领状态*/
    uint32_t highland_trapezium : 2;       /*bit9-10己方梯形高地的占领状态*/
    uint32_t enemy_last_hit_time : 9;      /*bit11-19对方飞镖最后一次击中的时间*/
    uint32_t enemy_last_hit_target : 3;    /*bit20-22对方飞镖最后击中的具体目标*/
    uint32_t highland_center_status : 2;   /*bit23-24中心增益点的占领状态*/
    uint32_t fortress_state : 2;           /* bit25-26己方堡垒增益点的占领状态 */
    uint32_t outpose_state : 2;            /* bit27-28己方前哨站增益点的占领状态 */
    uint32_t base_state : 1;               /* bit29己方基地增益点的占领状态 */
    uint32_t res : 1;                      /* 保留位 */
  };

  /**
   * @brief 0x0104 裁判警告数据，1 Hz，触发时发送。
   *        0x0104 referee warning data, 1 Hz, sent when triggered.
   */
  struct [[gnu::packed]] Warning
  {
    uint8_t level;    /* 己方最后一次受到判罚的等级 */
    uint8_t robot_id; /*己方最后一次受到判罚的违规机器人ID*/
    uint8_t count;    /*己方最后一次受到判罚的违规机器人对应判罚等级的违规次数*/
  };

  /**
   * @brief 0x0105 飞镖发射相关数据，固定以 1 Hz 发送。
   *        0x0105 dart launch data, sent at a fixed 1 Hz.
   */
  struct [[gnu::packed]] DartCountdown
  {
    uint8_t countdown; /*己方飞镖发射剩余时间，单位：秒*/

    uint16_t dart_last_target : 3; /*bit0-2最近一次己方飞镖击中的目标*/
    uint16_t attack_count : 3;     /*bit3-5对方最近被击中的目标累计被击中次数*/
    uint16_t dart_target : 2;      /*bit6-7飞镖此时选定的击打目标*/
    uint16_t res : 8;              /* bit8-15保留位 */
  };

  /**
   * @brief 0x0201 机器人性能体系数据，10 Hz。
   *        0x0201 robot performance data, 10 Hz.
   */
  using RobotStatus = RefereeTypes::RobotStatus;

  /**
   * @brief 0x0202 实时底盘缓冲能量和射击热量，10 Hz。
   *        0x0202 real-time chassis power buffer and shooting heat, 10 Hz.
   */
  struct [[gnu::packed]] PowerHeat
  {
    uint16_t res_1;                /* 保留位 */
    uint16_t res_2;                /* 保留位 */
    float res_3;                   /* 保留位 */
    uint16_t chassis_pwr_buff;     /* 缓冲能量 单位：J */
    uint16_t launcher_id1_17_heat; /* 第1个17mm发射机构的射击热量 */
    uint16_t launcher_42_heat;     /* 42mm发射机构的射击热量 */
  };

  /**
   * @brief 0x0203 机器人位置数据，1 Hz。
   *        0x0203 robot position data, 1 Hz.
   */
  using RobotPOS = RefereeTypes::RobotPOS;

  /**
   * @brief 0x0204 机器人增益和底盘能量数据，3 Hz。
   *        0x0204 robot buff and chassis energy data, 3 Hz.
   */
  struct [[gnu::packed]] RobotBuff
  {
    uint8_t healing_buff;                  /*机器人回血增益,百分比*/
    uint16_t cooling_acc;                  /*机器人射击热量冷却增益 单位是s^{-1}*/
    uint8_t defense_buff;                  /*机器人防御增益 百分比*/
    uint8_t vulnerability_buff;            /*机器人负防御增益 百分比*/
    uint16_t attack_buff;                  /*机器人攻击增益 百分比*/
    uint8_t percent_125_remain_energy : 1; /* 在剩余能量≥125%时为 1 */
    uint8_t percent_100_remain_energy : 1; /* 在剩余能量≥100%时为 1 */
    uint8_t percent_50_remain_energy : 1;  /* 在剩余能量≥50%时为 1 */
    uint8_t percent_30_remain_energy : 1;  /* 在剩余能量≥30%时为 1 */
    uint8_t percent_15_remain_energy : 1;  /* 在剩余能量≥15%时为 1 */
    uint8_t percent_5_remain_energy : 1;   /* 在剩余能量≥5%时为 1 */
    uint8_t percent_1_remain_energy : 1;   /* 在剩余能量≥1%时为 1 */
  };

  /**
   * @brief 0x0206 伤害状态数据，伤害发生后发送。
   *        0x0206 damage data, sent after damage occurs.
   */
  struct [[gnu::packed]] RobotDamage
  {
    uint8_t armor_id : 4;    /* 受击打的装甲板id */
    uint8_t damage_type : 4; /* 血量变化类型 */
  };

  /**
   * @brief 0x0207 实时射击数据，弹丸发射后发送。
   *        0x0207 real-time shooting data, sent after a projectile is fired.
   */
  struct [[gnu::packed]] LauncherData
  {
    uint8_t bullet_type;   /* 弹丸类型 */
    uint8_t launcherer_id; /* 发射机构 ID */
    uint8_t bullet_freq;   /* 弹丸射速 Hz */
    float bullet_speed;    /* 弹丸初速度 m/s */
  };

  /**
   * @brief 0x0208 允许发弹量，10 Hz。
   *        0x0208 projectile allowance, 10 Hz.
   */
  struct [[gnu::packed]] BulletRemain
  {
    uint16_t bullet_17_remain; /*  17mm 弹丸允许发弹量 */
    uint16_t bullet_42_remain; /*  42mm 弹丸允许发弹量 */
    uint16_t coin_remain;      /* 剩余金币数量 */
    uint16_t bullet_17_store;  /* 堡垒增益点提供的储备 17mm 弹丸允许发弹量 */
  };

  /**
   * @brief 0x0209 机器人 RFID 模块状态，3 Hz。
   *        0x0209 robot RFID module status, 3 Hz.
   */
  using RFID = RefereeTypes::RFID;

  /**
   * @brief 0x020A 飞镖选手端指令数据，3 Hz。
   *        0x020A dart operator-client command data, 3 Hz.
   */
  struct [[gnu::packed]] DartClient
  {
    uint8_t opening_status;              /*当前飞镖发射站的状态*/
    uint8_t res;                         /*保留位*/
    uint16_t target_changable_countdown; /*切换目标时比赛剩余时间，单位秒*/
    uint16_t operator_cmd_launch_time;   /*最后一次确定发射时的剩余时间，单位s*/
  };

  /**
   * @brief 0x020B 地面机器人位置数据，1 Hz。
   *        0x020B ground robot position data, 1 Hz.
   */
  using RobotPosForSentry = RefereeTypes::RobotPosForSentry;

  /**
   * @brief 0x020C 雷达标记进度数据，1 Hz。
   *        0x020C radar marking progress data, 1 Hz.
   */
  struct [[gnu::packed]] RadarMarkProgress
  {
    uint8_t mark_enemy_hero_state : 1;       /*对方 1 号英雄机器人易伤情况*/
    uint8_t mark_enemy_engineer_state : 1;   /*对方 2 号工程机器人易伤情况*/
    uint8_t mark_enemy_standard_3_state : 1; /*对方 3 号步兵机器人易伤情况*/
    uint8_t mark_enemy_standard_4_state : 1; /*对方 4 号步兵机器人易伤情况*/
    uint8_t mark_enemy_sentry_state : 1;     /*对方哨兵机器人易伤情况*/
    uint8_t mark_own_hero_state : 1;         /*己方 1 号英雄机器人特殊标识情况*/
    uint8_t mark_own_engineer_state : 1;     /*己方 2 号工程机器人特殊标识情况*/
    uint8_t mark_own_standard_3_state : 1;   /*己方 3 号步兵机器人特殊标识情况*/
    uint8_t mark_own_standard_4_state : 1;   /*己方 4 号步兵机器人特殊标识情况*/
    uint8_t mark_own_sentry_state : 1;       /*己方哨兵机器人特殊标识情况*/
  };

  /**
   * @brief 0x020D 哨兵自主决策相关信息同步，1 Hz。
   *        0x020D sentry autonomous decision information, 1 Hz.
   */
  using SentryInfo = RefereeTypes::SentryInfo;

  /**
   * @brief 0x020E 雷达自主决策相关信息同步，1 Hz。
   *        0x020E radar autonomous decision information, 1 Hz.
   */
  struct [[gnu::packed]] RadarInfo
  {
    uint8_t qualification : 2;      /*雷达是否拥有触发双倍易伤的机会*/
    uint8_t enemy_status : 1;       /*对方是否正在被触发双倍易伤*/
    uint8_t encript_level : 2;      /*己方加密等级（即对方干扰波难度等级）*/
    uint8_t could_exchange_key : 1; /*当前是否可以修改密钥*/
    uint8_t res : 2;                /*保留位*/
  };

  /**
   * @brief 0x0301 机器人交互数据，30 Hz。
   *        0x0301 robot interaction data, 30 Hz.
   */
  struct [[gnu::packed]] RobotInteractionData
  {
    CMDID data_cmd_id;     /* 子内容ID,需为开放的子内容 ID */
    uint16_t sender_id;    /*发送者 ID,需与自身 ID 匹配，ID 编号详见附录*/
    uint16_t receiver_id;  /*接收者 ID*/
    int8_t user_data[112]; /*内容数据段*/
  };

  /**
   * @brief 0x0302 自定义控制器与机器人交互数据，30 Hz，图传链路。
   *        0x0302 custom controller and robot data, 30 Hz, video link.
   *
   * @details 操作手可使用自定义控制器经图传链路向对应机器人发送数据。
   *          The operator can send data to the robot over the video link with a custom
   *          controller.
   */
  struct [[gnu::packed]] CustomController
  {
    uint8_t data[30];
  };

  /**
   * @brief 0x0303 选手端小地图交互数据，选手触发时发送。
   *        0x0303 operator-client minimap interaction data, sent when triggered by the
   *        operator.
   */
  struct [[gnu::packed]] ClientMap
  {
    float position_x;       /*目标位置 x 轴坐标，单位 m*/
    float position_y;       /*目标位置 y 轴坐标，单位 m*/
    uint8_t commd_keyboard; /*云台手按下的键盘按键通用键值*/
    uint8_t robot_id;       /*对方机器人 ID*/
    uint8_t cmd_source;     /*信息来源 ID*/
  };

  /**
   * @brief 0x0304 键鼠遥控数据，30 Hz，图传链路。
   *        0x0304 keyboard and mouse remote data, 30 Hz, video link.
   *
   * @details 通过遥控器发送的键鼠数据经图传链路同步发送给对应机器人。
   *          Keyboard and mouse data sent through the remote controller is forwarded to
   *          the robot over the video link.
   */
  struct [[gnu::packed]] KeyboardMouse
  {
    int16_t mouse_x;         /*鼠标 x 轴移动速度，负值标识向左移动*/
    int16_t mouse_y;         /*鼠标 y 轴移动速度，负值标识向下移动*/
    int16_t mouse_wheel;     /*鼠标滚轮移动速度，负值标识向后滚动*/
    int8_t button_l;         /*鼠标左键是否按下：0 为未按下；1 为按下*/
    int8_t button_r;         /*鼠标右键是否按下：0 为未按下，1 为按下*/
    uint16_t keyboard_value; /* 键盘按键信息 @enum KeyBoard */
    uint16_t res;            /*保留位*/
  };

  /**
   * @brief 0x0305 小地图接收雷达数据，最高 5 Hz。
   *        0x0305 minimap radar data, 5 Hz at most.
   */
  struct [[gnu::packed]] MapRobotData
  {
    uint16_t hero_x;       /*英雄机器人 x 位置坐标，单位：cm*/
    uint16_t hero_y;       /*英雄机器人 y 位置坐标，单位：cm*/
    uint16_t engineer_x;   /*工程机器人 x 位置坐标，单位：cm*/
    uint16_t engineer_y;   /*工程机器人 y 位置坐标，单位：cm*/
    uint16_t infantry_3_x; /*3 号步兵 x 位置坐标，单位：cm*/
    uint16_t infantry_3_y; /*3 号步兵 y 位置坐标，单位：cm*/
    uint16_t infantry_4_x; /*4 号步兵 x 位置坐标，单位：cm*/
    uint16_t infantry_4_y; /*4 号步兵 y 位置坐标，单位：cm*/
    uint16_t infantry_5_x; /*5 号步兵 x 位置坐标，单位：cm*/
    uint16_t infantry_5_y; /*5 号步兵 y 位置坐标，单位：cm*/
    uint16_t sentry_x;     /*哨兵机器人 x 位置坐标，单位：cm*/
    uint16_t sentry_y;     /*哨兵机器人 y 位置坐标，单位：cm*/
  };

  /**
   * @brief 0x0306 自定义控制器交互数据，30 Hz。
   *        0x0306 custom controller interaction data, 30 Hz.
   *
   * @details 操作手可使用自定义控制器模拟键鼠操作选手端。
   *          The operator can simulate keyboard and mouse input on the operator client
   *          with a custom controller.
   *
   * @note 没有新的按键信息时，保持上一帧数据的按下状态。
   *       Without new key information, the pressed state of the previous frame is kept.
   */
  struct [[gnu::packed]] CustomKeyMouseData
  {
    uint8_t key1_value;       /*按键 1 键值*/
    uint8_t key2_value;       /*按键 2 键值*/
    uint16_t x_position : 12; /*鼠标 X 轴像素位置*/
    uint16_t mouse_left : 4;  /*鼠标左键状态*/
    uint16_t y_position : 12; /*鼠标 Y 轴像素位置*/
    uint16_t mouse_right : 4; /*鼠标右键状态*/
    uint16_t res;             /*保留位*/
  };

  /**
   * @brief 0x0307 小地图接收路径数据，1 Hz。
   *        0x0307 minimap path data, 1 Hz.
   *
   * @details 哨兵机器人或半自动控制方式的机器人可经常规链路向对应操作手的选手端发送路径坐
   *          标数据，该路径显示在小地图上。
   *          A sentry robot or a robot under semi-automatic control can send path
   *          coordinates to the operator client over the regular link; the path is shown
   *          on the minimap.
   */
  struct [[gnu::packed]] SentryPosition
  {
    uint8_t intention;         /*移动到目标点或者到目标点攻击/防守*/
    uint16_t start_position_x; /*路径起点 x 轴坐标，单位：dm*/
    uint16_t start_position_y; /*路径起点 y 轴坐标，单位：dm*/
    int8_t delta_x[49];        /*路径点 x 轴增量数组，单位：dm*/
    int8_t delta_y[49];        /*路径点 y 轴增量数组，单位：dm*/
    uint16_t sender_id;        /*发送者 ID*/
  };

  /**
   * @brief 0x0308 选手端小地图接收机器人消息，3 Hz。
   *        0x0308 robot messages received by the operator-client minimap, 3 Hz.
   *
   * @note 编码发送时需考虑数据的字节序。
   *       Byte order has to be considered when encoding the data for sending.
   */
  struct [[gnu::packed]] RobotPosition
  {
    uint16_t sender_id;   /*发送者的 ID*/
    uint16_t receiver_id; /*接收者的 ID*/
    int8_t user_data[30]; /*字符，以 utf-16 格式编码发送，支持显示中文*/
  };

  /**
   * @brief 0x0309 自定义数据，30 字节，图传链路。
   *        0x0309 custom data, 30 bytes, video link.
   *
   * @details 机器人可经图传链路向操作手选手端连接的自定义控制器发送数据（RMUL
   *          暂不适用）。
   *          A robot can send data over the video link to the custom controller connected
   *          to the operator client (not applicable to RMUL yet).
   */
  struct [[gnu::packed]] CustomData1
  {
    uint8_t data[30]; /*自定义数据*/
  };

  /**
   * @brief 0x0310 自定义数据，150 字节，图传链路。
   *        0x0310 custom data, 150 bytes, video link.
   *
   * @details 机器人可经图传链路向操作手选手端连接的自定义控制器发送数据（RMUL
   *          暂不适用）。
   *          A robot can send data over the video link to the custom controller connected
   *          to the operator client (not applicable to RMUL yet).
   */
  struct [[gnu::packed]] CustomData2
  {
    uint8_t data[150]; /*自定义数据*/
  };

  /**
   * @brief 0x0A01 对方机器人的位置坐标。
   *        0x0A01 positions of the enemy robots.
   */
  struct [[gnu::packed]] RadarEnemyRobotPos
  {
    uint16_t enemy_hero_x;       /*对方英雄机器人 x 位置坐标，单位：cm*/
    uint16_t enemy_hero_y;       /*对方英雄机器人 y 位置坐标，单位：cm*/
    uint16_t enemy_engineer_x;   /*对方工程机器人 x 位置坐标，单位：cm*/
    uint16_t enemy_engineer_y;   /*对方工程机器人 y 位置坐标，单位：cm*/
    uint16_t enemy_infantry_3_x; /*对方3 号步兵 x 位置坐标，单位：cm*/
    uint16_t enemy_infantry_3_y; /*对方3 号步兵 y 位置坐标，单位：cm*/
    uint16_t enemy_infantry_4_x; /*对方4 号步兵 x 位置坐标，单位：cm*/
    uint16_t enemy_infantry_4_y; /*对方4 号步兵 y 位置坐标，单位：cm*/
    uint16_t enemy_drone_x;      /*对方空中机器人 x 位置坐标，单位：cm*/
    uint16_t enemy_drone_y;      /*对方空中机器人 y 位置坐标，单位：cm*/
    uint16_t enemy_sentry_x;     /*对方哨兵机器人 x 位置坐标，单位：cm*/
    uint16_t enemy_sentry_y;     /*对方哨兵机器人 y 位置坐标，单位：cm*/
  };

  /**
   * @brief 0x0A02 对方机器人的血量信息。
   *        0x0A02 HP of the enemy robots.
   */
  struct [[gnu::packed]] RadarEnemyRobotHP
  {
    uint16_t enemy_hero_hp;       /*对方 1 号英雄机器人血量*/
    uint16_t enemy_engineer_hp;   /*对方 2 号工程机器人血量*/
    uint16_t enemy_infantry_3_hp; /*对方 3 号步兵机器人血量*/
    uint16_t enemy_infantry_4_hp; /*对方 4 号步兵机器人血量*/
    uint16_t res;                 /*保留位*/
    uint16_t enemy_sentry_hp;     /*对方 7 号哨兵机器人血量*/
  };

  /**
   * @brief 0x0A03 对方机器人的剩余发弹量信息。
   *        0x0A03 remaining projectile allowance of the enemy robots.
   */
  struct [[gnu::packed]] RadarEnemyBullet
  {
    uint16_t enemy_hero_bullet;        /*对方 1 号英雄机器人允许发弹量*/
    uint16_t enemy_engineer_bullet;    /*对方 2 号工程机器人允许发弹量*/
    uint16_t enemy_infantry_3_bullet;  /*对方 3 号步兵机器人允许发弹量*/
    uint16_t enemy_infantry_4_bulletp; /*对方 4 号步兵机器人允许发弹量*/
    uint16_t enemy_drone_bullet;       /*对方 6 号空中机器人允许发弹量*/
    uint16_t enemy_sentry_bullet;      /*对方 7 号哨兵机器人允许发弹量*/
  };

  /**
   * @brief 0x0A04 对方队伍的宏观状态信息。
   *        0x0A04 overall status of the enemy team.
   */
  struct [[gnu::packed]] RadarEnemyState
  {
    uint16_t enemy_remain_coins;                /*对方剩余金币数*/
    uint16_t enemy_coins_total;                 /*对方累计总金币数*/
    uint8_t enemy_support_state : 1;            /*对方补给区占领状态*/
    uint8_t enemy_center_state : 2;             /*对方中央高地的占领状态*/
    uint8_t enemy_highland_trapezium_state : 1; /*对方梯形高地的占领状态*/
    uint8_t enemy_fortress_state : 2;           /*对方堡垒增益点的占领状态*/
    uint8_t enemy_outpose_state : 2;            /*对方前哨站增益点的占领状态*/
    uint8_t enemy_base_state : 1;               /*对方基地增益点的占领状态*/
    uint8_t enemy_tunnel_cross_enemy_f : 1;     /*靠近对方飞坡前增益点（隧道）状态*/
    uint8_t enemy_tunnel_cross_enemy_b : 1;     /*靠近对方飞坡后增益点（隧道）状态*/
    uint8_t enemy_tunnel_cross_own_f : 1;       /*靠近己方飞坡前增益点（隧道）状态*/
    uint8_t enemy_tunnel_cross_own_b : 1;       /*靠近己方飞坡后增益点（隧道）状态*/
    uint8_t enemy_highland_up_state : 1;        /*对方（高地）上部场地交互状态*/
    uint8_t enemy_slope_up_state : 1;           /*对方（飞坡）上部场地交互状态*/
    uint8_t enemy_cross_up_state : 1;           /*对方（公路）上部场地交互状态*/
  };

  /**
   * @brief 0x0A05 对方各机器人当前增益效果。
   *        0x0A05 current buffs of the enemy robots.
   */
  struct [[gnu::packed]] RadarRobotBuff
  {
    uint8_t enemy_hero_health;         /*对方英雄机器人回血增益, 百分比*/
    uint16_t enemy_hero_heat;          /* 对方英雄射击热量冷却增益,直接值,单位/s */
    uint8_t enemy_hero_protection;     /*对方英雄机器人防御增益,百分比*/
    uint8_t enemy_hero_neg_protection; /*对方英雄机器人负防御增益,百分比*/
    uint16_t enemy_hero_attact;        /*对方英雄机器人攻击增益,百分比*/

    uint8_t enemy_engineer_health;         /*对方工程机器人回血增益, 百分比*/
    uint16_t enemy_engineer_heat;          /* 对方工程射击热量冷却增益,直接值,单位/s */
    uint8_t enemy_engineer_protection;     /*对方工程机器人防御增益,百分比*/
    uint8_t enemy_engineer_neg_protection; /*对方工程机器人负防御增益,百分比*/
    uint16_t enemy_engineer_attact;        /*对方工程机器人攻击增益,百分比*/

    uint8_t enemy_infantry_3_health;         /*对方3号步兵回血增益, 百分比*/
    uint16_t enemy_infantry_3_heat;          /* 对方3号射击热量冷却增益,直接值,单位/s */
    uint8_t enemy_infantry_3_protection;     /*对方3号步兵防御增益,百分比*/
    uint8_t enemy_infantry_3_neg_protection; /*对方3号步兵负防御增益,百分比*/
    uint16_t enemy_infantry_3_attact;        /*对方3号步兵攻击增益,百分比*/

    uint8_t enemy_infantry_4_health;         /*对方4号步兵回血增益, 百分比*/
    uint16_t enemy_infantry_4_heat;          /* 对方4号射击热量冷却增益,直接值,单位/s */
    uint8_t enemy_infantry_4_protection;     /*对方4号步兵防御增益,百分比*/
    uint8_t enemy_infantry_4_neg_protection; /*对方4号步兵负防御增益,百分比*/
    uint16_t enemy_infantry_4_attact;        /*对方4号步兵攻击增益,百分比*/

    uint8_t enemy_sentry_health;         /*对方哨兵回血增益, 百分比*/
    uint16_t enemy_sentry_heat;          /* 对方哨兵射击热量冷却增益,直接值,单位/s */
    uint8_t enemy_sentry_protection;     /*对方哨兵防御增益,百分比*/
    uint8_t enemy_sentry_neg_protection; /*对方哨兵负防御增益,百分比*/
    uint16_t enemy_sentry_attact;        /*对方哨兵攻击增益,百分比*/
  };

  /**
   * @brief 0x0A06 对方干扰波密钥。
   *        0x0A06 interference-wave key of the enemy team.
   */
  struct [[gnu::packed]] RadarEnemyKey
  {
    uint8_t ascii[6]; /*每个字节均为 ASCII 码编码的字母或数字*/
  };

  /**
   * @brief 0x0120 哨兵自主决策指令。
   *        0x0120 sentry autonomous decision command.
   */
  struct [[gnu::packed]] SentryDecisionData
  {
    uint32_t confirm_resurrection : 1;    /* 哨兵机器人是否确认复活 */
    uint32_t buy_resurrection : 1;        /* 哨兵机器人是否确认兑换立即复活 */
    uint32_t buy_bullet_num : 11;         /* 哨兵将要兑换的发弹量值 */
    uint32_t remote_buy_bullet_times : 4; /* 哨兵远程兑换发弹量的请求次数 */
    uint32_t romote_buy_hp_times : 4;     /* 哨兵远程兑换血量的请求次数 */
    uint32_t current_state : 2;           /* 哨兵修改当前姿态指令,1进攻 2防御 3移动 */
    uint32_t comfirm_mech : 1;            /*哨兵机器人是否确认使能量机关进入正在激活状态*/
    uint32_t res : 8;                     /* 保留位 */
  };

  /**
   * @brief 0x0121 雷达自主决策指令。
   *        0x0121 radar autonomous decision command.
   */
  struct [[gnu::packed]] RadarDecisionData
  {
    uint8_t radar_cmd;    /* 雷达是否确认触发双倍易伤（单调+1） */
    uint8_t password_cmd; /* byte1：1改密钥；2提交破解密钥验证 */
    uint8_t password_1;
    uint8_t password_2;
    uint8_t password_3;
    uint8_t password_4;
    uint8_t password_5;
    uint8_t password_6;
  };

  /**
   * @brief 0x0F01 设置图传出图信道的请求与应答。
   *        0x0F01 request and reply for setting the video-link output channel.
   */
  struct [[gnu::packed]] SetVideoTransChannel
  {
    uint8_t value; /* 请求:1~6; 应答:0成功/1忙/2参数错误 */
  };

  /**
   * @brief 0x0F02 查询图传出图信道。
   *        0x0F02 query of the video-link output channel.
   *
   * @note 查询请求的数据段为空，应答为 1 字节的当前信道。
   *       The query request has an empty data segment; the reply is one byte holding the
   *       current channel.
   */
  struct [[gnu::packed]] GetVideoTransChannel
  {
    uint8_t channel; /* 0表示未设置；1~6为当前信道 */
  };

  /**
   * @brief 裁判系统数据集，保存各命令码最近一次解析得到的数据。
   *        Referee data set holding the latest parsed data of each command ID.
   */
  struct Data
  {
    Status status;                              /* 在线状态 */
    GameStatus game_status;                     /* 0x0001 */
    GameResult game_result;                     /* 0x0002 */
    RobotHP game_robot_hp;                      /* 0x0003 */
    FieldEvents field_event;                    /* 0x0101 */
    Warning warning;                            /* 0x0104 */
    DartCountdown dart_countdown;               /* 0x0105 */
    RobotStatus robot_status;                   /* 0x0201 */
    PowerHeat power_heat;                       /* 0x0202 */
    RobotPOS robot_pos;                         /* 0x0203 */
    RobotBuff robot_buff;                       /* 0x0204 */
    RobotDamage robot_damage;                   /* 0x0206 */
    LauncherData launcher_data;                 /* 0x0207 */
    BulletRemain bullet_remain;                 /* 0x0208 */
    RFID rfid;                                  /* 0x0209 */
    DartClient dart_client;                     /* 0x020A */
    RobotPosForSentry sentry_pos;               /* 0x020B */
    RadarMarkProgress radar_mark_progress;      /* 0x020C */
    SentryInfo sentry_decision;                 /* 0x020D */
    RadarInfo radar_decision;                   /* 0x020E */
    RobotInteractionData robot_ineraction_data; /* 0x0301 */
    CustomController custom_controller;         /* 0x0302 */
    ClientMap client_map;                       /* 0x0303 */
    KeyboardMouse keyboard_mouse;               /* 0x0304 */
    MapRobotData map_robot_data;                /* 0x0305 */
    CustomKeyMouseData custom_key_mouse_data;   /* 0x0306 */
    SentryPosition sentry_postion;              /* 0x0307 */
    RobotPosition robot_position;               /* 0x0308 */
    CustomData1 custom_data1;                   /* 0x0309 */
    CustomData2 custom_data2;                   /* 0x0310 */
    RadarEnemyRobotPos radar_enemy_robot_pos;   /* 0x0A01 */
    RadarEnemyRobotHP radar_enemy_robot_hp;     /* 0x0A02 */
    RadarEnemyBullet radar_enemy_bullet;        /* 0x0A03 */
    RadarEnemyState radar_enemy_state;          /* 0x0A04 */
    RadarRobotBuff radar_robot_buff;            /* 0x0A05 */
    RadarEnemyKey radar_enemy_key;              /* 0x0A06 */
    SentryDecisionData sentry_dec_data;         /* 0x0120 */
    RadarDecisionData radar_dec_data;           /* 0x0121 */
    SetVideoTransChannel set_vid_trans_ch;      /* 0x0F01 */
    GetVideoTransChannel get_vid_trans_ch;      /* 0x0F02 */
  };

  /**
   * @brief 发射模块使用的裁判系统数据。
   *        Referee data used by the launcher Module.
   */
  struct [[gnu::packed]] LauncherPack
  {
    RobotStatus rs;  /* 热量上限和冷却速率 */
    RobotBuff rb;    /* 冷却增益 */
    LauncherData ld; /* 射速 */
    DartClient dc;   /* 飞镖发射 */
    PowerHeat ph;
  };

  /**
   * @brief 底盘模块使用的裁判系统数据。
   *        Referee data used by the chassis Module.
   */
  struct [[gnu::packed]] ChassisPack
  {
    RobotStatus rs;        /* 等级和功率上限 */
    uint16_t power_buffer; /* 底盘缓冲能量，单位 J */
  };

  /**
   * @brief 哨兵姿态。
   *        Sentry posture.
   */
  enum class State : uint8_t
  {
    ATTACH = 1,     ///< 进攻 Attack
    DEFEND = 2,     ///< 防御 Defend
    GUERRILLA = 3,  ///< 移动 Move
  };

  /**
   * @brief 机器人、比赛与发射相关的裁判系统摘要。
   *        Referee summary of robot, game and launcher data.
   */
  using RobotGameRefereePack = RefereeTypes::RobotGameRefereePack;

  /**
   * @brief 雷达数据包，含地面机器人位置与本机位置。
   *        Radar data packet with the ground robot positions and the position of this
   *        robot.
   */
  struct [[gnu::packed]] RadarPack
  {
    Referee::RobotPosForSentry rf; /* 地面机器人位置数据 */
    float x;                       /* 本机器人位置x坐标，单位m */
    float y;                       /* 本机器人位置y坐标，单位m */
  };

  /**
   * @brief 累加哨兵将要兑换的发弹量。
   *        Add to the projectile amount the sentry is going to exchange.
   *
   * @param need_bullet 增加的发弹量。
   *                    Amount to add.
   */
  void SetNeedBullet(uint8_t need_bullet)
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->data_.sentry_dec_data.buy_bullet_num += need_bullet;
  }

  /**
   * @brief 设置哨兵是否确认复活。
   *        Set whether the sentry confirms revival.
   *
   * @param revival 是否确认复活。
   *                Whether revival is confirmed.
   */
  void SetConfirmRevival(bool revival)
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->data_.sentry_dec_data.confirm_resurrection = revival;
  }

  /**
   * @brief 记录一次哨兵远程兑换发弹量：请求次数加 1，兑换发弹量累加 `bullet_number`。
   *        Record one remote projectile exchange of the sentry: the request count is
   *        incremented by 1 and `bullet_number` is added to the exchanged amount.
   *
   * @param bullet_number 兑换的发弹量。
   *                      Projectile amount to exchange.
   */
  void SetBulletRemote(uint8_t bullet_number)
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->data_.sentry_dec_data.remote_buy_bullet_times += 1;
    this->data_.sentry_dec_data.buy_bullet_num += bullet_number;
  }

  /**
   * @brief 记录一次哨兵远程兑换血量：请求次数加 1。
   *        Record one remote HP exchange of the sentry: the request count is incremented
   *        by 1.
   */
  void SetHPRemote()
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->data_.sentry_dec_data.romote_buy_hp_times += 1;
  }

  /**
   * @brief 设置哨兵是否兑换立即复活。
   *        Set whether the sentry exchanges for an immediate revival.
   *
   * @param revival 是否兑换立即复活。
   *                Whether the immediate revival is exchanged.
   */
  void SetRevivalRemote(bool revival)
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->data_.sentry_dec_data.buy_resurrection = revival;
  }

  /**
   * @brief 设置哨兵姿态指令。
   *        Set the sentry posture command.
   *
   * @param state 目标姿态。
   *              Target posture.
   */
  void SetSwitchMode(State state)
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->data_.sentry_dec_data.current_state = static_cast<uint32_t>(state);
  }

  /**
   * @brief 把保存的哨兵决策数据（0x0120）发送给裁判系统服务器。
   *        Send the stored sentry decision data (0x0120) to the referee system server.
   *
   * @note 应在定时器线程中按裁判系统的带宽要求周期性调用。
   *       Intended to be called periodically from a timer thread at the rate the referee
   *       system bandwidth allows.
   *
   * @return `SendStudentCmd` 的返回值。
   *         The return value of `SendStudentCmd`.
   */
  LibXR::ErrorCode SendSentryPack()
  {
    SentryDecisionData payload{};
    {
      LibXR::Mutex::LockGuard lock(tx_data_mutex_);
      payload = this->data_.sentry_dec_data;
    }

    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_SENTRY_CMD, GetRobotID(), 0x8080,
                          payload);
  }

  /**
   * @brief 把保存的雷达数据包（0x0279）发送出去。
   *        Send the stored radar data packet (0x0279).
   *
   * @return `SendStudentCmd` 的返回值。
   *         The return value of `SendStudentCmd`.
   */
  LibXR::ErrorCode SendRadarPack()
  {
    RadarPack payload{};
    {
      LibXR::Mutex::LockGuard lock(tx_data_mutex_);
      payload = this->radar_pack_;
    }

    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_SEND_RADAR_DATA, GetRobotID(),
                          GetRobotID() + 2, payload);
  }

  /**
   * @brief 构造参数。
   *        Construction parameters.
   */
  struct Param
  {
    uint32_t task_stack_depth_uart;  ///< 线程栈深
    ///< Thread stack depth
    uint32_t baudrate;  ///< 串口波特率
    ///< UART baud rate
    const char* referee_chassis_tp_name;  ///< 底盘 Topic 名称
    ///< Chassis Topic name
    const char* referee_launcher_tp_name;  ///< 发射 Topic 名称
    ///< Launcher Topic name
    const char* referee_robot_game_tp_name;  ///< 摘要 Topic 名称
    ///< Summary Topic name
    const char* referee_radar_tp_name;  ///< 雷达 Topic 名称
    ///< Radar Topic name
    LibXR::Thread::Priority thread_priority_uart;  ///< 线程优先级
    ///< Thread priority
  };

  /**
   * @brief 构造 Referee，配置串口、创建 Topic 并启动解析线程。
   *        Construct Referee, configure the UART, create the Topics and start the parsing
   *        thread.
   *
   * @param uart 连接裁判系统或图传链路的串口。
   *             UART connected to the referee system or the video link.
   * @param cmd 接收图传键鼠控制的 CMD 实例，可为 `nullptr`。
   *            CMD instance that receives the video-link keyboard and mouse control, may
   *            be `nullptr`.
   * @param param 构造参数。
   *              Construction parameters.
   */
  Referee(LibXR::UART& uart, CMD* cmd,
          const Param& param = {.task_stack_depth_uart = 2048,
                                .baudrate = 115200,
                                .referee_chassis_tp_name = "chassis_ref",
                                .referee_launcher_tp_name = "launcher_ref",
                                .referee_robot_game_tp_name = "robot_game_ref",
                                .referee_radar_tp_name = "radar_ref",
                                .thread_priority_uart = LibXR::Thread::Priority::MEDIUM})
      : uart_(std::addressof(uart)),
        sem_(0),
        op_(sem_, 5000),
        sem_tx_(),
        tx_op_(sem_tx_, 5000),
        cmd_(cmd),
        chassispack_topic_(LibXR::Topic::CreateTopic<ChassisPack>(
            param.referee_chassis_tp_name, nullptr, true)),
        launcherpack_topic_(LibXR::Topic::CreateTopic<LauncherPack>(
            param.referee_launcher_tp_name, nullptr, true)),
        robot_game_referee_topic_(LibXR::Topic::CreateTopic<RobotGameRefereePack>(
            param.referee_robot_game_tp_name, nullptr, true)),
        radar_pack_topic_(LibXR::Topic::CreateTopic<RadarPack>(
            param.referee_radar_tp_name, nullptr, true))
  {
    uart_->SetConfig({param.baudrate, LibXR::UART::Parity::NO_PARITY, 8, 1});

    this->thread_.Create(this, ThreadFunc, "Referee", param.task_stack_depth_uart,
                         param.thread_priority_uart);
  }

  /**
   * @brief 绑定接收图传键鼠控制的 CMD。
   *        Bind the CMD that receives the video-link keyboard and mouse control.
   *
   * @param cmd CMD 实例。
   *            CMD instance.
   */
  void BindCMD(CMD& cmd) { cmd_ = &cmd; }

  /**
   * @brief 发送一帧裁判系统数据，写入由发送锁串行化。
   *        Send one referee system frame; writes are serialized by the transmit lock.
   *
   * @param cmd_id 命令码。
   *               Command ID.
   * @param payload 数据段指针，长度为 0 时可为 `nullptr`。
   *                Pointer to the data segment, may be `nullptr` when the length is 0.
   * @param PAYLOAD_LEN 数据段长度，单位字节。
   *                    Length of the data segment in bytes.
   * @return 帧超过发送缓冲区（256 字节）时为 `ErrorCode::ARG_ERR`，否则为串口写入结果。
   *         `ErrorCode::ARG_ERR` when the frame exceeds the transmit buffer (256 bytes),
   *         otherwise the UART write result.
   */
  LibXR::ErrorCode SendFrame(CommandID cmd_id, const void* payload, uint16_t PAYLOAD_LEN)
  {
    LibXR::Mutex::LockGuard lock(tx_mutex_);
    return SendFrameLocked(cmd_id, payload, PAYLOAD_LEN);
  }

  /**
   * @brief 发送一帧裁判系统数据，调用方已持有发送锁。
   *        Send one referee system frame; the caller already holds the transmit lock.
   *
   * @param cmd_id 命令码。
   *               Command ID.
   * @param payload 数据段指针，长度为 0 时可为 `nullptr`。
   *                Pointer to the data segment, may be `nullptr` when the length is 0.
   * @param PAYLOAD_LEN 数据段长度，单位字节。
   *                    Length of the data segment in bytes.
   * @return 帧超过发送缓冲区（256 字节）时为 `ErrorCode::ARG_ERR`，否则为串口写入结果。
   *         `ErrorCode::ARG_ERR` when the frame exceeds the transmit buffer (256 bytes),
   *         otherwise the UART write result.
   */
  LibXR::ErrorCode SendFrameLocked(CommandID cmd_id, const void* payload,
                                   uint16_t PAYLOAD_LEN)
  {
    constexpr size_t HEADER_SIZE = sizeof(Header);
    constexpr size_t CMD_ID_SIZE = sizeof(uint16_t);
    constexpr size_t FRAME_TAIL_SIZE = sizeof(uint16_t);
    const size_t TOTAL_SIZE = HEADER_SIZE + CMD_ID_SIZE + PAYLOAD_LEN + FRAME_TAIL_SIZE;

    if (TOTAL_SIZE > sizeof(tx_buf_))
    {
      return LibXR::ErrorCode::ARG_ERR;
    }

    Header header{};
    header.sof = 0xA5;
    header.data_length = PAYLOAD_LEN;
    header.seq = tx_seq_++;
    header.crc8 = LibXR::CRC8::Calculate(&header, sizeof(Header) - 1);

    size_t offset = 0;
    LibXR::Memory::FastCopy(&tx_buf_[offset], &header, sizeof(header));
    offset += sizeof(header);

    tx_buf_[offset++] = static_cast<uint8_t>(static_cast<uint16_t>(cmd_id) & 0xFF);
    tx_buf_[offset++] = static_cast<uint8_t>((static_cast<uint16_t>(cmd_id) >> 8) & 0xFF);

    if (PAYLOAD_LEN > 0 && payload != nullptr)
    {
      LibXR::Memory::FastCopy(&tx_buf_[offset], payload, PAYLOAD_LEN);
      offset += PAYLOAD_LEN;
    }

    const uint16_t CRC16 = LibXR::CRC16::Calculate(tx_buf_, offset);
    tx_buf_[offset++] = static_cast<uint8_t>(CRC16 & 0xFF);
    tx_buf_[offset++] = static_cast<uint8_t>((CRC16 >> 8) & 0xFF);

    return uart_->Write({tx_buf_, offset}, tx_op_);
  }

  /**
   * @brief 发送以 `payload` 为数据段的一帧。
   *        Send one frame whose data segment is `payload`.
   *
   * @param cmd_id 命令码。
   *               Command ID.
   * @param payload 数据段对象。
   *                Data segment object.
   * @return 同 `SendFrame(CommandID, const void*, uint16_t)`。
   *         Same as `SendFrame(CommandID, const void*, uint16_t)`.
   */
  template <typename PayloadType>
  LibXR::ErrorCode SendFrame(CommandID cmd_id, const PayloadType& payload)
  {
    return SendFrame(cmd_id, &payload, static_cast<uint16_t>(sizeof(PayloadType)));
  }

  /**
   * @brief 发送机器人交互数据（0x0301）。
   *        Send robot interaction data (0x0301).
   *
   * @param data_cmd_id 子内容 ID。
   *                    Sub-content ID.
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 子内容数据段。
   *                Sub-content data segment.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  template <typename PayloadType>
  LibXR::ErrorCode SendStudentCmd(CMDID data_cmd_id, uint16_t sender_id,
                                  uint16_t receiver_id, const PayloadType& payload)
  {
    LibXR::Mutex::LockGuard lock(tx_mutex_);
    struct [[gnu::packed]]
    {
      uint16_t data_cmd_id;
      uint16_t sender_id;
      uint16_t receiver_id;
      PayloadType payload;
    } interaction_pack{};
    interaction_pack.data_cmd_id = static_cast<uint16_t>(data_cmd_id);
    interaction_pack.sender_id = sender_id;
    interaction_pack.receiver_id = receiver_id;
    interaction_pack.payload = payload;
    return SendFrameLocked(CommandID::REF_CMD_ID_INTER_STUDENT, &interaction_pack,
                           static_cast<uint16_t>(sizeof(interaction_pack)));
  }

  /**
   * @brief 获取本机器人 ID。
   *        Get the ID of this robot.
   *
   * @return 最近一次 0x0201 数据中的 `robot_id`。
   *         The `robot_id` of the latest 0x0201 data.
   */
  uint16_t GetRobotID() const { return data_.robot_status.robot_id; }

  /**
   * @brief 初始化图形的公共字段。
   *        Initialize the common fields of a figure.
   *
   * @param fig 被初始化的图形。
   *            Figure to initialize.
   * @param name 图形名，取前 3 个字符。
   *             Figure name, the first 3 characters are used.
   * @param op 图形操作。
   *           Figure operation.
   * @param type 图形类型。
   *             Figure type.
   * @param layer 图层号，0~9。
   *              Layer number, 0 to 9.
   * @param color 颜色。
   *              Color.
   * @param width 线宽。
   *              Line width.
   * @param x 起点 x 坐标。
   *          Start x coordinate.
   * @param y 起点 y 坐标。
   *          Start y coordinate.
   */
  static void InitFigure(UIFigure& fig, const char* name, UIFigureOp op,
                         UIFigureType type, uint8_t layer, UIColor color, uint16_t width,
                         uint16_t x, uint16_t y)
  {
    fig = {};
    SetFigureName(fig.figure_name, name);
    fig.operate_type = static_cast<uint32_t>(op);
    fig.figure_type = static_cast<uint32_t>(type);
    fig.layer = layer;
    fig.color = static_cast<uint32_t>(color);
    fig.width = width;
    fig.start_x = x;
    fig.start_y = y;
  }

  /**
   * @brief 把 32 位整数拆分写入图形的 `details_c`、`details_d` 与 `details_e`。
   *        Split a 32-bit integer into `details_c`, `details_d` and `details_e` of a
   *        figure.
   *
   * @param fig 目标图形。
   *            Target figure.
   * @param value 整数值。
   *              Integer value.
   */
  static void SetFigureValue32(UIFigure& fig, int32_t value)
  {
    const uint32_t RAW = static_cast<uint32_t>(value);
    fig.details_c = RAW & 0x3FFu;
    fig.details_d = (RAW >> 10) & 0x7FFu;
    fig.details_e = (RAW >> 21) & 0x7FFu;
  }

  /**
   * @brief 生成字符图形。
   *        Build a character figure.
   *
   * @param fig 输出的字符图形。
   *            Output character figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param font_size 字号。
   *                  Font size.
   * @param width 线宽。
   *              Line width.
   * @param x 起点 x 坐标。
   *          Start x coordinate.
   * @param y 起点 y 坐标。
   *          Start y coordinate.
   * @param text 文本，最多取 30 字节，可为 `nullptr`。
   *             Text, at most 30 bytes are used, may be `nullptr`.
   */
  void FillCharacter(Referee::UICharacter& fig, const char* name, Referee::UIFigureOp op,
                     uint8_t layer, Referee::UIColor color, uint16_t font_size,
                     uint16_t width, uint16_t x, uint16_t y, const char* text)
  {
    fig = {};
    SetFigureName(fig.grapic_data_struct.figure_name, name);
    fig.grapic_data_struct.operate_type = static_cast<uint32_t>(op);
    fig.grapic_data_struct.figure_type =
        static_cast<uint32_t>(Referee::UIFigureType::UI_TYPE_CHAR);
    fig.grapic_data_struct.layer = layer;
    fig.grapic_data_struct.color = static_cast<uint32_t>(color);
    fig.grapic_data_struct.details_a = font_size;

    uint16_t text_len = 0;
    if (text != nullptr)
    {
      text_len = static_cast<uint16_t>(strnlen(text, sizeof(fig.data)));
      memcpy(fig.data, text, text_len);
    }
    fig.grapic_data_struct.details_b = text_len;
    fig.grapic_data_struct.width = width;
    fig.grapic_data_struct.start_x = x;
    fig.grapic_data_struct.start_y = y;
  }

  /**
   * @brief 生成直线图形。
   *        Build a line figure.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param width 线宽。
   *              Line width.
   * @param x1 起点 x 坐标。
   *           Start x coordinate.
   * @param y1 起点 y 坐标。
   *           Start y coordinate.
   * @param x2 终点 x 坐标。
   *           End x coordinate.
   * @param y2 终点 y 坐标。
   *           End y coordinate.
   */
  void FillLine(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
                uint8_t layer, Referee::UIColor color, uint16_t width, uint16_t x1,
                uint16_t y1, uint16_t x2, uint16_t y2)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_LINE, layer, color, width,
               x1, y1);
    fig.details_d = x2;
    fig.details_e = y2;
  }

  /**
   * @brief 生成矩形图形。
   *        Build a rectangle figure.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param width 线宽。
   *              Line width.
   * @param x1 一个顶点的 x 坐标。
   *           x coordinate of one vertex.
   * @param y1 一个顶点的 y 坐标。
   *           y coordinate of one vertex.
   * @param x2 对角顶点的 x 坐标。
   *           x coordinate of the opposite vertex.
   * @param y2 对角顶点的 y 坐标。
   *           y coordinate of the opposite vertex.
   */
  void FillRect(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
                uint8_t layer, Referee::UIColor color, uint16_t width, uint16_t x1,
                uint16_t y1, uint16_t x2, uint16_t y2)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_RECT, layer, color, width,
               x1, y1);
    fig.details_d = x2;
    fig.details_e = y2;
  }

  /**
   * @brief 生成圆形图形。
   *        Build a circle figure.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param width 线宽。
   *              Line width.
   * @param x 圆心 x 坐标。
   *          Center x coordinate.
   * @param y 圆心 y 坐标。
   *          Center y coordinate.
   * @param radius 半径。
   *               Radius.
   */
  void FillCircle(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
                  uint8_t layer, Referee::UIColor color, uint16_t width, uint16_t x,
                  uint16_t y, uint16_t radius)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_CIRCLE, layer, color, width,
               x, y);
    fig.details_c = radius;
  }

  /**
   * @brief 生成椭圆图形。
   *        Build an ellipse figure.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param width 线宽。
   *              Line width.
   * @param x 圆心 x 坐标。
   *          Center x coordinate.
   * @param y 圆心 y 坐标。
   *          Center y coordinate.
   * @param x_half_axis x 半轴长。
   *                    Half axis length along x.
   * @param y_half_axis y 半轴长。
   *                    Half axis length along y.
   */
  void FillEllipse(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
                   uint8_t layer, Referee::UIColor color, uint16_t width, uint16_t x,
                   uint16_t y, uint16_t x_half_axis, uint16_t y_half_axis)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_ELLIPSE, layer, color, width,
               x, y);
    fig.details_d = x_half_axis;
    fig.details_e = y_half_axis;
  }

  /**
   * @brief 生成圆弧图形。
   *        Build an arc figure.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param width 线宽。
   *              Line width.
   * @param x 圆心 x 坐标。
   *          Center x coordinate.
   * @param y 圆心 y 坐标。
   *          Center y coordinate.
   * @param start_angle 起始角度。
   *                    Start angle.
   * @param end_angle 终止角度。
   *                  End angle.
   * @param x_half_axis x 半轴长。
   *                    Half axis length along x.
   * @param y_half_axis y 半轴长。
   *                    Half axis length along y.
   */
  void FillArc(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
               uint8_t layer, Referee::UIColor color, uint16_t width, uint16_t x,
               uint16_t y, uint16_t start_angle, uint16_t end_angle, uint16_t x_half_axis,
               uint16_t y_half_axis)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_ARC, layer, color, width, x,
               y);
    fig.details_a = start_angle;
    fig.details_b = end_angle;
    fig.details_d = x_half_axis;
    fig.details_e = y_half_axis;
  }

  /**
   * @brief 生成浮点数图形，数值乘以 1000 后取整写入。
   *        Build a floating-point figure; the value is multiplied by 1000 and truncated
   *        to an integer.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param font_size 字号。
   *                  Font size.
   * @param width 线宽。
   *              Line width.
   * @param x 起点 x 坐标。
   *          Start x coordinate.
   * @param y 起点 y 坐标。
   *          Start y coordinate.
   * @param value 显示的数值。
   *              Value to display.
   */
  void FillFloat(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
                 uint8_t layer, Referee::UIColor color, uint16_t font_size,
                 uint16_t width, uint16_t x, uint16_t y, float value)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_FLOAT, layer, color, width,
               x, y);
    fig.details_a = font_size;
    SetFigureValue32(fig, static_cast<int32_t>(value * 1000.0f));
  }

  /**
   * @brief 生成整数图形。
   *        Build an integer figure.
   *
   * @param fig 输出的图形。
   *            Output figure.
   * @param name 图形名。
   *             Figure name.
   * @param op 图形操作。
   *           Figure operation.
   * @param layer 图层号。
   *              Layer number.
   * @param color 颜色。
   *              Color.
   * @param font_size 字号。
   *                  Font size.
   * @param width 线宽。
   *              Line width.
   * @param x 起点 x 坐标。
   *          Start x coordinate.
   * @param y 起点 y 坐标。
   *          Start y coordinate.
   * @param value 显示的数值。
   *              Value to display.
   */
  void FillInt(Referee::UIFigure& fig, const char* name, Referee::UIFigureOp op,
               uint8_t layer, Referee::UIColor color, uint16_t font_size, uint16_t width,
               uint16_t x, uint16_t y, int32_t value)
  {
    InitFigure(fig, name, op, Referee::UIFigureType::UI_TYPE_INT, layer, color, width, x,
               y);
    fig.details_a = font_size;
    SetFigureValue32(fig, value);
  }

  /**
   * @brief 由机器人 ID 计算对应的选手端 ID。
   *        Compute the operator client ID of a robot ID.
   *
   * @param robot_id 机器人 ID，大于 100 的为蓝方。
   *                 Robot ID; IDs above 100 belong to the blue team.
   * @return 选手端 ID。
   *         Operator client ID.
   */
  uint16_t GetClientID(uint16_t robot_id)
  {
    if (robot_id > 100)
    {
      return static_cast<uint16_t>(robot_id - 101 + 0x0165);
    }
    return static_cast<uint16_t>(robot_id + 0x0100);
  }

  /**
   * @brief 写入 3 字节图形名，不足部分以空格填充。
   *        Write the 3-byte figure name, padding the remainder with spaces.
   *
   * @param dst 目标缓冲区。
   *            Destination buffer.
   * @param name 图形名，可为 `nullptr`。
   *             Figure name, may be `nullptr`.
   */
  static void SetFigureName(uint8_t (&dst)[3], const char* name)
  {
    dst[0] = ' ';
    dst[1] = ' ';
    dst[2] = ' ';
    if (name == nullptr)
    {
      return;
    }
    for (int i = 0; i < 3 && name[i] != '\0'; ++i)
    {
      dst[i] = static_cast<uint8_t>(name[i]);
    }
  }

  /**
   * @brief 发送 UI 图层删除（0x0100）。
   *        Send a UI layer deletion (0x0100).
   *
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 图层删除包。
   *                Layer deletion packet.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendUILayerDelete(uint16_t sender_id, uint16_t receiver_id,
                                     const UILayerDelete& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_UI_DEL, sender_id, receiver_id,
                          payload);
  }

  /**
   * @brief 发送一个 UI 图形（0x0101）。
   *        Send one UI figure (0x0101).
   *
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 图形。
   *                Figure.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendUIFigure(uint16_t sender_id, uint16_t receiver_id,
                                const UIFigure& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_UI_DRAW1, sender_id, receiver_id,
                          payload);
  }

  /**
   * @brief 发送两个 UI 图形（0x0102）。
   *        Send two UI figures (0x0102).
   *
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 两个图形。
   *                Two figures.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendUIFigure2(uint16_t sender_id, uint16_t receiver_id,
                                 const UIFigure2& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_UI_DRAW2, sender_id, receiver_id,
                          payload);
  }

  /**
   * @brief 发送五个 UI 图形（0x0103）。
   *        Send five UI figures (0x0103).
   *
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 五个图形。
   *                Five figures.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendUIFigure5(uint16_t sender_id, uint16_t receiver_id,
                                 const UIFigure5& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_UI_DRAW5, sender_id, receiver_id,
                          payload);
  }

  /**
   * @brief 发送七个 UI 图形（0x0104）。
   *        Send seven UI figures (0x0104).
   *
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 七个图形。
   *                Seven figures.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendUIFigure7(uint16_t sender_id, uint16_t receiver_id,
                                 const UIFigure7& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_UI_DRAW7, sender_id, receiver_id,
                          payload);
  }

  /**
   * @brief 发送一个 UI 字符图形（0x0110）。
   *        Send one UI character figure (0x0110).
   *
   * @param sender_id 发送者 ID。
   *                  Sender ID.
   * @param receiver_id 接收者 ID。
   *                    Receiver ID.
   * @param payload 字符图形。
   *                Character figure.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendUICharacter(uint16_t sender_id, uint16_t receiver_id,
                                   const UICharacter& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_UI_STR, sender_id, receiver_id,
                          payload);
  }

  /**
   * @brief 把指定的哨兵决策数据（0x0120）发送给裁判系统服务器。
   *        Send the given sentry decision data (0x0120) to the referee system server.
   *
   * @param payload 哨兵决策数据。
   *                Sentry decision data.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendSentryDecision(const SentryDecisionData& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_SENTRY_CMD, data_.robot_status.robot_id,
                          static_cast<uint16_t>(ClientID::REF_CL_REFEREE_SERVER),
                          payload);
  }

  /**
   * @brief 把指定的雷达决策数据（0x0121）发送给裁判系统服务器。
   *        Send the given radar decision data (0x0121) to the referee system server.
   *
   * @param payload 雷达决策数据。
   *                Radar decision data.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendRadarDecision(const RadarDecisionData& payload)
  {
    return SendStudentCmd(CMDID::REF_STDNT_CMD_ID_RADAR_CMD, data_.robot_status.robot_id,
                          static_cast<uint16_t>(ClientID::REF_CL_REFEREE_SERVER),
                          payload);
  }

  /**
   * @brief 请求设置图传出图信道（0x0F01）。
   *        Request setting the video-link output channel (0x0F01).
   *
   * @param channel 信道，1~6。
   *                Channel, 1 to 6.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendSetVideoTransChannel(uint8_t channel)
  {
    SetVideoTransChannel payload{};
    payload.value = channel;
    return SendFrame(CommandID::REF_CMD_ID_SET_VIDEO_TRANS_CH, payload);
  }

  /**
   * @brief 请求查询当前图传出图信道（0x0F02）。
   *        Request the current video-link output channel (0x0F02).
   *
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendQueryVideoTransChannel()
  {
    return SendFrame(CommandID::REF_CMD_ID_GET_VIDEO_TRANS_CH, nullptr, 0);
  }

  /**
   * @brief 经图传链路向自定义控制器发送 30 字节数据（0x0309）。
   *        Send 30 bytes to the custom controller over the video link (0x0309).
   *
   * @param payload 自定义数据。
   *                Custom data.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendCustomDataToController(const CustomData1& payload)
  {
    return SendFrame(CommandID::REF_CMD_ID_CUSTOM_RECV_DATA, payload);
  }

  /**
   * @brief 经图传链路向自定义控制器发送 150 字节数据（0x0310）。
   *        Send 150 bytes to the custom controller over the video link (0x0310).
   *
   * @param payload 自定义数据。
   *                Custom data.
   * @return 同 `SendFrame`。
   *         Same as `SendFrame`.
   */
  LibXR::ErrorCode SendCustomDataToController(const CustomData2& payload)
  {
    return SendFrame(CommandID::REF_CMD_ID_DATA_TO_CUSTOM_CLIENT, payload);
  }

  /**
   * @brief 线程函数：循环查找包头、解析数据并发布 Topic，每轮休眠 10 ms。
   *        Thread function: repeatedly find the header, parse the data and publish the
   *        Topics, sleeping for 10 ms per iteration.
   *
   * @param ref Referee 实例。
   *            Referee instance.
   */
  static void ThreadFunc(Referee* ref)
  {
    while (1)
    {
      ref->FindHeader();
      ref->last_parse_ = ref->ParseData();
      ref->Publish();
      LibXR::Thread::Sleep(10);
    }
  }

  /**
   * @brief 读取串口直到收到校验通过的包头，读取串口失败时将状态置为 `OFFLINE`。
   *        Read the UART until a header with a valid CRC8 is received; a failed UART read
   *        sets the status to `OFFLINE`.
   */
  void FindHeader()
  {
    while (1)
    {
      if (this->uart_->Read({&this->byte_, 1}, this->op_) == LibXR::ErrorCode::OK)
      {
        if (this->byte_ != 0xA5)
        {
          continue;
        }
        this->pack_.header_.sof = this->byte_;
        if (this->uart_->Read(
                {reinterpret_cast<uint8_t*>(&this->pack_.header_) + 1, sizeof(Header) - 1},
                this->op_) != LibXR::ErrorCode::OK)
        {
          this->data_.status = Status::OFFLINE;
          continue;
        }

        if (LibXR::CRC8::Verify(reinterpret_cast<uint8_t*>(&this->pack_.header_),
                                sizeof(Header)))
        {
          this->data_.status = Status::RUNNING;
          break;
        }
      }
      else
      {
        this->data_.status = Status::OFFLINE;
      }
    }
  }

  /**
   * @brief 读取并解析包头之后的数据，校验 CRC16 并复制到数据集。
   *        Read and parse the data after the header, verify the CRC16 and copy it into
   *        the data set.
   *
   * @return 成功解析时为 true；数据超出缓冲区、读取失败、校验失败、
   *         命令码未知或数据段过短时为 false。
   *         True when the frame was parsed; false when the data exceeds the buffer, the
   *         read or the checksum fails, the command ID is unknown or the data segment is
   *         too short.
   */
  bool ParseData()
  {
    const uint16_t PAYLOAD_LEN = this->pack_.header_.data_length;
    const size_t BYTES_AFTER_HEADER = sizeof(uint16_t) + PAYLOAD_LEN + sizeof(uint16_t);

    if (BYTES_AFTER_HEADER > sizeof(this->pack_.buf_))
    {
      return false;
    }

    if (this->uart_->Read({this->pack_.buf_, BYTES_AFTER_HEADER}, this->op_) !=
        LibXR::ErrorCode::OK)
    {
      return false;
    }

    if (!LibXR::CRC16::Verify(&this->pack_, sizeof(Header) + BYTES_AFTER_HEADER))
    {
      return false;
    }

    const uint16_t CMD_ID = static_cast<uint16_t>(this->pack_.buf_[0]) |
                            (static_cast<uint16_t>(this->pack_.buf_[1]) << 8);
    const uint8_t* payload = &this->pack_.buf_[2];

    const auto COPY_PAYLOAD = [payload, PAYLOAD_LEN](auto& dst) -> bool
    {
      if (PAYLOAD_LEN < sizeof(dst))
      {
        return false;
      }
      LibXR::Memory::FastCopy(&dst, payload, sizeof(dst));
      return true;
    };

    this->data_.status = Status::RUNNING; /* 更新状态 */
    this->last_wake_up_ = LibXR::Timebase::GetMilliseconds();

    switch (static_cast<CommandID>(CMD_ID))
    {
      case CommandID::REF_CMD_ID_GAME_STATUS:
      {
        /* 0x0001, 比赛状态数据 */
        if (!COPY_PAYLOAD(this->data_.game_status))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_GAME_RESULT:
      {
        /* 0x0002, 比赛结果数据 */
        if (!COPY_PAYLOAD(this->data_.game_result))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_GAME_ROBOT_HP:
      {
        /* 0x0003, 机器人血量数据 */
        if (!COPY_PAYLOAD(this->data_.game_robot_hp))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_FIELD_EVENTS:
      {
        /* 0x0101, 场地事件数据 */
        if (!COPY_PAYLOAD(this->data_.field_event))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_WARNING:
      {
        /* 0x0104, 裁判警告数据 */
        if (!COPY_PAYLOAD(this->data_.warning))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_DART_COUNTDOWN:
      {
        /* 0x0105, 飞镖发射相关数据 */
        if (!COPY_PAYLOAD(this->data_.dart_countdown))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_ROBOT_STATUS:
      {
        /* 0x0201, 机器人性能体系数据 */
        if (!COPY_PAYLOAD(this->data_.robot_status))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_POWER_HEAT_DATA:
      {
        /* 0x0202, 实时底盘缓冲能量和射击热量 */
        if (!COPY_PAYLOAD(this->data_.power_heat))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_ROBOT_POS:
      {
        /* 0x0203, 机器人位置数据 */
        if (!COPY_PAYLOAD(this->data_.robot_pos))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_ROBOT_BUFF:
      {
        /* 0x0204, 机器人增益和底盘能量数据 */
        if (!COPY_PAYLOAD(this->data_.robot_buff))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_DRONE_ENERGY:
      {
        /* 0x0205, 空中机器人能量状态数据，不保存 */
        break;
      }

      case CommandID::REF_CMD_ID_ROBOT_DMG:
      {
        /* 0x0206, 伤害状态数据 */
        if (!COPY_PAYLOAD(this->data_.robot_damage))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_LAUNCHER_DATA:
      {
        /* 0x0207, 实时射击数据 */
        if (!COPY_PAYLOAD(this->data_.launcher_data))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_BULLET_REMAINING:
      {
        /* 0x0208, 允许发弹量 */
        if (!COPY_PAYLOAD(this->data_.bullet_remain))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RFID:
      {
        /* 0x0209, 机器人 RFID 模块状态 */
        if (!COPY_PAYLOAD(this->data_.rfid))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_DART_CLIENT:
      {
        /* 0x020A, 飞镖选手端指令数据 */
        if (!COPY_PAYLOAD(this->data_.dart_client))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_ROBOT_POS_TO_SENTRY:
      {
        /* 0x020B, 地面机器人位置数据 -> 哨兵 */
        if (!COPY_PAYLOAD(this->data_.sentry_pos))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_MARK:
      {
        /* 0x020C, 雷达标记进度数据 */
        if (!COPY_PAYLOAD(this->data_.radar_mark_progress))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_SENTRY_DECISION:
      {
        /* 0x020D, 哨兵自主决策相关信息同步 */
        if (!COPY_PAYLOAD(this->data_.sentry_decision))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_DECISION:
      {
        /* 0x020E, 雷达自主决策相关信息同步 */
        if (!COPY_PAYLOAD(this->data_.radar_decision))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_INTER_STUDENT:
      {
        /* 0x0301, 机器人交互数据 */
        if (PAYLOAD_LEN < 6)
        {
          return false;
        }
        memset(this->data_.robot_ineraction_data.user_data, 0,
               sizeof(this->data_.robot_ineraction_data.user_data));
        LibXR::Memory::FastCopy(&this->data_.robot_ineraction_data, payload, 6);
        const size_t USER_DATA_LEN = std::min<size_t>(
            PAYLOAD_LEN - 6, sizeof(this->data_.robot_ineraction_data.user_data));
        if (USER_DATA_LEN > 0)
        {
          LibXR::Memory::FastCopy(this->data_.robot_ineraction_data.user_data,
                                  payload + 6, USER_DATA_LEN);
        }
        break;
      }

      case CommandID::REF_CMD_ID_INTER_STUDENT_CUSTOM:
      {
        /* 0x0302, 自定义控制器和机器人（图传） */
        if (!COPY_PAYLOAD(this->data_.custom_controller))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_CLIENT_MAP:
      {
        /* 0x0303, 选手端小地图交互数据 */
        if (!COPY_PAYLOAD(this->data_.client_map))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_KEYBOARD_MOUSE:
      {
        /* 0x0304, 键鼠遥控数据 */
        if (!COPY_PAYLOAD(this->data_.keyboard_mouse))
        {
          return false;
        }
        FeedVideoLinkRemoteToCMD(this->data_.keyboard_mouse);
        break;
      }

      case CommandID::REF_CMD_ID_MAP_ROBOT_DATA:
      {
        /* 0x0305, 小地图接收雷达数据 */
        if (!COPY_PAYLOAD(this->data_.map_robot_data))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_CUSTOM_KEYBOARD_MOUSE:
      {
        /* 0x0306, 自定义控制器交互（键鼠模拟） */
        if (!COPY_PAYLOAD(this->data_.custom_key_mouse_data))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_SENTRY_POS_DATA:
      {
        /* 0x0307, 小地图接收路径数据 */
        if (!COPY_PAYLOAD(this->data_.sentry_postion))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_ROBOT_POS_DATA:
      {
        /* 0x0308, 选手端小地图接受机器人消息 */
        if (!COPY_PAYLOAD(this->data_.robot_position))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_CUSTOM_RECV_DATA:
      {
        /* 0x0309, 自定义控制器收包 */
        if (!COPY_PAYLOAD(this->data_.custom_data1))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_DATA_TO_CUSTOM_CLIENT:
      {
        /* 0x0310, 自定义客户端数据 */
        if (!COPY_PAYLOAD(this->data_.custom_data2))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_SET_VIDEO_TRANS_CH:
      {
        /* 0x0F01, 设置图传出图信道 (应答) */
        if (!COPY_PAYLOAD(this->data_.set_vid_trans_ch))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_GET_VIDEO_TRANS_CH:
      {
        /* 0x0F02, 查询当前出图信道 (应答) */
        if (!COPY_PAYLOAD(this->data_.get_vid_trans_ch))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_ENEMY_POS:
      {
        /* 0x0A01, 对方机器人的位置坐标 */
        if (!COPY_PAYLOAD(this->data_.radar_enemy_robot_pos))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_ENEMY_HP:
      {
        /* 0x0A02, 对方机器人的血量信息 */
        if (!COPY_PAYLOAD(this->data_.radar_enemy_robot_hp))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_ENEMY_BULLET:
      {
        /* 0x0A03, 对方剩余发弹量 */
        if (!COPY_PAYLOAD(this->data_.radar_enemy_bullet))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_ENEMY_INFO:
      {
        /* 0x0A04, 对方宏观状态信息 */
        if (!COPY_PAYLOAD(this->data_.radar_enemy_state))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_ENEMY_BUFF:
      {
        /* 0x0A05, 对方增益效果 */
        if (!COPY_PAYLOAD(this->data_.radar_robot_buff))
        {
          return false;
        }
        break;
      }

      case CommandID::REF_CMD_ID_RADAR_ENEMY_KEY:
      {
        /* 0x0A06, 对方干扰波密钥 */
        if (!COPY_PAYLOAD(this->data_.radar_enemy_key))
        {
          return false;
        }
        break;
      }

      default:
      {
        /* 未知命令 */
        return false;
      }
    }
    return true;
  }

  /**
   * @brief 把图传链路的键鼠数据转换后交给 CMD；`cmd_` 为 `nullptr` 时不处理。
   *        Convert the video-link keyboard and mouse data and feed it to CMD; nothing
   *        happens when `cmd_` is `nullptr`.
   *
   * @param rc 键鼠遥控数据。
   *           Keyboard and mouse remote data.
   */
  void FeedVideoLinkRemoteToCMD(const KeyboardMouse& rc)
  {
    if (cmd_ == nullptr)
    {
      return;
    }

    constexpr float MOUSE_SCALER = 1000.0f / 32768.0f;
    CMD::Data cmd_data{};

    const uint16_t KEY = rc.keyboard_value;
    if (KEY & static_cast<uint16_t>(KeyBoard::KEY_A))
    {
      cmd_data.chassis.x -= 0.5f;
    }
    if (KEY & static_cast<uint16_t>(KeyBoard::KEY_D))
    {
      cmd_data.chassis.x += 0.5f;
    }
    if (KEY & static_cast<uint16_t>(KeyBoard::KEY_S))
    {
      cmd_data.chassis.y -= 0.5f;
    }
    if (KEY & static_cast<uint16_t>(KeyBoard::KEY_W))
    {
      cmd_data.chassis.y += 0.5f;
    }
    if (KEY & static_cast<uint16_t>(KeyBoard::KEY_SHIFT))
    {
      cmd_data.chassis.x *= 2.0f;
      cmd_data.chassis.y *= 2.0f;
    }
    cmd_data.chassis.z = 0.0f;

    cmd_data.gimbal.pit = -static_cast<float>(rc.mouse_y) * MOUSE_SCALER;
    cmd_data.gimbal.yaw = -static_cast<float>(rc.mouse_x) * MOUSE_SCALER;
    cmd_data.gimbal.rol = 0.0f;

    cmd_data.stir.isfire = (rc.button_l != 0);
    cmd_data.chassis_online = true;
    cmd_data.gimbal_online = true;
    cmd_data.stir_online = true;
    cmd_data.ctrl_source = CMD::ControlSource::CTRL_SOURCE_RC;
    cmd_->FeedRC(cmd_data);
  }

  /**
   * @brief 上一次解析成功时，发布底盘、发射与比赛摘要 Topic 并更新雷达数据包。
   *        When the last parse succeeded, publish the chassis, launcher and game summary
   *        Topics and update the radar data packet.
   */
  void Publish()
  {
    if (!this->last_parse_)
    {
      return;
    }
    this->cp_.rs = this->data_.robot_status;
    this->cp_.power_buffer = this->data_.power_heat.chassis_pwr_buff;
    this->chassispack_topic_.Publish(this->cp_);
    this->lp_.rs = this->data_.robot_status;
    this->lp_.rb = this->data_.robot_buff;
    this->lp_.ld = this->data_.launcher_data;
    this->lp_.dc = this->data_.dart_client;
    this->lp_.ph = this->data_.power_heat;
    this->launcherpack_topic_.Publish(this->lp_);
    this->robot_game_referee_pack_.robot_status = this->data_.robot_status;
    this->robot_game_referee_pack_.game_status = this->data_.game_status;
    this->robot_game_referee_pack_.sentry_info = this->data_.sentry_decision;
    this->robot_game_referee_pack_.rfid = this->data_.rfid;
    this->robot_game_referee_pack_.bullet_17_remain =
        this->data_.bullet_remain.bullet_17_remain;
    this->robot_game_referee_pack_.our_outpose = this->data_.game_robot_hp.our_outpose;
    this->robot_game_referee_pack_.red_base = this->data_.game_robot_hp.red_base;
    this->robot_game_referee_pack_.sentry_pos = this->data_.sentry_pos;
    this->robot_game_referee_pack_.robot_pos = this->data_.robot_pos;
    this->robot_game_referee_topic_.Publish(this->robot_game_referee_pack_);
    UpdateRadarPack();
  }

 private:
  void UpdateRadarPack()
  {
    LibXR::Mutex::LockGuard lock(tx_data_mutex_);
    this->radar_pack_.rf = this->data_.sentry_pos;
    this->radar_pack_.x = this->data_.robot_pos.x;
    this->radar_pack_.y = this->data_.robot_pos.y;
  }

  /* 数据读取 */
  LibXR::UART* uart_;
  LibXR::Semaphore sem_;
  LibXR::ReadOperation op_;
  LibXR::Semaphore sem_tx_;
  LibXR::WriteOperation tx_op_;
  LibXR::Mutex tx_mutex_;
  LibXR::Mutex tx_data_mutex_;
  CMD* cmd_;
  uint8_t tx_buf_[256]{};
  uint8_t tx_seq_ = 0;
  uint8_t byte_ = 0;

  /* 协议,数据流 */
  struct [[gnu::packed]]
  {
    Header header_;
    uint8_t buf_[251]; /* 缓冲区，对齐256 */
  } pack_;
  Data data_; /* 数据包本体 */
  LibXR::Topic chassispack_topic_;
  ChassisPack cp_; /* 发给底盘的数据包缓冲 */
  LibXR::Topic launcherpack_topic_;
  LauncherPack lp_; /* 发给发射的数据包缓冲 */
  LibXR::Topic robot_game_referee_topic_;
  RobotGameRefereePack robot_game_referee_pack_; /* 裁判系统摘要包缓冲 */
  LibXR::Topic radar_pack_topic_;
  RadarPack radar_pack_;

  bool last_parse_; /* 上一次解包是否成功 */

  /* 线程相关 */
  uint64_t last_wake_up_; /* ms */
  LibXR::Thread thread_;
};
