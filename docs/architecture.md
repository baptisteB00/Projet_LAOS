# Architecture du système

## Vue d'ensemble

Le projet LAOS est composé de **5 types de cartes ESP32** reliées par un bus CAN à 10 kbps. La carte supervision joue le rôle de maître : elle émet les demandes de mesure et reçoit les réponses de toutes les autres cartes.

```mermaid
graph TD
    PC["💻 Ordinateur\n(interface série USB)"]

    subgraph SUPERVISION["Carte Supervision"]
        SUP["main_supervision.cpp\nRelais série ↔ CAN"]
    end

    PC <-->|"UART 115200 baud"| SUP

    subgraph BUS["Bus CAN – 10 kbps"]
        SUP <-->|"ID 0–53"| ALIM
        SUP <-->|"ID 0–53"| METEO
        SUP <-->|"ID 0–53"| VI1
        SUP <-->|"ID 0–53"| VI2
        SUP <-->|"ID 0–53"| TEN
    end

    subgraph ALIM["Carte Alimentation (n°12)"]
        REL["Relais\nGPIO 25"]
        LEDV["LED verte\nGPIO 18"]
        LEDR["LED rouge\nGPIO 19"]
    end

    subgraph METEO["Carte Météo (n°10)"]
        AM["AM2315\n(I2C)\nTemp + Humidité"]
        PH["Cellule photoélec.\nGPIO 34\nIrradiance"]
    end

    subgraph VI1["Carte VI n°1–5"]
        TC["TC74 (I2C)\nTempérature panneau"]
        RELVI["Relais charge\nGPIO 18"]
        PWM["PWM 50 kHz\nGPIO 19"]
        ADC["ADC Tension\nGPIO 32\nADC Courant\nGPIO 33"]
    end

    subgraph VI2["..."]
    end

    subgraph TEN["Carte Mesure Tension (n°13)"]
        MUX["Multiplexeur 8:1\nGPIO 25/33/32\n4 sorties ADC"]
    end

    RELVI --> PAN["☀️ Panneau Solaire"]
    REL --> PAN
    AM --> PAN
    PH --> PAN
    TC --> PAN
    ADC --> PAN
    MUX --> PAN
```

---

## Flux de données

```mermaid
sequenceDiagram
    participant PC as Ordinateur (PC)
    participant SUP as Carte Supervision
    participant ALI as Carte Alimentation
    participant MET as Carte Météo
    participant VI as Carte VI (n°X)
    participant TEN as Carte Tension

    Note over PC,TEN: Initialisation – identification des cartes
    PC->>SUP: "N\r"
    SUP->>ALI: CAN ID=0
    SUP->>MET: CAN ID=0
    SUP->>VI: CAN ID=0
    SUP->>TEN: CAN ID=0
    ALI-->>SUP: CAN ID=10, data[0]=12
    MET-->>SUP: CAN ID=10, data[0]=10
    VI-->>SUP: CAN ID=10, data[0]=X
    TEN-->>SUP: CAN ID=10, data[0]=13
    SUP-->>PC: "0;12\r\n", "0;10\r\n" ...

    Note over PC,TEN: Mesure météo groupée
    PC->>SUP: "M\r"
    SUP->>MET: CAN ID=5
    MET-->>SUP: CAN ID=7 (DEBUT)
    MET-->>SUP: CAN ID=45 (hum+temp+irr)
    MET-->>SUP: CAN ID=8 (FIN)
    SUP-->>PC: "10;hum;temp;irr\r\n"

    Note over PC,TEN: Mesure courbe I-V carte n°3
    PC->>SUP: "VI 3\r"
    SUP->>VI: CAN ID=11, data[0]=3
    VI-->>SUP: CAN ID=7 (DEBUT)
    loop NB_POINT fois
        VI-->>SUP: CAN ID=19 (tension, courant, n°carte)
    end
    VI-->>SUP: CAN ID=8 (FIN)
    SUP-->>PC: "1;3;V;I\r\n" × NB_POINT
```

---

## Anti-collision CAN pour l'identification

Lors d'une demande `CAN_ID_DEMANDE_NUM_CARTE`, toutes les cartes répondent. Pour éviter les collisions, chaque carte attend avant de répondre :

```
délai = numCarte × 10 ms
```

La carte n°1 répond après 10 ms, la n°12 après 120 ms, etc.

---

## Technologies par couche

| Couche | Technologie | Rôle |
|--------|-------------|------|
| Matériel | ESP32 (Xtensa LX6 dual-core 240 MHz) | MCU de chaque carte |
| Réseau | CAN 2.0A, 10 kbps, trames standard 11 bits | Communication inter-cartes |
| OS temps réel | FreeRTOS (intégré Arduino ESP32) | Multitâche sur cartes Météo et VI |
| Build system | PlatformIO | Compilation, dépendances, flash |
| Capteurs | AM2315 (I2C), TC74 (I2C), ADC interne ESP32 | Acquisition physique |
| Actionneurs | Relais GPIO, PWM 50 kHz 9 bits | Contrôle charge et alimentation |
