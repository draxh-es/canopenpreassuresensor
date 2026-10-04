/*
 * canopen_pressure.h
 * BCT 22 (CANopen) basinc sensoru okuyucu
 * Detaylar: README.md
 */

#ifndef CANOPEN_PRESSURE_H
#define CANOPEN_PRESSURE_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* PDO'daki olcum tipi (0x1A00 eslemesine gore) */
typedef enum {
    CP_TYPE_REAL32 = 0,   // 0x6130
    CP_TYPE_INT16,        // 0x7130
    CP_TYPE_INT24,        // 0x8130
    CP_TYPE_INT32         // 0x9130
} CP_DataType;

/* Hat */
#define CP_CAN_BITRATE        125000u    // CubeMX ile ayni olmali (sensor fabrika: 125k)

/* Sensorler */
#define CP_NODE_COUNT         1u
#define CP_NODE_IDS           { 1 }      // fabrika node-ID: 1

/* PDO icerigi - README 8. bolum, 3. adim */
#define CP_PDO_COBID_BASE     0x180u     // TPDO1
#define CP_PDO_TYPE           CP_TYPE_REAL32
#define CP_PDO_OFFSET         0u         // bayt
#define CP_PDO_SCALE          1.0f       // ham deger -> bar

/* Zamanlar (ms) */
#define CP_PDO_PERIOD_MS      10u        // sensordeki event timer
#define CP_PDO_TIMEOUT_MS     (3u * CP_PDO_PERIOD_MS)
#define CP_RESTART_MS         500u       // veri yoksa NMT start tekrari
#define CP_SDO_TIMEOUT_MS     100u
#define CP_SAVE_TIMEOUT_MS    1000u      // kaydetme uzun surebilir

/* Ortak kaynaklar, baska modul kullanmasin */
#define CP_CAN_FILTER_BANK    0u         // bu bankadan itibaren 5 banka
#define CP_CAN_RX_FIFO        CAN_RX_FIFO1

/* Kurulum araclari (SDO, LSS) */
#define CP_ENABLE_SETUP       1          // ucus yaziliminda 0 yapilabilir

/* NMT komutlari */
#define CP_NMT_START          0x01u
#define CP_NMT_STOP           0x02u
#define CP_NMT_PREOP          0x80u
#define CP_NMT_RESET          0x81u
#define CP_NMT_RESET_COMM     0x82u

/* Sensor durumu (heartbeat) */
#define CP_STATE_BOOT         0x00u
#define CP_STATE_STOPPED      0x04u
#define CP_STATE_OPER         0x05u
#define CP_STATE_PREOP        0x7Fu
#define CP_STATE_UNKNOWN      0xFFu

typedef enum {
    CP_OK = 0,
    CP_ERR_BITRATE,       // CubeMX hizi CP_CAN_BITRATE ile uyusmuyor
    CP_ERR_FILTER,
    CP_ERR_CAN_START,     // transceiver veya hat sorunu
    CP_ERR_NOT_READY,     // once CP_Init
    CP_ERR_TX,            // gonderilemedi
    CP_ERR_TIMEOUT,       // cevap gelmedi
    CP_ERR_ABORT,         // SDO reddedildi, kod: cp_stats.sdo_abort
    CP_ERR_LSS,           // LSS hata dondu
    CP_ERR_PARAM,
    CP_ERR_UNSUPPORTED
} CP_Status;

typedef struct {
    float    bar;
    bool     valid;        // veri var ve taze
    uint32_t age_ms;       // son PDO'dan beri
    uint32_t interval_ms;  // son iki PDO arasi
    uint8_t  state;        // CP_STATE_x
    uint16_t emcy_code;    // son hata kodu, 0: yok
    uint8_t  emcy_reg;
} CP_Reading;

typedef struct {
    uint32_t rx_pdo;
    uint32_t rx_hb;
    uint32_t rx_emcy;
    uint32_t nmt_sent;
    uint32_t tx_fail;
    uint32_t sdo_abort;    // son SDO abort kodu
} CP_Stats;

extern volatile CP_Stats cp_stats;          // Live Expressions ile izle

CP_Status CP_Init(CAN_HandleTypeDef *hcan);
void      CP_Process(void);                 // while(1) icinde
bool      CP_Get(uint8_t node_id, CP_Reading *out);
CP_Status CP_NmtSend(uint8_t node_id, uint8_t cmd);

#if CP_ENABLE_SETUP
/* Sadece tezgahta, ana donguden once. Cevap bekleyerek calisir. */

typedef enum {
    CP_LSS_1000K = 0,
    CP_LSS_800K  = 1,
    CP_LSS_500K  = 2,
    CP_LSS_250K  = 3,
    CP_LSS_125K  = 4,
    CP_LSS_50K   = 6,
    CP_LSS_20K   = 7,
    CP_LSS_10K   = 8
} CP_LssBitrate;

#define CP_LSS_WAITING        0x00u
#define CP_LSS_CONFIG         0x01u

CP_Status CP_SdoRead(uint8_t node_id, uint16_t index, uint8_t sub, uint32_t *value);
CP_Status CP_SdoWrite(uint8_t node_id, uint16_t index, uint8_t sub, uint32_t value, uint8_t size);
CP_Status CP_LssSwitchGlobal(uint8_t mode);
CP_Status CP_LssSetNodeId(uint8_t new_id);
CP_Status CP_LssSetBitrate(CP_LssBitrate rate);
CP_Status CP_LssStore(void);
#endif

#endif
