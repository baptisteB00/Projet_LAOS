/**
 * @file main_carte_vi.cpp
 * @brief Carte Mesure I-V – caractérisation courant-tension d'un panneau solaire.
 *
 * Mesure la courbe I-V en faisant varier le rapport cyclique PWM (0–100 %)
 * sur une résistance de charge, puis en lisant tension et courant via ADC.
 * Le numéro de carte (1 à 5) est lu au démarrage sur un DIP switch 4 bits.
 */

#include "main_carte_vi.h"

/* ---- Coefficients de correction matérielle (un jeu par carte) ------ */
float facteurTension[5]   = {-0.00806, -0.00126, -0.00847, -0.00955, -0.01900};
float constanteTension[5] = { 1.13,     1.210,    1.14,     1.6,      1.31   };
float facteurCourant[5]   = { 0.00188, -0.0154,  -0.0066,  -0.0138,  -0.00653};
float constanteCourant[5] = { 1.01,     1.11,     1.04,     1.07,     1.05   };

/* ---- Variables globales -------------------------------------------- */
unsigned char numCarte   = 0;        // numéro de carte lu sur le DIP switch
TC74          tc74(0x48);            // capteur de température I2C
CanMessage_t  rxMsg;                 // dernier message CAN reçu (rempli par l'ISR)
bool          canAvailable = false;  // vrai quand un nouveau message CAN est prêt

/* ==================================================================== */
/*  SETUP – initialisation matérielle                                    */
/* ==================================================================== */
void setup()
{
    Serial.begin(115200);

    /* Démarrage du bus CAN à 10 kbps */
    if (!CAN.begin(10E3))
    {
        Serial.printf("Erreur : demarrage CAN impossible !\n");
        while (1);
    }

    /* Relais de connexion du panneau : ouvert par défaut */
    pinMode(PIN_RELAIS, OUTPUT);
    digitalWrite(PIN_RELAIS, LOW);

    /* Configuration du PWM sur la broche 19
     *   CANAL 0, fréquence 50 kHz, résolution 9 bits (0 à 511) */
    ledcSetup(CANAL, FREQUENCE, RESOlUTION);
    ledcAttachPin(19, CANAL);
    ledcWrite(CANAL, 0); // rapport cyclique initial = 0 %

    /* Lecture du numéro de carte sur les 4 interrupteurs DIP (BP1 à BP4)
     * BP1 = bit 0, BP2 = bit 1, BP3 = bit 2, BP4 = bit 3 → valeur 0 à 15 */
    pinMode(PIN_SWITCH_BP1, INPUT);
    pinMode(PIN_SWITCH_BP2, INPUT);
    pinMode(PIN_SWITCH_BP3, INPUT);
    pinMode(PIN_SWITCH_BP4, INPUT);
    numCarte = digitalRead(PIN_SWITCH_BP1)
             | (digitalRead(PIN_SWITCH_BP2) << 1)
             | (digitalRead(PIN_SWITCH_BP3) << 2)
             | (digitalRead(PIN_SWITCH_BP4) << 3);
    Serial.printf("Numero de carte : %d\n", numCarte);

    if (numCarte < 1 || numCarte > 5)
    {
        Serial.printf("Erreur : numCarte = %d invalide\n\r", numCarte);
        while (1);
    }

    /* Enregistrement des callbacks */
    Serial.onReceive(OnReceiveSerial);
    CAN.onReceive(OnReceiveCan);

    tc74.begin();
}

/* ==================================================================== */
/*  LOOP – traitement des messages reçus                                 */
/* ==================================================================== */
void loop()
{
    if (canAvailable == true)
    {
        canAvailable = false;
        CanMessage_t localRxMsg = rxMsg; // copie locale pour éviter une modification par l'ISR

        if ((localRxMsg.id == CAN_ID_DEMANDE_MESURE_VI) && (localRxMsg.data[0] == numCarte))
        {
            MesureCourbeVI();
        }
        else if ((localRxMsg.id == CAN_ID_DEMANDE_TEMP_PANNEAU) && (localRxMsg.data[0] == numCarte))
        {
            MesureTemperatureTc74();
        }
        else if (localRxMsg.id == CAN_ID_DEMANDE_NUM_CARTE)
        {
            EnvoiNumCarte();
        }
    }
}

/* ==================================================================== */
/*  CALLBACKS                                                            */
/* ==================================================================== */

void OnReceiveCan(int packetSize)
{
    rxMsg.id  = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    for (int i = 0; CAN.available(); i++)
    {
        rxMsg.data[i] = CAN.read();
    }
    canAvailable = true;
}

