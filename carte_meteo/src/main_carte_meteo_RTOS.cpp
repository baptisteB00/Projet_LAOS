/**
 * @file main_carte_meteo_RTOS.cpp
 * @brief Carte Météo – mesure température, humidité et irradiance solaire.
 *
 * Mesure les conditions météorologiques autour du panneau solaire via :
 * - Capteur AM2315 (I2C) : température extérieure + humidité
 * - Cellule photoélectrique sur `PIN_CPT_IRR` (GPIO 34) : irradiance
 *
 * **Architecture FreeRTOS – 6 tâches :**
 * | Tâche | Prio | Rôle |
 * |-------|------|------|
 * | TaskEnvoiMessageCan | 10 | Envoie les trames CAN depuis `canTxQueue` |
 * | TaskTraitementMessagesSerie | 10 | Traite les commandes série |
 * | TaskMesureHumiditeTemperature | 9 | Lit le AM2315 |
 * | TaskMesureIrradiance | 9 | Lit la cellule photoélectrique |
 * | TaskRegroupementDonnee | 9 | Assemble les 3 mesures en un message |
 * | TaskEnvoiNumeroCarte | 9 | Répond aux demandes d'identification |
 */

#include <Arduino.h>
#include <Adafruit_AM2315.h>
#include <CAN.h>
#include <can_id.h>

/* ---- Objets FreeRTOS globaux --------------------------------------- */
EventGroupHandle_t systemEventGroup;  // événements principaux (CAN, série)
EventGroupHandle_t serialEventGroup;  // événement "caractère série reçu"
SemaphoreHandle_t  serialMutex;       // protège l'accès à Serial.printf()
QueueHandle_t      canTxQueue;        // file des messages CAN à envoyer
QueueHandle_t      tempHumQueue;      // file pour partager temp+hum vers TaskRegroupement
QueueHandle_t      irrQueue;          // file pour partager l'irradiance vers TaskRegroupement

/* ---- Drapeaux d'événements (bits dans systemEventGroup) ------------ */
#define EVENT_TEMP          BIT0  // demande de température seule
#define EVENT_HUM           BIT1  // demande d'humidité seule
#define EVENT_IRR           BIT2  // demande d'irradiance seule
#define EVENT_ALL_TEMP_HUM  BIT3  // demande groupée : temp + humidité (pour le regroupement)
#define EVENT_ALL_IRR       BIT4  // demande groupée : irradiance (pour le regroupement)
#define EVENT_ALL_REG       BIT5  // ordre d'envoyer le message groupé complet
#define EVENT_CARD_NUM      BIT6  // demande d'identification
#define EVENT_SERIAL_MSG_RX BIT7  // caractère reçu sur la liaison série

/* ---- Broche matérielle -------------------------------------------- */
#define PIN_CPT_IRR 34  // entrée analogique de la cellule d'irradiance

/* ---- Structures de données ----------------------------------------- */

/* Données température + humidité partagées entre les tâches */
typedef struct TempHumData_t
{
  float temperature; // en degrés Celsius
  float humidity;    // en pourcentage
} TempHumData_t;

/* Message CAN générique */
typedef struct CanMessage_t
{
  unsigned int  id      = 0;    // identifiant CAN
  char          len     = 0;    // nombre d'octets (0 à 8)
  unsigned char data[8] = {0};  // données
} CanMessage_t;

/* ---- Déclarations des fonctions ------------------------------------ */
void TaskEnvoiMessageCan(void *pvParameters);
void TaskMesureIrradiance(void *pvParameters);
void TaskMesureHumiditeTemperature(void *pvParameters);
void TaskRegroupementDonnee(void *pvParameters);
void TaskTraitementMessagesSerie(void *pvParameters);
void TaskEnvoiNumeroCarte(void *pvParameters);
void OnReceiveCan(int packetSize);
void OnReceiveSerial();

