/*
 * canopen_pressure.c
 * BCT 22 (CANopen) basinc sensoru okuyucu
 */

#include "canopen_pressure.h"
#include <string.h>

#if (CP_NODE_COUNT < 1u)
#error "En az bir sensor olmali"
#endif

/* CANopen sabit ID'leri */
#define COB_NMT        0x000u
#define COB_EMCY       0x080u
#define COB_SDO_RESP   0x580u     // sensor -> biz
#define COB_SDO_REQ    0x600u     // biz -> sensor
#define COB_HB         0x700u
#define COB_LSS_REQ    0x7E5u
#define COB_LSS_RESP   0x7E4u

#define FUNC_MASK      0x780u     // ID'nin fonksiyon kismi

typedef struct {
    uint8_t  id;
    uint8_t  state;
    bool     have_data;
    float    bar;
    uint32_t t_pdo;
    uint32_t interval;
    uint32_t t_start;
    uint16_t emcy_code;
    uint8_t  emcy_reg;
} Node;

volatile CP_Stats cp_stats;

static CAN_HandleTypeDef *s_hcan;
static bool s_ready;
static Node s_node[CP_NODE_COUNT];
static const uint8_t s_ids[CP_NODE_COUNT] = CP_NODE_IDS;


/* Yardimcilar */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static Node *find_node(uint8_t id)
{
    for (uint32_t i = 0; i < CP_NODE_COUNT; i++)
        if (s_node[i].id == id)
            return &s_node[i];
    return NULL;
}


/* Donanim katmani - FDCAN'a geciste sadece bu kisim degisir */

static bool hw_send(uint16_t id, const uint8_t *d, uint8_t len)
{
    CAN_TxHeaderTypeDef h = {0};
    uint8_t  buf[8] = {0};
    uint32_t mailbox;

    memcpy(buf, d, len);
    h.StdId = id;
    h.IDE   = CAN_ID_STD;
    h.RTR   = CAN_RTR_DATA;
    h.DLC   = len;
    h.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_GetTxMailboxesFreeLevel(s_hcan) == 0)
        return false;    // 3 kutu da dolu

    return HAL_CAN_AddTxMessage(s_hcan, &h, buf, &mailbox) == HAL_OK;
}

static bool hw_read(uint16_t *id, uint8_t d[8], uint8_t *len)
{
    CAN_RxHeaderTypeDef h;

    if (HAL_CAN_GetRxFifoFillLevel(s_hcan, CP_CAN_RX_FIFO) == 0)
        return false;
    if (HAL_CAN_GetRxMessage(s_hcan, CP_CAN_RX_FIFO, &h, d) != HAL_OK)
        return false;

    *id  = (h.IDE == CAN_ID_STD) ? (uint16_t)h.StdId : 0xFFFFu;
    *len = (uint8_t)h.DLC;
    return true;
}

// 32 bit maske filtresi, sadece standart veri cercevesi
static bool add_filter(uint32_t bank, uint16_t id, uint16_t mask)
{
    CAN_FilterTypeDef f = {0};

    f.FilterBank           = bank;
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterIdHigh         = (uint32_t)id << 5;
    f.FilterIdLow          = 0;
    f.FilterMaskIdHigh     = (uint32_t)mask << 5;
    f.FilterMaskIdLow      = 0x0006;    // IDE=0, RTR=0
    f.FilterFIFOAssignment = (CP_CAN_RX_FIFO == CAN_RX_FIFO0) ? CAN_FILTER_FIFO0
                                                              : CAN_FILTER_FIFO1;
    f.FilterActivation     = ENABLE;
    f.SlaveStartFilterBank = 14;

    return HAL_CAN_ConfigFilter(s_hcan, &f) == HAL_OK;
}

