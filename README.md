![FRITZ!Box-Status-ESP32 Logo](src/fritzbox-status-logo.png)

# fritzbox-Status-ESP32

ESP32 Dashboard fuer FRITZ!Box mit konfigurierbaren Board-Profilen (z. B. LilyGO T-Display S3 oder CYD).

## Features
- Captive Portal zur Erstkonfiguration (SSID: FritzBox-Status-AP)
- Speicherung von WLAN und FRITZ!Box Host-Konfiguration im NVS (Preferences)
- Abruf von Gateway-Status ueber FRITZ!Box TR-064 (SOAP/UPnP)
- Abruf von WAN-Traffic ueber TR-064 Counter (Bytes sent/received)
- NerdMiner-inspiriertes Live-Dashboard auf dem 320x170 Display

## Voraussetzungen
- Ein unterstuetztes ESP32-Board:
	- LilyGO T-Display S3 (`lilygo-t-display-s3`)
	- CYD 2.8" (`cyd-2432s028`)
- PlatformIO (VS Code)
- FRITZ!Box mit aktiviertem TR-064 Zugriff im Heimnetz

## Schnellstart
1. Passende PlatformIO-Umgebung waehlen:
	- LilyGO: `lilygo-t-display-s3`
	- CYD: `cyd-2432s028`
2. Projekt in PlatformIO bauen und flashen.
3. Beim ersten Start mit WLAN `FritzBox-Status-AP` verbinden.
4. Browser auf `192.168.4.1` oeffnen.
5. Heim-WLAN und FRITZ!Box Host/IP speichern.
6. ESP32 verbindet sich neu und zeigt die Statusdaten an.

## Board-Profile (Bruce-Stil)
- Die board-spezifischen Defines liegen unter `include/boards/`.
- Aktive Auswahl erfolgt je Environment ueber Build-Flag in `platformio.ini`:
	- `BOARD_PROFILE_LILYGO_T_DISPLAY_S3`
	- `BOARD_PROFILE_CYD_2432S028`
- Neue Boards kannst du hinzufuegen, indem du:
	1. Ein neues Profil in `include/boards/<dein_board>.h` anlegst.
	2. `include/boards/board_profile.h` um das Profil erweiterst.
	3. In `platformio.ini` ein neues Environment mit passendem `BOARD_PROFILE_...` erstellst.

## Hinweise
- Die HTTPS-Verbindung fuer TR-064 nutzt `setInsecure()` fuer Self-Signed Zertifikate.
- Fuer Produktivbetrieb ist Zertifikats-Pinning sicherer.