/* ==================================================================== */
/*  SETUP                                                                */
/* ==================================================================== */
void setup()
{
  Serial.begin(115200);

  /* Démarrage du bus CAN à 10 kbps */
  if (!CAN.begin(10E3))
  {
    Serial.printf("Erreur : demarrage CAN impossible !\n");
    while (1);
  }

  /* Enregistrement des callbacks d'interruption */
  Serial.onReceive(OnReceiveSerial);
  CAN.onReceive(OnReceiveCan);

  /* Création des objets FreeRTOS */
  irrQueue         = xQueueCreate(3, sizeof(int));
  canTxQueue       = xQueueCreate(5, sizeof(CanMessage_t));
  tempHumQueue     = xQueueCreate(3, sizeof(TempHumData_t));
  systemEventGroup = xEventGroupCreate();
  serialEventGroup = xEventGroupCreate();
  serialMutex      = xSemaphoreCreateMutex();

  /* Création des tâches FreeRTOS */
  xTaskCreate(TaskEnvoiMessageCan,           "TASK_TX_CAN",        3072, NULL, 10, NULL);
  xTaskCreate(TaskMesureIrradiance,          "TASK_MEASURE_IRR",   2048, NULL,  9, NULL);
  xTaskCreate(TaskMesureHumiditeTemperature, "TASK_MEASURE_TH",    2048, NULL,  9, NULL);
  xTaskCreate(TaskRegroupementDonnee,        "TASK_GROUPING",      2048, NULL,  9, NULL);
  xTaskCreate(TaskTraitementMessagesSerie,   "TASK_SERIAL_RX",     2048, NULL, 10, NULL);
  xTaskCreate(TaskEnvoiNumeroCarte,          "TASK_CARD_NUM",      2048, NULL,  9, NULL);
}

/* FreeRTOS gère la boucle principale ; loop() ne sert plus à rien */
void loop()
{
}

/**
 * @brief Tâche FreeRTOS – envoi des messages CAN (priorité 10).
 *
 * Seul point d'écriture sur le bus CAN. Toutes les autres tâches déposent
 * leurs messages dans `canTxQueue` ; cette tâche les envoie un par un et
 * affiche un log série via `serialMutex`.
 * @param pvParameters Non utilisé.
 */
void TaskEnvoiMessageCan(void *pvParameters)
{
  CanMessage_t txMsg;
  while (1)
  {
    /* Bloquant : attend qu'un message soit dans la file */
    xQueueReceive(canTxQueue, &txMsg, portMAX_DELAY);

    /* Envoi sur le bus CAN */
    CAN.beginPacket(txMsg.id);
    CAN.write(txMsg.data, txMsg.len);
    CAN.endPacket();

    /* Affichage série pour débogage
     * Les valeurs sont encodées sur 2 octets : (octet_fort<<8 | octet_faible) / 100 */
    xSemaphoreTake(serialMutex, portMAX_DELAY);
    if (txMsg.id == CAN_ID_RENVOI_HUMIDITE)
    {
      float hum = ((txMsg.data[0] << 8) | txMsg.data[1]) / 100.0;
      Serial.printf("Humidite envoyee : %.2f %%\n", hum);
    }
    else if (txMsg.id == CAN_ID_RENVOI_TEMPERATURE)
    {
      float temp = ((txMsg.data[0] << 8) | txMsg.data[1]) / 100.0;
      Serial.printf("Temperature envoyee : %.2f C\n", temp);
    }
    else if (txMsg.id == CAN_ID_RENVOI_IRRADIANCE)
    {
      int irr = (txMsg.data[0] << 8) | txMsg.data[1];
      Serial.printf("Irradiance envoyee : %d\n", irr / 100);
    }
    else if (txMsg.id == CAN_ID_RENVOI_HUM_IRR_TEMP_EXT)
    {
      float hum  = ((txMsg.data[0] << 8) | txMsg.data[1]) / 100.0;
      float temp = ((txMsg.data[2] << 8) | txMsg.data[3]) / 100.0;
      int   irr  =  (txMsg.data[4] << 8) | txMsg.data[5];
      Serial.printf("%.2f;%.2f;%.2d\n\r", temp, hum, irr / 100);
    }
    xSemaphoreGive(serialMutex);
  }
}

/* ==================================================================== */
/*  CALLBACKS D'INTERRUPTION                                             */
/* ==================================================================== */

/**
 * @brief Callback CAN – appelée en ISR à chaque trame reçue.
 *
 * Lit l'identifiant de la trame et lève le bit d'événement correspondant
 * dans `systemEventGroup` pour réveiller la tâche appropriée.
 * @param packetSize Taille de la trame (fournie par la bibliothèque CAN).
 */
