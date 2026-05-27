/**
 * @file main_carte_VI_RTOS.cpp
 * @brief Carte Mesure I-V – caractérisation courant-tension d'un panneau solaire.
 *
 * Mesure la courbe I-V en faisant varier le rapport cyclique PWM (0–100 %)
 * sur une résistance de charge, puis en lisant tension et courant via ADC.
 * Le numéro de carte (1 à 5) est lu au démarrage sur un DIP switch 4 bits.
 *
 * **Architecture FreeRTOS – 6 tâches :**
 * | Tâche | Prio | Pile | Rôle |
 * |-------|------|------|------|
 * | TaskEnvoiMessageCan | 10 | 2048 | Envoie les trames CAN de `balTxCanMsg` |
 * | TaskTraitementMessageSerie | 9 | 3072 | Interprète les commandes série |
 * | TaskMesureCourbeVI | 5 | 4096 | Trace la courbe complète (23 points) |
 * | TaskMesurePointVI | 5 | 3072 | Mesure un point à alpha fixé |
 * | TaskMesureTemperatureTc74 | 5 | 3072 | Lecture TC74 via I2C |
 * | TaskEnvoiNumCarte | 5 | 2048 | Répond aux demandes d'identification |
 */

#include "main_carte_VI_RTOS.h"

/* ---- Coefficients de correction matérielle (un jeu par carte) ------ */
float facteurTension[5]   = {-0.00806, -0.00126, -0.00847, -0.00955, -0.01900};
float constanteTension[5] = { 1.13,     1.210,    1.14,     1.6,      1.31   };
float facteurCourant[5]   = { 0.00188, -0.0154,  -0.0066,  -0.0138,  -0.00653};
float constanteCourant[5] = { 1.01,     1.11,     1.04,     1.07,     1.05   };

/* ---- Objets FreeRTOS globaux --------------------------------------- */
EventGroupHandle_t flagsSystemeEvent;   // bits d'événements principaux
EventGroupHandle_t flagsMessageSerial;  // bit signalant un message série reçu
SemaphoreHandle_t  mutexSerialLink;     // protège l'accès à Serial.printf()
SemaphoreHandle_t  mutexCanLink;        // protège l'accès à la file CAN
QueueHandle_t      balTxCanMsg;         // file d'attente des messages CAN à envoyer
QueueHandle_t      balRapportCyclique;  // file d'attente des points VI à mesurer

/* ---- Numéro de cette carte (lu sur le DIP switch au démarrage) ----- */
unsigned char numCarte = 0;

/* ==================================================================== */
/*  SETUP – initialisation matérielle et création des tâches FreeRTOS   */
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

    /* Enregistrement des callbacks d'interruption */
    Serial.onReceive(OnReceiveSerial);
    CAN.onReceive(OnReceiveCan);

    /* Création des objets FreeRTOS */
    flagsSystemeEvent  = xEventGroupCreate();
    flagsMessageSerial = xEventGroupCreate();
    mutexSerialLink    = xSemaphoreCreateMutex();
    mutexCanLink       = xSemaphoreCreateMutex();
    balTxCanMsg        = xQueueCreate(20, sizeof(CanMessage_t));
    balRapportCyclique = xQueueCreate(10, sizeof(PointDeMesure_t));

    /* Création des tâches FreeRTOS
     *  xTaskCreate(fonction, nom, taille_pile, parametre, priorite, handle)
     *  Le paramètre (numCarte) est passé via un cast void* ; chaque tâche
     *  le récupère avec (char)(intptr_t)pvParameters */
    xTaskCreate(TaskEnvoiMessageCan,        "TACHE_TX_CAN",          2048, (void *)(intptr_t)numCarte, 10, NULL);
    xTaskCreate(TaskEnvoiNumCarte,          "TACHE_NUM_CARTE",        2048, (void *)(intptr_t)numCarte,  5, NULL);
    xTaskCreate(TaskMesurePointVI,          "TACHE_POINT_VI",         3072, (void *)(intptr_t)numCarte,  5, NULL);
    xTaskCreate(TaskMesureTemperatureTc74,  "TACHE_TEMPERATURE",      3072, (void *)(intptr_t)numCarte,  5, NULL);
    xTaskCreate(TaskTraitementMessageSerie, "TACHE_TRAITEMENT_SERIE", 3072, (void *)(intptr_t)numCarte,  9, NULL);
    xTaskCreate(TaskMesureCourbeVI,         "TACHE_MESURE_COURBE_VI", 4096, (void *)(intptr_t)numCarte,  5, NULL);
}

