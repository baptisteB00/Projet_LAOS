#include "main_carte_VI_RTOS.h"

EventGroupHandle_t xFlagsSystemeEvent;
EventGroupHandle_t xFlagsMessageSerial;
SemaphoreHandle_t xMutexSerialLink;
SemaphoreHandle_t xMutexCanLink;
QueueHandle_t xBalTxCanMsg;
QueueHandle_t xBalRapportCyclique;

char num_carte = 0;

void setup()
{
    Serial.begin(115200); // Initialisation de la communication série (moniteur)
    if (!CAN.begin(10E3))
    {
        Serial.printf("can marche pas \n");
        while (1)
            ;
    }

    ledcSetup(CANAL, FREQUENCE, RESOlUTION); // Configure le canal 0 avec la fréquence et la résolution définies
    ledcAttachPin(19, CANAL);                // Attache la broche 19 au canal PWM 0
    ledcWrite(CANAL, 0);                     // Applique un rapport cyclique initial (255 sur 511, soit ~50%)

    pinMode(PIN_SWITCH_BP1, INPUT);
    pinMode(PIN_SWITCH_BP2, INPUT);
    pinMode(PIN_SWITCH_BP3, INPUT);
    pinMode(PIN_SWITCH_BP4, INPUT);
    num_carte = digitalRead(PIN_SWITCH_BP1) | (digitalRead(PIN_SWITCH_BP2) << 1) | (digitalRead(PIN_SWITCH_BP3) << 2) | (digitalRead(PIN_SWITCH_BP4) << 3);
    Serial.printf("Numero de carte : %d\n", num_carte);

    Serial.onReceive(onReceiveSerial);
    CAN.onReceive(onReceiveCan);

    xFlagsSystemeEvent = xEventGroupCreate();
    xFlagsMessageSerial = xEventGroupCreate();
    xMutexSerialLink = xSemaphoreCreateMutex();
    xMutexCanLink = xSemaphoreCreateMutex();
    xBalTxCanMsg = xQueueCreate(20, sizeof(CANMessage));
    xBalRapportCyclique = xQueueCreate(10, sizeof(point_de_mesure));

    xTaskCreate(TACHE_envoie_message_CAN, "TACHE_TX_CAN", 2048, (void *)(intptr_t)num_carte, 10, NULL);
    xTaskCreate(TACHE_envoie_num_carte, "TACHE_NUM_CARTE", 2048, (void *)(intptr_t)num_carte, 5, NULL);
    xTaskCreate(TACHE_mesure_point_VI, "TACHE_POINT_VI", 3072, (void *)(intptr_t)num_carte, 5, NULL);
    xTaskCreate(TACHE_mesure_temperature_TC74, "TACHE_TEMPERATURE", 3072, (void *)(intptr_t)num_carte, 5, NULL);
    xTaskCreate(TACHE_Traitement_message_Serie, "TACHE_TRAITEMENT_SERIE", 3072, (void *)(intptr_t)num_carte, 9, NULL);
    xTaskCreate(TACHE_mesure_courbe_VI, "TACHE_MESURE_COURBE_VI", 4096, (void *)(intptr_t)num_carte, 5, NULL);
}

void loop()
{
    vTaskDelay(portMAX_DELAY);
}