void OnReceiveSerial()
{
    String chaine  = "";
    String commande;
    String valeur;

    while (Serial.available() > 0)
    {
        char ch = Serial.read();

        if (ch == '\r' || ch == '\n')
        {
            if (chaine.length() > 0)
            {
                int index = chaine.indexOf(' ');
                if (index == -1)
                {
                    commande = chaine;
                    valeur   = "";
                }
                else
                {
                    commande = chaine.substring(0, index);
                    valeur   = chaine.substring(index + 1);
                }

                if (commande == "M")
                {
                    PointDeMesure_t point;
                    point.alpha = valeur.toInt();
                    MesureVI(&point);
                }
                else if (commande == "A")
                    MesureCourbeVI();
                else if (commande == "T")
                    MesureTemperatureTc74();

                chaine = "";
            }
        }
        else
        {
            chaine += ch;
        }
    }
}

/* ==================================================================== */
/*  FONCTIONS DE MESURE                                                  */
/* ==================================================================== */

void MesureVI(PointDeMesure_t *point)
{
    /* Étape 1 : fermeture du relais */
    digitalWrite(PIN_RELAIS, HIGH);

    /* Étape 2 : application du rapport cyclique PWM
     *  alpha (0–100 %) → valeur PWM (0–511) pour une résolution 9 bits */
    ledcWrite(CANAL, (int)(point->alpha / 100.0 * 512));

    /* Étape 3 : attente de stabilisation */
    delay(100);

    /* Étape 4 : accumulation de MOYENNE lectures pour réduire le bruit */
    float sommeCourant = 0;
    float sommeTension = 0;
    for (int i = 0; i < MOYENNE; i++)
    {
        sommeCourant += analogReadMilliVolts(PIN_LECTURE_COURANT);
        sommeTension += analogReadMilliVolts(PIN_LECTURE_TENSION);
    }

    /* Calcul de la valeur moyenne et mise à l'échelle physique
     *   Courant : diviseur de tension ×4, pleine échelle 1319 mV → Ampères
     *   Tension : diviseur de tension ×22, pleine échelle 1954 mV → Volts  */
    float courantBrut  = (sommeCourant / MOYENNE) * 4  / 1319.0f;
    float tensionBrute = (sommeTension / MOYENNE) * 22 / 1954.0f;

    /* Étape 5 : ouverture du relais */
    digitalWrite(PIN_RELAIS, LOW);

    /* Étape 6 : correction de la non-linéarité des capteurs
     *  Formule : mesure_corrigee = mesure * (mesure * facteur + constante)
     *  Les coefficients dépendent du numéro de carte (index = numCarte - 1) */
    tensionBrute = tensionBrute * (tensionBrute * facteurTension[numCarte - 1] + constanteTension[numCarte - 1]);
    courantBrut  = courantBrut  * (courantBrut  * facteurCourant[numCarte - 1] + constanteCourant[numCarte - 1]);

    point->courant = courantBrut;
    point->tension = tensionBrute;
}

