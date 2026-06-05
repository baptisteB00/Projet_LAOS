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

#define MOYENNAGE 100

#define PIN_SELECTION_MULTIPLEXEUR_A 25
#define PIN_SELECTION_MULTIPLEXEUR_B 33
#define PIN_SELECTION_MULTIPLEXEUR_C 32

#define PIN_SORTIE_MULTIPLEXEUR_1 36
#define PIN_SORTIE_MULTIPLEXEUR_2 39
#define PIN_SORTIE_MULTIPLEXEUR_3 34
#define PIN_SORTIE_MULTIPLEXEUR_4 35

#define FACTEUR_TENSION 0.081f
#define FACTEUR_COURANT 0.003462f
#define NUM_CARTE 13

typedef struct CANMessage_t
{
  unsigned int id = 0;
  char len = 0;
  unsigned char data[8] = {0};
} CANMessage_t;

CANMessage_t rxMsg;
volatile bool canAvailable = false;

void onReceiveCan(int packetSize);
void set_multiplexeur(int chanel);
float lecture_tension_multiplexeur(char numero);
void mesure_tension_string(char num_string);
void mesure_courant_string(void);
void reception(char ch);
void envoi_num_carte();

void setup(){


	Serial.begin(115200);

	Serial.println("Carte mesure de tension");

	  // start the CAN bus at 10 kbps
	if (!CAN.begin(10E3)) {
	    Serial.println("Starting CAN failed!");
	    while(1);
	}

	CAN.onReceive(onReceiveCan);
	pinMode(PIN_SELECTION_MULTIPLEXEUR_A,OUTPUT);
	pinMode(PIN_SELECTION_MULTIPLEXEUR_B,OUTPUT);
	pinMode(PIN_SELECTION_MULTIPLEXEUR_C,OUTPUT);

	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_A,LOW);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_B,LOW);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_C,LOW);

}


