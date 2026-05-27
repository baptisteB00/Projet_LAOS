# Protocole CAN

## Paramètres physiques

| Paramètre | Valeur |
|-----------|--------|
| Vitesse | 10 kbps |
| Format | Standard (11 bits d'identifiant) |
| Résistances de terminaison | 120 Ohm aux deux extrémités |
| Longueur de données max | 8 octets |

---

## Tableau complet des identifiants

| ID | Nom symbolique | Direction | Longueur | Description |
|:--:|----------------|-----------|:--------:|-------------|
| 0 | `CAN_ID_DEMANDE_NUM_CARTE` | Supervision vers Toutes | 0 | Demande d'identification générale |
| 1 | `CAN_ID_DEMANDE_ALIMENTATION` | Supervision vers Alim | 1 | `data[0]` : 1=allumer, 0=éteindre |
| 2 | `CAN_ID_DEMANDE_HUMIDITE` | Supervision vers Météo | 0 | Demande humidité seule |
| 3 | `CAN_ID_DEMANDE_IRRADIANCE` | Supervision vers Météo | 0 | Demande irradiance seule |
| 4 | `CAN_ID_DEMANDE_TEMP_EXTERIEUR` | Supervision vers Météo | 0 | Demande température extérieure seule |
| 5 | `CAN_ID_DEMANDE_HUM_IRR_TEMP_EXT` | Supervision vers Météo | 0 | Demande groupée (3 mesures) |
| 7 | `CAN_ID_DEBUT_TRANSMISSION` | Carte vers Supervision | 0 | Marqueur début de séquence multi-trames |
| 8 | `CAN_ID_FIN_TRANSMISSION` | Carte vers Supervision | 0 | Marqueur fin de séquence multi-trames |
| 10 | `CAN_ID_RENVOI_NUM_CARTE` | Toutes vers Supervision | 1 | `data[0]` = numéro de la carte |
| 11 | `CAN_ID_DEMANDE_MESURE_VI` | Supervision vers VI | 1 | `data[0]` = numéro de carte cible |
| 12 | `CAN_ID_DEMANDE_TEMP_PANNEAU` | Supervision vers VI | 1 | `data[0]` = numéro de carte cible |
| 18 | `CAN_ID_RENVOI_TEMP_PANNEAU` | VI vers Supervision | 3 | `data[0]`=signe, `data[1]`=valeur °C, `data[2]`=n° carte |
| 19 | `CAN_ID_RENVOI_MESURE_VI` | VI vers Supervision | 5 | `data[0-1]`=tension×100, `data[2-3]`=courant×100, `data[4]`=n° carte |
| 42 | `CAN_ID_RENVOI_HUMIDITE` | Météo vers Supervision | 2 | `data[0-1]` = humidité × 100 |
| 43 | `CAN_ID_RENVOI_TEMPERATURE` | Météo vers Supervision | 2 | `data[0-1]` = température × 100 |
| 44 | `CAN_ID_RENVOI_IRRADIANCE` | Météo vers Supervision | 2 | `data[0-1]` = irradiance × 100 |
| 45 | `CAN_ID_RENVOI_HUM_IRR_TEMP_EXT` | Météo vers Supervision | 6 | `data[0-1]`=hum×100, `data[2-3]`=temp×100, `data[4-5]`=irr×100 |
| 50 | `CAN_ID_DEMANDE_TENSION_STRING` | Supervision vers Tension | 1 | `data[0]` = numéro de string |
| 51 | `CAN_ID_RENVOI_TENSION_STRING` | Tension vers Supervision | 3 | `data[0]`=indice, `data[1-2]`=valeur |
| 52 | `CAN_ID_DEMANDE_COURANT_STRING` | Supervision vers Tension | 1 | `data[0]` = numéro de string |
| 53 | `CAN_ID_RENVOI_COURANT_STRING` | Tension vers Supervision | 3 | `data[0]`=indice, `data[1-2]`=valeur |

---

## Encodage des flottants

Toutes les valeurs physiques sont transmises sous forme d'entiers 16 bits non signés, en **big-endian**.

```
Encodage :
  entier    = (int)(valeur_float x 100)
  data[n]   = entier >> 8         -- octet fort (MSB)
  data[n+1] = entier & 0xFF       -- octet faible (LSB)

Décodage :
  valeur_float = (data[n] * 256 + data[n+1]) / 100.0f
```

**Exemple** – Température de 23,45 °C :

| Étape | Calcul | Résultat |
|-------|--------|----------|
| Encodage | 23.45 × 100 | entier = 2345 |
| Octet fort | 2345 / 256 | data[0] = 9 |
| Octet faible | 2345 % 256 | data[1] = 41 |
| Décodage | (9 × 256 + 41) / 100.0 | 23.45 ✓ |

---

## Format détaillé des trames

### Réponse identification (ID=10)

| Octet | data[0] |
|-------|---------|
| Contenu | Numéro de carte |
| Longueur | 1 octet |

### Réponse mesure I-V (ID=19)

| Octet | data[0] | data[1] | data[2] | data[3] | data[4] |
|-------|---------|---------|---------|---------|---------|
| Contenu | Tension MSB | Tension LSB | Courant MSB | Courant LSB | N° carte |
| Valeur | Tension × 100 (16 bits BE) | | Courant × 100 (16 bits BE) | | |

### Réponse groupée météo (ID=45)

| Octet | data[0] | data[1] | data[2] | data[3] | data[4] | data[5] |
|-------|---------|---------|---------|---------|---------|---------|
| Contenu | Hum MSB | Hum LSB | Temp MSB | Temp LSB | Irr MSB | Irr LSB |
| Valeur | Humidité × 100 (16 bits BE) | | Température × 100 (16 bits BE) | | Irradiance × 100 (16 bits BE) | |

### Réponse température panneau (ID=18)

| Octet | data[0] | data[1] | data[2] |
|-------|---------|---------|---------|
| Contenu | Signe | Valeur absolue °C | N° carte |
| Valeur | 1 = positif, 0 = négatif | Entier | |

---

## Séquences de communication

### Identification de toutes les cartes

```mermaid
sequenceDiagram
    participant SUP as Supervision
    participant C10 as Météo n°10
    participant C12 as Alimentation n°12
    participant C13 as Tension n°13
    participant VI as VI n°X

    SUP->>C10: CAN ID=0
    SUP->>C12: CAN ID=0
    SUP->>C13: CAN ID=0
    SUP->>VI: CAN ID=0

    Note over C10: délai 100 ms
    Note over C12: délai 120 ms
    Note over C13: délai 130 ms

    C10-->>SUP: CAN ID=10 data=10
    C12-->>SUP: CAN ID=10 data=12
    C13-->>SUP: CAN ID=10 data=13
    VI-->>SUP: CAN ID=10 data=X
```

### Mesure météo individuelle (humidité)

```mermaid
sequenceDiagram
    participant SUP as Supervision
    participant MET as Carte Météo

    SUP->>MET: CAN ID=2 DEMANDE_HUMIDITE
    MET-->>SUP: CAN ID=7 DEBUT_TRANSMISSION
    MET-->>SUP: CAN ID=42 RENVOI_HUMIDITE data=hum x100
    MET-->>SUP: CAN ID=8 FIN_TRANSMISSION
```

### Mesure courbe I-V

```mermaid
sequenceDiagram
    participant SUP as Supervision
    participant VI as Carte VI n°X

    SUP->>VI: CAN ID=11 data=X
    Note over VI: Mesure Icc alpha=100%
    Note over VI: Mesure V0 alpha=0%
    Note over VI: Calcul 23 points log
    Note over VI: Mesure réelle 23 points
    VI-->>SUP: CAN ID=7 DEBUT
    loop 23 fois
        VI-->>SUP: CAN ID=19 tension+courant+n°carte
    end
    VI-->>SUP: CAN ID=8 FIN
```

---

## Numéros de cartes

| Numéro | Carte |
|:------:|-------|
| 1 à 5 | Cartes Mesure VI (lu sur DIP switch) |
| 10 | Carte Météo (fixe dans le firmware) |
| 12 | Carte Alimentation (fixe dans le firmware) |
| 13 | Carte Mesure Tension String (fixe dans le firmware) |
