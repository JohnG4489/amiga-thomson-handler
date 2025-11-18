# 🇫🇷 Amiga Thomson Handler – Filesystem Thomson pour AmigaOS

## 🎯 Objectif

Ce projet fournit un **handler AmigaOS** permettant de lire le **système de fichiers Thomson** (TO8, TO9, TO9+) directement depuis AmigaOS 2.0 et supérieur.

Le handler utilise le **device AmigaOS `todisk.device`** pour accéder aux secteurs Thomson d’une disquette.  
Il expose ensuite les fichiers Thomson comme un volume Amiga standard.

---

## ✨ Fonctionnalités principales

- Lecture du **filesystem Thomson** (TO8 / TO9 / TO9+)
- Format plat : **pas de répertoires**, tous les fichiers sont à la racine
- Lecture du **nom**, de la **taille** et du **commentaire Thomson**
- **Date/heure AmigaOS optionnelles**, générées via un flag dans la mountlist
- Compatible **AmigaOS 2.0+**
- Implémenté en **C** pour 68000→68060
- Utilisation transparente via `Assign` / `Mount`
- S’appuie sur **`todisk.device`** pour la lecture des secteurs

---

## 🧾 Gestion des faces : TA0 / TB0

Sur les machines Thomson, les deux faces d’une disquette sont considérées comme **deux unités distinctes**.

Le handler reproduit exactement ce fonctionnement :

| Unité Amiga | Face physique | Unité Thomson | Description |
|-------------|---------------|---------------|-------------|
| **TA0:**    | Face 0        | 0             | Face principale du disque |
| **TB0:**    | Face 1        | 1             | Deuxième face du même disque |

Une disquette ↦ **deux unités logiques** : TA0: et TB0:

---

## 🛠️ Aspects techniques

- C pour 68000 → 68060  
- Architecture standard des handlers AmigaOS :
  - packets (`dos.library`)
  - message ports
  - FileInfoBlock
- Lecture des structures Thomson :
  - table unique de fichiers  
  - secteurs Thomson  
  - champ commentaire  
- Dépend de **`todisk.device`**  
- Lecture seule

---

# 🇬🇧 Amiga Thomson Handler – Thomson Filesystem for AmigaOS

## 🎯 Purpose

This project provides an **AmigaOS handler** that reads the **Thomson filesystem** (TO8, TO9, TO9+) directly from AmigaOS 2.0 and above.

The handler uses the AmigaOS **`todisk.device`** to access Thomson disk sectors and exposes the files as a standard Amiga volume.

---

## ✨ Main Features

- Reading of the **Thomson filesystem** (TO8 / TO9 / TO9+)
- Flat layout: **no directories**, all files at root level
- Reads **filename**, **size**, and **Thomson comment**
- Optional **AmigaOS timestamp generation** (mountlist flag)
- Works on **AmigaOS 2.0+**
- Implemented in **C** for 68000→68060 CPUs
- Usable via `Assign` / `Mount`
- Uses **`todisk.device`** for sector access

---

## 🧾 Disk face handling: TA0 / TB0

Thomson systems expose both sides of a floppy disk as **two independent logical drives**.

The handler mirrors this behavior:

| Amiga Unit | Physical Side | Thomson Unit | Description |
|------------|----------------|---------------|-------------|
| **TA0:**   | Side 0         | 0             | Main side of the disk |
| **TB0:**   | Side 1         | 1             | Second side of the same disk |

One physical disk → **TA0:** and **TB0:**

---

## 🛠️ Technical details

- Written in **C** for 68000 → 68060  
- Uses standard AmigaOS handler mechanisms:
  - `dos.library` packets  
  - message ports  
  - FileInfoBlock  
- Thomson filesystem parsing :
  - single root file table  
  - sector-based layout  
  - internal comment field  
- Relies on **`todisk.device`**  
- Read-only

---

# 🟩 Baseline recommandée  
👉 Handler AmigaOS pour filesystem Thomson (C)

# 🟦 Topics recommandés  