void MesureCourbeVI(void)
{
    PointDeMesure_t courbeVI[NB_POINT];
    PointDeMesure_t icc, v0;
    CanMessage_t txMsg;
    txMsg.data[4] = numCarte;

    /* --- Étape 1 : mesure des points extrêmes --- */
    icc.alpha = 100;  // court-circuit : courant maximal
    MesureVI(&icc);

    v0.alpha = 0;     // circuit ouvert : tension maximale
    MesureVI(&v0);

    Serial.printf("Icc = %2.2f A, Imin = %2.2f A, V0 = %2.2f V, Vmin = %2.2f V\n",
                  icc.courant, v0.courant, v0.tension, icc.tension);

    /* --- Étape 2 : répartition logarithmique des points ---
     *  Première moitié (i < NB_POINT_V0_CONST) : tension fixée à V0, courant varie
     *  Deuxième moitié (i >= NB_POINT_V0_CONST) : courant fixé à Icc, tension varie */
    for (int i = 0; i < NB_POINT; i++)
    {
        if (i < NB_POINT_V0_CONST)
        {
            courbeVI[i].tension = v0.tension;
            courbeVI[i].courant = v0.courant + (icc.courant - v0.courant)
                                  * log10(1 + (i * 9) / (float)(NB_POINT_V0_CONST - 1));
        }
        else
        {
            courbeVI[i].tension = icc.tension + (v0.tension - icc.tension)
                                  * log10(1 + ((i - NB_POINT_V0_CONST) * 9) / (float)(NB_POINT_Icc_CONST - 1));
            courbeVI[i].courant = icc.courant;
        }
        Serial.printf("point %d : I = %2.2f A, V = %2.2f V\n", i, courbeVI[i].courant, courbeVI[i].tension);
    }

    /* --- Étape 3 : calcul du rapport cyclique pour chaque point ---
     *  R_eq = V / I  →  alpha = (1 - R_eq / R_mesure) * 100 % */
    for (int i = 0; i < NB_POINT; i++)
    {
        if (courbeVI[i].courant == 0.0f)
        {
            courbeVI[i].alpha = 0.0f;
            continue;
        }
        float rEq = courbeVI[i].tension / courbeVI[i].courant;
        courbeVI[i].alpha = (1.0f - (rEq / R_mesure)) * 100.0f;
        if (courbeVI[i].alpha < 0.0f)   courbeVI[i].alpha = 0.0f;
        if (courbeVI[i].alpha > 100.0f) courbeVI[i].alpha = 100.0f;
    }

    /* --- Étape 4 : mesure réelle de chaque point --- */
    for (int i = 0; i < NB_POINT; i++)
    {
        MesureVI(&courbeVI[i]);
    }

    /* Affichage CSV pour tracé sur PC */
    Serial.printf("tension,courant,alpha\n");
    for (int i = 0; i < NB_POINT; i++)
    {
        Serial.printf("%.2f,%.2f,%2.2f\n", courbeVI[i].tension, courbeVI[i].courant, courbeVI[i].alpha);
    }

    /* --- Étape 5 : envoi sur le bus CAN ---
     *  Protocole : DEBUT_TRANSMISSION + N trames + FIN_TRANSMISSION
     *  Chaque flottant est encodé sur 2 octets : valeur * 100 → octet fort | octet faible */
    txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
    txMsg.len = 0;
    EnvoiMessageCan(txMsg);

    txMsg.id = CAN_ID_RENVOI_MESURE_VI;
    for (int i = 0; i < NB_POINT; i++)
    {
        txMsg.len = 5;
        int tensionEncode = (int)(courbeVI[i].tension * 100.0);
        int courantEncode = (int)(courbeVI[i].courant * 100.0);
        txMsg.data[0] = (unsigned char)((tensionEncode >> 8) % 256); // octet fort tension
        txMsg.data[1] = (unsigned char)( tensionEncode       % 256); // octet faible tension
        txMsg.data[2] = (unsigned char)((courantEncode >> 8) % 256); // octet fort courant
        txMsg.data[3] = (unsigned char)( courantEncode       % 256); // octet faible courant
        EnvoiMessageCan(txMsg);
    }

    txMsg.id  = CAN_ID_FIN_TRANSMISSION;
    txMsg.len = 0;
    EnvoiMessageCan(txMsg);
}

void MesureTemperatureTc74(void)
{
    CanMessage_t txMsg;
    CanMessage_t marqueurMsg;
    marqueurMsg.len = 0;
    txMsg.len = 3;
    txMsg.id  = CAN_ID_RENVOI_TEMP_PANNEAU;

    float temperature = tc74.readTemperature('C');

    /* Encodage : data[0] = signe (1 si T>0), data[1] = valeur absolue, data[2] = numéro de carte */
    if (temperature > 0)
    {
        txMsg.data[0] = 1;
        txMsg.data[1] = (char) temperature;
    }
    else
    {
        txMsg.data[0] = 0;
        txMsg.data[1] = (char) -temperature;
    }
    txMsg.data[2] = numCarte;

    marqueurMsg.id = CAN_ID_DEBUT_TRANSMISSION;
    EnvoiMessageCan(marqueurMsg);
    EnvoiMessageCan(txMsg);
    marqueurMsg.id = CAN_ID_FIN_TRANSMISSION;
    EnvoiMessageCan(marqueurMsg);

    Serial.printf("Temperature panneau : %2.2f deg C\n", temperature);
}

/* ==================================================================== */
/*  FONCTIONS D'ENVOI CAN                                                */
/* ==================================================================== */

void EnvoiMessageCan(CanMessage_t message)
{
    CAN.beginPacket(message.id);
    CAN.write(message.data, message.len);
    CAN.endPacket();
}

void EnvoiNumCarte(void)
{
    CanMessage_t txMsg;
    txMsg.id      = CAN_ID_RENVOI_NUM_CARTE;
    txMsg.len     = 1;
    txMsg.data[0] = numCarte;
    EnvoiMessageCan(txMsg);
}
