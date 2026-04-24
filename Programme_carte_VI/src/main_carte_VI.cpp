/*-------------------------------------------------------------------------------------------
- Description générale (Objectif du programme) :
- attente reception commande liaison serie M XXX où XXX est le rapport cyclique en %
- Apres recpetion
     - fixer la PWM
     - fermer les relais
     - mesurer I et U
     - ouvrir les relais
---------------------------------------------------------------------------------------------*/

#include <Arduino.h>
#include <CAN.h>
#include "TC74.h"
#include "math.h"

#define MOYENNE 100 // Définit le nombre d'échantillons pour la moyenne des mesures
#define R_mesure 22 // definit la valeur de la resistance de mesure

#define NB_POINT_Icc_CONST 8
#define NB_POINT_V0_CONST 15
#define NB_POINT NB_POINT_Icc_CONST + NB_POINT_V0_CONST // definit le nombre de point de mesure (ex: 3 => 3 point a Icc constant + 3point a V0 constant et 1 point a Icc et V0)

typedef struct CANMessage
{
  unsigned int id = 0;
  char len = 0;
  unsigned char data[8] = {0};
} CANMessage;

CANMessage rxMsg;
bool canAvailable = false;

struct point_de_mesure
{
  float tension;
  float courant;
  float alpha;
};

point_de_mesure couple_VI2[NB_POINT]; // tableau des points de mesures

float Icc, V0, Imin, Vmin;
// --- Configuration PWM ---
int frequence = 50000; // Fréquence du signal PWM (50 kHz)
int canal = 0;         // Canal PWM (l'ESP32 en a 16)
int resolution = 9;    // Résolution PWM : 9 bits = 2^9 = 512 pas (0-511)

// --- Assignation des broches (Pins) ---
int relais = 18;         // Broche de commande du relais (GPIO 18)
const int Vpanneau = 32; // Broche d'entrée analogique pour la tension panneau (GPIO 32)
const int Vcourant = 33; // Broche d'entrée analogique pour le courant (GPIO 33)
const int BP1 = 25;
const int BP2 = 26;
const int BP3 = 27;
const int BP4 = 14;

int num_carte = 0; // variable pour stocker le numero de carte

// --- Variables globales pour les mesures ---
float voltage_Vcourant = 0; // Variable pour stocker la tension liée au courant
float voltage_Vpanneau = 0; // Variable pour stocker la tension du panneau

void onReceive(int packetSize);

TC74 dvc(0x48); // A5 Address, also default

void mesure_temperature();

void setup()
{
  Serial.begin(115200); // Initialisation de la communication série (moniteur)
  if (!CAN.begin(10E3))
  {
    Serial.printf("can marche pas \n");
    while (1)
      ;
  }
  dvc.begin();
  while (dvc.isStandby())
  { // wait until the sensor is ready
    Serial.println("La loose");
    delay(3000);
  }
  Serial.println("carte VI");
  pinMode(19, OUTPUT); // Définit la broche 19 (GPIO 19) comme sortie pour le PWM

  // Configuration du "LEDC" (Contrôleur PWM de l'ESP32)
  ledcSetup(canal, frequence, resolution); // Configure le canal 0 avec la fréquence et la résolution définies
  ledcAttachPin(19, canal);                // Attache la broche 19 au canal PWM 0
  ledcWrite(canal, 0);                     // Applique un rapport cyclique initial (255 sur 511, soit ~50%)

  // Configuration du relais
  pinMode(relais, OUTPUT);   // Définit la broche du relais comme sortie
  digitalWrite(relais, LOW); // Met le relais à l'état BAS (supposé "ouvert" / "off")

  // configuration du numero de carte
  pinMode(BP1, INPUT);
  pinMode(BP2, INPUT);
  pinMode(BP3, INPUT);
  pinMode(BP4, INPUT);

  num_carte = digitalRead(BP1) | (digitalRead(BP2) << 1) | (digitalRead(BP3) << 2) | (digitalRead(BP4) << 3);
  Serial.printf("Numero de carte : %d\n", num_carte);

  CAN.onReceive(onReceive);
}

void mesureVI(float alpha)
{
  int i; // Compteur de boucle

  // 1. Fermer le relais
  digitalWrite(relais, HIGH); // Met le relais à l'état HAUT (supposé "fermé" / "on")

  // 2. Fixer la PWM
  // Convertit le pourcentage 'alpha' (0-100) en valeur 9 bits (0-511)
  ledcWrite(canal, (int)(alpha / 100.0 * 512));

  // 3. Attente de stabilisation
  delay(100); // Pause de 1 seconde pour laisser le circuit se stabiliser

  // 4. Mesurer I et U
  voltage_Vcourant = 0; // Réinitialise les accumulateurs de mesure
  voltage_Vpanneau = 0;

  for (i = 0; i < MOYENNE; i++) // Boucle pour faire la moyenne
  {
    // Lit les valeurs analogiques en millivolts et les ajoute au total
    voltage_Vcourant += analogReadMilliVolts(Vcourant);
    voltage_Vpanneau += analogReadMilliVolts(Vpanneau);
  }

  voltage_Vcourant = voltage_Vcourant / MOYENNE * 4 / 1319.0f;
  voltage_Vpanneau = voltage_Vpanneau / MOYENNE * 22 / 1954.0f;
  /*
    Serial.println(); // Saut de ligne pour la lisibilité

    // Affiche les moyennes (Valeur totale / nombre d'échantillons)
    Serial.print("Vpanneau ");
    Serial.println(voltage_Vpanneau);
    Serial.print("Vcourant ");
    Serial.println(voltage_Vcourant);
  */
  // 5. Ouvrir les relais
  digitalWrite(relais, LOW); // Met le relais à l'état BAS ("ouvert" / "off")
  
}


