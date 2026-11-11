# Sicherheit

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/sicherheit.md)

Der ESP hängt ohne TLS im Heimnetz. Was die Firmware dagegen tut, und was nicht.

## Schutz

- **Gerätepasswort (optional, empfohlen):** unter *WLAN & Einstellungen* setzen. Danach brauchen alle ändernden Zugriffe (Einstellungen, Löschen, Einheit beenden, Firmware-Update) das Passwort; der Browser fragt einmal (Benutzername beliebig). Gespeichert wird nur ein gesalzener Hash. Vergessen: neu flashen mit *Erase Flash* (siehe [Flashen](flashen.md)); Einstellungen und Einheiten gehen dabei verloren.
- **Hotspot-Passwort:** dort ebenfalls änderbar. Der Standard `waterrower` ist öffentlich bekannt; ändern, wenn Fremde in Reichweite sind.
- **Fremde Webseiten:** Anfragen werden nur angenommen, wenn der ESP per IP oder lokalem Namen (`waterrower.local`, `waterrower.lan` …) angesprochen wird (DNS-Rebinding) und ändernde Anfragen von seiner eigenen Seite kommen (CSRF). Hinter anderem Namen (z. B. `waterrower.fritz.box`) antwortet die API mit 403; dann die IP nehmen.
- **Arena-Token:** wird nie ausgegeben, nur an die eingetragene Adresse gesendet und muss bei Adressänderung neu eingegeben werden.
- **Firmware-Update:** nur Images dieses Projekts, nicht während einer Aufzeichnung; ein Image, das nicht sauber startet, wird beim nächsten Boot zurückgerollt.

## Bewusst offen

- Lesende Zugriffe (Live-Werte, Einheiten, Export) sind im WLAN ohne Passwort möglich.
- Kein TLS auf dem Gerät: Gerätepasswort und WLAN-Eingaben gehen im lokalen Netz im Klartext.
- WLAN-, MQTT- und Arena-Zugangsdaten liegen unverschlüsselt im Flash (keine Flash-Verschlüsselung, kein Secure Boot); wer das Gerät hat, kann sie auslesen und beliebige Firmware flashen.
- MQTT ohne `mqtts://` überträgt Zugangsdaten im Klartext.
