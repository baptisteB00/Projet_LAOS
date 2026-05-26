#include <Arduino.h>
#include <Adafruit_AM2315.h>
#include <CAN.h>

EventGroupHandle_t xSystemEventGroup;
EventGroupHandle_t xSerialEventGroup;
SemaphoreHandle_t xSerialMutex;
QueueHandle_t xCanTxQueue;
QueueHandle_t xTempHumQueue;
QueueHandle_t xIrrQueue;

#define EVENT_TEMP          BIT0
#define EVENT_HUM           BIT1
#define EVENT_IRR           BIT2
#define EVENT_ALL_TEMP_HUM  BIT3
#define EVENT_ALL_IRR       BIT4
#define EVENT_ALL_REG       BIT5
#define EVENT_CARD_NUM      BIT6
#define EVENT_SERIAL_MSG_RX BIT7

#define PIN_CPT_IRR 34

typedef struct
{
  float temperature;
  float humidity;
} TempHumData;

typedef struct CANMessage
{
  unsigned int id = 0;
  char len = 0;
  unsigned char data[8] = {0};
} CANMessage;

void task_CAN_TX(void *pvParameters);
void task_Measure_IRR(void *pvParameters);
void task_Measure_TEMP_HUM(void *pvParameters);
void task_Grouping(void *pvParameters);
void task_Serial_RX(void *pvParameters);
void task_Send_Card_Num(void *pvParameters);
void onReceiveCan(int packetSize);
void onReceiveSerial();

void setup()
{
  Serial.begin(115200);

  if (!CAN.begin(10E3))
  {
    Serial.printf("can marche pas \n");
    while (1)
      ;
  }

  Serial.onReceive(onReceiveSerial);
  CAN.onReceive(onReceiveCan);

  xIrrQueue = xQueueCreate(3, sizeof(int));
  xCanTxQueue = xQueueCreate(5, sizeof(CANMessage));
  xTempHumQueue = xQueueCreate(3, sizeof(TempHumData));
  xSystemEventGroup = xEventGroupCreate();
  xSerialEventGroup = xEventGroupCreate();
  xSerialMutex = xSemaphoreCreateMutex();

  xTaskCreate(task_CAN_TX, "TASK_TX_CAN", 3072, NULL, 10, NULL);
  xTaskCreate(task_Measure_IRR, "TASK_MEASURE_IRR", 2048, NULL, 9, NULL);
  xTaskCreate(task_Measure_TEMP_HUM, "TASK_MEASURE_TEMP_HUM", 2048, NULL, 9, NULL);
  xTaskCreate(task_Grouping, "TASK_GROUPING", 2048, NULL, 9, NULL);
  xTaskCreate(task_Serial_RX, "TASK_SERIAL_RX", 2048, NULL, 10, NULL);
  xTaskCreate(task_Send_Card_Num, "TASK_CARD_NUM", 2048, NULL, 9, NULL);
}

void loop()
{
}

void task_CAN_TX(void *pvParameters)
{
  CANMessage txMsg;
  while (1)
  {
    xQueueReceive(xCanTxQueue, &txMsg, portMAX_DELAY);
    CAN.beginPacket(txMsg.id);
    CAN.write(txMsg.data, txMsg.len);
    CAN.endPacket();

    xSemaphoreTake(xSerialMutex, portMAX_DELAY);
    if (txMsg.id == 42) {
      float hum = ((txMsg.data[0] << 8) | txMsg.data[1]) / 100.0;
      Serial.printf("[CAN TX] Humidite envoyee : %.2f %%\n", hum);
    } else if (txMsg.id == 43) {
      float temp = ((txMsg.data[0] << 8) | txMsg.data[1]) / 100.0;
      Serial.printf("[CAN TX] Temperature envoyee : %.2f C\n", temp);
    } else if (txMsg.id == 44) {
      int irr = (txMsg.data[0] << 8) | txMsg.data[1];
      Serial.printf("[CAN TX] Irradiance envoyee : %d\n", irr / 100);
    } else if (txMsg.id == 45) {
      float hum = ((txMsg.data[0] << 8) | txMsg.data[1]) / 100.0;
      float temp = ((txMsg.data[2] << 8) | txMsg.data[3]) / 100.0;
      int irr = (txMsg.data[4] << 8) | txMsg.data[5];
      //Serial.printf("[CAN TX] Groupement envoye -> Temp: %.2f C | Hum: %.2f %% | Irr: %d\n", temp, hum, irr / 100);
      Serial.printf("%.2f;%.2f;%.2d\n\r",temp,hum,irr/100);
    }
    xSemaphoreGive(xSerialMutex);
  }
}

