/**
 * @file main_carte_vi.h
 * @brief Carte Mesure I-V – définitions des broches, structures de données
 *        et déclarations des fonctions.
 */

#include <Arduino.h>
#include <CAN.h>
#include <tc74.h>
#include "math.h"
#include <can_id.h>

/* ---- Broches matérielles ------------------------------------------- */
#define PIN_SWITCH_BP1      25  // DIP switch bit 0 (numéro de carte)
#define PIN_SWITCH_BP2      26  // DIP switch bit 1
#define PIN_SWITCH_BP3      27  // DIP switch bit 2
#define PIN_SWITCH_BP4      14  // DIP switch bit 3
#define PIN_RELAIS          18  // Relais de connexion du panneau
#define PIN_LECTURE_TENSION 32  // Entrée ADC : tension panneau
#define PIN_LECTURE_COURANT 33  // Entrée ADC : courant panneau

/* ---- Paramètres de mesure ------------------------------------------ */
#define MOYENNE   100  // nombre de mesures pour faire une moyenne
#define R_mesure   22  // résistance de mesure du courant (en ohms)

/* ---- Nombre de points sur la courbe VI ----------------------------- */
#define NB_POINT_Icc_CONST   8   // points à courant constant (près de Icc)
#define NB_POINT_V0_CONST   15   // points à tension constante (près de V0)
#define NB_POINT  (NB_POINT_Icc_CONST + NB_POINT_V0_CONST)  // total de points

/* ---- Configuration du PWM ------------------------------------------ */
#define FREQUENCE   50000  // fréquence PWM : 50 kHz
#define CANAL           0  // canal PWM de l'ESP32 (0 à 15)
#define RESOlUTION      9  // résolution 9 bits → valeurs de 0 à 511

/* ---- Structure d'un message CAN ------------------------------------ */
typedef struct CanMessage_t
{
  unsigned int  id      = 0;    // identifiant CAN (11 bits)
  char          len     = 0;    // nombre d'octets de données (0 à 8)
  unsigned char data[8] = {0};  // données du message
} CanMessage_t;

/* ---- Structure d'un point de mesure sur la courbe I-V -------------- */
typedef struct PointDeMesure_t
{
  float tension; // tension mesurée (Volts)
  float courant; // courant mesuré (Ampères)
  float alpha;   // rapport cyclique PWM appliqué (0–100 %)
} PointDeMesure_t;

/* ---- Coefficients de correction matérielle -------------------------
 *  Les capteurs ne sont pas parfaitement linéaires. On corrige la mesure
 *  brute avec une équation du second degré :
 *    mesure_corrigee = mesure * (mesure * facteur + constante)
 *  Il y a un jeu de coefficients par numéro de carte (index = numCarte - 1).
 * --------------------------------------------------------------------- */
extern float facteurTension[5];
extern float constanteTension[5];
extern float facteurCourant[5];
extern float constanteCourant[5];

/* ---- Déclarations des fonctions ------------------------------------ */
void OnReceiveCan(int packetSize); // callback CAN : stocke le message reçu
void OnReceiveSerial();            // callback série : traite les commandes

void MesureVI(PointDeMesure_t *point);  // mesure un point VI (tension + courant)
void MesureCourbeVI(void);              // mesure la courbe VI complète
void MesureTemperatureTc74(void);       // mesure la température via TC74
void EnvoiNumCarte(void);               // envoie le numéro de cette carte
void EnvoiMessageCan(CanMessage_t msg); // envoie un message sur le bus CAN