void mesure_VI_All()
{
  mesureVI(100); // mesure de Icc ET Vmin
  Icc = voltage_Vcourant;
  Vmin = voltage_Vpanneau;

  mesureVI(0); // mesure de V0 et Imin
  V0 = voltage_Vpanneau;
  Imin = voltage_Vcourant;

  Serial.printf("Icc = %2.2f, Imin = %2.2f , V0 = %2.2f ,Vmin = %2.2f\n", Icc, Imin, V0, Vmin);

  // calculs des points de mesures
  for (int i = 0; i < NB_POINT; i++)
  {
    if (i < NB_POINT_V0_CONST)
    {
      couple_VI2[i].tension = V0;
      couple_VI2[i].courant = Imin + (Icc - Imin) * log10(1 + ((i) * 9) / (float) (NB_POINT_V0_CONST - 1));
    }
    else
    {

      couple_VI2[i].tension = Vmin + (V0 - Vmin) *  log10(1 + ((i-NB_POINT_V0_CONST) * 9) / (float) (NB_POINT_Icc_CONST - 1));
      couple_VI2[i].courant = Icc;
    }
    Serial.printf("point %d : I = %2.2f, V = %2.2f\n", i, couple_VI2[i].courant, couple_VI2[i].tension);
  }

  // calculs de alpha
  for (int i = 0; i < NB_POINT; i++)
  {
    float R_eq = couple_VI2[i].tension / couple_VI2[i].courant;
    couple_VI2[i].alpha = (1.0 - (R_eq / R_mesure)) * 100;
  }

  // mesures effectives
  for (int i = 0; i < NB_POINT; i++)
  {
    mesureVI(couple_VI2[i].alpha);
    couple_VI2[i].tension = voltage_Vpanneau;
    couple_VI2[i].courant = voltage_Vcourant;
  }

  // print a CSV file

  Serial.printf("tension,courant,alpha\n");

  for (int i = 0; i < NB_POINT; i++)
  {
    Serial.printf("%.2f,%.2f,%2.2f\n", couple_VI2[i].tension, couple_VI2[i].courant, couple_VI2[i].alpha);
  }

  // envoie des message CAN
  // trame de debut de message
  CAN.beginPacket(7);
  CAN.endPacket();
  // contenue du message
  for (int i = 0; i < NB_POINT; i++)
  {
    CAN.beginPacket(19);
    CAN.write((unsigned char)(((int)(couple_VI2[i].tension * 100.0) / 256) % 256));
    CAN.write((unsigned char)((int)(couple_VI2[i].tension * 100.0) % 256));
    CAN.write((unsigned char)(((int)(couple_VI2[i].courant * 100.0) / 256) % 256));
    CAN.write((unsigned char)((int)(couple_VI2[i].courant * 100.0) % 256));
    CAN.write((unsigned char)num_carte);
    CAN.endPacket();
    delay(10); // petit delai pour laisser le temps au recepteur de traiter
  }

  CAN.beginPacket(8);
  CAN.endPacket();
}

void reception(char ch)
{
  static int i = 0;          // Variable statique 'i' déclarée mais non utilisée ici.
  static String chaine = ""; // Buffer statique pour accumuler les caractères
  String commande;
  String valeur;
  int index, length;

  // Vérifie si le caractère est une fin de ligne (Entrée)
  if ((ch == 13) or (ch == 10))
  {
    // Une commande complète a été reçue

    index = chaine.indexOf(' '); // Trouve la position de l'espace
    length = chaine.length();    // Longueur totale de la chaîne

    if (index == -1) // Pas d'espace trouvé
    {
      commande = chaine; // La chaîne entière est la commande
      valeur = "";       // Pas de valeur
    }
    else // Espace trouvé
    {
      commande = chaine.substring(0, index);        // Extrait la commande (ex: "M")
      valeur = chaine.substring(index + 1, length); // Extrait la valeur (ex: "50")
    }

    // Traitement de la commande
    if (commande == "M")
    {
      // Si la commande est "M", lance une mesure VI
      mesureVI(valeur.toInt()); // Convertit la valeur en entier et appelle la fonction
    }
    else if (commande == "A")
    {
      mesure_VI_All();
    }
    else if (commande == "T")
    {
      mesure_temperature();
    }

    chaine = ""; // Réinitialise le buffer pour la prochaine commande
  }
  else
  {
    // Ce n'est pas une fin de ligne, on ajoute le caractère au buffer
    chaine += ch;
  }
}

void loop()
{
  if (canAvailable == true)
  {
    if ((rxMsg.id == (11)) && (rxMsg.data[0] == num_carte)) // Si l'ID du message CAN est 1, on lance la mesure VI
    {
      mesure_VI_All();
    }
    else if ((rxMsg.id == (12)) && (rxMsg.data[0] == num_carte)) // Si l'ID du message CAN est 1, on lance la mesure VI
    {
      mesure_temperature();
      Serial.println("reçu");
    }
    else if (rxMsg.id == (0))
    {
      delay(num_carte * 10);
      CAN.beginPacket(10);
      CAN.write(num_carte);
      CAN.endPacket();
    }
    canAvailable = false;
  }
}

void serialEvent()
{
  while (Serial.available() > 0) // Tant qu'il y a des caractères à lire
  {
    reception(Serial.read()); // Lit un caractère et l'envoie à notre fonction "reception"
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

void mesure_temperature()
{

  int temp_c = dvc.readTemperature('c');

  Serial.printf("Temperature : %2.2f °C\n", temp_c);

  CAN.beginPacket(18);
  CAN.write((unsigned char)temp_c % 2);
  CAN.write((unsigned char)temp_c);
  CAN.write((unsigned char)num_carte);
  CAN.endPacket();
}
