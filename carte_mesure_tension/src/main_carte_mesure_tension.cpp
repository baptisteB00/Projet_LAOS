/**
 * @file main_carte_mesure_tension.cpp
 * @brief Carte Mesure Tension String – lecture des tensions/courant via multiplexeur.
 *
 * Mesure les tensions des 5 branches (strings) et le courant du champ solaire
 * via un multiplexeur 8:1 connecté à 4 entrées ADC de l'ESP32.
 * Numéro de carte : 13 (fixe dans le firmware).
 */

#include <Arduino.h>
#include <CAN.h>
#include "can_id.h"

#define MOYENNAGE       100
#define FACTEUR_TENSION 0.081f
#define FACTEUR_COURANT 0.003462f
#define NUM_CARTE       13

#define PIN_SELECTION_MULTIPLEXEUR_A 25
#define PIN_SELECTION_MULTIPLEXEUR_B 33
#define PIN_SELECTION_MULTIPLEXEUR_C 32

#define PIN_SORTIE_MULTIPLEXEUR_1 36
#define PIN_SORTIE_MULTIPLEXEUR_2 39
#define PIN_SORTIE_MULTIPLEXEUR_3 34
#define PIN_SORTIE_MULTIPLEXEUR_4 35

typedef struct CANMessage_t
{
	unsigned int  id      = 0;
	char          len     = 0;
	unsigned char data[8] = {0};
} CANMessage_t;

CANMessage_t  rxMsg;
volatile bool canAvailable = false;

void  onReceiveCan(int packetSize);
void  envoie_message_CAN(CANMessage_t TxMsg);
void  set_multiplexeur(int chanel);
float lecture_tension_multiplexeur(char numero);
void  mesure_tension_string(char num_string);
void  mesure_courant_string(void);
void  reception(char ch);
void  envoi_num_carte(void);

/* ==================================================================== */
void setup()
{
	Serial.begin(115200);
	Serial.println("Carte mesure de tension");

	if (!CAN.begin(10E3))
	{
		Serial.println("Starting CAN failed!");
		while (1);
	}

	CAN.onReceive(onReceiveCan);

	pinMode(PIN_SELECTION_MULTIPLEXEUR_A, OUTPUT);
	pinMode(PIN_SELECTION_MULTIPLEXEUR_B, OUTPUT);
	pinMode(PIN_SELECTION_MULTIPLEXEUR_C, OUTPUT);

	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_A, LOW);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_B, LOW);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_C, LOW);
}

/* ==================================================================== */
void loop()
{
	CANMessage_t rxMsgLocal;
	if (canAvailable == true)
	{
		rxMsgLocal = rxMsg;
		if (rxMsgLocal.id == CAN_ID_DEMANDE_TENSION_STRING)
		{
			mesure_tension_string(rxMsgLocal.data[0]);
		}
		else if (rxMsgLocal.id == CAN_ID_DEMANDE_COURANT_STRING)
		{
			mesure_courant_string();
		}
		else if (rxMsgLocal.id == CAN_ID_DEMANDE_NUM_CARTE)
		{
			envoi_num_carte();
		}
		canAvailable = false;
	}
}

/* ==================================================================== */
void onReceiveCan(int packetSize)
{
	rxMsg.id  = CAN.packetId();
	rxMsg.len = CAN.packetDlc();
	int i = 0;
	while (CAN.available())
	{
		rxMsg.data[i] = CAN.read();
		i++;
	}
	canAvailable = true;
}

/* ==================================================================== */
void envoie_message_CAN(CANMessage_t TxMsg)
{
	CAN.beginPacket(TxMsg.id);
	CAN.write(TxMsg.data, TxMsg.len);
	CAN.endPacket();
}

/* ==================================================================== */
void serialEvent()
{
	while (Serial.available() > 0)
	{
		reception(Serial.read());
	}
}

/* ==================================================================== */
void reception(char ch)
{
	static String chaine = "";
	String commande;
	String valeur;
	int index, length;

	if ((ch == 13) or (ch == 10))
	{
		index  = chaine.indexOf(' ');
		length = chaine.length();

		if (index == -1)
		{
			commande = chaine;
			valeur   = "";
		}
		else
		{
			commande = chaine.substring(0, index);
			valeur   = chaine.substring(index + 1, length);
		}

		if (commande == "R")
		{
			mesure_tension_string(valeur.toInt());
		}
		else if (commande == "T")
		{
			mesure_courant_string();
		}
		else if (commande == "N")
		{
			envoi_num_carte();
		}
		chaine = "";
	}
	else
	{
		chaine += ch;
	}
}

/**
 * @brief Mesure les 5 tensions d'un string.
 * @param num_string Numéro du string (1 à 4).
 */
