/**
 * @file can_id.h
 * @brief Identifiants des messages CAN du bus LAOS (10 kbps, trames standard 11 bits).
 *
 * Ce fichier est partagé entre toutes les cartes. Chaque constante correspond
 * à un identifiant CAN utilisé pour la communication inter-cartes.
 *
 * Les IDs sont groupés en zones selon les 4 bits de poids fort :
 *  - 0x0xx : zone broadcast       (toutes les cartes écoutent)
 *  - 0x1xx : zone carte Alimentation
 *  - 0x2xx : zone carte Météo
 *  - 0x3xx : zone carte Mesure I-V
 *  - 0x4xx : zone carte Mesure Tension
 *
 * Plus l'ID est petit, plus la priorité d'arbitrage CAN est haute.
 *
 * Au sein d'une zone de carte, les **demandes** Supervision → Carte
 * sont en 0xZ0x..0xZ7x et les **réponses** Carte → Supervision
 * sont en 0xZ8x..0xZFx.
 *
 * Cette structure permet à chaque carte d'utiliser un filtre matériel
 * unique pour ne recevoir que les messages la concernant (voir doc CAN).
 */
#ifndef CAN_ID_H
#define CAN_ID_H

/** @brief Identifiants CAN de tous les messages du système. */
typedef enum CanMessageId_t
{
	/* ---- Zone broadcast (0x000 - 0x0FF) ---------------------------- */
	CAN_ID_DEBUT_TRANSMISSION       = 0x010, ///< Carte→Sup : début de rafale multi-trames
	CAN_ID_FIN_TRANSMISSION         = 0x011, ///< Carte→Sup : fin de rafale multi-trames
	CAN_ID_DEMANDE_NUM_CARTE        = 0x020, ///< Sup→Toutes : demande d'identification
	CAN_ID_RENVOI_NUM_CARTE         = 0x021, ///< Toutes→Sup : data[0]=numéro de carte

	/* ---- Zone carte Alimentation (0x100 - 0x1FF) ------------------- */
	CAN_ID_DEMANDE_ALIMENTATION     = 0x100, ///< Sup→Alim : data[0]=1 allume, 0 éteint

	/* ---- Zone carte Météo (0x200 - 0x2FF) -------------------------- */
	CAN_ID_DEMANDE_HUM_IRR_TEMP_EXT = 0x200, ///< Sup→Météo : demande groupée (3 mesures)
	CAN_ID_DEMANDE_HUMIDITE         = 0x201, ///< Sup→Météo : demande humidité seule
	CAN_ID_DEMANDE_TEMP_EXTERIEUR   = 0x202, ///< Sup→Météo : demande température extérieure
	CAN_ID_DEMANDE_IRRADIANCE       = 0x203, ///< Sup→Météo : demande irradiance seule
	CAN_ID_RENVOI_HUM_IRR_TEMP_EXT  = 0x280, ///< Météo→Sup : 6 octets (hum, temp, irr ×100)
	CAN_ID_RENVOI_HUMIDITE          = 0x281, ///< Météo→Sup : data[0-1]=humidité×100
	CAN_ID_RENVOI_TEMPERATURE       = 0x282, ///< Météo→Sup : data[0-1]=température×100
	CAN_ID_RENVOI_IRRADIANCE        = 0x283, ///< Météo→Sup : data[0-1]=irradiance×100

	/* ---- Zone carte Mesure I-V (0x300 - 0x3FF) --------------------- */
	CAN_ID_DEMANDE_MESURE_VI        = 0x300, ///< Sup→VI : data[0]=numéro carte cible
	CAN_ID_DEMANDE_TEMP_PANNEAU     = 0x301, ///< Sup→VI : data[0]=numéro carte cible
	CAN_ID_RENVOI_MESURE_VI         = 0x380, ///< VI→Sup : data[0-1]=V×100, [2-3]=I×100, [4]=n°carte
	CAN_ID_RENVOI_TEMP_PANNEAU      = 0x381, ///< VI→Sup : data[0]=signe, [1]=°C, [2]=n°carte

	/* ---- Zone carte Mesure Tension (0x400 - 0x4FF) ----------------- */
	CAN_ID_DEMANDE_TENSION_STRING   = 0x400, ///< Sup→Tension : data[0]=numéro string
	CAN_ID_DEMANDE_COURANT_STRING   = 0x401, ///< Sup→Tension : data[0]=numéro string
	CAN_ID_RENVOI_TENSION_STRING    = 0x480, ///< Tension→Sup : data[0]=indice, [1-2]=valeur
	CAN_ID_RENVOI_COURANT_STRING    = 0x481  ///< Tension→Sup : data[0]=indice, [1-2]=valeur
} CanMessageId_t;

#endif