/* FreeRTOS gère la boucle principale ; loop() ne sert plus à rien */
void loop()
{
    vTaskDelay(portMAX_DELAY);
}

/**
 * @brief Callback CAN – appelée en ISR à chaque trame reçue.
 *
 * Lit l'ID et les données, puis lève le bit d'événement correspondant
 * dans `flagsSystemeEvent` **uniquement si le message est destiné à cette carte**
 * (vérification `data[0] == numCarte` pour ID=11 et ID=12).
 * @param packetSize Taille de la trame (fournie par la bibliothèque CAN).
 */
void OnReceiveCan(int packetSize)
{
    CanMessage_t localRxMsg;
    BaseType_t higherPriorityTaskWoken = pdFALSE; // sera mis à pdTRUE si une tâche de haute priorité doit être réveillée

    /* Lecture du message CAN */
    localRxMsg.id  = CAN.packetId();
    localRxMsg.len = CAN.packetDlc();
    int i = 0;
    while (CAN.available())
    {
        localRxMsg.data[i] = CAN.read();
        i++;
    }

    /* Selon l'identifiant CAN et si le message nous est destiné,
     * on lève le bit d'événement correspondant pour réveiller la bonne tâche */
    if ((localRxMsg.id == CAN_ID_DEMANDE_MESURE_VI) && (localRxMsg.data[0] == numCarte))
    {
        xEventGroupSetBitsFromISR(flagsSystemeEvent, FLAG_CAN_VI_ALL, &higherPriorityTaskWoken);
    }
    else if ((localRxMsg.id == CAN_ID_DEMANDE_TEMP_PANNEAU) && (localRxMsg.data[0] == numCarte))
    {
        xEventGroupSetBitsFromISR(flagsSystemeEvent, FLAG_CAN_TEMPERATURE, &higherPriorityTaskWoken);
    }
    else if (localRxMsg.id == CAN_ID_DEMANDE_NUM_CARTE)
    {
        xEventGroupSetBitsFromISR(flagsSystemeEvent, FLAG_CAN_NUM_CARTE, &higherPriorityTaskWoken);
    }

    /* Si une tâche de plus haute priorité a été réveillée, on lui donne la main immédiatement */
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/**
 * @brief Callback UART – appelée en ISR à chaque réception série.
 *
 * Lève `BIT_SERIAL_MSG` dans `flagsMessageSerial` pour réveiller
 * `TaskTraitementMessageSerie`.
 */
void OnReceiveSerial()
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(flagsMessageSerial, BIT_SERIAL_MSG, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

/**
 * @brief Tâche FreeRTOS – traitement des commandes série (priorité 9).
 *
 * Attend `BIT_SERIAL_MSG`, lit les caractères et exécute :
 * - `"M <alpha>"` → envoie un point dans `balRapportCyclique`
 * - `"A"` → lève `FLAG_SERIE_VI_ALL`
 * - `"T"` → lève `FLAG_SERIE_TEMPERATURE`
 * @param pvParameters Numéro de carte (cast `(char)(intptr_t)`).
 */
void TaskTraitementMessageSerie(void *pvParameters)
{
    String chaine   = "";  // accumule les caractères jusqu'au retour chariot
    String commande;
    String valeur;

    while (1)
    {
        /* Mise en sommeil jusqu'à réception d'un caractère série */
        xEventGroupWaitBits(flagsMessageSerial, BIT_SERIAL_MSG, pdTRUE, pdTRUE, portMAX_DELAY);

        /* Lecture caractère par caractère */
        while (Serial.available() > 0)
        {
            char ch = Serial.read();

            if (ch == '\r' || ch == '\n') // fin de commande
            {
                if (chaine.length() > 0)
                {
                    /* Découpage "COMMANDE VALEUR" */
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

                    /* Exécution de la commande */
                    if (commande == "M")
                    {
                        /* Envoie un point à mesurer dans la file d'attente */
                        PointDeMesure_t point;
                        point.alpha = valeur.toFloat();
                        xQueueSend(balRapportCyclique, &point, portMAX_DELAY);
                    }
                    else if (commande == "A")
                        xEventGroupSetBits(flagsSystemeEvent, FLAG_SERIE_VI_ALL);
                    else if (commande == "T")
                        xEventGroupSetBits(flagsSystemeEvent, FLAG_SERIE_TEMPERATURE);

                    chaine = ""; // réinitialisation du buffer
                }
            }
            else
            {
                chaine += ch; // accumulation des caractères
            }
        }
    }
}

/**
 * @brief Mesure un point de la courbe I-V.
 *
 * Séquence : ferme le relais → applique le PWM → attend 100 ms →
 * moyenne `MOYENNE` (100) lectures ADC → ouvre le relais → corrige
 * la non-linéarité avec les coefficients propres à `numCarte`.
 *
 * @param point En entrée : `alpha` (rapport cyclique 0–100 %).
 *              En sortie : `tension` (V) et `courant` (A) mesurés.
 */
void MesureVI(PointDeMesure_t *point)
{
    /* Étape 1 : fermeture du relais */
    digitalWrite(PIN_RELAIS, HIGH);

    /* Étape 2 : application du rapport cyclique PWM
     *  alpha (0–100 %) → valeur PWM (0–511) pour une résolution 9 bits */
    ledcWrite(CANAL, (int)(point->alpha / 100.0 * 512));

    /* Étape 3 : attente de stabilisation */
    vTaskDelay(pdMS_TO_TICKS(100));

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
    float courantBrut = (sommeCourant / MOYENNE) * 4  / 1319.0f;
    float tensionBrute = (sommeTension / MOYENNE) * 22 / 1954.0f;

    /* Étape 5 : ouverture du relais */
    digitalWrite(PIN_RELAIS, LOW);

    /* Étape 6 : correction de la non-linéarité des capteurs
     *  Formule : mesure_corrigee = mesure * (mesure * facteur + constante)
     *  Les coefficients dépendent du numéro de carte (index = numCarte - 1) */
    tensionBrute = tensionBrute * (tensionBrute * facteurTension[numCarte - 1] + constanteTension[numCarte - 1]);
    courantBrut  = courantBrut  * (courantBrut  * facteurCourant[numCarte - 1] + constanteCourant[numCarte - 1]);

    /* Écriture du résultat dans la structure pointée */
    point->courant = courantBrut;
    point->tension = tensionBrute;
}

/**
 * @brief Tâche FreeRTOS – mesure de la courbe I-V complète (priorité 5).
 *
 * Attend `FLAG_CAN_VI_ALL` ou `FLAG_SERIE_VI_ALL`, puis :
 * 1. Mesure Icc (alpha=100 %) et V0 (alpha=0 %)
 * 2. Répartit `NB_POINT` (23) points en échelle logarithmique
 * 3. Calcule le alpha nécessaire pour chaque point : `R_eq = V/I → alpha`
 * 4. Mesure chaque point réel via `MesureVI()`
 * 5. Envoie la séquence CAN : DEBUT + 23×`CAN_ID_RENVOI_MESURE_VI` + FIN
 * @param pvParameters Numéro de carte (cast `(char)(intptr_t)`).
 */
void TaskMesureCourbeVI(void *pvParameters)
{
    PointDeMesure_t courbeVI[NB_POINT]; // tableau de tous les points de la courbe
    PointDeMesure_t icc, v0;            // points aux extrêmes : court-circuit et circuit ouvert
    CanMessage_t txMsg;
    txMsg.data[4] = ((unsigned char)(intptr_t)pvParameters); // numéro de carte dans le message

    while (1)
    {
        /* Attente d'un ordre de mesure (CAN ou série) */
        xEventGroupWaitBits(flagsSystemeEvent, FLAG_CAN_VI_ALL | FLAG_SERIE_VI_ALL, pdTRUE, pdFALSE, portMAX_DELAY);

        /* --- Étape 1 : mesure des points extrêmes --- */
        icc.alpha = 100;  // court-circuit : relais fermé, PWM à 100 % → courant maximal
        MesureVI(&icc);

        v0.alpha = 0;     // circuit ouvert : PWM à 0 % → tension maximale
        MesureVI(&v0);

        xSemaphoreTake(mutexSerialLink, portMAX_DELAY);
        Serial.printf("Icc = %2.2f A, Imin = %2.2f A, V0 = %2.2f V, Vmin = %2.2f V\n",
                      icc.courant, v0.courant, v0.tension, icc.tension);
        xSemaphoreGive(mutexSerialLink);

        /* --- Étape 2 : répartition logarithmique des points ---
         *  On utilise log10 pour densifier les points près des extrémités
         *  où la courbe VI varie le plus rapidement.
         *
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
            xSemaphoreTake(mutexSerialLink, portMAX_DELAY);
            Serial.printf("point %d : I = %2.2f A, V = %2.2f V\n", i, courbeVI[i].courant, courbeVI[i].tension);
            xSemaphoreGive(mutexSerialLink);
        }

        /* --- Étape 3 : calcul du rapport cyclique pour chaque point ---
         *  On modélise la charge comme une résistance : R_eq = V / I
         *  Puis on en déduit l'alpha par le rapport de diviseur :
         *    alpha = (1 - R_eq / R_mesure) * 100 % */
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

        /* Affichage des résultats en format CSV pour tracé sur PC */
        xSemaphoreTake(mutexSerialLink, portMAX_DELAY);
        Serial.printf("tension,courant,alpha\n");
        for (int i = 0; i < NB_POINT; i++)
        {
            Serial.printf("%.2f,%.2f,%2.2f\n", courbeVI[i].tension, courbeVI[i].courant, courbeVI[i].alpha);
        }
        xSemaphoreGive(mutexSerialLink);

        /* --- Étape 5 : envoi des points sur le bus CAN ---
         *  Protocole : trame DEBUT_TRANSMISSION, puis N trames de données, puis FIN_TRANSMISSION.
         *  Chaque flottant est encodé sur 2 octets : valeur * 100 → octet fort | octet faible */
        xSemaphoreTake(mutexCanLink, portMAX_DELAY);

        txMsg.id  = CAN_ID_DEBUT_TRANSMISSION;
        txMsg.len = 0;
        xQueueSend(balTxCanMsg, &txMsg, portMAX_DELAY);

        txMsg.id = CAN_ID_RENVOI_MESURE_VI;
        for (int i = 0; i < NB_POINT; i++)
        {
            txMsg.len    = 5;
            int tensionEncode  = (int)(courbeVI[i].tension * 100.0);
            int courantEncode  = (int)(courbeVI[i].courant * 100.0);
            txMsg.data[0] = (unsigned char)((tensionEncode >> 8) % 256); // octet fort tension
            txMsg.data[1] = (unsigned char)( tensionEncode       % 256); // octet faible tension
            txMsg.data[2] = (unsigned char)((courantEncode >> 8) % 256); // octet fort courant
            txMsg.data[3] = (unsigned char)( courantEncode       % 256); // octet faible courant
            xQueueSend(balTxCanMsg, &txMsg, portMAX_DELAY);
        }

        txMsg.id  = CAN_ID_FIN_TRANSMISSION;
        txMsg.len = 0;
        xQueueSend(balTxCanMsg, &txMsg, portMAX_DELAY);

        xSemaphoreGive(mutexCanLink);
    }
}

/**
 * @brief Tâche FreeRTOS – envoi des messages CAN (priorité 10).
 *
 * Seul point d'écriture sur le bus CAN. Toutes les autres tâches déposent
 * leurs messages dans `balTxCanMsg` ; cette tâche les envoie un par un de
 * manière bloquante (`portMAX_DELAY`).
 * @param pvParameters Non utilisé.
 */
void TaskEnvoiMessageCan(void *pvParameters)
{
    CanMessage_t txMsg;
    while (1)
    {
        /* Bloquant : attend qu'un message soit disponible dans la file */
        xQueueReceive(balTxCanMsg, &txMsg, portMAX_DELAY);
        CAN.beginPacket(txMsg.id);
        CAN.write(txMsg.data, txMsg.len);
        CAN.endPacket();
    }
}

/**
 * @brief Tâche FreeRTOS – mesure de la température panneau via TC74 (priorité 5).
 *
 * Attend `FLAG_CAN_TEMPERATURE` ou `FLAG_SERIE_TEMPERATURE`, lit le TC74 (I2C, 0x48)
 * et envoie `CAN_ID_RENVOI_TEMP_PANNEAU` (ID=18) :
 * - `data[0]` = signe (1 si T > 0, 0 sinon)
 * - `data[1]` = valeur absolue en °C (entier)
 * - `data[2]` = numéro de carte
 * @param pvParameters Numéro de carte (cast `(char)(intptr_t)`).
 */
void TaskMesureTemperatureTc74(void *pvParameters)
{
    CanMessage_t txMsg;
    txMsg.len     = 3;
    txMsg.id      = CAN_ID_RENVOI_TEMP_PANNEAU;
    txMsg.data[2] = (char)(intptr_t)pvParameters; // numéro de carte

    TC74 tc74(0x48); // adresse I2C du capteur TC74
    tc74.begin();

    /* Attente que le capteur soit prêt (sortie du mode veille) */
    while (tc74.isStandby())
    {
        xSemaphoreTake(mutexSerialLink, portMAX_DELAY);
        Serial.println("TC74 en veille, attente...");
        xSemaphoreGive(mutexSerialLink);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    while (1)
    {
        /* Mise en sommeil jusqu'à un ordre de mesure de température */
        xEventGroupWaitBits(flagsSystemeEvent, FLAG_CAN_TEMPERATURE | FLAG_SERIE_TEMPERATURE, pdTRUE, pdFALSE, portMAX_DELAY);

        float temperature = tc74.readTemperature('C');

        /* Encodage du signe et de la valeur */
        txMsg.data[0] = (temperature > 0) ? 1 : 0;
        txMsg.data[1] = (char)temperature;

        /* Dépôt dans la file CAN */
        xSemaphoreTake(mutexCanLink, portMAX_DELAY);
        xQueueSend(balTxCanMsg, &txMsg, portMAX_DELAY);
        xSemaphoreGive(mutexCanLink);

        xSemaphoreTake(mutexSerialLink, portMAX_DELAY);
        Serial.printf("Temperature panneau : %2.2f °C\n", temperature);
        xSemaphoreGive(mutexSerialLink);
    }
}

/**
 * @brief Tâche FreeRTOS – réponse aux demandes d'identification (priorité 5).
 *
 * Attend `FLAG_CAN_NUM_CARTE` puis envoie `CAN_ID_RENVOI_NUM_CARTE` (ID=10)
 * avec `data[0]` = numéro de cette carte.
 * @param pvParameters Numéro de carte (cast `(char)(intptr_t)`).
 */
void TaskEnvoiNumCarte(void *pvParameters)
{
    CanMessage_t txMsg;
    txMsg.len     = 1;
    txMsg.id      = CAN_ID_RENVOI_NUM_CARTE;
    txMsg.data[0] = (char)(intptr_t)pvParameters; // numéro de carte

    while (1)
    {
        xEventGroupWaitBits(flagsSystemeEvent, FLAG_CAN_NUM_CARTE, pdTRUE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(mutexCanLink, portMAX_DELAY);
        xQueueSend(balTxCanMsg, &txMsg, portMAX_DELAY);
        xSemaphoreGive(mutexCanLink);
    }
}

/**
 * @brief Tâche FreeRTOS – mesure d'un point I-V à alpha fixé (priorité 5).
 *
 * Attend un `PointDeMesure_t` dans `balRapportCyclique` (déposé par la
 * commande série `"M <alpha>"`), appelle `MesureVI()` et affiche le résultat.
 * @param pvParameters Non utilisé.
 */
void TaskMesurePointVI(void *pvParameters)
{
    PointDeMesure_t point;
    while (1)
    {
        /* Bloquant : attend un point à mesurer dans la file */
        xQueueReceive(balRapportCyclique, &point, portMAX_DELAY);
        MesureVI(&point);
        xSemaphoreTake(mutexSerialLink, portMAX_DELAY);
        Serial.printf("Mesure VI : Alpha = %2.2f %%, Tension = %2.2f V, Courant = %2.2f A\n",
                      point.alpha, point.tension, point.courant);
        xSemaphoreGive(mutexSerialLink);
    }
}

