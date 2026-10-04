/*
 * kurulum_main.c
 * Tezgah kurulumu, sensor basina bir kez.
 *
 * Bu dosya derlenmez. Parcalar CubeMX'in main.c dosyasindaki
 * ayni isimli USER CODE bloklarina kopyalanir.
 *
 * Hazirlik:
 *  - Hatta sadece bu sensor ve kart olsun
 *  - CubeMX: CAN1 = 125 kbit/s (sensor fabrika ayari)
 *  - canopen_pressure.h: CP_CAN_BITRATE = 125000, CP_NODE_IDS = { 1 }
 */

/* USER CODE BEGIN Includes */
#include "canopen_pressure.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */

uint32_t   dev_type;     // alt 16 bit 0x0194 olmali (CiA 404)
uint32_t   map_n;        // TPDO1'deki nesne sayisi
uint32_t   map1;         // TPDO1'in ilk nesnesi, ornek 0x61300120
uint32_t   tx_type;      // 254 veya 255 olmali
uint32_t   event_ms;     // fabrika ayari 1000
uint32_t   hb_ms;        // 0 ise heartbeat kapali
CP_Status  st[16];       // hepsi 0 (CP_OK) olmali
CP_Reading olcum;        // ayarlardan sonra canli veri
/* USER CODE END PV */

int main(void)
{
  /* CubeMX kodu: HAL_Init, SystemClock_Config, MX_GPIO_Init, MX_CAN1_Init */

  /* USER CODE BEGIN 2 */
  st[0] = CP_Init(&hcan1);

  /* 1. adim: sensoru tani (sadece okuma) */
  st[1] = CP_SdoRead(1, 0x1000, 0, &dev_type);    // cihaz tipi
  st[2] = CP_SdoRead(1, 0x1A00, 0, &map_n);       // TPDO1 nesne sayisi
  st[3] = CP_SdoRead(1, 0x1A00, 1, &map1);        // TPDO1 ilk nesne
  st[4] = CP_SdoRead(1, 0x1800, 2, &tx_type);     // gonderim tipi
  st[5] = CP_SdoRead(1, 0x1800, 5, &event_ms);    // gonderim periyodu
  st[6] = CP_SdoRead(1, 0x1017, 0, &hb_ms);       // heartbeat

#if 0  /* 2. adim: okumalar kontrol edildikten sonra 1 yap */
  if (tx_type != 254u && tx_type != 255u)
    st[7] = CP_SdoWrite(1, 0x1800, 2, 255, 1);         // zaman tetiklemeli
  st[8]  = CP_SdoWrite(1, 0x1800, 5, 10, 2);           // 10 ms = 100 Hz
  st[9]  = CP_SdoWrite(1, 0x1017, 0, 100, 2);          // heartbeat 100 ms
  st[10] = CP_SdoWrite(1, 0x1010, 1, 0x65766173, 4);   // "save": kalici kaydet
#endif

#if 0  /* Istege bagli: node-ID ve bit hizi. Hatta TEK sensor varken! */
  st[11] = CP_LssSwitchGlobal(CP_LSS_CONFIG);
  st[12] = CP_LssSetNodeId(5);                   // yeni node-ID
  st[13] = CP_LssSetBitrate(CP_LSS_500K);        // yeni bit hizi
  st[14] = CP_LssStore();
  st[15] = CP_LssSwitchGlobal(CP_LSS_WAITING);
  /* sonra: sensorun gucunu kes-ver, CubeMX ve canopen_pressure.h'yi yeni degerlere gore guncelle */
#endif
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    CP_Process();          // sensoru baslatir
    CP_Get(1, &olcum);     // ayardan sonra interval_ms ~10, valid = 1 olmali
  }
  /* USER CODE END 3 */
}
