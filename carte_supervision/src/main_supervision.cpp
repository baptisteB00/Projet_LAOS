/**
 * @file main_supervision.cpp
 * @brief Carte Supervision – réception CAN et interface série avec le PC.
 *
 * Reçoit tous les messages CAN du bus et les retransmet sur la liaison série
 * au format CSV pour traitement par le PC. Accepte également des commandes
 * série pour envoyer des requêtes CAN vers les autres cartes.
 *
 * **Format des trames reçues (série → PC) :**
 * - "0"                     : début de rafale (CAN_ID_DEBUT_TRANSMISSION)
 * - "9999"                  : fin de rafale (CAN_ID_FIN_TRANSMISSION)
 * - "0;<n>"                 : numéro de carte n (CAN_ID_RENVOI_NUM_CARTE)
 * - "1;<carte>;<V>;<I>"     : mesure I-V (CAN_ID_RENVOI_MESURE_VI)
 * - "2;<carte>;<T>"         : température panneau (CAN_ID_RENVOI_TEMP_PANNEAU)
 * - "10;<hum>;<temp>;<irr>" : météo groupée (CAN_ID_RENVOI_HUM_IRR_TEMP_EXT)
 * - "11;<hum>"              : humidité (CAN_ID_RENVOI_HUMIDITE)
 * - "12;<temp>"             : température extérieure (CAN_ID_RENVOI_TEMPERATURE)
 * - "13;<irr>"              : irradiance (CAN_ID_RENVOI_IRRADIANCE)
 * - "20;<str>;<pan>;<V>"    : tension string (CAN_ID_RENVOI_TENSION_STRING)
 * - "21;<I1>;<I2>;<I3>;<I4>": courants strings (CAN_ID_RENVOI_COURANT_STRING)
 *
 * **Commandes série (PC → carte) :**
 * - "R <0|1>"   : allume (1) ou éteint (0) le relais alimentation
 * - "M"         : demande météo groupée
 * - "VI <n>"    : demande mesure I-V de la carte n
 * - "T <n>"     : demande température panneau de la carte n
 * - "N"         : demande numéro de toutes les cartes
 * - "V <1-4>"   : demande tensions du string n
 * - "C"         : demande courants des 4 strings
 */

#include <Arduino.h>
#include <CAN.h>
#include <can_id.h>

typedef struct CANMessage
{
  bool         extented = false;
  bool         RTR      = false;
  unsigned int id       = 0;
  char         len      = 0;
  unsigned char data[8] = {0};
} CANMessage;

CANMessage    rxMsg;
volatile bool canAvailable = false;

void onReceive(int packetSize);
void reception(char ch);

/* ==================================================================== */
void setup()
{
  Serial.begin(115200);
  while (!Serial);

  Serial.println("CAN Receiver");

  if (!CAN.begin(10E3))
  {
    Serial.println("Starting CAN failed!");
    while (1);
  }
  CAN.onReceive(onReceive);
}