void onReceiveCan(int packetSize)
{
  CANMessage rxMsg;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  rxMsg.id = CAN.packetId();
  rxMsg.len = CAN.packetDlc();
  int i = 0;
  while (CAN.available())
  {
    rxMsg.data[i] = CAN.read();
    i++;
  }

  if (rxMsg.id == 2)
  {
    xEventGroupSetBitsFromISR(xSystemEventGroup, EVENT_HUM, &xHigherPriorityTaskWoken);
  }
  else if (rxMsg.id == 3)
  {
    xEventGroupSetBitsFromISR(xSystemEventGroup, EVENT_TEMP, &xHigherPriorityTaskWoken);
  }
  else if (rxMsg.id == (4))
  {
    xEventGroupSetBitsFromISR(xSystemEventGroup, EVENT_IRR, &xHigherPriorityTaskWoken);
  }
  else if (rxMsg.id == 5)
  {
    xEventGroupSetBitsFromISR(xSystemEventGroup, EVENT_ALL_IRR | EVENT_ALL_REG | EVENT_ALL_TEMP_HUM, &xHigherPriorityTaskWoken);
  }
  else if (rxMsg.id == 0)
  {
    xEventGroupSetBitsFromISR(xSystemEventGroup, EVENT_CARD_NUM, &xHigherPriorityTaskWoken);
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void onReceiveSerial()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xEventGroupSetBitsFromISR(xSerialEventGroup, EVENT_SERIAL_MSG_RX, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void task_Serial_RX(void *pvParameters)
{
  String serialBuffer = "";
  String serialCommand;
  String serialValue;
  while (1)
  {
    xEventGroupWaitBits(xSerialEventGroup, EVENT_SERIAL_MSG_RX, pdTRUE, pdTRUE, portMAX_DELAY);
    while (Serial.available() > 0)
    {
      char serialChar = Serial.read();

      if (serialChar == '\r' || serialChar == '\n')
      {
        if (serialBuffer.length() > 0)
        {
          int index = serialBuffer.indexOf(' ');

          if (index == -1)
          {
            serialCommand = serialBuffer;
            serialValue = "";
          }
          else
          {
            serialCommand = serialBuffer.substring(0, index);
            serialValue = serialBuffer.substring(index + 1);
          }

          if (serialCommand == "M")
          {
            xEventGroupSetBits(xSystemEventGroup, EVENT_ALL_IRR | EVENT_ALL_REG | EVENT_ALL_TEMP_HUM);
          }

          serialBuffer = "";
        }
      }
      else
      {
        serialBuffer += serialChar;
      }
    }
  }
}

void task_Measure_TEMP_HUM(void *pvParameters)
{
  float temperature, humidity;
  int humidityInt, temperatureInt;
  Adafruit_AM2315 am2315;
  CANMessage txMsg;
  TempHumData tempHumData;
  if (!am2315.begin())
  {
    xSemaphoreTake(xSerialMutex, portMAX_DELAY);
    Serial.println("Starting am2315 failed!");
    xSemaphoreGive(xSerialMutex);
    vTaskDelay(portMAX_DELAY);
  }

  xSemaphoreTake(xSerialMutex, portMAX_DELAY);
  Serial.println("Starting am2315 succed!");
  xSemaphoreGive(xSerialMutex);

  while (1)
  {
    EventBits_t uxBits = xEventGroupWaitBits(xSystemEventGroup, EVENT_HUM | EVENT_TEMP | EVENT_ALL_TEMP_HUM, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (uxBits & EVENT_HUM)
    {
      txMsg.id = 7;
      txMsg.len = 0;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);

      do {
        humidity = am2315.readHumidity();
      } while (isnan(humidity));

      txMsg.id = 42;
      txMsg.len = 2;
      humidityInt = humidity * 100;
      txMsg.data[0] = humidityInt / 256;
      txMsg.data[1] = humidityInt % 256;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);

      txMsg.id = 8;
      txMsg.len = 0;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);
    }

    if (uxBits & EVENT_TEMP)
    {
      txMsg.id = 7;
      txMsg.len = 0;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);

      do {
        temperature = am2315.readTemperature();
      } while (isnan(temperature));

      txMsg.id = 43;
      txMsg.len = 2;
      temperatureInt = temperature * 100;
      txMsg.data[0] = temperatureInt / 256;
      txMsg.data[1] = temperatureInt % 256;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);

      txMsg.id = 8;
      txMsg.len = 0;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);
    }
    
    if (uxBits & EVENT_ALL_TEMP_HUM)
    {

      do
      {
      am2315.readTemperatureAndHumidity(&temperature, &humidity);
      } while (isnan(temperature)||isnan(humidity));

      
      
      tempHumData.humidity = humidity;
      tempHumData.temperature = temperature;
      xQueueSend(xTempHumQueue, &tempHumData, portMAX_DELAY);
    }
  }
}

