/*
 * ucus_main.c
 * Ucus yazilimi: sensoru baslatir ve basinci okur.
 *
 * Bu dosya derlenmez. Parcalar CubeMX'in main.c dosyasindaki
 * ayni isimli USER CODE bloklarina kopyalanir.
 *
 * Once kurulum_main.c ile sensor bir kez ayarlanmis olmali.
 */

/* USER CODE BEGIN Includes */
#include "canopen_pressure.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
CP_Reading tank;          // Live Expressions ile izlenebilir
/* USER CODE END PV */

int main(void)
{
  /* CubeMX kodu: HAL_Init, SystemClock_Config, MX_GPIO_Init, MX_CAN1_Init */

  /* USER CODE BEGIN 2 */
  if (CP_Init(&hcan1) != CP_OK)
  {
    Error_Handler();      // CAN ayari veya transceiver sorunu
  }
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    CP_Process();         // start'i kendisi gonderir, mesajlari okur

    if (CP_Get(1, &tank) && tank.valid)
    {
      // tank.bar kullanilabilir
    }
    else
    {
      // veri yok veya eski
    }

    if (tank.emcy_code != 0)
    {
      // sensor hata bildirdi
    }
  }
  /* USER CODE END 3 */
}