void mesure_tension_string(char num_string)
{
	CANMessage_t txMsg;
	CANMessage_t marqueurMsg;
	marqueurMsg.len = 0;
	float mesure[5];

	if (num_string < 1 || num_string > 4) return;

	for (int i = 0; i < 5; i++)
	{
		set_multiplexeur(i + 1);
		mesure[i] = lecture_tension_multiplexeur(num_string) * FACTEUR_TENSION;
	}

	marqueurMsg.id = CAN_ID_DEBUT_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);

	txMsg.len = 4;
	txMsg.id  = CAN_ID_RENVOI_TENSION_STRING;
	for (int i = 0; i < 5; i++)
	{
		int valeur_int = (int)(mesure[i] * 100);
		txMsg.data[0]  = num_string;
		txMsg.data[1]  = i;
		txMsg.data[2]  = valeur_int / 256;
		txMsg.data[3]  = valeur_int % 256;
		Serial.printf("string %d, panneau %d, tension = %f\n\r", num_string, i + 1, mesure[i]);
		envoie_message_CAN(txMsg);
	}

	marqueurMsg.id = CAN_ID_FIN_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);
}

/**
 * @brief Mesure le courant des 4 strings et envoie une trame CAN de 8 octets.
 */
void mesure_courant_string(void)
{
	CANMessage_t txMsg;
	CANMessage_t marqueurMsg;
	marqueurMsg.len = 0;
	float mesure[4];

	for (int i = 0; i < 4; i++)
	{
		set_multiplexeur(6);
		mesure[i] = lecture_tension_multiplexeur(i + 1) * FACTEUR_COURANT;
	}

	marqueurMsg.id = CAN_ID_DEBUT_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);

	txMsg.len = 8;
	txMsg.id  = CAN_ID_RENVOI_COURANT_STRING;
	for (int i = 0; i < 4; i++)
	{
		int valeur_int        = (int)(mesure[i] * 100);
		txMsg.data[2 * i]     = valeur_int / 256;
		txMsg.data[2 * i + 1] = valeur_int % 256;
		Serial.printf("courant %d = %f\n\r", i + 1, mesure[i]);
	}
	envoie_message_CAN(txMsg);

	marqueurMsg.id = CAN_ID_FIN_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);
}

/**
 * @brief Configure le multiplexeur pour sélectionner un canal.
 * @param chanel Canal à sélectionner : 1=V1, 2=V2, 3=V3, 4=V4, 5=V5, 6=courant.
 */
void set_multiplexeur(int chanel)
{
	bool inA, inB, inC;

	switch (chanel)
	{
		case 1:  inA = HIGH; inB = HIGH; inC = LOW;  break;
		case 2:  inA = LOW;  inB = LOW;  inC = LOW;  break;
		case 3:  inA = HIGH; inB = LOW;  inC = LOW;  break;
		case 4:  inA = LOW;  inB = HIGH; inC = LOW;  break;
		case 5:  inA = LOW;  inB = LOW;  inC = HIGH; break;
		case 6:  inA = HIGH; inB = LOW;  inC = HIGH; break;
		default: inA = HIGH; inB = HIGH; inC = LOW;  break;
	}

	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_A, inA);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_B, inB);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_C, inC);
}

/**
 * @brief Lit la tension à la sortie du multiplexeur (moyenne de MOYENNAGE lectures).
 * @param numero Numéro du string (1 à 4) → sélectionne le pin ADC correspondant.
 * @return Tension en volts moyennée.
 */
float lecture_tension_multiplexeur(char numero)
{
	float lecture_tension = 0;
	char  pin;

	switch (numero)
	{
		case 1:  pin = PIN_SORTIE_MULTIPLEXEUR_1; break;
		case 2:  pin = PIN_SORTIE_MULTIPLEXEUR_2; break;
		case 3:  pin = PIN_SORTIE_MULTIPLEXEUR_3; break;
		case 4:  pin = PIN_SORTIE_MULTIPLEXEUR_4; break;
		default: pin = PIN_SORTIE_MULTIPLEXEUR_1; break;
	}

	for (int i = 0; i < MOYENNAGE; i++)
	{
		lecture_tension += analogReadMilliVolts(pin) * 48.52f / 1000.0f;
	}
	lecture_tension /= MOYENNAGE;

	return lecture_tension;
}

/**
 * @brief Envoie le numéro de cette carte (13) sur le bus CAN.
 */
void envoi_num_carte(void)
{
	CANMessage_t txMsg;
	txMsg.id      = CAN_ID_RENVOI_NUM_CARTE;
	txMsg.data[0] = NUM_CARTE;
	txMsg.len     = 1;
	envoie_message_CAN(txMsg);
	Serial.println("numero de carte envoye");
}