void task_Measure_IRR(void *pvParameters)
{
  int irradiance, irradianceInt;
  CANMessage txMsg;

  pinMode(PIN_CPT_IRR, INPUT);

  while (1)
  {
    EventBits_t uxBits = xEventGroupWaitBits(xSystemEventGroup, EVENT_IRR | EVENT_ALL_IRR, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (uxBits & EVENT_IRR)
    {
      txMsg.id = 7;
      txMsg.len = 0;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);

      irradiance = analogRead(PIN_CPT_IRR);
      
      txMsg.id = 44;
      txMsg.len = 2;
      irradianceInt = irradiance * 100;
      txMsg.data[0] = irradianceInt / 256;
      txMsg.data[1] = irradianceInt % 256;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);

      txMsg.id = 8;
      txMsg.len = 0;
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);
    }
    
    if (uxBits & EVENT_ALL_IRR)
    {
      irradiance = analogRead(PIN_CPT_IRR);
      xQueueSend(xIrrQueue, &irradiance, portMAX_DELAY);
    }
  }
}

void task_Grouping(void *pvParameters)
{
  CANMessage txMsg;
  txMsg.id = 45;
  txMsg.len = 6;
  int irradiance, irradianceInt;
  TempHumData meteoData;

  while (1)
  {
    EventBits_t uxBits = xEventGroupWaitBits(xSystemEventGroup, EVENT_ALL_REG, pdTRUE, pdFALSE, portMAX_DELAY);
    
    if (uxBits & EVENT_ALL_REG) 
    {
      if (xQueueReceive(xIrrQueue, &irradiance, pdMS_TO_TICKS(2000)) == pdPASS)
      {
        irradianceInt = irradiance * 100;
        txMsg.data[4] = irradianceInt / 256;
        txMsg.data[5] = irradianceInt % 256;
      }
      else
      {
        xSemaphoreTake(xSerialMutex, portMAX_DELAY);
        Serial.println("Pas d'irradiance recue pour le regroupement");
        xSemaphoreGive(xSerialMutex);
        txMsg.data[4] = 0;
        txMsg.data[5] = 0;
      }

      if (xQueueReceive(xTempHumQueue, &meteoData, pdMS_TO_TICKS(5000)) == pdPASS)
      {
        int humidityInt = meteoData.humidity * 100;
        txMsg.data[0] = humidityInt / 256;
        txMsg.data[1] = humidityInt % 256;

        int temperatureInt = meteoData.temperature * 100;
        txMsg.data[2] = temperatureInt / 256;
        txMsg.data[3] = temperatureInt % 256;
      }
      else
      {
        xSemaphoreTake(xSerialMutex, portMAX_DELAY);
        Serial.println("Pas de temp et d'humidite recues pour le regroupement");
        xSemaphoreGive(xSerialMutex);
        txMsg.data[0] = 0;
        txMsg.data[1] = 0;
        txMsg.data[2] = 0;
        txMsg.data[3] = 0;
      }

      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);
      
      //xSemaphoreTake(xSerialMutex, portMAX_DELAY);
      //Serial.println("Message de regroupement complet envoye");
      //xSemaphoreGive(xSerialMutex);
    }
  }
}

void task_Send_Card_Num(void *pvParameters)
{
  CANMessage txMsg;
  txMsg.len = 1;
  txMsg.id = 10;
  txMsg.data[0] = 10;
  while (1)
  {
    EventBits_t uxBits = xEventGroupWaitBits(xSystemEventGroup, EVENT_CARD_NUM, pdTRUE, pdFALSE, portMAX_DELAY);
    if (uxBits & EVENT_CARD_NUM)
    {
      xQueueSend(xCanTxQueue, &txMsg, portMAX_DELAY);
    }
  }
}