void onReceiveCan(int packetSize)
{

    CANMessage rxMsg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    rxMsg.id = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    int i = 0;
    while (CAN.available())
    {
        rxMsg.data[i] = CAN.read();
        i++;
    }

    if ((rxMsg.id == (11)) && (rxMsg.data[0] == num_carte)) // Si l'ID du message CAN est 11, on lance la mesure VI
    {
        xEventGroupSetBitsFromISR(xFlagsSystemeEvent, FLAG_CAN_VI_ALL, &xHigherPriorityTaskWoken);
    }
    else if ((rxMsg.id == (12)) && (rxMsg.data[0] == num_carte)) // Si l'ID du message CAN est 12, on lance la mesure de la temperature
    {
        xEventGroupSetBitsFromISR(xFlagsSystemeEvent, FLAG_CAN_TEMPERATURE, &xHigherPriorityTaskWoken);
    }
    else if (rxMsg.id == (0))
    {
        xEventGroupSetBitsFromISR(xFlagsSystemeEvent, FLAG_CAN_NUM_CARTE, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void onReceiveSerial()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xEventGroupSetBitsFromISR(xFlagsMessageSerial, BIT_SERIAL_MSG, &xHigherPriorityTaskWoken);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void TACHE_Traitement_message_Serie(void *pvParameters)
{
    String chaine = "";
    String commande;
    String valeur;
    while (1)
    {
        xEventGroupWaitBits(xFlagsMessageSerial, BIT_SERIAL_MSG, pdTRUE, pdTRUE, portMAX_DELAY); // Attente d'un message série
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
                        valeur = "";
                    }
                    else
                    {
                        commande = chaine.substring(0, index);
                        valeur = chaine.substring(index + 1);
                    }

                    if (commande == "M")
                    {
                        point_de_mesure point;
                        point.alpha = valeur.toFloat();
                        xQueueSend(xBalRapportCyclique, &point, portMAX_DELAY);
                    }
                    else if (commande == "A")
                        xEventGroupSetBits(xFlagsSystemeEvent, FLAG_SERIE_VI_ALL);
                    else if (commande == "T")
                        xEventGroupSetBits(xFlagsSystemeEvent, FLAG_SERIE_TEMPERATURE);

                    chaine = "";
                }
            }
            else
            {
                chaine += ch;
            }
        }
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
    vTaskDelay(pdMS_TO_TICKS(100)); // Pause de 0.1 seconde pour laisser le circuit se stabiliser

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
    
    Vpanneau = Vpanneau * (Vpanneau*facteur_tension[num_carte-1] + constante_tension[num_carte-1]);
    Vcourant = Vcourant * (Vcourant*facteur_courant[num_carte-1] + constante_courant[num_carte-1]);
    
    Serial.printf("avant correction courant : %.2f , tension : %.2f",Vcourant,Vpanneau);    
   
    point->courant = Vpanneau;
    point->tension = Vcourant;
    
    Serial.printf("apres correction courant : %.2f , tension : %.2f",Vcourant,Vpanneau);    
}

void TACHE_mesure_courbe_VI(void *pvParameters)
{
    point_de_mesure couple_VI2[NB_POINT]; // tableau des points de mesures

    point_de_mesure Icc, V0;
    CANMessage RxMsg;
    RxMsg.data[4] = ((unsigned char)(intptr_t)pvParameters); // stocke le numero de carte dans le message CAN pour l'envoyer ensuite
    while (1)
    {
        xEventGroupWaitBits(xFlagsSystemeEvent, FLAG_CAN_VI_ALL | FLAG_SERIE_VI_ALL, pdTRUE, pdFALSE, portMAX_DELAY); // attente du message de la callback de reveiller la tache de mesure
        Icc.alpha = 100;
        mesureVI(&Icc); // mesure de Icc ET Vmin

        V0.alpha = 0;
        mesureVI(&V0); // mesure de V0 et Imin
        xSemaphoreTake(xMutexSerialLink, portMAX_DELAY);
        Serial.printf("Icc = %2.2f, Imin = %2.2f , V0 = %2.2f ,Vmin = %2.2f\n", Icc.courant, V0.courant, V0.tension, Icc.tension);
        xSemaphoreGive(xMutexSerialLink);
        // calcule des point theorique a mesuré
        for (int i = 0; i < NB_POINT; i++)
        {
            if (i < NB_POINT_V0_CONST)
            {
                couple_VI2[i].tension = V0.tension;
                couple_VI2[i].courant = V0.courant + (Icc.courant - V0.courant) * log10(1 + ((i) * 9) / (float)(NB_POINT_V0_CONST - 1));
            }
            else
            {
                couple_VI2[i].tension = Icc.tension + (V0.tension - Icc.tension) * log10(1 + ((i - NB_POINT_V0_CONST) * 9) / (float)(NB_POINT_Icc_CONST - 1));
                couple_VI2[i].courant = Icc.courant;
            }
            xSemaphoreTake(xMutexSerialLink, portMAX_DELAY);
            Serial.printf("point %d : I = %2.2f, V = %2.2f\n", i, couple_VI2[i].courant, couple_VI2[i].tension);
            xSemaphoreGive(xMutexSerialLink);
        }
        // calculs des rapports cyclique a appliquer pour la mesure du point theorique
        for (int i = 0; i < NB_POINT; i++)
        {
            float R_eq = couple_VI2[i].tension / couple_VI2[i].courant;
            couple_VI2[i].alpha = (1.0 - (R_eq / R_mesure)) * 100;
        }

        // mesures
        for (int i = 0; i < NB_POINT; i++)
        {
            mesureVI(&couple_VI2[i]);
        }

        xSemaphoreTake(xMutexSerialLink, portMAX_DELAY);
        Serial.printf("tension,courant,alpha\n");
        for (int i = 0; i < NB_POINT; i++)
        {
            Serial.printf("%.2f,%.2f,%2.2f\n", couple_VI2[i].tension, couple_VI2[i].courant, couple_VI2[i].alpha);
        }
        xSemaphoreGive(xMutexSerialLink);

        xSemaphoreTake(xMutexCanLink, portMAX_DELAY);
        RxMsg.id = 7;
        RxMsg.len = 0; // indicateur de debut de transmition des poitns
        xQueueSend(xBalTxCanMsg, &RxMsg, portMAX_DELAY);
        RxMsg.id = 19;
        for (int i = 0; i < NB_POINT; i++)
        {
            RxMsg.len = 5;
            RxMsg.data[0] = (unsigned char)(((int)(couple_VI2[i].tension * 100.0) >> 8) % 256);
            RxMsg.data[1] = (unsigned char)((int)(couple_VI2[i].tension * 100.0) % 256);
            RxMsg.data[2] = (unsigned char)(((int)(couple_VI2[i].courant * 100.0) >> 8) % 256);
            RxMsg.data[3] = (unsigned char)((int)(couple_VI2[i].courant * 100.0) % 256);
            xQueueSend(xBalTxCanMsg, &RxMsg, portMAX_DELAY);
        }
        RxMsg.id = 8;
        RxMsg.len = 0; // indicateur de fin de transmition des points
        xQueueSend(xBalTxCanMsg, &RxMsg, portMAX_DELAY);
        xSemaphoreGive(xMutexCanLink);
    }
}

