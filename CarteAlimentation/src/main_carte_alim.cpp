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

/**************************************** define constante hard***********************************/
#define PinRelais 25
#define PinLedVerte 18
#define PinLedRouge 19
/*************************************** de claration variable globales  *************************/
int NumCarte = 12;

void onReceive(int packetSize);
void controleRelais(int OnOff);
void reception(char ch);

void setup()
{
  Serial.begin(115200);
while (!Serial);
  
  Serial.println("Carte Alimentation");

  // start the CAN bus at 1000 kbps
  if (!CAN.begin(10E3))
  {
    Serial.println("Starting CAN failed!");
    while (1)
      ;
  }
  // register the receive callback
  CAN.onReceive(onReceive);
  // pinMode
  pinMode(PinRelais, OUTPUT);
  pinMode(PinLedVerte, OUTPUT);
  pinMode(PinLedRouge, OUTPUT);
}

void loop()
{
  if (canAvailable == true)
  {
    



    if (rxMsg.id == 0)
    {
      CAN.beginPacket(0x10);
      CAN.write(NumCarte);
      delay(10 * NumCarte);
      CAN.endPacket();
    }
    else if (rxMsg.id == 1)
    {
      controleRelais(rxMsg.data[0]);
    }

    canAvailable = false;
  }
}

void controleRelais(int OnOff)
{
  if (OnOff == 0)
  {
    digitalWrite(PinRelais, 0);
    digitalWrite(PinLedVerte, 0);
    digitalWrite(PinLedRouge, 1);
  }
  else
  {
    digitalWrite(PinRelais, 1);
    digitalWrite(PinLedVerte, 1);
    digitalWrite(PinLedRouge, 0);
  }
}

/*****************************************************************************/
/*    Fonction de callback appelée  lors de la reception d'un message can    */
/*****************************************************************************/

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
      controleRelais(valeur.toInt());
    }
    chaine = "";
  }
  else
  {
    chaine += ch;
  }
}