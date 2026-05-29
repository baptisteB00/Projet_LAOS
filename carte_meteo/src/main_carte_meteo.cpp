/**
 * @file main_carte_meteo.cpp
 * @brief Carte Météo – mesure température, humidité et irradiance solaire.
 *
 * Mesure les conditions météorologiques autour du panneau solaire via :
 *  - Capteur AM2315 (I2C) : température extérieure + humidité
 *  - Cellule photoélectrique sur PIN_CPT_IRR (GPIO 34) : irradiance
 *
 * Fonctionnement séquentiel :
 *  - setup() : initialise le matériel et enregistre les callbacks
 *  - OnReceiveCan() : stocke le message reçu et lève un flag (ISR)
 *  - OnReceiveSerial() : lit et exécute les commandes série
 *  - loop() : vérifie le flag et appelle la fonction de mesure appropriée
 */

#include <Arduino.h>
#include <Adafruit_AM2315.h>
#include <CAN.h>
#include <can_id.h>

/* ---- Broche matérielle -------------------------------------------- */
#define PIN_CPT_IRR 34  // entrée analogique de la cellule d'irradiance

/* ---- Structure d'un message CAN ------------------------------------ */
typedef struct CanMessage_t
{
  unsigned int  id      = 0;    // identifiant CAN
  char          len     = 0;    // nombre d'octets (0 à 8)
  unsigned char data[8] = {0};  // données
} CanMessage_t;

/* ---- Variables globales -------------------------------------------- */
Adafruit_AM2315 am2315;              // capteur température + humidité I2C
CanMessage_t    rxMsg;               // dernier message CAN reçu (rempli par l'ISR)
bool            canAvailable = false; // vrai quand un nouveau message CAN est prêt

/* ---- Déclarations des fonctions ------------------------------------ */
void OnReceiveCan(int packetSize);
void OnReceiveSerial();
void EnvoiMessageCan(CanMessage_t message);
void EnvoiNumeroCarte(void);
void MesureHumiditeTemperatureIrradiance(void);
void MesureHumidite(void);
void MesureTemperature(void);
void MesureIrradiance(void);

/* ==================================================================== */
/*  SETUP – initialisation matérielle                                    */
/* ==================================================================== */

/**
 * @brief Initialise le matériel et enregistre les callbacks.
 *
 * Séquence :
 *  1. Démarrage liaison série (115200 bauds)
 *  2. Démarrage bus CAN à 10 kbps
 *  3. Initialisation du capteur AM2315 (boucle jusqu'au succès)
 *  4. Enregistrement des callbacks CAN et série
 *  5. Configuration de la broche d'irradiance en entrée
 */
void setup()
{
  Serial.begin(115200);

  if (!CAN.begin(10E3))
  {
    Serial.printf("Erreur : demarrage CAN impossible !\n");
    while (1);
  }

  while (!am2315.begin())
  {
    Serial.println("Erreur : demarrage AM2315 impossible !");
  }

  Serial.onReceive(OnReceiveSerial);
  CAN.onReceive(OnReceiveCan);

  pinMode(PIN_CPT_IRR, INPUT);
}

/* ==================================================================== */
/*  LOOP – traitement des messages reçus                                 */
/* ==================================================================== */

/**
 * @brief Boucle principale – traite les messages CAN reçus.
 *
 * Vérifie le flag `canAvailable` levé par l'ISR. Si un message est prêt,
 * copie `rxMsg` dans une variable locale, remet le flag à false, puis
 * appelle la fonction de mesure correspondant à l'identifiant CAN reçu.
 */
