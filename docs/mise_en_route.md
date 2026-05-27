# Mise en route

## Prérequis logiciels

| Outil | Version minimale | Lien |
|-------|-----------------|------|
| PlatformIO Core | 6.x | https://platformio.org/install/cli |
| Python | 3.8+ | Requis par PlatformIO |
| VSCode (optionnel) | – | + extension PlatformIO IDE |
| Doxygen (optionnel) | 1.9+ | Pour générer la doc HTML |

---

## Prérequis matériels

| Composant | Quantité | Notes |
|-----------|----------|-------|
| ESP32 DevKit v1 | 1 par carte | ou tout module ESP32 compatible |
| Transceiver CAN | 1 par carte | SN65HVD230 (3.3V) recommandé |
| Résistance 120 Ω | 2 | Terminaisons aux extrémités du bus |
| Capteur AM2315 | 1 | Carte Météo uniquement |
| Capteur TC74 (0x48) | 1 par carte VI | Température panneau |
| Relais 5V | 1 par carte VI + 1 alim | Connexion panneau / charge |
| DIP switch 4 bits | 1 par carte VI | Numérotation cartes 1 à 5 |
| Multiplexeur 8:1 | 1 | Carte Mesure Tension |

---

## Câblage du bus CAN

```
ESP32 (GPIO 4/5 par défaut)           ESP32
  TX ──────────────────────── TX
  RX ──────────────────────── RX
         │                         │
    [SN65HVD230]             [SN65HVD230]
         │                         │
  120Ω ──┤── CANH ──────── CANH ──┤── 120Ω
         │── CANL ──────── CANL ──┤
```

> **Attention :** Toutes les cartes partagent le même câble bus CAN. Les 120 Ω ne sont placés qu'aux deux extrémités physiques du câble.

---

## Compilation et flash – pas à pas

### 1. Cloner le dépôt

```bash
git clone https://github.com/<votre-compte>/Projet_LAOS.git
cd Projet_LAOS
```

### 2. Compiler et flasher une carte

Chaque sous-dossier est un projet PlatformIO **indépendant**.

```bash
# Exemple : carte météo
cd carte_meteo

# Compiler seulement
pio run

# Compiler et flasher
pio run --target upload

# Ouvrir le moniteur série (Ctrl+C pour quitter)
pio device monitor --baud 115200
```

### 3. Flasher toutes les cartes

```bash
# Depuis la racine du projet
for dir in carte_alimentation carte_meteo carte_vi \
           carte_mesure_tension carte_supervision; do
    echo "=== Flash $dir ==="
    cd $dir && pio run --target upload && cd ..
done
```

---

## Configuration des cartes VI (DIP switch)

Chaque carte VI a un DIP switch 4 bits qui définit son numéro (1 à 5) :

| Numéro carte | BP1 (GPIO 25) | BP2 (GPIO 26) | BP3 (GPIO 27) | BP4 (GPIO 14) |
|:---:|:---:|:---:|:---:|:---:|
| 1 | ON | OFF | OFF | OFF |
| 2 | OFF | ON | OFF | OFF |
| 3 | ON | ON | OFF | OFF |
| 4 | OFF | OFF | ON | OFF |
| 5 | ON | OFF | ON | OFF |

> **Note :** BP1 = bit 0 (LSB). Le numéro est la valeur binaire des 4 bits. Tout numéro hors de 1–5 bloque le démarrage.

---

## Vérification du démarrage

À l'ouverture du moniteur série, chaque carte affiche un message :

| Carte | Message attendu |
|-------|----------------|
| Alimentation | `Carte Alimentation` |
| Météo | `AM2315 pret.` |
| VI (n°X) | `Numero de carte : X` |
| Mesure Tension | `Carte mesure de tension` |
| Supervision | `CAN Receiver` |

---

## Test rapide du bus CAN

Depuis le moniteur série de la **carte supervision** :

```
N           → Identification de toutes les cartes
M           → Mesure météo groupée (hum + temp + irr)
VI 1        → Courbe I-V de la carte n°1
T 1         → Température panneau de la carte n°1
R 1         → Allume le relais alimentation
R 0         → Éteint le relais alimentation
```

---

## Génération de la documentation Doxygen

```bash
# Depuis la racine du projet
doxygen Doxyfile

# Ouvrir la doc générée
xdg-open docs/doxygen/html/index.html   # Linux
open docs/doxygen/html/index.html       # macOS
```