void TACHE_envoie_message_CAN(void *pvParameters)
{
    CANMessage TxMsg;
    while (1)
    {
        xQueueReceive(xBalTxCanMsg, &TxMsg, portMAX_DELAY);
        CAN.beginPacket(TxMsg.id);
        CAN.write(TxMsg.data, TxMsg.len);
        CAN.endPacket();
    }
}

void TACHE_mesure_temperature_TC74(void *pvParameters)
{
    CANMessage TxMsg;
    TxMsg.len = 3;
    TxMsg.id = 18;
    TxMsg.data[2] = (char)(intptr_t)pvParameters;

    TC74 Tc74(0x48);
    Tc74.begin();

    while (Tc74.isStandby())
    { // wait until the sensor is ready
        xSemaphoreTake(xMutexSerialLink, portMAX_DELAY);
        Serial.println("La loose");
        xSemaphoreGive(xMutexSerialLink);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    while (1)
    {

        xEventGroupWaitBits(xFlagsSystemeEvent, FLAG_CAN_TEMPERATURE | FLAG_SERIE_TEMPERATURE, pdTRUE,pdFALSE, portMAX_DELAY);
        float temperature = Tc74.readTemperature('C');
        if (temperature > 0)
        {
            TxMsg.data[0] = 1;
        }
        else
        {
            TxMsg.data[0] = 0;
        }
        TxMsg.data[1] = (char)temperature;

        xSemaphoreTake(xMutexCanLink, portMAX_DELAY);
        xQueueSend(xBalTxCanMsg, &TxMsg, portMAX_DELAY);
        xSemaphoreGive(xMutexCanLink);
        xSemaphoreTake(xMutexSerialLink, portMAX_DELAY);
        Serial.printf("Temperature : %2.2f °C\n", temperature);
        xSemaphoreGive(xMutexSerialLink);
    }
}

void TACHE_envoie_num_carte(void *pvParameters)
{
    CANMessage TxMsg;
    TxMsg.len = 1;
    TxMsg.id = 10;
    TxMsg.data[0] = (char)(intptr_t)pvParameters;
    while (1)
    {
        xEventGroupWaitBits(xFlagsSystemeEvent, FLAG_CAN_NUM_CARTE, pdTRUE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(xMutexCanLink, portMAX_DELAY);
        xQueueSend(xBalTxCanMsg, &TxMsg, portMAX_DELAY);
        xSemaphoreGive(xMutexCanLink);    }
}
void TACHE_mesure_point_VI(void *pvParameters)
{
    point_de_mesure point;
    while (1)
    {
        xQueueReceive(xBalRapportCyclique, &point, portMAX_DELAY);
        mesureVI(&point);
        xSemaphoreTake(xMutexSerialLink, portMAX_DELAY);
        Serial.printf("Mesure VI : Alpha = %2.2f, Tension = %2.2f V, Courant = %2.2f A\n", point.alpha, point.tension, point.courant);
        xSemaphoreGive(xMutexSerialLink);
    }
}