/* ==================================================================== */
void loop()
{
  int   numero_carte_recu;
  float tension         = 0.0f;
  float courant         = 0.0f;
  float humidite        = 0.0f;
  float temperature_ext = 0.0f;
  float irradiance      = 0.0f;
  int   numero_carte    = 0;
  int   temperature_panneau = 0;
  char  num_string;
  char  num_panneau;

  if (canAvailable == true)
  {
    switch (rxMsg.id)
    {
    case CAN_ID_DEBUT_TRANSMISSION:
      Serial.println("0");
      break;

    case CAN_ID_FIN_TRANSMISSION:
      Serial.println("9999");
      break;

    case CAN_ID_RENVOI_NUM_CARTE:
      numero_carte_recu = rxMsg.data[0];
      Serial.printf("0;%d\n\r", numero_carte_recu);
      break;

    case CAN_ID_RENVOI_TEMP_PANNEAU:
      temperature_panneau = rxMsg.data[1];
      if (rxMsg.data[0] == 0)
      {
        temperature_panneau = -temperature_panneau;
      }
      numero_carte = rxMsg.data[2];
      Serial.printf("2;%2d;%d", numero_carte, temperature_panneau);
      Serial.println();
      break;

    case CAN_ID_RENVOI_MESURE_VI:
      tension      = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      courant      = (rxMsg.data[2] * 256 + rxMsg.data[3]) / 100.0f;
      numero_carte = rxMsg.data[4];
      Serial.printf("1;%d;%.2f;%.2f\n\r", numero_carte, tension, courant);
      Serial.println();
      break;

    case CAN_ID_RENVOI_HUMIDITE:
      humidite = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      Serial.printf("11;%.2f", humidite);
      Serial.println();
      break;

    case CAN_ID_RENVOI_TEMPERATURE:
      temperature_ext = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      Serial.printf("12;%.2f", temperature_ext);
      Serial.println();
      break;

    case CAN_ID_RENVOI_IRRADIANCE:
      irradiance = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      Serial.printf("13;%.2f", irradiance);
      Serial.println();
      break;

    case CAN_ID_RENVOI_HUM_IRR_TEMP_EXT:
      humidite        = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      temperature_ext = (rxMsg.data[2] * 256 + rxMsg.data[3]) / 100.0f;
      irradiance      = (rxMsg.data[4] * 256 + rxMsg.data[5]) / 100.0f;
      Serial.printf("10;%.2f;%.2f;%.2f", humidite, temperature_ext, irradiance);
      Serial.println();
      break;

    case CAN_ID_RENVOI_TENSION_STRING:
      tension     = (rxMsg.data[3] | rxMsg.data[2] << 8) / 100.0f;
      num_string  = rxMsg.data[0];
      num_panneau = rxMsg.data[1];
      Serial.printf("20;%d;%d;%4.2f", num_string, num_panneau, tension);
      Serial.println();
      break;

    case CAN_ID_RENVOI_COURANT_STRING:
    {
      float courant_string_1 = (rxMsg.data[1] | rxMsg.data[0] << 8) / 100.0f;
      float courant_string_2 = (rxMsg.data[3] | rxMsg.data[2] << 8) / 100.0f;
      float courant_string_3 = (rxMsg.data[5] | rxMsg.data[4] << 8) / 100.0f;
      float courant_string_4 = (rxMsg.data[7] | rxMsg.data[6] << 8) / 100.0f;
      Serial.printf("21;%2.2f;%2.2f;%2.2f;%2.2f\n\r",
                    courant_string_1, courant_string_2,
                    courant_string_3, courant_string_4);
      Serial.println();
      break;
    }
    }
    canAvailable = false;
  }
}

/**
 * @brief Callback CAN – appelée à chaque trame reçue.
 *
 * Copie la trame dans rxMsg et lève canAvailable. Le traitement se fait
 * dans loop() pour ne pas bloquer la réception d'autres trames.
 * @param packetSize Taille de la trame reçue (fournie par la bibliothèque CAN).
 */
/* ==================================================================== */
void onReceive(int packetSize)
{
  rxMsg.id  = CAN.packetId();
  rxMsg.len = CAN.packetDlc();
  int i = 0;
  while (CAN.available())
  {
    rxMsg.data[i] = CAN.read();
    i++;
  }
  canAvailable = true;
}

/**
 * @brief Événement Arduino – appelé automatiquement à chaque réception UART.
 */
/* ==================================================================== */
void serialEvent()
{
  while (Serial.available() > 0)
  {
    reception(Serial.read());
  }
}

/**
 * @brief Analyse les caractères série un par un et envoie la commande CAN correspondante.
 *
 * Accumule les caractères jusqu'à CR/LF, découpe "COMMANDE VALEUR" puis
 * construit et envoie la trame CAN adaptée. Voir la liste des commandes
 * dans l'en-tête de fichier.
 * @param ch Caractère reçu depuis la liaison série.
 */
/* ==================================================================== */
void reception(char ch)
{
  static String chaine = "";
  String commande;
  String valeur;
  int index, length;

  if ((ch == 13) or (ch == 10))
  {
    index  = chaine.indexOf(' ');
    length = chaine.length();

    if (index == -1)
    {
      commande = chaine;
      valeur   = "";
    }
    else
    {
      commande = chaine.substring(0, index);
      valeur   = chaine.substring(index + 1, length);
    }

    if (commande == "R")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_ALIMENTATION);
      CAN.write(valeur.toInt());
      CAN.endPacket();
    }
    else if (commande == "M")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_HUM_IRR_TEMP_EXT);
      CAN.endPacket();
    }
    else if (commande == "VI")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_MESURE_VI);
      CAN.write(valeur.toInt());
      CAN.endPacket();
    }
    else if (commande == "T")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_TEMP_PANNEAU);
      CAN.write(valeur.toInt());
      CAN.endPacket();
    }
    else if (commande == "N")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_NUM_CARTE);
      CAN.write(valeur.toInt());
      CAN.endPacket();
    }
    else if (commande == "V")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_TENSION_STRING);
      CAN.write(valeur.toInt());
      CAN.endPacket();
    }
    else if (commande == "C")
    {
      CAN.beginPacket(CAN_ID_DEMANDE_COURANT_STRING);
      CAN.endPacket();
    }

    chaine = "";
  }
  else
  {
    chaine += ch;
  }
}
