# brscan5-longpaper-fix

> **Enable continuous Long Paper & Receipt Scanning (up to 1.828 m / 72 inches) for Brother mobile scanners on Linux.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-orange.svg)]()
[![SANE Backend: brscan5](https://img.shields.io/badge/SANE-brscan5-green.svg)]()

---

## 🇩🇪 Deutsch

### Das Problem
Die Hardware moderner mobiler Brother-Scanner (wie **DS-740D, DS-640, DS-940DW, ADS-1200, ADS-1700W**) unterstützt problemlos das Einziehen langer Belege bis zu **1828,8 mm (1,828 Meter / 72 Zoll)**.

Im offiziellen Linux-Treiber `brscan5` existiert jedoch ein gravierender Fehler in der SANE-Schnittstelle (`libsane-brother5.so`):
* Die SANE-Option für die Seitenhöhe (`br-y`) ist intern fest auf maximal **355,6 mm** (US Legal) beschränkt.
* Jede Scan-Software (wie NAPS2, Simple Scan oder `scanimage`), die einen längeren Beleg anfordert, wird von SANE auf 355,6 mm gekürzt.
* Da die Obergrenze nie überschritten wird, aktiviert der Treiber niemals das gerätespezifische USB-Kommando `LONG=ON` (Long Paper Mode).
* **Die Folge:** Nach ca. 35 cm stoppt der Einzugsmotor mit dem Hardware-Fehler `1 page too long end`, was SANE als `Document feeder jammed` (Papierstau) meldet.

### Die Lösung
Dieser **Open-Source C-Shim** klinkt sich transparent in den Brother SANE-Treiber ein. Sobald der Scanner geöffnet wird, passt der Shim die interne Obergrenze `rangeYmm->max` dynamisch auf die tatsächliche Hardwaregrenze von **1828,8 mm** an.

* ✅ Ermöglicht unterbrechungsfreies Scannen bis 1,82 m.
* ✅ Funktioniert transparent mit allen SANE-Frontends (**NAPS2**, `scanimage`, XSane, Simple Scan, Paperless-ngx).
* ✅ Keine Modifikation von Binärcode nötig (dynamischer Interposer).

### Wichtige Hardware-Regeln
1. **Nur Simplex (Einseitig):** Die Hardware des DS-740D unterstützt Überlängen bis 1,82 m nur im Simplex-Betrieb. Bei Duplex limitiert die Hardware strikt auf 406 mm (16 Zoll).
2. **Gerader Papierauslauf:** Die Ausgabeklappe oben am Scanner muss **geöffnet** sein, damit der Bon hinten geradlinig herauslaufen kann.

---

## 🇬🇧 English

### The Problem
Brother mobile document scanners (**DS-740D, DS-640, DS-940DW, ADS-1200, ADS-1700W**) physically support continuous paper feed up to **1828.8 mm (72 inches / 1.828 m)** in Simplex mode.

However, Brother's official Linux SANE driver (`brscan5` / `libsane-brother5.so`) has a hardcoded limitation:
* The vertical scan area range (`br-y`) is capped at **355.6 mm** (US Legal).
* Whenever a frontend (NAPS2, Simple Scan, XSane, `scanimage`) requests a larger scan length, SANE clamps it to 355.6 mm.
* Because the requested length never exceeds Legal size, Brother's low-level engine never issues the hardware USB command `LONG=ON`.
* **Result:** After ~35 cm, the scanner firmware aborts the feed motor with `1 page too long end`, reported by SANE as `Document feeder jammed`.

### The Solution
`brscan5-longpaper-fix` is a lightweight, zero-overhead C interposer. It hooks into the SANE device initialization and dynamically sets `rangeYmm->max` to **1828.8 mm**.

---

## 🚀 Installation

### 1. Requirements
* `brscan5` (installed via package manager / AUR)
* `sane`
* `gcc` and `make`

### 2. Install via script
```bash
git clone https://github.com/tsdun/brscan5-longpaper-fix.git
cd brscan5-longpaper-fix
chmod +x scripts/*.sh
sudo ./scripts/install.sh
```

### 3. Arch Linux / CachyOS (PKGBUILD)
```bash
cd packaging/arch
makepkg -si
```

---

## ⚙️ Configuration in NAPS2

1. Open **NAPS2** -> **Profiles** -> **Edit Profile**.
2. Set **Paper Source**: `Feeder` (Simplex, **not** Duplex).
3. Set **Page Size**: `Custom`.
   * **Width:** `215.9 mm` (or width of receipt, e.g. 80 mm)
   * **Height:** `1828.8 mm`
4. Under **Advanced Settings**:
   * Enable **Auto-crop** (automatically trims trailing whitespace after the receipt ends).
   * Enable **Auto-deskew** (straightens crooked receipts in software).

---

## 📜 Uninstallation

To restore Brother's untouched original driver:
```bash
sudo ./scripts/uninstall.sh
```

---

## 📄 License
This project is licensed under the [MIT License](LICENSE).
The Brother proprietary driver `brscan5` remains the property of Brother Industries, Ltd.
