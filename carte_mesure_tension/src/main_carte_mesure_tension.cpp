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

#define MOYENNAGE 100

#define PIN_SELECTION_MULTIPLEXEUR_A 25
#define PIN_SELECTION_MULTIPLEXEUR_B 33
#define PIN_SELECTION_MULTIPLEXEUR_C 32

#define PIN_SORTIE_MULTIPLEXEUR_1 36
#define PIN_SORTIE_MULTIPLEXEUR_2 39
#define PIN_SORTIE_MULTIPLEXEUR_3 34
#define PIN_SORTIE_MULTIPLEXEUR_4 35

typedef struct CANMessage
{
  unsigned int id = 0;
  char len = 0;
  unsigned char data[8] = {0};
} CANMessage;

QueueHandle_t xBalTxCanMsg;

void onReceiveCan(int packetSize);
void onReceive(int packetSize);
void set_multiplexeur(int chanel);
float lecture_tension_multiplexeur(char numero);
void mesure_string(char num_string);
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

	xBalTxCanMsg = xQueueCreate(12, sizeof(CANMessage));
}


void loop(){


}

void onReceiveCan(int packetSize)
{
    CANMessage rxMsg;
    rxMsg.id = CAN.packetId();
    rxMsg.len = CAN.packetDlc();
    int i = 0;
    while (CAN.available())
    {
        rxMsg.data[i] = CAN.read();
        i++;
    }

    if (rxMsg.id == 50 )
    {
	mesure_string(rxMsg.data[0]);
    }
    else if (rxMsg.id == (0))
    {
	envoi_num_carte();
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

void serialEvent()
{
  while (Serial.available() > 0) // tant qu'il y a des caractères à lire
  {
    reception(Serial.read());
  }
}

void reception(char ch)
{
  static int i = 0;
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
	mesure_string(valeur.toInt());
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
void mesure_string(char num_string){
	CANMessage txMsg;
	float mesure[6];
	float facteur;
	for (int i =0 ; i<6;i++){
		if(i<5){
			facteur = 0.081f;
		}else{
			facteur = 0.003462f;
		}
		set_multiplexeur(i+1);
		mesure[i] = lecture_tension_multiplexeur(num_string) * facteur;
	}
	Serial.printf("STRING : %d \n\r tension 1 : %2.2f \n\r tension 2 : %2.2f\n\r tension 3 : %2.2f\n\r tension 4 : %2.2f\n\r tension 5 : %2.2f\n\r courant : %2.2f\n\r ",num_string,mesure[0],mesure[1],mesure[2],mesure[3],mesure[4],mesure[5]);
	txMsg.len = 3;
	txMsg.id = 51;
	for (int i =0 ; i<6 ; i++){
		int valeur = (int)(mesure[i] * 100); // encodage x100, comme le reste du projet
		txMsg.data[0] = i;                    // indice : 0-4 = tensions, 5 = courant
		txMsg.data[1] = valeur / 256;         // octet fort
		txMsg.data[2] = valeur % 256;         // octet faible
		xQueueSend(xBalTxCanMsg,&txMsg,portMAX_DELAY);
	}
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
			inA = LOW;
			inB = LOW;
			inC = HIGH;
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
	int lecture_tension = 0;
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
			return -1.0f;
		break;
	}

	for (int i =0 ;i< MOYENNAGE;i++){
		lecture_tension += analogRead(pin);
	}

	lecture_tension/=MOYENNAGE;

	return lecture_tension;
}

/**
 * @brief Envoie le numéro de cette carte (13) sur le bus CAN (ID=10).
 */
void envoi_num_carte(){
	CANMessage txMsg;
	txMsg.id = 10;
	txMsg.data[0] = 13;
	txMsg.len = 1;
	xQueueSend(xBalTxCanMsg,&txMsg,portMAX_DELAY);
	Serial.println("numero de carte envoye");
}
