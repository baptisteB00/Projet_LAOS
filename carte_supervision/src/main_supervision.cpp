#include <Arduino.h>
#include <CAN.h>
#include <can_id.h>

typedef struct CANMessage
{
  bool extented = false;
  bool RTR = false;
  unsigned int id = 0;
  char len = 0;
  unsigned char data[8] = {0};
} CANMessage;

CANMessage rxMsg;
bool canAvailable = false;

void onReceive(int packetSize);
void reception(char ch);

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;

  Serial.println("CAN Receiver");

  // start the CAN bus at 1000 kbps
  if (!CAN.begin(10E3))
  {
    Serial.println("Starting CAN failed!");
    while (1)
      ;
  }
  // register the receive callback
  CAN.onReceive(onReceive);
}

void loop()
{
  int numero_carte_recu ;
  float tension = 0.0f;
  float courant = 0.0f;
  float humidite = 0.0f;
  float temperature_ext = 0.0f;
  float irradiance = 0.0f;
  int numero_carte = 0;
  int temperature_panneau = 0;
  if (canAvailable == true)
  {
    switch (rxMsg.id)
    {

    case CAN_ID_DEBUT_TRANSMITION:
      Serial.println("0");
      break;

    case CAN_ID_FIN_TRANSMISSION:
      Serial.println("99");
      break;

    case CAN_ID_DEMANDE_NUM_CARTE:
    numero_carte_recu = rxMsg.data[0];
    Serial.printf("0;%d\n\r", numero_carte_recu);
    break;

    case CAN_ID_DEMANDE_TEMP_PANNEAU:
      temperature_panneau = rxMsg.data[1];
      numero_carte = rxMsg.data[2];
      Serial.printf("2;%2d;%d", numero_carte, temperature_panneau);
      break;

    case CAN_ID_RENVOI_MESURE_VI:
      tension = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      courant = (rxMsg.data[2] * 256 + rxMsg.data[3]) / 100.0f;
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
      temperature_ext = (rxMsg.data[1] * 256 + rxMsg.data[2]) / 100.0f;
      if (rxMsg.data[0] == 0)
      {
        temperature_ext = -temperature_ext;
      }
      Serial.printf("12;%.2f", temperature_ext);
      Serial.println();

      break;

    case CAN_ID_RENVOI_IRRADIANCE:
      irradiance = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      Serial.printf("13;%.2f", irradiance);
      Serial.println();

      break;

    case CAN_ID_RENVOI_HUM_IRR_TEMP_EXT:
      humidite = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100.0f;
      temperature_ext = (rxMsg.data[2] * 256 + rxMsg.data[3]) / 100.0f;
      irradiance = (rxMsg.data[4] * 256 + rxMsg.data[5]) / 100.0f;

      Serial.printf("10;%.2f;%.2f;%.2f", humidite, temperature_ext, irradiance);
      Serial.println();

      break;
    }
    /*
    printf("ID  = %d\n", rxMsg.id);
    printf("Len = %d\n", rxMsg.len);
    if (rxMsg.len > 0)
    {
      Serial.print("Data(s) : ");
      for (int i = 0; i < rxMsg.len; i++)
      {
        Serial.print(rxMsg.data[i]);
        Serial.print(" ");
      }
      Serial.println();
    }*/
    canAvailable = false;
  }
}

void onReceive(int packetSize)
{
  rxMsg.id = CAN.packetId();
  rxMsg.len = CAN.packetDlc();
  int i = 0;
  while (CAN.available())
  {
    rxMsg.data[i] = CAN.read();
    i++;
  }
  canAvailable = true;
}

void serialEvent()
{
  while (Serial.available() > 0) // tant qu'il y a des caractères à lire
  {
    reception(Serial.read());
  }
}

void reception(char ch)
{
  static int i = 0;
  static String chaine = "";
  String commande;
  String valeur;
  int index, length;

  if ((ch == 13) or (ch == 10))
  {
    index = chaine.indexOf(' ');
    length = chaine.length();

    if (index == -1)
    {
      commande = chaine;
      valeur = "";
    }
    else
    {
      commande = chaine.substring(0, index);
      valeur = chaine.substring(index + 1, length);
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

    chaine = "";
  }
  else
  {
    chaine += ch;
  }
}

