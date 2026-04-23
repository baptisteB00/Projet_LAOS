#include <Arduino.h>
#include <CAN.h>
#include <Adafruit_AM2315.h>

#define PIN_CPT_IRR 34

Adafruit_AM2315 am2315;

float humidite, temperature;
int humiditeInt, temperatureInt, irradianceInt, irradiance; // codé en int 100 * humidite et temp

typedef struct CANMessage
{
  unsigned int id = 0;
  char len = 0;
  unsigned char data[8] = {0};
} CANMessage;

CANMessage rxMsg;
bool canAvailable = false;

int NumCarte = 11;

void onReceive(int packetSize);
// void reception(char ch);

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;

  Serial.println("Carte Meteo");

  // start the CAN bus at 1000 kbps
  if (!CAN.begin(10E3))
  {
    Serial.println("Starting CAN failed!");
    while (1)
      ;
  }
  // register the receive callback
  CAN.onReceive(onReceive);

  if (!am2315.begin())
  {
    Serial.println("Starting am2315 failed!");
    while (1)
      ;
  }
  Serial.println("Starting am2315 succed!");
  pinMode(PIN_CPT_IRR, INPUT);
}

void loop()
{

  if (canAvailable == true)
  {

    switch (rxMsg.id)
    {
    case 2:
      CAN.beginPacket(7);
      CAN.endPacket();

      CAN.beginPacket(42);
      humidite = am2315.readHumidity();
      // envoi humidité
      humiditeInt = humidite * 100;
      CAN.write(humiditeInt / 256);
      CAN.write(humiditeInt % 256);
      CAN.endPacket();

      CAN.beginPacket(8);
      CAN.endPacket();
      break;
    case 3:
      CAN.beginPacket(7);
      CAN.endPacket();

      CAN.beginPacket(43);
      temperature = am2315.readTemperature();
      // envoi temp
      temperatureInt = temperature * 100;
      CAN.write(temperatureInt / 256);
      CAN.write(temperatureInt % 256);
      CAN.endPacket();

      CAN.beginPacket(8);
      CAN.endPacket();
      break;
    case 4:
      CAN.beginPacket(7);
      CAN.endPacket();

      CAN.beginPacket(44);
      irradiance = analogRead(PIN_CPT_IRR);
      irradianceInt = irradiance * 100;
      CAN.write(irradianceInt / 256);
      CAN.write(irradianceInt % 256);
      CAN.endPacket();

      CAN.beginPacket(8);
      CAN.endPacket();
      break;
    case 0:
      CAN.beginPacket(7);
      CAN.endPacket();

      CAN.beginPacket(10);
      CAN.write(NumCarte);
      CAN.endPacket();

      CAN.beginPacket(8);
      CAN.endPacket();
      break;
    case 5:

      CAN.beginPacket(7);
      CAN.endPacket();

      CAN.beginPacket(45);

      humidite = am2315.readHumidity();
      temperature = am2315.readTemperature();
      irradiance = analogRead(PIN_CPT_IRR);
      Serial.printf("humidité : %f, temp : %f, irr : %d\n", humidite, temperature, irradiance);
      // envoi humidité
      humiditeInt = humidite * 100;
      CAN.write(humiditeInt / 256);
      CAN.write(humiditeInt % 256);

      // envoi temp et son signe
      temperatureInt = temperature * 100;
      if (temperature < 0)
      {
        CAN.write(1);
      }
      else
      {
        CAN.write(0);
      }
      CAN.write(temperatureInt / 256);
      CAN.write(temperatureInt % 256);

      // envoi irradiance
      irradianceInt = irradiance * 100;
      CAN.write(irradianceInt / 256);
      CAN.write(irradianceInt % 256);

      CAN.endPacket();

      CAN.beginPacket(8);
      CAN.endPacket();
      break;
    }
    canAvailable = false;
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


void reception(char ch);
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

    if (commande == "M")
    {
      //print all meteo data
      humidite = am2315.readHumidity();
      temperature = am2315.readTemperature();
      irradiance = analogRead(PIN_CPT_IRR);
      Serial.printf("humidité : %f, temp : %f, irr : %d\n", humidite, temperature, irradiance);

    }
    chaine = "";
  }
  else
  {
    chaine += ch;
  }
}