// CubeMX'teki CAN ayari saatle uyumlu mu
static bool bitrate_ok(void)
{
    uint32_t btr = s_hcan->Instance->BTR;
    uint32_t brp = ((btr & CAN_BTR_BRP_Msk) >> CAN_BTR_BRP_Pos) + 1u;
    uint32_t ts1 = ((btr & CAN_BTR_TS1_Msk) >> CAN_BTR_TS1_Pos) + 1u;
    uint32_t ts2 = ((btr & CAN_BTR_TS2_Msk) >> CAN_BTR_TS2_Pos) + 1u;

    uint32_t rate = HAL_RCC_GetPCLK1Freq() / (brp * (1u + ts1 + ts2));
    uint32_t diff = (rate > CP_CAN_BITRATE) ? (rate - CP_CAN_BITRATE)
                                            : (CP_CAN_BITRATE - rate);

    return diff <= (CP_CAN_BITRATE / 200u);    // %0.5
}


/* PDO cozme */

static uint8_t type_size(void)
{
    switch (CP_PDO_TYPE) {
    case CP_TYPE_INT16: return 2;
    case CP_TYPE_INT24: return 3;
    default:            return 4;    // REAL32, INT32
    }
}

static float decode(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8);

    switch (CP_PDO_TYPE) {
    case CP_TYPE_INT16:
        return (float)(int16_t)u * CP_PDO_SCALE;

    case CP_TYPE_INT24:
        u |= (uint32_t)p[2] << 16;
        if (u & 0x800000u)
            u |= 0xFF000000u;        // isaret genislet
        return (float)(int32_t)u * CP_PDO_SCALE;

    case CP_TYPE_INT32:
        return (float)(int32_t)rd32(p) * CP_PDO_SCALE;

    default: {                       // REAL32
        uint32_t raw = rd32(p);
        float f;
        memcpy(&f, &raw, sizeof(f));
        return f * CP_PDO_SCALE;
    }
    }
}


/* Gelen mesajlar */

static void handle_frame(uint16_t id, const uint8_t *d, uint8_t len)
{
    Node    *n   = find_node((uint8_t)(id & 0x7Fu));
    uint32_t now = HAL_GetTick();

    if (n == NULL)
        return;    // listede olmayan dugum

    switch (id & FUNC_MASK) {
    case CP_PDO_COBID_BASE:
        if (len < CP_PDO_OFFSET + type_size())
            break;
        if (n->have_data)
            n->interval = now - n->t_pdo;
        n->bar       = decode(&d[CP_PDO_OFFSET]);
        n->t_pdo     = now;
        n->have_data = true;
        n->state     = CP_STATE_OPER;
        cp_stats.rx_pdo++;
        break;

    case COB_HB:
        if (len < 1)
            break;
        n->state = d[0] & 0x7Fu;
        if (n->state == CP_STATE_BOOT) {     // sensor yeniden basladi
            n->have_data = false;
            n->t_start   = now - CP_RESTART_MS;   // hemen start gonder
        }
        cp_stats.rx_hb++;
        break;

    case COB_EMCY:
        if (len < 3)
            break;
        n->emcy_code = (uint16_t)(d[0] | (d[1] << 8));
        n->emcy_reg  = d[2];
        cp_stats.rx_emcy++;
        break;

    default:
        break;    // SDO ve LSS cevaplari kurulum fonksiyonlarinda
    }
}


/* Disari acik fonksiyonlar */

