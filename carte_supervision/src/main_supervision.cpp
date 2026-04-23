#include <Arduino.h>

#include <CAN.h>

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
  float tension = 0.0;
  float courant = 0.0;
  float humidite = 0.0;
  float temperature = 0.0;
  float irradiance = 0.0;

  if (canAvailable == true)
  {
    switch (rxMsg.id)
    {

    case 7:
      Serial.println("0");
      break;

    case 8:
      Serial.println("99");
      break;

    case 19:
      tension = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100;
      courant = (rxMsg.data[2] * 256 + rxMsg.data[3]) / 100;
      Serial.printf(" %d;%.2f;%.2f", rxMsg.data[4], tension, courant);
      Serial.println();

      break;

    case 42:
      humidite = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100;
      Serial.printf("11;%.2f", humidite);
      Serial.println();

      break;

    case 43:
      temperature = (rxMsg.data[1] * 256 + rxMsg.data[2]) / 100;
      if (rxMsg.data[0] < 0)
      {
        temperature = -temperature;
      }
      Serial.printf("12;%.2f", temperature);
      Serial.println();

      break;

    case 44:
      irradiance = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100;
      Serial.printf("13;%.2f", irradiance);
      Serial.println();

      break;

    case 45:
      humidite = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100;
      temperature = (rxMsg.data[3] * 256 + rxMsg.data[4]) / 100;
      if (rxMsg.data[2] < 0)
      {
        temperature = -temperature;
      }
      irradiance = (rxMsg.data[0] * 256 + rxMsg.data[1]) / 100;

      Serial.printf("10;%.2f;%.2f;%.2f", humidite, temperature, irradiance);
      Serial.println();

      break;
    }
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
    }
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
      CAN.beginPacket(1);
      CAN.write(valeur.toInt());
      CAN.endPacket();
    }else if (commande == "M")
    {
      CAN.beginPacket(5);
      CAN.endPacket();
    }
    else if (commande == "VI")
    {
      if((valeur.toInt()) == 1)
      {
        CAN.beginPacket(11);
      }
      else if ((valeur.toInt()) == 2)
      {
        CAN.beginPacket(12);
      }
      else if ((valeur.toInt()) == 3)
      {
        CAN.beginPacket(13);
      }
      else if (valeur.toInt() == 4)
      {
        CAN.beginPacket(14);
      }
      else if (valeur.toInt() == 5)
      {
        CAN.beginPacket(15);
      }
      CAN.endPacket();
    }
    {
      CAN.beginPacket(6);
      CAN.endPacket();
    }
    chaine = "";
  }
  else
  {
    chaine += ch;
  }
}