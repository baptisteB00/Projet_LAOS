/*
 * ============================================================
 *  CARTE ALIMENTATION
 * ============================================================
 *  Rôle : contrôle le relais principal qui alimente le panneau
 *         solaire, et signale son état via deux LEDs.
 *
 *  Matériel :
 *    - Relais         (broche PIN_RELAIS)   : coupe/rétablit l'alimentation
 *    - LED verte      (broche PIN_LED_VERTE): allumée = panneau alimenté
 *    - LED rouge      (broche PIN_LED_ROUGE): allumée = panneau coupé
 *    - Bus CAN 10 kbps
 *
 *  Commandes CAN reçues :
 *    - CAN_ID_DEMANDE_NUM_CARTE   → renvoie le numéro de cette carte
 *    - CAN_ID_DEMANDE_ALIMENTATION → allume (data[0]=1) ou éteint (data[0]=0)
 *
 *  Commande série (débogage) :
 *    - "R 1" → allume le relais
 *    - "R 0" → éteint le relais
 * ============================================================
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

/* ==================================================================== */
/*  Commande le relais et les LEDs d'état
 *    onOff = 0 → relais ouvert  (panneau coupé,  LED rouge)
 *    onOff = 1 → relais fermé   (panneau alimenté, LED verte)
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

/* ==================================================================== */
/*  Callback CAN – appelée automatiquement à chaque message reçu.
 *  ATTENTION : cette fonction s'exécute en interruption.
 *  On se contente de copier le message dans rxMsg et de lever un drapeau ;
 *  le traitement réel est fait dans loop() pour rester hors interruption.
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

/* ==================================================================== */
/*  Appelée automatiquement par Arduino quand des caractères arrivent
 *  sur la liaison série.
 */
void serialEvent()
{
  while (Serial.available() > 0)
  {
    Reception(Serial.read());
  }
}

/* ==================================================================== */
/*  Analyse les caractères reçus un par un sur la liaison série.
 *  On reconstruit la chaîne jusqu'au retour chariot (CR ou LF),
 *  puis on découpe "COMMANDE VALEUR" et on exécute la commande.
 *
 *  Exemple : "R 1\r" → commande="R", valeur="1" → ControleRelais(1)
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