void loop()
{
  if (canAvailable == true)
  {
    canAvailable = false;
    CanMessage_t rxMsgLocal = rxMsg; // copie locale pour éviter une modification par l'ISR

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

/* ==================================================================== */
/*  CALLBACKS                                                            */
/* ==================================================================== */

/**
 * @brief Callback CAN – appelée à chaque trame reçue (interruption).
 *
 * Lit l'identifiant, la longueur et les données de la trame reçue,
 * les stocke dans `rxMsg`, puis lève le flag `canAvailable` pour
 * signaler à `loop()` qu'un message est prêt à être traité.
 *
 * @param packetSize Taille de la trame reçue (fournie par la bibliothèque CAN).
 */
void OnReceiveCan(int packetSize)
{
    rxMsg.id  = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    for (int i = 0; CAN.available(); i++)
    {
      rxMsg.data[i] = CAN.read();
    }
    canAvailable = true;
}

/**
 * @brief Callback série – appelée à chaque réception UART.
 *
 * Lit les caractères un par un et accumule jusqu'au retour chariot.
 * Commande reconnue :
 *  - `"M"` : déclenche la mesure groupée (humidité + température + irradiance)
 */
void OnReceiveSerial()
{
  String serialBuffer  = "";
  String serialCommand;
  String serialValue;

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

/* ==================================================================== */
/*  FONCTIONS DE MESURE                                                  */
/* ==================================================================== */

/**
 * @brief Mesure l'irradiance solaire et l'envoie sur le bus CAN.
 *
 * Lit la valeur ADC sur PIN_CPT_IRR et l'encode sur 2 octets (valeur × 100).
 * Protocole CAN (ID = CAN_ID_RENVOI_IRRADIANCE) :
 *  - data[0] : octet fort (valeur * 100 / 256)
 *  - data[1] : octet faible (valeur * 100 % 256)
 */
void MesureIrradiance(void)
{
  int irradiance, irradianceInt;
  CanMessage_t txMsg;

  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  irradiance = analogRead(PIN_CPT_IRR);

  txMsg.id      = CAN_ID_RENVOI_IRRADIANCE;
  txMsg.len     = 2;
  irradianceInt = irradiance * 100;
  txMsg.data[0] = irradianceInt / 256;
  txMsg.data[1] = irradianceInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  Serial.printf("Irradiance : %d\n\r", irradiance);
}

/**
 * @brief Mesure la température extérieure via AM2315 et l'envoie sur le bus CAN.
 *
 * Lit la température en °C via I2C et l'encode sur 2 octets (valeur × 100).
 * Protocole CAN (ID = CAN_ID_RENVOI_TEMPERATURE) :
 *  - data[0] : octet fort (valeur * 100 / 256)
 *  - data[1] : octet faible (valeur * 100 % 256)
 */
void MesureTemperature(void)
{
  float temperature;
  int   temperatureInt;
  CanMessage_t txMsg;

  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  temperature = am2315.readTemperature();

  txMsg.id        = CAN_ID_RENVOI_TEMPERATURE;
  txMsg.len       = 2;
  temperatureInt  = temperature * 100;
  txMsg.data[0]   = temperatureInt / 256;
  txMsg.data[1]   = temperatureInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  Serial.printf("Temperature : %3.2f deg C\n\r", temperature);
}

/**
 * @brief Mesure l'humidité relative via AM2315 et l'envoie sur le bus CAN.
 *
 * Lit l'humidité en % via I2C et l'encode sur 2 octets (valeur × 100).
 * Protocole CAN (ID = CAN_ID_RENVOI_HUMIDITE) :
 *  - data[0] : octet fort (valeur * 100 / 256)
 *  - data[1] : octet faible (valeur * 100 % 256)
 */
void MesureHumidite(void)
{
  float humidite;
  int   humiditeInt;
  CanMessage_t txMsg;

  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  humidite = am2315.readHumidity();

  txMsg.id      = CAN_ID_RENVOI_HUMIDITE;
  txMsg.len     = 2;
  humiditeInt   = humidite * 100;
  txMsg.data[0] = humiditeInt / 256;
  txMsg.data[1] = humiditeInt % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  Serial.printf("Humidite : %3.2f %%\n\r", humidite);
}

/**
 * @brief Mesure groupée : humidité + température + irradiance, envoi en une trame CAN.
 *
 * Lit les trois grandeurs puis les envoie dans un seul message CAN (6 octets).
 * Protocole CAN (ID = CAN_ID_RENVOI_HUM_IRR_TEMP_EXT) :
 *  - data[0-1] : humidité   × 100 (octet fort | octet faible)
 *  - data[2-3] : température × 100 (octet fort | octet faible)
 *  - data[4-5] : irradiance  × 100 (octet fort | octet faible)
 */
void MesureHumiditeTemperatureIrradiance(void)
{
  float humidite, temperature;
  int   irradiance, irradianceInt, humiditeInt, temperatureInt;
  CanMessage_t txMsg;

  txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);

  humidite    = am2315.readHumidity();
  temperature = am2315.readTemperature();
  irradiance  = analogRead(PIN_CPT_IRR);

  humiditeInt    = humidite    * 100;
  temperatureInt = temperature * 100;
  irradianceInt  = irradiance  * 100;

  txMsg.id      = CAN_ID_RENVOI_HUM_IRR_TEMP_EXT;
  txMsg.len     = 6;
  txMsg.data[0] = humiditeInt    / 256;
  txMsg.data[1] = humiditeInt    % 256;
  txMsg.data[2] = temperatureInt / 256;
  txMsg.data[3] = temperatureInt % 256;
  txMsg.data[4] = irradianceInt  / 256;
  txMsg.data[5] = irradianceInt  % 256;
  EnvoiMessageCan(txMsg);

  txMsg.id  = CAN_ID_FIN_TRANSMISSION;
  txMsg.len = 0;
  EnvoiMessageCan(txMsg);
}

/* ==================================================================== */
/*  FONCTIONS D'ENVOI CAN                                                */
/* ==================================================================== */

/**
 * @brief Envoie un message sur le bus CAN.
 *
 * @param message Message à envoyer (id, len, data[]).
 */
void EnvoiMessageCan(CanMessage_t message)
{
    CAN.beginPacket(message.id);
    CAN.write(message.data, message.len);
    CAN.endPacket();
}

/**
 * @brief Envoie le numéro de cette carte sur le bus CAN.
 *
 * Répond à une demande d'identification (CAN_ID_DEMANDE_NUM_CARTE).
 * Envoie CAN_ID_RENVOI_NUM_CARTE avec data[0] = 10 (numéro fixe de la carte météo).
 */
void EnvoiNumeroCarte(void)
{
  CanMessage_t txMsg;
  txMsg.len     = 1;
  txMsg.id      = CAN_ID_RENVOI_NUM_CARTE;
  txMsg.data[0] = 10;
  EnvoiMessageCan(txMsg);
}