CP_Status CP_Init(CAN_HandleTypeDef *hcan)
{
    uint32_t b = CP_CAN_FILTER_BANK;
    uint32_t now;

    s_hcan  = hcan;
    s_ready = false;

    if (!bitrate_ok())
        return CP_ERR_BITRATE;

    if (!add_filter(b + 0, CP_PDO_COBID_BASE, FUNC_MASK) ||   // olcum
        !add_filter(b + 1, COB_EMCY,          FUNC_MASK) ||   // hata
        !add_filter(b + 2, COB_SDO_RESP,      FUNC_MASK) ||   // SDO cevap
        !add_filter(b + 3, COB_HB,            FUNC_MASK) ||   // heartbeat
        !add_filter(b + 4, COB_LSS_RESP,      0x7FFu))        // LSS cevap
        return CP_ERR_FILTER;

    // baska modul baslattiysa tekrar baslatma
    if (HAL_CAN_GetState(s_hcan) != HAL_CAN_STATE_LISTENING) {
        if (HAL_CAN_Start(s_hcan) != HAL_OK)
            return CP_ERR_CAN_START;
    }

    now = HAL_GetTick();
    for (uint32_t i = 0; i < CP_NODE_COUNT; i++) {
        memset(&s_node[i], 0, sizeof(s_node[i]));
        s_node[i].id      = s_ids[i];
        s_node[i].state   = CP_STATE_UNKNOWN;
        s_node[i].t_start = now - CP_RESTART_MS;   // ilk CP_Process'te start
    }

    s_ready = true;
    return CP_OK;
}

void CP_Process(void)
{
    uint16_t id;
    uint8_t  d[8];
    uint8_t  len;
    uint32_t now;

    if (!s_ready)
        return;

    for (uint32_t k = 0; k < 16u && hw_read(&id, d, &len); k++)
        handle_frame(id, d, len);

    // veri gelmeyen sensoru tekrar baslat
    now = HAL_GetTick();
    for (uint32_t i = 0; i < CP_NODE_COUNT; i++) {
        Node *n = &s_node[i];
        bool stale = !n->have_data || (now - n->t_pdo > CP_PDO_TIMEOUT_MS);

        if (stale && (now - n->t_start >= CP_RESTART_MS)) {
            n->t_start = now;
            CP_NmtSend(n->id, CP_NMT_START);
        }
    }
}

bool CP_Get(uint8_t node_id, CP_Reading *out)
{
    Node    *n = find_node(node_id);
    Node     c;
    uint32_t pm;

    if (n == NULL || out == NULL)
        return false;

    // RTOS'ta yarim okuma olmasin
    pm = __get_PRIMASK();
    __disable_irq();
    c = *n;
    __set_PRIMASK(pm);

    out->bar         = c.bar;
    out->age_ms      = c.have_data ? (HAL_GetTick() - c.t_pdo) : UINT32_MAX;
    out->valid       = c.have_data && (out->age_ms <= CP_PDO_TIMEOUT_MS);
    out->interval_ms = c.interval;
    out->state       = c.state;
    out->emcy_code   = c.emcy_code;
    out->emcy_reg    = c.emcy_reg;
    return true;
}

CP_Status CP_NmtSend(uint8_t node_id, uint8_t cmd)
{
    uint8_t d[2] = { cmd, node_id };

    if (!s_ready)
        return CP_ERR_NOT_READY;

    if (!hw_send(COB_NMT, d, 2)) {
        cp_stats.tx_fail++;
        return CP_ERR_TX;
    }

    cp_stats.nmt_sent++;
    return CP_OK;
}


#if CP_ENABLE_SETUP

/* SDO (sadece 4 bayta kadar, hizli aktarim) */

static CP_Status sdo_request(uint8_t node_id, const uint8_t req[8], uint8_t resp[8])
{
    uint16_t index   = (uint16_t)(req[1] | (req[2] << 8));
    uint32_t timeout = (index == 0x1010u || index == 0x1011u) ? CP_SAVE_TIMEOUT_MS
                                                              : CP_SDO_TIMEOUT_MS;
    uint32_t t0;
    uint16_t id;
    uint8_t  len;

    if (!s_ready)
        return CP_ERR_NOT_READY;
    if (!hw_send(COB_SDO_REQ + node_id, req, 8))
        return CP_ERR_TX;

    t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < timeout) {
        if (!hw_read(&id, resp, &len))
            continue;

        // baska mesajsa normal isle
        if (id != COB_SDO_RESP + node_id || len < 8 ||
            resp[1] != req[1] || resp[2] != req[2] || resp[3] != req[3]) {
            handle_frame(id, resp, len);
            continue;
        }

        if (resp[0] == 0x80u) {
            cp_stats.sdo_abort = rd32(&resp[4]);
            return CP_ERR_ABORT;
        }
        return CP_OK;
    }
    return CP_ERR_TIMEOUT;
}

