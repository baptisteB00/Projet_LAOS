/**
 * @file can_id.h
 * @brief Identifiants des messages CAN du bus LAOS (10 kbps).
 *
 * Ce fichier est partagé entre toutes les cartes. Chaque constante correspond
 * à un identifiant CAN 11 bits utilisé pour la communication inter-cartes.
 */
#ifndef CAN_ID_H
#define CAN_ID_H

/** @brief Identifiants CAN de tous les messages du système. */
typedef enum CanMessageId_t
{
	CAN_ID_DEMANDE_NUM_CARTE        = 0,  ///< Supervision→Toutes : demande d'identification
	CAN_ID_DEMANDE_ALIMENTATION     = 1,  ///< Supervision→Alim : data[0]=1 allume, 0 éteint
	CAN_ID_DEMANDE_HUMIDITE         = 2,  ///< Supervision→Météo : demande humidité
	CAN_ID_DEMANDE_IRRADIANCE       = 3,  ///< Supervision→Météo : demande irradiance
	CAN_ID_DEMANDE_TEMP_EXTERIEUR   = 4,  ///< Supervision→Météo : demande température ext.
	CAN_ID_DEMANDE_HUM_IRR_TEMP_EXT = 5,  ///< Supervision→Météo : demande groupée (3 mesures)
	CAN_ID_DEBUT_TRANSMISSION       = 7,  ///< Carte→Supervision : début de séquence multi-trames
	CAN_ID_FIN_TRANSMISSION         = 8,  ///< Carte→Supervision : fin de séquence multi-trames
	CAN_ID_RENVOI_NUM_CARTE         = 10, ///< Toutes→Supervision : data[0]=numéro de carte
	CAN_ID_DEMANDE_MESURE_VI        = 11, ///< Supervision→VI : data[0]=numéro carte cible
	CAN_ID_DEMANDE_TEMP_PANNEAU     = 12, ///< Supervision→VI : data[0]=numéro carte cible
	CAN_ID_RENVOI_TEMP_PANNEAU      = 18, ///< VI→Supervision : data[0]=signe, [1]=°C, [2]=n°carte
	CAN_ID_RENVOI_MESURE_VI         = 19, ///< VI→Supervision : data[0-1]=V×100, [2-3]=I×100, [4]=n°carte
	CAN_ID_RENVOI_HUMIDITE          = 42, ///< Météo→Supervision : data[0-1]=humidité×100
	CAN_ID_RENVOI_TEMPERATURE       = 43, ///< Météo→Supervision : data[0-1]=température×100
	CAN_ID_RENVOI_IRRADIANCE        = 44, ///< Météo→Supervision : data[0-1]=irradiance×100
	CAN_ID_RENVOI_HUM_IRR_TEMP_EXT  = 45, ///< Météo→Supervision : 6 octets (hum, temp, irr ×100)
	CAN_ID_DEMANDE_TENSION_STRING   = 50, ///< Supervision→Tension : data[0]=numéro string
	CAN_ID_RENVOI_TENSION_STRING    = 51, ///< Tension→Supervision : data[0]=indice, [1-2]=valeur
	CAN_ID_DEMANDE_COURANT_STRING   = 52, ///< Supervision→Tension : data[0]=numéro string
	CAN_ID_RENVOI_COURANT_STRING    = 53  ///< Tension→Supervision : data[0]=indice, [1-2]=valeur
} CanMessageId_t;

#endif
