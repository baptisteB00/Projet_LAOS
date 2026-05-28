/**
 * @file main_carte_vi.h
 * @brief Carte Mesure I-V – définitions des broches, drapeaux FreeRTOS,
 *        structures de données et déclarations des fonctions.
 */

#include <Arduino.h>
#include <CAN.h>
#include <tc74.h>
#include "math.h"
#include <can_id.h>

/** @defgroup Broches Broches matérielles
 * @{ */
#define PIN_SWITCH_BP1     25   ///< DIP switch bit 0 (numéro de carte)
#define PIN_SWITCH_BP2     26   ///< DIP switch bit 1
#define PIN_SWITCH_BP3     27   ///< DIP switch bit 2
#define PIN_SWITCH_BP4     14   ///< DIP switch bit 3
#define PIN_RELAIS         18   ///< Relais de connexion du panneau
#define PIN_LECTURE_TENSION 32  ///< Entrée ADC : tension panneau
#define PIN_LECTURE_COURANT 33  ///< Entrée ADC : courant panneau
/** @} */

/* ---- Paramètres de mesure ------------------------------------------ */
#define MOYENNE         100  // nombre de mesures pour faire une moyenne
#define R_mesure         22  // résistance de mesure du courant (en ohms)

/* ---- Nombre de points sur la courbe VI ----------------------------- */
#define NB_POINT_Icc_CONST   8   // points à courant constant (près de Icc)
#define NB_POINT_V0_CONST   15   // points à tension constante (près de V0)
#define NB_POINT  (NB_POINT_Icc_CONST + NB_POINT_V0_CONST)  // total de points

/* ---- Configuration du PWM ------------------------------------------ */
#define FREQUENCE   50000  // fréquence PWM : 50 kHz
#define CANAL           0  // canal PWM de l'ESP32 (0 à 15)
#define RESOlUTION      9  // résolution 9 bits → valeurs de 0 à 511

/* ---- Drapeaux d'événements (bits dans le groupe d'événements) -------
 *  Un groupe d'événements FreeRTOS est un registre de bits.
 *  Chaque tâche attend un ou plusieurs bits précis avec xEventGroupWaitBits().
 *  Quand l'événement arrive (réception CAN, commande série...),
 *  on lève le bit correspondant avec xEventGroupSetBits() pour
 *  réveiller la tâche concernée.
 * --------------------------------------------------------------------- */
#define FLAG_CAN_TEMPERATURE  BIT1   // demande de température reçue par CAN
#define FLAG_CAN_VI_ALL       BIT2   // demande de courbe VI reçue par CAN
#define FLAG_CAN_NUM_CARTE    BIT3   // demande d'identification reçue par CAN

#define BIT_SERIAL_MSG        BIT10  // message série reçu (levé dans la callback)
#define FLAG_SERIE_TEMPERATURE BIT11 // commande "T" reçue en série
#define FLAG_SERIE_VI_ALL     BIT12  // commande "A" reçue en série
#define FLAG_SERIE_NUM_CARTE  BIT13  // (réservé)

/** @brief Message CAN générique. */
typedef struct CanMessage_t
{
  unsigned int  id      = 0;      ///< Identifiant CAN (11 bits)
  char          len     = 0;      ///< Nombre d'octets de données (0 à 8)
  unsigned char data[8] = {0};    ///< Données du message
} CanMessage_t;

/** @brief Point de mesure sur la courbe I-V. */
typedef struct PointDeMesure_t
{
  float tension; ///< Tension mesurée (Volts)
  float courant; ///< Courant mesuré (Ampères)
  float alpha;   ///< Rapport cyclique PWM appliqué (0–100 %)
} PointDeMesure_t;

/* ---- Coefficients de correction matérielle -------------------------
 *  Les capteurs ne sont pas parfaitement linéaires. On corrige la mesure
 *  brute avec une équation du second degré : mesure_corrigee = mesure * (mesure*facteur + constante)
 *  Il y a un jeu de coefficients par numéro de carte (index = numCarte - 1).
 * --------------------------------------------------------------------- */
extern float facteurTension[5];
extern float constanteTension[5];
extern float facteurCourant[5];
extern float constanteCourant[5];

/* ---- Déclarations des fonctions ------------------------------------ */
void OnReceiveCan(int packetSize);    // callback CAN (interruption)
void OnReceiveSerial();               // callback série (interruption)

void TaskTraitementMessageSerie(void *pvParameters); // traite les commandes série
void TaskMesureTemperatureTc74(void *pvParameters);  // mesure temp. panneau (TC74)
void TaskMesurePointVI(void *pvParameters);          // mesure un seul point VI
void TaskEnvoiMessageCan(void *pvParameters);        // envoie les messages CAN en attente
void TaskEnvoiNumCarte(void *pvParameters);          // répond aux demandes d'identification
void TaskMesureCourbeVI(void *pvParameters);         // trace la courbe VI complète

void MesureVI(PointDeMesure_t *point); // mesure un point VI (tension + courant)