void loop(){
	CANMessage_t rxMsgLocal;
	if (canAvailable == true){
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

void onReceiveCan(int packetSize)
{
    rxMsg.id = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    int i = 0;
    while (CAN.available())
    {
        rxMsg.data[i] = CAN.read();
        i++;
    }
	canAvailable = true;
}

void envoie_message_CAN(CANMessage_t TxMsg)
{
	CAN.beginPacket(TxMsg.id);
	CAN.write(TxMsg.data, TxMsg.len);
	CAN.endPacket();
}

void serialEvent()
{
  while (Serial.available() > 0) // tant qu'il y a des caractères à lire
  {
    reception(Serial.read());
  }
}

void reception(char ch)
{
  static String chaine = "";
  String commande;
  String valeur;
  int index, length;

  if ((ch == 13) or (ch == 10))
  {
    index = chaine.indexOf(' ');
    length = chaine.length();

    if (index == -1)
    {
      commande = chaine;
      valeur = "";
    }
    else
    {
      commande = chaine.substring(0, index);
      valeur = chaine.substring(index + 1, length);
    }

    if (commande == "R")
    {
	mesure_tension_string(valeur.toInt());
    }else if(commande == "T"){
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
 * @brief Mesure les 5 tensions et le courant d'un string.
 *
 * Sélectionne chaque canal du multiplexeur (1 à 6), lit la tension via ADC
 * (moyenne de `MOYENNAGE` lectures), applique le facteur d'échelle, puis
 * envoie 6 trames CAN (ID=51) avec l'indice et la valeur encodée.
 * @param num_string Numéro du string (1 à 4, correspond à la sortie multiplexeur).
 */
void mesure_tension_string(char num_string){
	CANMessage_t txMsg;
	CANMessage_t marqueurMsg;
	marqueurMsg.len = 0;
	float mesure[5];
	float facteur;
	if(num_string < 1 || num_string > 4) return;
	for (int i =0 ; i<5;i++){
		set_multiplexeur(i+1);
		mesure[i] = lecture_tension_multiplexeur(num_string) * FACTEUR_TENSION;
	}

	/* Marqueur de début de rafale */
	marqueurMsg.id = CAN_ID_DEBUT_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);
	txMsg.len = 4;
	txMsg.id = CAN_ID_RENVOI_TENSION_STRING;
	for (int i =0 ; i<5 ; i++){
		int valeur_int = (int)(mesure[i] * 100); // encodage x100
		txMsg.data[0] = num_string;
		txMsg.data[1] = i;                    // indice : 0-4 = tensions, 5 = courant
		txMsg.data[2] = valeur_int / 256;         // octet fort
		txMsg.data[3] = valeur_int % 256;         // octet faible
		Serial.printf("numero de string : %d , numero de panneau : %d , tension = %f\n\r",num_string,i+1,mesure[i]);
		envoie_message_CAN(txMsg);
	}


	/* Marqueur de fin de rafale */
	marqueurMsg.id = CAN_ID_FIN_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);
}
void mesure_courant_string(void){
	CANMessage_t txMsg;
	CANMessage_t marqueurMsg;
	marqueurMsg.len = 0;
	float mesure[4];
	int valeur_int;

	for (int i =0 ; i<4;i++){
		set_multiplexeur(6);
		mesure[i] = lecture_tension_multiplexeur(i) * FACTEUR_COURANT;
	}

	/* Marqueur de début de rafale */
	marqueurMsg.id = CAN_ID_DEBUT_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);
	txMsg.len = 8;
	txMsg.id = CAN_ID_RENVOI_COURANT_STRING;
	for (int i = 0 ; i< 4 ; i++){
		valeur_int = (int)(mesure[i] * 100); // encodage x100
		txMsg.data[2*i]   = valeur_int / 256;         // octet fort
		txMsg.data[2*i+1] = valeur_int % 256;         // octet faible
		Serial.printf("courant %d = %f\n\r",i,mesure[i]);
	 }
	envoie_message_CAN(txMsg);


	/* Marqueur de fin de rafale */
	marqueurMsg.id = CAN_ID_FIN_TRANSMISSION;
	envoie_message_CAN(marqueurMsg);
}
/**
 * @brief Configure le multiplexeur pour sélectionner un canal.
 * @param chanel Canal à sélectionner : 1=V1, 2=V2, 3=V3, 4=V4, 5=V5, 6=courant.
 */
void set_multiplexeur(int chanel){
	bool inA,inB,inC; //variable pour stocker l'etat souhaiter des pin de configuration du multiplexeur

	switch(chanel){
		case 1 ://case pour selectionner la tension 1 
			inA = HIGH;
			inB = HIGH;
			inC = LOW;
		break;
		case 2://case pour selectionner la tension 2 
			inA = LOW;
			inB = LOW;
			inC = LOW;
		break;
		case 3://case pour selectionner la tension 3 
			inA = HIGH;
			inB = LOW;
			inC = LOW;
		break;
		case 4://case pour selectionner la tension 4 
			inA = LOW;
			inB = HIGH;
			inC = LOW;
		break;

		case 5://case pour selectionner la tension 5 
			inA = LOW;
			inB = LOW;
			inC = HIGH;
		break;
		case 6://case pour selectionner le courant
			inA = HIGH;
			inB = LOW;
			inC = HIGH;
		break;
		default:// par defaut le multiplexeur mesure la tension 1 si on entre un numero non valide
			inA = HIGH;
			inB = HIGH;
			inC = LOW;
		break;

	}
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_A,inA);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_B,inB);
	digitalWrite(PIN_SELECTION_MULTIPLEXEUR_C,inC);
}

/**
 * @brief Lit la valeur ADC à la sortie du multiplexeur (moyenne de `MOYENNAGE` lectures).
 * @param numero Numéro du string (1 à 4) → sélectionne le pin ADC correspondant.
 * @return Valeur ADC brute moyennée, ou -1.0f si le numéro est invalide.
 */
float lecture_tension_multiplexeur(char numero){
	float lecture_tension = 0;
	char pin;
	switch(numero){
		case 1:
			pin = PIN_SORTIE_MULTIPLEXEUR_1;
		break;
		case 2:
			pin = PIN_SORTIE_MULTIPLEXEUR_2;
		break;
		case 3:
			pin = PIN_SORTIE_MULTIPLEXEUR_3;
		break;
		case 4:
			pin = PIN_SORTIE_MULTIPLEXEUR_4;
		break;
		default:
			pin = PIN_SORTIE_MULTIPLEXEUR_1;
		break;
	}

	for (int i =0 ;i< MOYENNAGE;i++){
		lecture_tension += analogReadMilliVolts(pin)*48.52f/1000.0f;
	}

	lecture_tension/=MOYENNAGE;

	return lecture_tension;
}

/**
 * @brief Envoie le numéro de cette carte (13) sur le bus CAN (ID=10).
 */
void envoi_num_carte(){
	CANMessage_t txMsg;
	txMsg.id = CAN_ID_RENVOI_NUM_CARTE;
	txMsg.data[0] = NUM_CARTE;
	txMsg.len = 1;
	envoie_message_CAN(txMsg);
	Serial.println("numero de carte envoye");
}
