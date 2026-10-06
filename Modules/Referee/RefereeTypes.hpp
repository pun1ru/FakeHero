#pragma once

#include <cstdint>

/**
 * @brief 裁判模块发布的数据类型。
 *        Data types published by the referee Module.
 */
namespace RefereeTypes
{
/**
 * @brief 0x0001 比赛状态数据。
 *        0x0001 game status data.
 */
struct [[gnu::packed]] GameStatus
{
  uint8_t game_type : 4;      /* 比赛类型 */
  uint8_t game_progress : 4;  /* 当前比赛阶段 */
  uint16_t stage_remain_time; /* 当前阶段剩余时间 */
  uint64_t sync_time_stamp;   /* 时间戳 */
};

/**
 * @brief 0x0201 机器人性能体系数据。
 *        0x0201 robot performance data.
 */
struct [[gnu::packed]] RobotStatus
{
  uint8_t robot_id;                  /* 本机器人 ID */
  uint8_t robot_level;               /* 机器人等级 */
  uint16_t remain_hp;                /* 机器人当前血量 */
  uint16_t max_hp;                   /* 机器人血量上限 */
  uint16_t shooter_cooling_value;    /* 机器人射击热量每秒冷却值 */
  uint16_t shooter_heat_limit;       /* 机器人射击热量上限 */
  uint16_t chassis_power_limit;      /* 机器人底盘功率上限 */
  uint8_t power_gimbal_output : 1;   /* gimbal输出，0为无输出，1为24V输出 */
  uint8_t power_chassis_output : 1;  /* chassis输出，0为无输出，1为24V输出*/
  uint8_t power_launcher_output : 1; /* shooter输出，0为无输出，1为24V 输出 */
};

/**
 * @brief 0x0203 机器人位置数据。
 *        0x0203 robot position data.
 */
struct [[gnu::packed]] RobotPOS
{
  float x;     /* 本机器人位置x坐标，单位m */
  float y;     /* 本机器人位置y坐标，单位m */
  float angle; /* 本机器人测速模块的朝向 单位：度 正北为0度 */
};

/**
 * @brief 0x0209 机器人 RFID 模块状态。
 *        0x0209 robot RFID module status.
 */
struct [[gnu::packed]] RFID
{
  uint32_t own_base : 1;                          /*己方基地增益点*/
  uint32_t own_highland_center : 1;               /*己方中央高地增益点*/
  uint32_t enemy_highland_center : 1;             /*对方中央高地增益点*/
  uint32_t own_trapezium : 1;                     /*己方梯形高地增益点*/
  uint32_t enemy_trapezium : 1;                   /*对方梯形高地增益点*/
  uint32_t own_slope_before_R1B1 : 1;             /*己方飞坡点（靠近己方一侧飞坡前*/
  uint32_t own_slope_after_R1B1 : 1;              /*己方飞坡点（靠近己方一侧飞坡后*/
  uint32_t enemy_slope_before_R4B4 : 1;           /*对方飞坡点（靠近己方一侧飞坡前*/
  uint32_t enemy_slope_after_R4B4 : 1;            /*对方飞坡点（靠近己方一侧飞坡后*/
  uint32_t own_terrain_crossing_up_R2B2 : 1;      /*己方地形增益(中央高地下方*/
  uint32_t own_terrain_crossing_down_R2B2 : 1;    /*己方地形增益(中央高地上方*/
  uint32_t enemy_terrain_corrssing_up_R2B2 : 1;   /*对方地形增益(中央高地下方*/
  uint32_t enemy_terrain_corrssing_down_R2B2 : 1; /*对方地形增益(中央高地上*/
  uint32_t own_terrain_crossing_up_R3B3 : 1;      /*己方地形增益点(公路下方*/
  uint32_t own_terrain_crossing_down_R3B3 : 1;    /*己方地形增益点(公路上方*/
  uint32_t enemy_terrain_corrssing_up_R3B5 : 1;   /*对方地形增益点(公路下方*/
  uint32_t enemy_terrain_corrssing_down_R3B3 : 1; /*对方地形增益(公路上方*/
  uint32_t own_fortress : 1;                      /*己方堡垒增益点*/
  uint32_t own_outpost : 1;                       /*己方前哨站增益点*/
  uint32_t own_blood_supply_unoverlapping : 1;    /*与资源区不重叠的/UL补给区*/
  uint32_t own_blood_supply_overlapping : 1;      /*己方与资源区重叠的补给区*/
  uint32_t own_assemble : 1;                      /*己方装配增益点*/
  uint32_t enemy_assemble : 1;                    /*对方装配增益点*/
  uint32_t center_resource_RMUL : 1;              /*中心增益点（仅 RMUL 适用）*/
  uint32_t enemy_fortress : 1;                    /*对方堡垒增益点*/
  uint32_t enemy_outpost : 1;                     /*对方前哨站增益点*/
  uint32_t own_tunnel_cross_down : 1;             /*己方隧道增益点（己方一侧公路区下方）*/
  uint32_t own_tunnel_cross_up : 1;               /*己方隧道增益点（己方一侧公路区上方）*/
  uint32_t own_tunnel_zrapezium_down : 1;         /*己方隧道增益(己方梯形高地较低处*/
  uint32_t own_tunnel_zrapezium_up : 1;           /*己方隧道增益(己方梯形高地较高处*/
  uint32_t enemy_tunnel_cross_down : 1;           /*对方隧道增益（对方一侧公路区下方*/
  uint32_t enemy_tunnel_cross_up : 1;             /*对方隧道增益（对方一侧公路区上方*/