void OnReceiveCan(int packetSize)
{
  CanMessage_t rxMsg;
  BaseType_t higherPriorityTaskWoken = pdFALSE;

  rxMsg.id  = CAN.packetId();
  rxMsg.len = CAN.packetDlc();
  int i = 0;
  while (CAN.available())
  {
    rxMsg.data[i] = CAN.read();
    i++;
  }

  /* Identification de la demande et réveil de la tâche concernée */
  if (rxMsg.id == CAN_ID_DEMANDE_HUMIDITE)
    xEventGroupSetBitsFromISR(systemEventGroup, EVENT_HUM, &higherPriorityTaskWoken);
  else if (rxMsg.id == CAN_ID_DEMANDE_TEMP_EXTERIEUR)
    xEventGroupSetBitsFromISR(systemEventGroup, EVENT_TEMP, &higherPriorityTaskWoken);
  else if (rxMsg.id == CAN_ID_DEMANDE_IRRADIANCE)
    xEventGroupSetBitsFromISR(systemEventGroup, EVENT_IRR, &higherPriorityTaskWoken);
  else if (rxMsg.id == CAN_ID_DEMANDE_HUM_IRR_TEMP_EXT)
  {
    /* Demande groupée : réveille les 3 tâches de mesure + la tâche de regroupement */
    xEventGroupSetBitsFromISR(systemEventGroup,
      EVENT_ALL_IRR | EVENT_ALL_REG | EVENT_ALL_TEMP_HUM, &higherPriorityTaskWoken);
  }
  else if (rxMsg.id == CAN_ID_DEMANDE_NUM_CARTE)
    xEventGroupSetBitsFromISR(systemEventGroup, EVENT_CARD_NUM, &higherPriorityTaskWoken);

  portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/**
 * @brief Callback UART – appelée en ISR à chaque réception série.
 *
 * Lève `EVENT_SERIAL_MSG_RX` dans `serialEventGroup` pour réveiller
 * `TaskTraitementMessagesSerie`.
 */
void OnReceiveSerial()
{
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  xEventGroupSetBitsFromISR(serialEventGroup, EVENT_SERIAL_MSG_RX, &higherPriorityTaskWoken);
  portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/**
 * @brief Tâche FreeRTOS – traitement des commandes série (priorité 10).
 *
 * Attend `EVENT_SERIAL_MSG_RX`, lit les caractères et exécute la commande.
 * Commande reconnue : `"M"` → déclenche l'envoi groupé de toutes les mesures.
 * @param pvParameters Non utilisé.
 */
void TaskTraitementMessagesSerie(void *pvParameters)
{
  String serialBuffer  = ""; // accumule les caractères jusqu'au retour chariot
  String serialCommand;
  String serialValue;

  while (1)
  {
    xEventGroupWaitBits(serialEventGroup, EVENT_SERIAL_MSG_RX, pdTRUE, pdTRUE, portMAX_DELAY);

    while (Serial.available() > 0)
    {
      char serialChar = Serial.read();

      if (serialChar == '\r' || serialChar == '\n') // fin de commande
      {
        if (serialBuffer.length() > 0)
        {
          int index = serialBuffer.indexOf(' ');
          if (index == -1)
          {
            serialCommand = serialBuffer;
            serialValue   = "";
          }
          else
          {
            serialCommand = serialBuffer.substring(0, index);
            serialValue   = serialBuffer.substring(index + 1);
          }

          if (serialCommand == "M")
          {
            /* Déclenche les 3 tâches de mesure + la tâche de regroupement */
            xEventGroupSetBits(systemEventGroup, EVENT_ALL_IRR | EVENT_ALL_REG | EVENT_ALL_TEMP_HUM);
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

/**
 * @brief Tâche FreeRTOS – lecture du capteur AM2315 (priorité 9).
 *
 * Attend `EVENT_HUM`, `EVENT_TEMP` ou `EVENT_ALL_TEMP_HUM`.
 * Pour les demandes individuelles, envoie directement dans `canTxQueue`.
 * Pour la demande groupée, dépose les données dans `tempHumQueue`.
 *
 * **Encodage CAN (float → 2 octets) :**
 * `data[n] = (int)(val*100) / 256`, `data[n+1] = (int)(val*100) % 256`
 * @param pvParameters Non utilisé.
 */
void TaskMesureHumiditeTemperature(void *pvParameters)
{
  float temperature, humidity;
  int   humidityInt, temperatureInt;
  Adafruit_AM2315 am2315;
  CanMessage_t txMsg;
  TempHumData_t tempHumData;

  /* Initialisation du capteur AM2315 */
  while (!am2315.begin())
  {
    xSemaphoreTake(serialMutex, portMAX_DELAY);
    Serial.println("Erreur : demarrage AM2315 impossible !");
    xSemaphoreGive(serialMutex);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  xSemaphoreTake(serialMutex, portMAX_DELAY);
  Serial.println("AM2315 pret.");
  xSemaphoreGive(serialMutex);

  while (1)
  {
    /* Attend un des 3 événements possibles */
    EventBits_t bits = xEventGroupWaitBits(systemEventGroup,
                         EVENT_HUM | EVENT_TEMP | EVENT_ALL_TEMP_HUM,
                         pdTRUE, pdFALSE, portMAX_DELAY);

    /* --- Demande d'humidité seule --- */
    if (bits & EVENT_HUM)
    {
      txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
      txMsg.len = 0;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);

      do { humidity = am2315.readHumidity(); } while (isnan(humidity)); // relecture si erreur

      txMsg.id      = CAN_ID_RENVOI_HUMIDITE;
      txMsg.len     = 2;
      humidityInt   = humidity * 100;
      txMsg.data[0] = humidityInt / 256;
      txMsg.data[1] = humidityInt % 256;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);

      txMsg.id  = CAN_ID_FIN_TRANSMISSION;
      txMsg.len = 0;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);
    }

    /* --- Demande de température seule --- */
    if (bits & EVENT_TEMP)
    {
      txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
      txMsg.len = 0;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);

      do { temperature = am2315.readTemperature(); } while (isnan(temperature));

      txMsg.id        = CAN_ID_RENVOI_TEMPERATURE;
      txMsg.len       = 2;
      temperatureInt  = temperature * 100;
      txMsg.data[0]   = temperatureInt / 256;
      txMsg.data[1]   = temperatureInt % 256;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);

      txMsg.id  = CAN_ID_FIN_TRANSMISSION;
      txMsg.len = 0;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);
    }

    /* --- Mesure groupée : temp + humidité pour le message regroupé ---
     *  On dépose les données dans tempHumQueue ;
     *  TaskRegroupementDonnee les récupérera pour construire le message complet. */
    if (bits & EVENT_ALL_TEMP_HUM)
    {
      do
      {
        am2315.readTemperatureAndHumidity(&temperature, &humidity);
      } while (isnan(temperature) || isnan(humidity));

      tempHumData.humidity    = humidity;
      tempHumData.temperature = temperature;
      xQueueSend(tempHumQueue, &tempHumData, portMAX_DELAY);
    }
  }
}

/**
 * @brief Tâche FreeRTOS – lecture de l'irradiance solaire (priorité 9).
 *
 * Lecture analogique sur `PIN_CPT_IRR` (GPIO 34).
 * Attend `EVENT_IRR` (envoi individuel dans `canTxQueue`) ou
 * `EVENT_ALL_IRR` (envoi dans `irrQueue` pour le regroupement).
 * @param pvParameters Non utilisé.
 */
void TaskMesureIrradiance(void *pvParameters)
{
  int irradiance, irradianceInt;
  CanMessage_t txMsg;

  pinMode(PIN_CPT_IRR, INPUT);

  while (1)
  {
    EventBits_t bits = xEventGroupWaitBits(systemEventGroup,
                         EVENT_IRR | EVENT_ALL_IRR,
                         pdTRUE, pdFALSE, portMAX_DELAY);

    /* --- Demande d'irradiance seule --- */
    if (bits & EVENT_IRR)
    {
      txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
      txMsg.len = 0;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);

      irradiance = analogRead(PIN_CPT_IRR);

      txMsg.id        = CAN_ID_RENVOI_IRRADIANCE;
      txMsg.len       = 2;
      irradianceInt   = irradiance * 100;
      txMsg.data[0]   = irradianceInt / 256;
      txMsg.data[1]   = irradianceInt % 256;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);

      txMsg.id  = CAN_ID_FIN_TRANSMISSION;
      txMsg.len = 0;
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);
    }

    /* --- Mesure groupée : irradiance pour le message regroupé ---
     *  On dépose la valeur dans irrQueue pour TaskRegroupementDonnee. */
    if (bits & EVENT_ALL_IRR)
    {
      irradiance = analogRead(PIN_CPT_IRR);
      xQueueSend(irrQueue, &irradiance, portMAX_DELAY);
    }
  }
}

