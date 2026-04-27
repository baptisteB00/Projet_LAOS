#include <Arduino.h>
#include <CAN.h>
#include "TC74.h"
#include "math.h"

#define PIN_RELAIS 18
#define PIN_LECTURE_TENSION 32
#define PIN_LECTURE_COURANT 33

#define FREQUENCE 50000 // Fréquence du signal PWM (50 kHz)
#define CANAL 0         // Canal PWM (l'ESP32 en a 16)
#define RESOLUTION 9    // Résolution PWM : 9 bits = 2^9 = 512 pas (0-511)
#define MOYENNE 100     // Définit le nombre d'échantillons pour la moyenne des mesures
#define R_mesure 22     // definit la valeur de la resistance de mesure

#define NB_POINT_Icc_CONST 8
#define NB_POINT_V0_CONST 15
#define NB_POINT NB_POINT_Icc_CONST + NB_POINT_V0_CONST // definit le nombre de point de mesure (ex: 3 => 3 point a Icc constant + 3point a V0 constant et 1 point a Icc et V0)

#define BIT_0 (1<<0)

EventGroupHandle_t xFlagsMessageCan;

char num_carte = 0;

void onReceive(int packetSize);

struct point_de_mesure
{
    float tension;
    float courant;
    float alpha;
};

void setup()
{
    Serial.begin(115200); // Initialisation de la communication série (moniteur)
    if (!CAN.begin(10E3))
    {
        Serial.printf("can marche pas \n");
        while (1)
            ;
    }

    CAN.onReceive(onReceive);

    xFlagsMessageCan= xEventGroupCreate();
}

void onReceive(int packetSize)
{

    typedef struct CANMessage
    {
        unsigned int id = 0;
        char len = 0;
        unsigned char data[8] = {0};
    } CANMessage;

    CANMessage rxMsg;

    rxMsg.id = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    int i = 0;
    while (CAN.available())
    {
        rxMsg.data[i] = CAN.read();
        i++;
    }

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
}

void mesureVI(point_de_mesure *point)
{
    // 1. Fermer le PIN_RELAIS
    digitalWrite(PIN_RELAIS, HIGH); // Met le PIN_RELAIS à l'état HAUT (supposé "fermé" / "on")

    // 2. Fixer la PWM
    // Convertit le pourcentage 'alpha' (0-100) en valeur 9 bits (0-511)
    ledcWrite(CANAL, (int)(point->alpha / 100.0 * 512));

    // 3. Attente de stabilisation
    delay(100); // Pause de 0.1 seconde pour laisser le circuit se stabiliser

    // 4. Mesurer I et U
    float voltage_Vcourant = 0; // Réinitialise les accumulateurs de mesure
    float voltage_Vpanneau = 0;

    for (int i = 0; i < MOYENNE; i++) // Boucle pour faire la moyenne
    {
        // Lit les valeurs analogiques en millivolts et les ajoute au total
        voltage_Vcourant += analogReadMilliVolts(PIN_LECTURE_COURANT);
        voltage_Vpanneau += analogReadMilliVolts(PIN_LECTURE_TENSION);
    }

    float Vcourant = voltage_Vcourant / MOYENNE * 4 / 1319.0f;
    float Vpanneau = voltage_Vpanneau / MOYENNE * 22 / 1954.0f;

    // 5. Ouvrir les PIN_RELAIS
    digitalWrite(PIN_RELAIS, LOW); // Met le PIN_RELAIS à l'état BAS ("ouvert" / "off")
    point->courant = voltage_Vcourant;
    point->tension = voltage_Vpanneau;
}

void Tache_mesure_courbe_VI()
{
    point_de_mesure couple_VI2[NB_POINT]; // tableau des points de mesures

    while (1)
    {
        xEventGroupWaitBits(xFlagsMessageCan,BIT0,pdTRUE,pdTRUE,portMAX_DELAY); // attente du message de la callback de reveiller la tache de mesure
        
    }
}