  uint32_t enemy_tunnel_zrapezium_down : 1; /*对方隧道增益(对方梯形高地低处*/
  uint32_t enemy_tunnel_zrapezium_up : 1;   /*对方隧道增益(对方梯形高地较高处*/
};

/**
 * @brief 0x020B 地面机器人位置数据。
 *        0x020B ground robot position data.
 */
struct [[gnu::packed]] RobotPosForSentry
{
  float hero_x;       /*己方英雄机器人位置 x 轴坐标，单位：m*/
  float hero_y;       /*己方英雄机器人位置 y 轴坐标，单位：m*/
  float engineer_x;   /*己方工程机器人位置 x 轴坐标，单位：m*/
  float engineer_y;   /*己方工程机器人位置 y 轴坐标，单位：m*/
  float standard_3_x; /*己方 3 号步兵机器人位置 x 轴坐标，单位：m*/
  float standard_3_y; /*己方 3 号步兵机器人位置 y 轴坐标，单位：m*/
  float standard_4_x; /*己方 4 号步兵机器人位置 x 轴坐标，单位：m*/
  float standard_4_y; /*己方 4 号步兵机器人位置 y 轴坐标，单位：m*/
  float res_1;        /*保留位*/
  float res_2;        /*保留位*/
};

/**
 * @brief 0x020D 哨兵自主决策相关信息同步。
 *        0x020D sentry autonomous decision information.
 */
struct [[gnu::packed]] SentryInfo
{
  uint32_t exchanged_bullet_num : 11;  /*允许发弹量*/
  uint32_t exchanged_bullet_times : 4; /*成功远程兑换允许发弹量的次数*/
  uint32_t exchanged_blood_times : 4;  /*哨兵机器人成功远程兑换血量的次数*/
  uint32_t could_risen_free : 1;       /*当前是否可以确认免费复活*/
  uint32_t could_risen_exchanged : 1;  /*哨兵机器人当前是否可以兑换立即复活*/
  uint32_t risen_cost : 10;            /*哨兵机器人当前若兑换立即复活需要花费的金币数*/
  uint32_t res1 : 1;                   /*保留位*/

  uint32_t current_state : 2;  /*哨兵当前姿态*/
  uint32_t own_mech_state : 1; /* 己方能量机关是否能进入正在激活状态 */
  uint32_t res2 : 1;           /*保留位*/
};

/**
 * @brief 机器人、比赛与发射相关的裁判系统摘要，紧凑布局共 92 字节。
 *        Referee summary of robot, game and launcher data, 92 bytes in packed layout.
 */
struct [[gnu::packed]] RobotGameRefereePack
{
  RobotStatus robot_status;     /* 机器人状态 */
  GameStatus game_status;       /* 比赛信息 */
  SentryInfo sentry_info;       /*哨兵数据*/
  RFID rfid;                    /*机器人RFID模块状态*/
  uint16_t bullet_17_remain;    /*  17mm 弹丸允许发弹量 */
  uint16_t our_outpose;         /* 己方前哨站 */
  uint16_t red_base;            /* 己方基地 */
  RobotPosForSentry sentry_pos; /* 0x020B */
  RobotPOS robot_pos;
};
}  // namespace RefereeTypes
