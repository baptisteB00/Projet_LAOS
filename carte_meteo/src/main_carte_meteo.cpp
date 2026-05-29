/**
 * @file main_carte_meteo.cpp
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


/* ---- Broche matérielle -------------------------------------------- */
#define PIN_CPT_IRR 34  // entrée analogique de la cellule d'irradiance
Adafruit_AM2315 am2315;
bool canAvailable;

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

CanMessage_t rxMsg;

/* ---- Déclarations des fonctions ------------------------------------ */
void OnReceiveCan(int packetSize);
void OnReceiveSerial();
void EnvoiNumeroCarte(void);
void MesureHumiditeTemperatureIrradiance(void);
void MesureHumidite(void);
void MesureTemperature(void);
void MesureIrradiance(void);

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

  while (!am2315.begin())
  {
    Serial.println("Erreur : demarrage AM2315 impossible !");
  }

  /* Enregistrement des callbacks d'interruption */
  Serial.onReceive(OnReceiveSerial);
  CAN.onReceive(OnReceiveCan);

  pinMode(PIN_CPT_IRR, INPUT);

}

void loop()
{
  CanMessage_t rxMsgLocal;
  if ( canAvailable == true){
    canAvailable = false;
    rxMsgLocal = rxMsg;

    /* Identification de la demande et réveil de la tâche concernée */
    if (rxMsgLocal.id == CAN_ID_DEMANDE_HUMIDITE)
        MesureHumidite();
    else if (rxMsgLocal.id == CAN_ID_DEMANDE_TEMP_EXTERIEUR)
        MesureTemperature();
    else if (rxMsgLocal.id == CAN_ID_DEMANDE_IRRADIANCE)
        MesureIrradiance();
    else if (rxMsgLocal.id == CAN_ID_DEMANDE_HUM_IRR_TEMP_EXT)
        MesureHumiditeTemperatureIrradiance();
    else if (rxMsgLocal.id == CAN_ID_DEMANDE_NUM_CARTE)
        EnvoiNumeroCarte();
  }
}

/**
 * @brief Tâche FreeRTOS – envoi des messages CAN (priorité 10).
 *
 * Seul point d'écriture sur le bus CAN. Toutes les autres tâches déposent
 * leurs messages dans `canTxQueue` ; cette tâche les envoie un par un et
 * affiche un log série via `serialMutex`.
 * @param  Non utilisé.
 */
void EnvoiMessageCan(CanMessage_t message)
{
    /* Envoi sur le bus CAN */
    CAN.beginPacket(message.id);
    CAN.write(message.data, message.len);
    CAN.endPacket();
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
    rxMsg.id = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    for (int i = 0 ; CAN.available() ; i++){
      rxMsg.data[i] = CAN.read();
    }
  canAvailable = true;
}

/**
 * @brief Callback UART – appelée en ISR à chaque réception série.
 *
 * Lève `EVENT_SERIAL_MSG_RX` dans `serialEventGroup` pour réveiller
 * `TaskTraitementMessagesSerie`.
 */
void OnReceiveSerial()
{
  String serialBuffer  = ""; // accumule les caractères jusqu'au retour chariot
  String serialCommand;
  String serialValue;


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
          MesureHumiditeTemperatureIrradiance();
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


void MesureIrradiance(void)
{
  int irradiance, irradianceInt;
  CanMessage_t txMsg;

  /* --- Demande d'irradiance seule --- */
  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  irradiance = analogRead(PIN_CPT_IRR);

  txMsg.id        = CAN_ID_RENVOI_IRRADIANCE;
  txMsg.len       = 2;
  irradianceInt   = irradiance * 100;
  txMsg.data[0]   = irradianceInt / 256;
  txMsg.data[1]   = irradianceInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  Serial.printf("l'irradiance est de %d °C\n\r",irradiance);
}

void MesureTemperature(void){

  float temperature;
  int temperatureInt;
  CanMessage_t txMsg;
  
  /* --- Demande de temperature seule --- */
  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  temperature = am2315.readTemperature();

  txMsg.id        = CAN_ID_RENVOI_TEMPERATURE;
  txMsg.len       = 2;
  temperatureInt   = temperature * 100;
  txMsg.data[0]   = temperatureInt / 256;
  txMsg.data[1]   = temperatureInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  Serial.printf("la temperature est de %3.2f °C\n\r",temperature);
}


void MesureHumidite(void){

  float humidite;
  int humiditeInt;
  CanMessage_t txMsg;
  
  /* --- Demande de humidite seule --- */
  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  humidite = am2315.readHumidity();

  txMsg.id        = CAN_ID_RENVOI_HUMIDITE;
  txMsg.len       = 2;
  humiditeInt   = humidite * 100;
  txMsg.data[0]   = humiditeInt / 256;
  txMsg.data[1]   = humiditeInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  Serial.printf("l'huidité est de %3.2f °C\n\r",humidite);
}

void MesureHumiditeTemperatureIrradiance(void){

  float humidite, temperature;
  int irradiance, irradianceInt,humiditeInt,temperatureInt;
  CanMessage_t txMsg;
  
  /* --- Demande de humidite seule --- */
  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
  humidite = am2315.readHumidity();
  temperature = am2315.readTemperature();
  irradiance = analogRead(PIN_CPT_IRR);

  humiditeInt     = humidite * 100;
  temperatureInt  = temperature * 100;
  irradianceInt   = irradiance * 100;

  txMsg.id        = CAN_ID_RENVOI_HUM_IRR_TEMP_EXT;
  txMsg.len       = 6;
  txMsg.data[0]   = humiditeInt / 256;
  txMsg.data[1]   = humiditeInt % 256;
  txMsg.data[2]   = temperatureInt / 256;
  txMsg.data[3]   = temperatureInt % 256;
  txMsg.data[4]   = irradianceInt / 256;
  txMsg.data[5]   = irradianceInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
}

void EnvoiNumeroCarte(void)
{
  CanMessage_t txMsg;
  txMsg.len     = 1;
  txMsg.id      = CAN_ID_RENVOI_NUM_CARTE;
  txMsg.data[0] = 10; // numéro fixe de la carte météo
  EnvoiMessageCan(txMsg);
}