CP_Status CP_SdoRead(uint8_t node_id, uint16_t index, uint8_t sub, uint32_t *value)
{
    uint8_t req[8] = { 0x40, (uint8_t)index, (uint8_t)(index >> 8), sub };
    uint8_t resp[8];
    CP_Status st;
    uint32_t v;

    if (value == NULL)
        return CP_ERR_PARAM;

    st = sdo_request(node_id, req, resp);
    if (st != CP_OK)
        return st;

    if ((resp[0] & 0xE0u) != 0x40u || !(resp[0] & 0x02u))
        return CP_ERR_UNSUPPORTED;    // 4 bayttan uzun veri

    v = rd32(&resp[4]);
    if (resp[0] & 0x01u) {            // boyut belli
        uint8_t unused = (resp[0] >> 2) & 0x03u;
        if (unused)
            v &= 0xFFFFFFFFu >> (8u * unused);
    }

    *value = v;
    return CP_OK;
}

CP_Status CP_SdoWrite(uint8_t node_id, uint16_t index, uint8_t sub, uint32_t value, uint8_t size)
{
    static const uint8_t cmd[5] = { 0, 0x2F, 0x2B, 0x27, 0x23 };
    uint8_t req[8]  = { 0, (uint8_t)index, (uint8_t)(index >> 8), sub };
    uint8_t resp[8];
    CP_Status st;

    if (size < 1u || size > 4u)
        return CP_ERR_PARAM;

    req[0] = cmd[size];
    for (uint8_t i = 0; i < size; i++)
        req[4 + i] = (uint8_t)(value >> (8u * i));

    st = sdo_request(node_id, req, resp);
    if (st != CP_OK)
        return st;

    return (resp[0] == 0x60u) ? CP_OK : CP_ERR_UNSUPPORTED;
}


/* LSS - node-ID ve bit hizi. Hatta tek sensor varken kullan! */

static CP_Status lss_request(const uint8_t req[8], bool wait_reply, uint32_t timeout)
{
    uint8_t  resp[8];
    uint8_t  len;
    uint16_t id;
    uint32_t t0;

    if (!s_ready)
        return CP_ERR_NOT_READY;
    if (!hw_send(COB_LSS_REQ, req, 8))
        return CP_ERR_TX;
    if (!wait_reply)
        return CP_OK;

    t0 = HAL_GetTick();
    while (HAL_GetTick() - t0 < timeout) {
        if (!hw_read(&id, resp, &len))
            continue;

        if (id != COB_LSS_RESP || len < 2 || resp[0] != req[0]) {
            handle_frame(id, resp, len);
            continue;
        }
        return (resp[1] == 0) ? CP_OK : CP_ERR_LSS;
    }
    return CP_ERR_TIMEOUT;
}

CP_Status CP_LssSwitchGlobal(uint8_t mode)
{
    uint8_t req[8] = { 0x04, mode };
    return lss_request(req, false, 0);
}

CP_Status CP_LssSetNodeId(uint8_t new_id)
{
    uint8_t req[8] = { 0x11, new_id };

    if (new_id < 1u || new_id > 127u)
        return CP_ERR_PARAM;
    return lss_request(req, true, CP_SDO_TIMEOUT_MS);
}

CP_Status CP_LssSetBitrate(CP_LssBitrate rate)
{
    uint8_t req[8] = { 0x13, 0x00, (uint8_t)rate };    // 0: standart tablo
    return lss_request(req, true, CP_SDO_TIMEOUT_MS);
}

CP_Status CP_LssStore(void)
{
    uint8_t req[8] = { 0x17 };
    return lss_request(req, true, CP_SAVE_TIMEOUT_MS);
}

#endif
