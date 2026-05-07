#include <Arduino.h>
#include <CAN.h>
#include "TC74.h"
#include "math.h"


#define PIN_SWITCH_BP1 25
#define PIN_SWITCH_BP2 26
#define PIN_SWITCH_BP3 27
#define PIN_SWITCH_BP4 14
#define PIN_RELAIS 18
#define PIN_LECTURE_TENSION 32
#define PIN_LECTURE_COURANT 33

#define MOYENNE 100 // Définit le nombre d'échantillons pour la moyenne des mesures
#define R_mesure 22 // definit la valeur de la resistance de mesure

#define NB_POINT_Icc_CONST 8
#define NB_POINT_V0_CONST 15
#define NB_POINT NB_POINT_Icc_CONST + NB_POINT_V0_CONST // definit le nombre de point de mesure (ex: 3 => 3 point a Icc constant + 3point a V0 constant et 1 point a Icc et V0)


#define FLAG_CAN_TEMPERATURE BIT1
#define FLAG_CAN_VI_ALL BIT2
#define FLAG_CAN_NUM_CARTE BIT3

#define BIT_SERIAL_MSG BIT10
#define FLAG_SERIE_TEMPERATURE BIT11
#define FLAG_SERIE_VI_ALL BIT12
#define FLAG_SERIE_NUM_CARTE BIT13


// --- Configuration PWM ---
#define FREQUENCE  50000 // Fréquence du signal PWM (50 kHz)
#define CANAL  0         // Canal PWM (l'ESP32 en a 16)
#define RESOlUTION 9    // Résolution PWM : 9 bits = 2^9 = 512 pas (0-511)

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

float facteur_tension[5] ={-0.00806,-0.00126,-0.00847,-0.01900,-0.00955,};
float constante_tension[5] ={1.13,1.210,1.14,1.31,1.6,};
float facteur_courant[5] ={0.00188,-0.0154,-0.0066,-0.00653,-0.0138,};
float constante_courant[5] ={1.01,1.11,1.04,1.05,1.07,};

void onReceiveCan(int packetSize);
void onReceiveSerial();
void TACHE_Traitement_message_Serie(void *pvParameters);
void TACHE_mesure_temperature_TC74(void *pvParameters);
void TACHE_mesure_point_VI(void *pvParameters);
void TACHE_envoie_message_CAN(void *pvParameters);
void TACHE_envoie_num_carte(void *pvParameters);
void TACHE_mesure_courbe_VI(void *pvParameters);
void mesureVI(point_de_mesure *point);