/**
 * @brief Tâche FreeRTOS – assemblage du message météo groupé (priorité 9).
 *
 * Attend `EVENT_ALL_REG`, puis récupère l'irradiance (`irrQueue`, timeout 2 s)
 * et la température+humidité (`tempHumQueue`, timeout 5 s) pour construire
 * le message `CAN_ID_RENVOI_HUM_IRR_TEMP_EXT` (6 octets, ID=45).
 *
 * En cas de timeout, les octets correspondants sont mis à `0xFF`.
 * @param pvParameters Non utilisé.
 */
void TaskRegroupementDonnee(void *pvParameters)
{
  CanMessage_t txMsg;
  txMsg.id  = CAN_ID_RENVOI_HUM_IRR_TEMP_EXT;
  txMsg.len = 6;
  int irradiance, irradianceInt;
  TempHumData_t meteoData;

  while (1)
  {
    EventBits_t bits = xEventGroupWaitBits(systemEventGroup,
                         EVENT_ALL_REG, pdTRUE, pdFALSE, portMAX_DELAY);

    if (bits & EVENT_ALL_REG)
    {
      /* Récupération de l'irradiance (timeout 2 s) */
      if (xQueueReceive(irrQueue, &irradiance, pdMS_TO_TICKS(2000)) == pdPASS)
      {
        irradianceInt = irradiance * 100;
        txMsg.data[4] = irradianceInt / 256;
        txMsg.data[5] = irradianceInt % 256;
      }
      else
      {
        xSemaphoreTake(serialMutex, portMAX_DELAY);
        Serial.println("Pas d'irradiance recue pour le regroupement");
        xSemaphoreGive(serialMutex);
        txMsg.data[4] = 0xFF;
        txMsg.data[5] = 0xFF;
      }

      /* Récupération de la température et de l'humidité (timeout 5 s) */
      if (xQueueReceive(tempHumQueue, &meteoData, pdMS_TO_TICKS(5000)) == pdPASS)
      {
        int humidityInt    = meteoData.humidity    * 100;
        txMsg.data[0] = humidityInt / 256;
        txMsg.data[1] = humidityInt % 256;

        int temperatureInt = meteoData.temperature * 100;
        txMsg.data[2] = temperatureInt / 256;
        txMsg.data[3] = temperatureInt % 256;
      }
      else
      {
        xSemaphoreTake(serialMutex, portMAX_DELAY);
        Serial.println("Pas de temp et d'humidite recues pour le regroupement");
        xSemaphoreGive(serialMutex);
        txMsg.data[0] = 0xFF;
        txMsg.data[1] = 0xFF;
        txMsg.data[2] = 0xFF;
        txMsg.data[3] = 0xFF;
      }

      /* Envoi du message groupé */
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);
    }
  }
}

/**
 * @brief Tâche FreeRTOS – réponse aux demandes d'identification (priorité 9).
 *
 * Attend `EVENT_CARD_NUM` puis envoie `CAN_ID_RENVOI_NUM_CARTE` (ID=10)
 * avec `data[0]=10` (numéro fixe de la carte météo).
 * @param pvParameters Non utilisé.
 */
void TaskEnvoiNumeroCarte(void *pvParameters)
{
  CanMessage_t txMsg;
  txMsg.len     = 1;
  txMsg.id      = CAN_ID_RENVOI_NUM_CARTE;
  txMsg.data[0] = 10; // numéro fixe de la carte météo

  while (1)
  {
    EventBits_t bits = xEventGroupWaitBits(systemEventGroup,
                         EVENT_CARD_NUM, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & EVENT_CARD_NUM)
    {
      xQueueSend(canTxQueue, &txMsg, portMAX_DELAY);
    }
  }
}
