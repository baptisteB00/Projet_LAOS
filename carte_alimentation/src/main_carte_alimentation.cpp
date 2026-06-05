/**
 * @file main_carte_alimentation.cpp
 * @brief Carte Alimentation – contrôle du relais et des LEDs d'état.
 *
 * Contrôle le relais principal qui alimente le panneau solaire et signale
 * son état via deux LEDs (verte = alimenté, rouge = coupé).
 *
 * **Messages CAN reçus :**
 * - `CAN_ID_DEMANDE_NUM_CARTE`    → renvoie le numéro de cette carte (12)
 * - `CAN_ID_DEMANDE_ALIMENTATION` → `data[0]=1` allume, `data[0]=0` éteint
 *
 * **Commandes série (débogage) :**
 * - `"R 1"` → allume le relais
 * - `"R 0"` → éteint le relais
 */

#include <Arduino.h>
#include <CAN.h>
#include <can_id.h>

/* ---- Structure d'un message CAN ------------------------------------ */
typedef struct CanMessage_t
{
  bool         extended = false;    // trame étendue (29 bits) ou standard (11 bits)
  bool         rtr      = false;    // trame de requête distante
  unsigned int id       = 0;        // identifiant CAN
  char         len      = 0;        // nombre d'octets de données (0 à 8)
  unsigned char data[8] = {0};      // données du message
} CanMessage_t;

/* ---- Broches matérielles ------------------------------------------- */
#define PIN_RELAIS    25
#define PIN_LED_VERTE 18
#define PIN_LED_ROUGE 19

/* ---- Variables globales -------------------------------------------- */
unsigned char numCarte   = 12;   // numéro identifiant cette carte sur le bus CAN
CanMessage_t  rxMsg;             // dernier message CAN reçu (rempli par la callback)
volatile bool canAvailable = false; // vrai quand un nouveau message est prêt à traiter

/* ---- Déclarations des fonctions ------------------------------------ */
void OnReceiveCan(int packetSize);
void ControleRelais(int onOff);
void Reception(char ch);

/* ==================================================================== */
void setup()
{
  Serial.begin(115200);
  while (!Serial);

  Serial.println("Carte Alimentation");

  /* Démarrage du bus CAN à 10 kbps */
  if (!CAN.begin(10E3))
  {
    Serial.println("Erreur : demarrage CAN impossible !");
    while (1);  // bloque ici si le CAN ne démarre pas
  }

  /* On enregistre la fonction qui sera appelée à chaque réception CAN */
  CAN.onReceive(OnReceiveCan);

  /* Configuration des sorties */
  pinMode(PIN_RELAIS,    OUTPUT);
  pinMode(PIN_LED_VERTE, OUTPUT);
  pinMode(PIN_LED_ROUGE, OUTPUT);
  ControleRelais(0);
}

/* ==================================================================== */
void loop()
{
  /* On vérifie si la callback CAN a reçu un nouveau message */
  if (canAvailable == true)
  {
    canAvailable = false;
    CanMessage_t msgLocal = rxMsg;

    if (msgLocal.id == CAN_ID_DEMANDE_NUM_CARTE)
    {
      /* La supervision demande à toutes les cartes de se présenter.
       * On attend numCarte * 10 ms avant de répondre pour éviter
       * que toutes les cartes répondent en même temps sur le bus. */
      delay(10 * numCarte);
      CAN.beginPacket(CAN_ID_RENVOI_NUM_CARTE);
      CAN.write(numCarte);
      CAN.endPacket();
    }
    else if (msgLocal.id == CAN_ID_DEMANDE_ALIMENTATION)
    {
      /* data[0] = 1 → allumer, data[0] = 0 → éteindre */
      ControleRelais(msgLocal.data[0]);
    }
  }
}

/**
 * @brief Commande le relais et les LEDs d'état.
 * @param onOff 0 = relais ouvert (LED rouge), 1 = relais fermé (LED verte).
 */
void ControleRelais(int onOff)
{
  if (onOff == 0)
  {
    digitalWrite(PIN_RELAIS,    0);
    digitalWrite(PIN_LED_VERTE, 0);
    digitalWrite(PIN_LED_ROUGE, 1);
  }
  else
  {
    digitalWrite(PIN_RELAIS,    1);
    digitalWrite(PIN_LED_VERTE, 1);
    digitalWrite(PIN_LED_ROUGE, 0);
  }
}

/**
 * @brief Callback CAN – appelée en interruption à chaque trame reçue.
 *
 * Copie la trame dans `rxMsg` et lève `canAvailable`. Le traitement
 * réel se fait dans `loop()` hors interruption.
 * @param packetSize Taille de la trame reçue (fournie par la bibliothèque CAN).
 */
void OnReceiveCan(int packetSize)
{
  rxMsg.id  = CAN.packetId();
  rxMsg.len = CAN.packetDlc();

  int i = 0;
  while (CAN.available())
  {
    rxMsg.data[i] = CAN.read();
    i++;
  }

  canAvailable = true; // signale à loop() qu'un message est prêt
}

/**
 * @brief Événement Arduino – appelé automatiquement à chaque réception UART.
 *
 * Lit tous les caractères disponibles et les transmet un par un à `Reception()`.
 */
void serialEvent()
{
  while (Serial.available() > 0)
  {
    Reception(Serial.read());
  }
}

/**
 * @brief Analyse les caractères série un par un et exécute la commande.
 *
 * Accumule les caractères jusqu'à CR/LF, puis découpe `"COMMANDE VALEUR"`.
 * Commande reconnue : `"R <0|1>"` → `ControleRelais()`.
 * @param ch Caractère reçu.
 */
void Reception(char ch)
{
  static String chaine = ""; // static = conservé entre deux appels
  String commande;
  String valeur;
  int index, length;

  if ((ch == 13) or (ch == 10))  // retour chariot ou saut de ligne
  {
    index  = chaine.indexOf(' ');
    length = chaine.length();

    if (index == -1)
    {
      commande = chaine;  // pas d'espace : toute la chaîne est la commande
      valeur   = "";
    }
    else
    {
      commande = chaine.substring(0, index);        // mot avant l'espace
      valeur   = chaine.substring(index + 1, length); // mot après l'espace
    }

    if (commande == "R")
    {
      ControleRelais(valeur.toInt());
    }

    chaine = ""; // on vide le buffer pour la prochaine commande
  }
  else
  {
    chaine += ch; // on accumule les caractères
  }
}
